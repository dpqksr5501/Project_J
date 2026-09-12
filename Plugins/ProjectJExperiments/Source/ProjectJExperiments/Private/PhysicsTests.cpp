#include "Misc/AutomationTest.h"
#include "ChaosSolversModule.h"
#include "PBDRigidsSolver.h"
#include "Chaos/SimCallbackObject.h"
#include "HAL/PlatformTLS.h"
#include "ExperimentOutput.h"

namespace ProjectJ::Experiments
{
struct FPhysicsInput : Chaos::FSimCallbackInput
{
    uint64 Revision = 0, Epoch = 0;
    double Submitted = 0;
    int32 Count = 0;
    void Reset() { Revision = Epoch = 0; Submitted = 0; Count = 0; }
};
struct FPhysicsOutput : Chaos::FSimCallbackOutput
{
    uint64 Revision = 0, Epoch = 0;
    uint32 Thread = 0;
    double Submitted = 0, ComputeUs = 0, SumZ = 0;
    void Reset() { Revision = Epoch = 0; Thread = 0; Submitted = ComputeUs = SumZ = 0; }
};

// An independent prediction workload on a real Chaos simulation callback.
// It does not move CMC, damage, notifies or authoritative rigid bodies to PT.
class FPhysicsSnapshotCallback final : public Chaos::TSimCallbackObject<FPhysicsInput, FPhysicsOutput>
{
    virtual void OnPreSimulate_Internal() override
    {
        const auto* Input = GetConsumerInput_Internal();
        if (!Input || !Input->Revision) { return; }
        const double Start = FPlatformTime::Seconds();
        auto& Output = GetProducerOutputData_Internal();
        Output.Revision = Input->Revision; Output.Epoch = Input->Epoch;
        Output.Submitted = Input->Submitted; Output.Thread = FPlatformTLS::GetCurrentThreadId();
        double Sum = 0;
        for (int32 I = 0; I < Input->Count; ++I)
        {
            double Z = I, Velocity = 200;
            for (int32 Step = 0; Step < 60; ++Step) { Velocity -= 980.0 / 60; Z += Velocity / 60; }
            Sum += Z;
        }
        Output.SumZ = Sum;
        Output.ComputeUs = (FPlatformTime::Seconds() - Start) * 1e6;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPhysicsSnapshot, "ProjectJ.GroupF.Physics.SnapshotLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPhysicsSnapshot::RunTest(const FString&)
{
    using namespace ProjectJ::Experiments;
    auto* Module = FChaosSolversModule::GetModule();
    FString Csv = TEXT("taskgraph,cycle,revision,compute_us,dispatch_us,gt_wait_us,delivery_us,off_gt,discarded\n");
    for (bool bAsync : {false, true})
    for (int32 Cycle = 0; Cycle < 4; ++Cycle)
    {
        auto* Solver = Module->CreateSolver(nullptr, -1);
        Solver->SetThreadingMode_External(bAsync ? Chaos::EThreadingModeTemp::TaskGraph : Chaos::EThreadingModeTemp::SingleThread);
        auto* Callback = Solver->CreateAndRegisterSimCallbackObject_External<FPhysicsSnapshotCallback>();
        uint64 Epoch = 1, LastApplied = 0;
        int32 Received = 0, Discarded = 0;
        for (uint64 Revision = 1; Revision <= 40; ++Revision)
        {
            auto* Input = Callback->GetProducerInputData_External();
            Input->Revision = Revision; Input->Epoch = Epoch; Input->Count = 1024;
            Input->Submitted = FPlatformTime::Seconds();
            const double Submit = Input->Submitted;
            Solver->AdvanceAndDispatch_External(1.0 / 60);
            const double Dispatched = FPlatformTime::Seconds();
            if (Revision % 10 == 0) { ++Epoch; } // Simulated owner invalidation while submitted work exists.
            Solver->WaitOnPendingTasks_External();
            const double Joined = FPlatformTime::Seconds();
            while (auto Output = Callback->PopFutureOutputData_External())
            {
                ++Received;
                TestEqual(TEXT("Solver preserves submitted sequence"), Output->Revision, Revision);
                // Semi-implicit Euler: 60 steps of velocity then position.
                const double Expected = 1024.0 * 1023 / 2 + 1024.0 * (200 - 980.0 * 61 / 120);
                TestTrue(TEXT("PT prediction matches analytic reference"), FMath::IsNearlyEqual(Output->SumZ, Expected, 1e-6));
                const bool bStale = Output->Epoch != Epoch;
                if (bStale) { ++Discarded; }
                else { TestTrue(TEXT("GT applies strictly newer live results"), Output->Revision > LastApplied); LastApplied = Output->Revision; }
                Csv += FString::Printf(TEXT("%d,%d,%llu,%.4f,%.4f,%.4f,%.4f,%d,%d\n"), bAsync, Cycle, Revision,
                    Output->ComputeUs, (Dispatched - Submit) * 1e6, (Joined - Dispatched) * 1e6,
                    (Joined - Output->Submitted) * 1e6, Output->Thread != FPlatformTLS::GetCurrentThreadId(), bStale);
            }
        }
        TestEqual(TEXT("All bounded requests drained"), Received, 40);
        TestEqual(TEXT("Invalidated generations never applied"), Discarded, 4);
        Solver->UnregisterAndFreeSimCallbackObject_External(Callback);
        Callback = nullptr;
        Solver->AdvanceAndDispatch_External(0); Solver->WaitOnPendingTasks_External();
        Module->DestroySolver(Solver);
    }
    TestTrue(TEXT("Save physics measurements"), Save(TEXT("physics.csv"), Csv));
    return true;
}
