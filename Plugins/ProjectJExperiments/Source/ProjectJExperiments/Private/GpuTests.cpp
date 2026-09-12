#include "Misc/AutomationTest.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "ExperimentOutput.h"

namespace ProjectJ::Experiments
{
class FValueComputeCS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FValueComputeCS);
    SHADER_USE_PARAMETER_STRUCT(FValueComputeCS, FGlobalShader);
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(uint32, Count)
        SHADER_PARAMETER(uint32, Seed)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, Output)
    END_SHADER_PARAMETER_STRUCT()
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P)
    { return IsFeatureLevelSupported(P.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FValueComputeCS, "/ProjectJExperiments/ValueCompute.usf", "MainCS", SF_Compute);

struct FGpuTransfer
{
    // Only RT accesses readback and output. Release/acquire publishes completion
    // to GT. Render commands retain this owner until all queued use is finished.
    TUniquePtr<FRHIGPUBufferReadback> Readback;
    std::atomic<bool> Ready{false}, PollQueued{false};
    bool bMatches = false;
    uint32 Seed = 0;
    uint64 Epoch = 0;
};

class FGpuExperiment final : public IAutomationLatentCommand
{
public:
    explicit FGpuExperiment(FAutomationTestBase* InTest) : Test(InTest) {}
    virtual bool Update() override
    {
        constexpr uint32 Count = 65536;
        if (!State)
        {
            State = MakeShared<FGpuTransfer, ESPMode::ThreadSafe>();
            State->Seed = 20260912 + Sample;
            State->Epoch = CurrentEpoch;
            Submitted = FPlatformTime::Seconds();
            const bool bAsync = Sample % 2 != 0;
            auto Transfer = State;
            ENQUEUE_RENDER_COMMAND(ProjectJ_F_GPUDispatch)([Transfer, bAsync](FRHICommandListImmediate& RHICmdList)
            {
                FRDGBuilder Graph(RHICmdList);
                TShaderMapRef<FValueComputeCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
                auto Dispatch = [&](const TCHAR* Name, ERDGPassFlags Flags, uint32 Seed)
                {
                    auto Buffer = Graph.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), Count), Name);
                    auto* P = Graph.AllocParameters<FValueComputeCS::FParameters>();
                    P->Count = Count; P->Seed = Seed; P->Output = Graph.CreateUAV(Buffer);
                    FComputeShaderUtils::AddPass(Graph, RDG_EVENT_NAME("ProjectJ_F_%s", Name), Flags, Shader, P, FIntVector(Count / 64, 1, 1));
                    return Buffer;
                };
                FRDGBufferRef Value;
                {
                    RDG_EVENT_SCOPE(Graph, "ProjectJ_F_ReadbackValues");
                    Value = Dispatch(TEXT("ReadbackValues"), bAsync ? ERDGPassFlags::AsyncCompute : ERDGPassFlags::Compute, Transfer->Seed);
                }
                // Independent compute on the graphics queue. NeverCull retains
                // the controlled competing workload even without a consumer.
                {
                    RDG_EVENT_SCOPE(Graph, "ProjectJ_F_GraphicsQueueCompetition");
                    Dispatch(TEXT("GraphicsQueueCompetition"), ERDGPassFlags::Compute | ERDGPassFlags::NeverCull, 42);
                }
                Transfer->Readback = MakeUnique<FRHIGPUBufferReadback>(TEXT("ProjectJ.F.Values"));
                AddEnqueueCopyPass(Graph, Transfer->Readback.Get(), Value, Count * sizeof(uint32));
                Graph.Execute();
            });
            // Invalidate on GT after submission, before observing completion.
            // The queued render command still owns/drains the old request.
            if (Sample % 7 == 0) { ++CurrentEpoch; }
            return false;
        }
        if (State->Ready.load(std::memory_order_acquire))
        {
            Test->TestTrue(TEXT("GPU values equal CPU reference"), State->bMatches);
            const bool bCancelledGeneration = State->Epoch != CurrentEpoch;
            // Completion is still drained after cancellation; a stale result
            // never crosses the GT application boundary.
            if (!bCancelledGeneration) { ++Applied; } else { ++Discarded; }
            Csv += FString::Printf(TEXT("%d,%d,%d,%.4f,%d\n"), Sample, Sample % 2, GRHIGlobals.SupportsEfficientAsyncCompute,
                (FPlatformTime::Seconds() - Submitted) * 1000, bCancelledGeneration);
            State.Reset();
            if (++Sample == 32)
            {
                Test->TestEqual(TEXT("Live generations applied"), Applied, 27);
                Test->TestEqual(TEXT("Cancelled generations drained and discarded"), Discarded, 5);
                Test->TestTrue(TEXT("Save GPU measurements"), Save(TEXT("gpu.csv"), Csv));
                return true;
            }
            return false;
        }
        if (FPlatformTime::Seconds() - Submitted > 60 && !bTimeoutReported)
        {
            Test->AddError(TEXT("GPU readback exceeded 60 seconds; retaining resources until completion")); bTimeoutReported = true;
        }
        if (!State->PollQueued.exchange(true))
        {
            auto Transfer = State;
            ENQUEUE_RENDER_COMMAND(ProjectJ_F_GPUPoll)([Transfer](FRHICommandListImmediate&)
            {
                if (Transfer->Readback->IsReady())
                {
                    const uint32* Values = static_cast<const uint32*>(Transfer->Readback->Lock(Count * sizeof(uint32)));
                    bool bMatches = Values != nullptr;
                    if (Values)
                    {
                        for (uint32 I = 0; I < Count; ++I)
                        {
                            uint32 Value = I ^ Transfer->Seed;
                            for (int32 J = 0; J < 64; ++J) { Value ^= Value >> 13; Value = Value * 1664525u + 1013904223u; }
                            bMatches &= Values[I] == Value;
                        }
                        Transfer->Readback->Unlock();
                    }
                    Transfer->Readback.Reset();
                    Transfer->bMatches = bMatches;
                    Transfer->Ready.store(true, std::memory_order_release);
                }
                Transfer->PollQueued.store(false);
            });
        }
        return false;
    }
private:
    FAutomationTestBase* Test;
    TSharedPtr<FGpuTransfer, ESPMode::ThreadSafe> State;
    int32 Sample = 0, Applied = 0, Discarded = 0;
    uint64 CurrentEpoch = 1;
    double Submitted = 0;
    bool bTimeoutReported = false;
    FString Csv = TEXT("sample,requested_async,efficient_async_supported,host_delivery_ms,cancelled\n");
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGpuTest, "ProjectJ.GroupF.GPU.ReadbackLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)
bool FProjectJGpuTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(ProjectJ::Experiments::FGpuExperiment(this));
    return true;
}
