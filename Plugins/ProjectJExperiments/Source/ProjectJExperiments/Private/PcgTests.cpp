#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/WorldSettings.h"
#include "Components/BoxComponent.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "Elements/PCGCreatePointsGrid.h"
#include "Data/PCGBasePointData.h"
#include "ExperimentOutput.h"

namespace ProjectJ::Experiments
{
class FPcgExperiment final : public IAutomationLatentCommand
{
public:
    explicit FPcgExperiment(FAutomationTestBase* InTest) : Test(InTest) {}
    virtual ~FPcgExperiment() override
    {
        if (World)
        {
            for (auto* C : Components) { C->CancelGeneration(); C->CleanupLocalImmediate(true); }
            World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
        }
    }
    virtual bool Update() override
    {
        if (!World)
        {
            World = UWorld::CreateWorld(EWorldType::Game, false);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->InitializeActorsForPlay(FURL());
            World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
            auto* Graph = NewObject<UPCGGraph>(World);
            UPCGCreatePointsGridSettings* Settings;
            auto* Grid = Graph->AddNodeOfType(Settings);
            Settings->GridExtents = FVector(3200, 3200, 50); Settings->CellSize = FVector(100);
            Graph->AddEdge(Grid, PCGPinConstants::DefaultOutputLabel, Graph->GetOutputNode(), PCGPinConstants::DefaultOutputLabel);
            // Four components form a hard admission bound for this fixture.
            // The test never creates or queues one component per crowd member.
            for (int32 I = 0; I < 4; ++I)
            {
                auto* Owner = World->SpawnActor<AActor>();
                auto* Bounds = NewObject<UBoxComponent>(Owner); Owner->SetRootComponent(Bounds);
                Bounds->SetBoxExtent(FVector(3200, 3200, 50));
                Bounds->SetCanEverAffectNavigation(false); Bounds->RegisterComponent();
                auto* C = NewObject<UPCGComponent>(Owner); Owner->AddInstanceComponent(C);
                C->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;
                C->Seed = 20260912; C->SetGraph(Graph); C->RegisterComponent(); Components.Add(C);
            }
        }
        const double Start = FPlatformTime::Seconds();
        World->Tick(LEVELTICK_All, 1.0f / 60.0f);
        PeakTickUs = FMath::Max(PeakTickUs, (FPlatformTime::Seconds() - Start) * 1e6);
        if (!bSubmitted)
        {
            Submitted = FPlatformTime::Seconds(); PeakTickUs = 0;
            for (auto* C : Components)
            {
                Test->TestTrue(TEXT("PCG accepted bounded generation"), C->GenerateLocalGetTaskId(true) != InvalidPCGTaskId);
                if (Round == 2) { C->CancelGeneration(); }
            }
            bSubmitted = true; return false;
        }
        if (FPlatformTime::Seconds() - Submitted > 30) { Test->AddError(TEXT("PCG generation did not finish in 30 seconds")); return true; }
        for (auto* C : Components) { if (C->IsGenerating()) { return false; } }
        if (++SettledFrames < 4) { return false; }
        for (int32 I = 0; I < Components.Num(); ++I)
        {
            auto* C = Components[I]; int32 Count = 0; uint32 Hash = 0;
            for (const auto& Tagged : C->GetGeneratedGraphOutput().TaggedData)
            {
                if (const auto* Points = Cast<UPCGBasePointData>(Tagged.Data))
                {
                    Count += Points->GetNumPoints();
                    const auto Transforms = Points->GetConstTransformValueRange();
                    const auto Seeds = Points->GetConstSeedValueRange();
                    for (int32 P = 0; P < Points->GetNumPoints(); ++P)
                    { Hash = HashCombineFast(Hash, HashCombineFast(GetTypeHash(Transforms[P].GetLocation()), GetTypeHash(Seeds[P]))); }
                }
            }
            if (Round < 2)
            {
                Test->TestEqual(TEXT("Real graph produces bounded grid"), Count, 4096);
                if (Round == 0) { Baselines.Add(Hash); }
                else { Test->TestEqual(TEXT("Same seed and input reproduce output after cleanup"), Hash, Baselines[I]); }
            }
            else { Test->TestEqual(TEXT("Cancelled generation publishes no late output"), Count, 0); }
            Csv += FString::Printf(TEXT("%d,%d,%d,%u,%.4f,%.4f\n"), Round, I, Count, Hash,
                (FPlatformTime::Seconds() - Submitted) * 1000, PeakTickUs);
            C->CleanupLocalImmediate(true);
            Test->TestTrue(TEXT("Stream-out cleanup releases generated output"), C->GetGeneratedGraphOutput().TaggedData.IsEmpty());
        }
        bSubmitted = false; SettledFrames = 0;
        if (++Round == 3) { Test->TestTrue(TEXT("Save PCG measurements"), Save(TEXT("pcg.csv"), Csv)); return true; }
        return false;
    }
private:
    FAutomationTestBase* Test;
    UWorld* World = nullptr;
    TArray<UPCGComponent*> Components;
    TArray<uint32> Baselines;
    int32 Round = 0, SettledFrames = 0;
    double Submitted = 0, PeakTickUs = 0;
    bool bSubmitted = false;
    FString Csv = TEXT("round,component,points,hash,delivery_ms,peak_world_tick_us\n");
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPcgTest, "ProjectJ.GroupF.PCG.GenerationLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPcgTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(ProjectJ::Experiments::FPcgExperiment(this)); return true;
}
