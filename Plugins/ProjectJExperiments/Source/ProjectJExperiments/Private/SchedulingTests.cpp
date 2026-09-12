#include "Misc/AutomationTest.h"
#include "Optimization/Project_JTargetScoring.h"
#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "Tasks/Task.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Event.h"
#include "Misc/ScopeLock.h"
#include "Containers/Queue.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ExperimentOutput.h"

namespace ProjectJ::Experiments
{
using namespace TargetScoring;
static FSnapshot Input(int32 Count)
{
    FSnapshot S;
    FRandomStream Random(20260912);
    for (int32 I = 0; I < Count; ++I) { S.Candidates.Add({I, FVector(Random.FRandRange(-2500, 2500), Random.FRandRange(-2500, 2500), 0)}); }
    return S;
}

struct FTimedResult
{
    FResult Value;
    double Started = 0, Ended = 0;
    uint32 Thread = 0;
};

// Per-run RAII owner. Capacity covers queued, executing AND unconsumed results.
// Cancel discards queued work; active value-only work observes atomic stop.
class FBoundedConsumer final : public FRunnable
{
public:
    static constexpr int32 Capacity = 32;
    explicit FBoundedConsumer(TSharedRef<const FSnapshot, ESPMode::ThreadSafe> InInput) : Snapshot(InInput)
    {
        Wake = FPlatformProcess::GetSynchEventFromPool(false);
        Worker = FRunnableThread::Create(this, TEXT("ProjectJ.F.BoundedConsumer"));
    }
    virtual ~FBoundedConsumer() override
    {
        Stop();
        if (Worker) { Worker->WaitForCompletion(); delete Worker; }
        FPlatformProcess::ReturnSynchEventToPool(Wake);
    }
    bool IsStarted() const { return Worker != nullptr; }
    bool Submit(uint64 Ticket)
    {
        FScopeLock Guard(&Mutex);
        if (Stopping.load() || Outstanding == Capacity) { return false; }
        Queue.Enqueue(Ticket); ++Outstanding;
        Wake->Trigger();
        return true;
    }
    bool Pop(uint64& Ticket)
    {
        FScopeLock Guard(&Mutex);
        if (!Completed.Dequeue(Ticket)) { return false; }
        --Outstanding;
        return true;
    }
    virtual void Stop() override
    {
        // A concurrent submit either enters before stop or observes stop under
        // the same mutex. No work can be enqueued after the shutdown boundary.
        FScopeLock Guard(&Mutex);
        Stopping.store(true); Wake->Trigger();
    }
    virtual uint32 Run() override
    {
        while (!Stopping.load())
        {
            uint64 Ticket;
            bool bWork = false;
            { FScopeLock Guard(&Mutex); bWork = Queue.Dequeue(Ticket); }
            if (!bWork) { ++IdleWaits; Wake->Wait(); continue; }
            TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_F_DedicatedConsumer);
            const FResult Result = Evaluate(*Snapshot, false, Stopping);
            if (Result.Status == EStatus::Completed)
            {
                FScopeLock Guard(&Mutex);
                if (!Stopping.load()) { Completed.Enqueue(Ticket); }
            }
        }
        return 0;
    }
    std::atomic<int32> IdleWaits{0};
private:
    TSharedRef<const FSnapshot, ESPMode::ThreadSafe> Snapshot;
    FCriticalSection Mutex;
    TQueue<uint64> Queue, Completed; // Every access is under Mutex.
    int32 Outstanding = 0;
    std::atomic<bool> Stopping{false};
    FEvent* Wake = nullptr;
    FRunnableThread* Worker = nullptr;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJExecutionAPITest, "ProjectJ.GroupF.Scheduling.APIs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJExecutionAPITest::RunTest(const FString&)
{
    using namespace ProjectJ::Experiments;
    FString CSV(TEXT("candidates,api,sample,submit_us,queue_us,compute_us,wait_us,total_us,worker_thread\n"));
    for (int32 Count : {128, 16384})
    {
        const auto Snapshot = MakeShared<const FSnapshot, ESPMode::ThreadSafe>(Input(Count));
        const auto Cancel = MakeShared<std::atomic<bool>, ESPMode::ThreadSafe>(false);
        const FResult Reference = Evaluate(*Snapshot, false, *Cancel);
        // Rotate API order each sample to avoid always assigning one API the
        // warmest cache. One in-flight job; a latency comparison, not throughput.
        for (int32 Sample = -5; Sample < 40; ++Sample)
        {
            for (int32 Offset = 0; Offset < 5; ++Offset)
            {
                const int32 API = (Sample + 5 + Offset) % 5;
                const auto Result = MakeShared<FTimedResult, ESPMode::ThreadSafe>();
                auto Work = [Snapshot, Cancel, Result, API]()
                {
                    TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_F_ScoringWork);
                    Result->Started = FPlatformTime::Seconds(); Result->Thread = FPlatformTLS::GetCurrentThreadId();
                    Result->Value = Evaluate(*Snapshot, API == 4, *Cancel);
                    Result->Ended = FPlatformTime::Seconds();
                };
                const double Begin = FPlatformTime::Seconds();
                double Submitted = Begin;
                if (API == 0) { Work(); Submitted = Begin; }
                else if (API == 1 || API == 4)
                {
                    auto Task = UE::Tasks::Launch(UE_SOURCE_LOCATION, MoveTemp(Work));
                    Submitted = FPlatformTime::Seconds(); Task.Wait();
                }
                else if (API == 2)
                {
                    auto Task = FFunctionGraphTask::CreateAndDispatchWhenReady(Work, TStatId(), nullptr, ENamedThreads::AnyBackgroundThreadNormalTask);
                    Submitted = FPlatformTime::Seconds(); FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
                }
                else
                {
                    auto Task = Async(EAsyncExecution::ThreadPool, Work);
                    Submitted = FPlatformTime::Seconds(); Task.Wait();
                }
                const double End = FPlatformTime::Seconds();
                TestEqual(TEXT("Stable winner matches serial"), Result->Value.BestId, Reference.BestId);
                TestEqual(TEXT("Stable score matches serial"), Result->Value.BestScore, Reference.BestScore);
                if (Sample >= 0)
                {
                    const TCHAR* Names[] = {TEXT("Serial"), TEXT("Tasks"), TEXT("TaskGraph"), TEXT("ThreadPool"), TEXT("TasksParallelFor")};
                    CSV += FString::Printf(TEXT("%d,%s,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%u\n"), Count, Names[API], Sample,
                        (Submitted-Begin)*1e6, (Result->Started-Begin)*1e6, (Result->Ended-Result->Started)*1e6,
                        API ? (End-Submitted)*1e6 : 0, (End-Begin)*1e6, Result->Thread);
                }
            }
        }
        Cancel->store(true);
        auto Cancelled = Async(EAsyncExecution::ThreadPool, [Snapshot, Cancel]() { return Evaluate(*Snapshot, false, *Cancel); });
        TestTrue(TEXT("Cancelled shared input returns no applicable result"), Cancelled.Get().Status == EStatus::Cancelled);
    }
    TestTrue(TEXT("Measured samples saved"), Save(TEXT("scheduling.csv"), CSV));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJConsumerLifecycleTest, "ProjectJ.GroupF.Scheduling.ConsumerLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJConsumerLifecycleTest::RunTest(const FString&)
{
    using namespace ProjectJ::Experiments;
    const auto Snapshot = MakeShared<const FSnapshot, ESPMode::ThreadSafe>(Input(128));
    FString CSV(TEXT("cycle,accepted,completed,shutdown_ms\n"));
    for (int32 Cycle = 0; Cycle < 16; ++Cycle)
    {
        auto Worker = MakeUnique<FBoundedConsumer>(Snapshot);
        if (!TestTrue(TEXT("Dedicated worker starts"), Worker->IsStarted())) { return false; }
        for (uint64 Ticket = 0; Ticket < FBoundedConsumer::Capacity; ++Ticket) { TestTrue(TEXT("Admitted within bound"), Worker->Submit(Ticket)); }
        TestFalse(TEXT("Unconsumed completions retain capacity"), Worker->Submit(1000));
        TSet<uint64> Seen;
        const double Deadline = FPlatformTime::Seconds() + 5;
        while (Seen.Num() < FBoundedConsumer::Capacity && FPlatformTime::Seconds() < Deadline)
        {
            uint64 Ticket;
            if (Worker->Pop(Ticket)) { TestFalse(TEXT("Exactly once"), Seen.Contains(Ticket)); Seen.Add(Ticket); }
            else { FPlatformProcess::SleepNoStats(0.001f); }
        }
        TestEqual(TEXT("Every admitted ticket completed"), Seen.Num(), FBoundedConsumer::Capacity);
        // Stop both sleeping and busy consumers; destructor must wake and join.
        if (Cycle % 2) { for (uint64 Ticket = 32; Ticket < 64; ++Ticket) { Worker->Submit(Ticket); } }
        const double StopStart = FPlatformTime::Seconds();
        Worker->Stop(); TestFalse(TEXT("Stop closes admission"), Worker->Submit(999)); Worker.Reset();
        CSV += FString::Printf(TEXT("%d,32,%d,%.6f\n"), Cycle, Seen.Num(), (FPlatformTime::Seconds()-StopStart)*1000);
    }
    TestTrue(TEXT("Shutdown measurements saved"), Save(TEXT("consumer.csv"), CSV));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJConsumerContentionTest, "ProjectJ.GroupF.Scheduling.ConsumerContention",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJConsumerContentionTest::RunTest(const FString&)
{
    using namespace ProjectJ::Experiments;
    const auto Snapshot = MakeShared<const FSnapshot, ESPMode::ThreadSafe>(Input(128));
    FString Csv = TEXT("cycle,completed,idle_waits,drain_ms,stop_join_ms\n");
    for (int32 Cycle = 0; Cycle < 8; ++Cycle)
    {
        auto Worker = MakeUnique<FBoundedConsumer>(Snapshot);
        if (!TestTrue(TEXT("Consumer starts"), Worker->IsStarted())) { return false; }
        std::atomic<bool> Exit{false};
        TArray<TFuture<void>> Producers;
        const double Start = FPlatformTime::Seconds();
        for (uint64 Producer = 0; Producer < 4; ++Producer)
        {
            Producers.Add(Async(EAsyncExecution::ThreadPool, [&, Producer]
            {
                for (uint64 I = 0; I < 256 && !Exit.load(); ++I)
                {
                    while (!Exit.load() && !Worker->Submit(Producer * 256 + I))
                    { FPlatformProcess::SleepNoStats(0); }
                }
            }));
        }
        TSet<uint64> Seen;
        while (Seen.Num() < 1024 && FPlatformTime::Seconds() - Start < 10)
        {
            uint64 Ticket;
            if (Worker->Pop(Ticket)) { TestFalse(TEXT("Contending producers never duplicate completion"), Seen.Contains(Ticket)); Seen.Add(Ticket); }
            else { FPlatformProcess::SleepNoStats(0); }
        }
        Exit.store(true);
        for (auto& Producer : Producers) { Producer.Get(); }
        TestEqual(TEXT("All 1024 admitted requests consumed"), Seen.Num(), 1024);
        const double DrainMs = (FPlatformTime::Seconds() - Start) * 1000;
        // Let the empty consumer enter its event wait before shutdown. This is
        // a blocking-behavior check, not a measurement of idle CPU utilisation.
        FPlatformProcess::SleepNoStats(.002f);
        const int32 Waits = Worker->IdleWaits.load();
        TestTrue(TEXT("Idle consumer used its blocking event"), Waits > 0);
        Producers.Empty(); Exit.store(false);
        std::atomic<int32> AcceptedAfterStop{0};
        for (int32 I = 0; I < 4; ++I)
        {
            Producers.Add(Async(EAsyncExecution::ThreadPool, [&]
            {
                while (!Exit.load()) { Worker->Submit(2000); FPlatformProcess::SleepNoStats(0); }
                for (int32 Attempt = 0; Attempt < 100; ++Attempt) { AcceptedAfterStop += Worker->Submit(3000); }
            }));
        }
        FPlatformProcess::SleepNoStats(.002f);
        const double Stop = FPlatformTime::Seconds();
        Worker->Stop(); Exit.store(true);
        for (auto& Producer : Producers) { Producer.Get(); }
        TestEqual(TEXT("No admission after concurrent stop boundary"), AcceptedAfterStop.load(), 0);
        Worker.Reset();
        Csv += FString::Printf(TEXT("%d,%d,%d,%.4f,%.4f\n"), Cycle, Seen.Num(), Waits, DrainMs, (FPlatformTime::Seconds() - Stop) * 1000);
    }
    TestTrue(TEXT("Save contention measurements"), Save(TEXT("consumer-contention.csv"), Csv));
    return true;
}
