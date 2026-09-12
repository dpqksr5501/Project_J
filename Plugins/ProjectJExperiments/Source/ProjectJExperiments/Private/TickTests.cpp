#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformTLS.h"
#include "ExperimentOutput.h"

namespace ProjectJ::Experiments
{
struct FFrameSnapshot
{
    FCriticalSection Mutex;
    uint64 Version = 0;
};

// Tick functions outlive registration and are destroyed only after World::Tick's
// completion barrier. Workers touch plain values, never the prerequisite UObject.
struct FSnapshotTick final : FTickFunction
{
    FFrameSnapshot* Frame = nullptr;
    bool bProducer = false;
    int32 Iterations = 0;
    uint64 Observed = 0, Checksum = 0, Executions = 0;
    uint32 Thread = 0;
    double ComputeUs = 0;
    FSnapshotTick()
    {
        bCanEverTick = bStartWithTickEnabled = true;
        bAllowTickBatching = false;
        TickGroup = EndTickGroup = TG_PrePhysics;
    }
    virtual void ExecuteTick(float, ELevelTick, ENamedThreads::Type, const FGraphEventRef&) override
    {
        const double Start = FPlatformTime::Seconds();
        ++Executions;
        Thread = FPlatformTLS::GetCurrentThreadId();
        {
            FScopeLock Guard(&Frame->Mutex);
            if (bProducer) { ++Frame->Version; }
            Observed = Frame->Version;
        }
        uint64 Value = Observed;
        for (int32 I = 0; I < Iterations; ++I) { Value = Value * 1664525u + 1013904223u; }
        Checksum = Value;
        ComputeUs = (FPlatformTime::Seconds() - Start) * 1e6;
    }
    virtual FString DiagnosticMessage() override { return TEXT("ProjectJ.F.ValueSnapshotTick"); }
};
class FTickExperiment final : public IAutomationLatentCommand
{
public:
    explicit FTickExperiment(FAutomationTestBase* InTest) : Test(InTest)
    {
        World = UWorld::CreateWorld(EWorldType::Game, false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->InitializeActorsForPlay(FURL());
        World->GetWorldSettings()->NotifyBeginPlay();
        World->GetWorldSettings()->NotifyMatchStarted();
    }
    virtual ~FTickExperiment() override
    {
        ClearTicks();
        World->EndPlay(EEndPlayReason::Quit);
        World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    }
    virtual bool Update() override
    {
        // TickTaskManager visits a tick once per engine frame. Repeated World::Tick
        // calls inside one synchronous test would silently skip most samples.
        if (LastEngineFrame == GFrameCounter) { return false; }
        LastEngineFrame = GFrameCounter;
        const int32 Work = Mode < 4 ? 128 : 16384;
        const bool bConcurrent = (Mode & 2) != 0, bPrerequisite = (Mode & 1) != 0;
        if (!Producer)
        {
            Frame.Version = 0;
            Producer = MakeUnique<FSnapshotTick>(); Producer->Frame = &Frame; Producer->bProducer = true;
            Producer->RegisterTickFunction(World->PersistentLevel);
            for (int32 I = 0; I < 64; ++I)
            {
                auto C = MakeUnique<FSnapshotTick>(); C->Frame = &Frame; C->Iterations = Work;
                C->bRunOnAnyThread = bConcurrent;
                if (bPrerequisite) { C->AddPrerequisite(World, *Producer); }
                C->RegisterTickFunction(World->PersistentLevel); Consumers.Add(MoveTemp(C));
            }
        }
        const uint64 Previous = Frame.Version;
        const double Start = FPlatformTime::Seconds();
        World->Tick(LEVELTICK_All, 1.0f / 60.0f);
        const double TickUs = (FPlatformTime::Seconds() - Start) * 1e6;
        Test->TestEqual(TEXT("Producer actually executed this engine frame"), Frame.Version, Previous + 1);
        int32 Stale = 0, OffGT = 0; double Compute = 0; uint64 Checksum = 0;
        for (const auto& C : Consumers)
        {
            Stale += C->Observed != Frame.Version; OffGT += C->Thread != FPlatformTLS::GetCurrentThreadId();
            Compute += C->ComputeUs; Checksum += C->Checksum;
            Test->TestEqual(TEXT("Consumer actually executed each sample"), C->Executions, uint64(Sample + 6));
            if (bPrerequisite) { Test->TestEqual(TEXT("Consumes current snapshot"), C->Observed, Frame.Version); }
        }
        if (Sample >= 0) { Csv += FString::Printf(TEXT("%d,%d,%d,%d,%.4f,%.4f,%d,%d,%llu\n"), Work, bConcurrent, bPrerequisite, Sample, TickUs, Compute, Stale, OffGT, Checksum); }
        if (Sample == 20) for (auto& C : Consumers) { C->UnRegisterTickFunction(); C->RegisterTickFunction(World->PersistentLevel); }
        if (++Sample < 60) { return false; }
        ClearTicks(); Sample = -5;
        if (++Mode < 8) { return false; }
        Test->TestTrue(TEXT("Write tick measurements"), Save(TEXT("tick.csv"), Csv));
        return true;
    }
private:
    void ClearTicks()
    {
        for (auto& C : Consumers)
        {
            C->UnRegisterTickFunction();
            if (Mode & 1) { C->RemovePrerequisite(World, *Producer); }
        }
        Consumers.Empty();
        if (Producer) { Producer->UnRegisterTickFunction(); Producer.Reset(); }
    }
    FAutomationTestBase* Test;
    UWorld* World;
    FFrameSnapshot Frame;
    TUniquePtr<FSnapshotTick> Producer;
    TArray<TUniquePtr<FSnapshotTick>> Consumers;
    uint64 LastEngineFrame = MAX_uint64;
    int32 Sample = -5, Mode = 0;
    FString Csv = TEXT("iterations,concurrent,prerequisite,frame,world_tick_us,consumer_compute_sum_us,stale,off_gt,checksum\n");
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJTickExperiment, "ProjectJ.GroupF.Tick.SnapshotOrdering",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJTickExperiment::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(ProjectJ::Experiments::FTickExperiment(this)); return true;
}
