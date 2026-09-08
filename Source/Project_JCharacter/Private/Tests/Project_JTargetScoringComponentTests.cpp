#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "System/Project_JTargetScoringSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "HAL/PlatformTime.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJTargetScoringComponentTest, "ProjectJ.AsyncTargeting.ComponentContext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJTargetScoringComponentTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	auto* Subsystem = World->GetSubsystem<UProject_JTargetScoringSubsystem>();
	const auto Spawn = [World](const FVector& Location)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		auto* Root = NewObject<USceneComponent>(Actor);
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->SetActorLocation(Location);
		return Actor;
	};
	AActor* Owner = Spawn(FVector::ZeroVector);
	AActor* Target = Spawn(FVector(100, 0, 0));
	auto* Equipment = NewObject<UProject_JEquipmentManagerComponent>(Owner);
	Owner->AddInstanceComponent(Equipment);
	Equipment->RegisterComponent();
	auto* Component = NewObject<UProject_JTargetScoringComponent>(Owner);
	Owner->AddInstanceComponent(Component);
	Component->RegisterComponent();
	// Keep this synchronous policy fixture explicit; the default worker path is covered below.
	Component->Execution = EProject_JTargetScoringExecution::Serial;
	TestTrue(TEXT("Optional component accepts explicit candidates"), Component->RequestTargets({Owner, nullptr, Target}));
	Subsystem->Tick(0);
	TestEqual(TEXT("Valid result resolves weak actor on GT"), Component->GetLastScoredTarget(), Target);
	Component->RequestTargets({Target});
	Equipment->OnEquipmentEquipped.Broadcast(EProject_JEquipmentSlot::None, nullptr);
	Subsystem->Tick(0);
	TestNull(TEXT("Equipment change invalidates old selection and queued result"), Component->GetLastScoredTarget());
	TestEqual(TEXT("Equipment invalidation releases queued query"), Subsystem->GetPendingCount(), 0);
	Component->RequestTargets({Target});
	Component->InvalidateQueryContext();
	Subsystem->Tick(0);
	TestNull(TEXT("Explicit context change discards result"), Component->GetLastScoredTarget());
	Component->RequestTargets({Target});
	Target->SetActorLocation(FVector(99999, 0, 0));
	Subsystem->Tick(0);
	TestNull(TEXT("Current range revalidated after snapshot"), Component->GetLastScoredTarget());
	Target->SetActorLocation(FVector(100, 0, 0));
	Component->RequestTargets({Target});
	Target->Destroy();
	Subsystem->Tick(0);
	TestNull(TEXT("Destroyed candidate discarded"), Component->GetLastScoredTarget());
	Component->RequestTargets({Owner});
	Component->DestroyComponent();
	TestEqual(TEXT("Component destruction before BeginPlay cancels request"), Subsystem->GetPendingCount(), 0);
	World->DestroyWorld(false);
	return true;
}

namespace
{
	class FProjectJDefaultParallelComponentCommand : public IAutomationLatentCommand
	{
	public:
		explicit FProjectJDefaultParallelComponentCommand(FAutomationTestBase* InTest)
			: Test(InTest), Started(FPlatformTime::Seconds())
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Subsystem = World->GetSubsystem<UProject_JTargetScoringSubsystem>();
			AActor* Owner = World->SpawnActor<AActor>();
			Equipment = NewObject<UProject_JEquipmentManagerComponent>(Owner);
			Owner->AddInstanceComponent(Equipment);
			Equipment->RegisterComponent();
			Component = NewObject<UProject_JTargetScoringComponent>(Owner);
			Owner->AddInstanceComponent(Component);
			Component->RegisterComponent();
			Test->TestTrue(TEXT("New component defaults to TaskParallelFor"),
				Component->Execution == EProject_JTargetScoringExecution::TaskParallelFor);
			// More than one scoring batch, with a known nearest/front-facing winner.
			for (int32 Index = 0; Index < 257; ++Index)
			{
				AActor* Candidate = World->SpawnActor<AActor>();
				auto* Root = NewObject<USceneComponent>(Candidate);
				Candidate->SetRootComponent(Root);
				Root->RegisterComponent();
				Candidate->SetActorLocation(FVector(100 + Index * 5, 0, 0));
				Candidates.Add(Candidate);
			}
			Test->TestTrue(TEXT("Default parallel query accepted"), Component->RequestTargets(Candidates));
			Subsystem->Tick(0);
			Test->TestNull(TEXT("Dispatch does not compute and apply synchronously on GT"), Component->GetLastScoredTarget());
		}
		virtual ~FProjectJDefaultParallelComponentCommand() override { World->DestroyWorld(false); }
		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 10.0)
			{
				Test->AddError(TEXT("Default parallel component timed out"));
				return true;
			}
			Subsystem->Tick(0);
			if (Subsystem->GetPendingCount() != 0) { return false; }
			if (!bTestingCancellation)
			{
				Test->TestEqual(TEXT("Default parallel result resolves the expected actor"), Component->GetLastScoredTarget(), Candidates[0]);
				Test->TestTrue(TEXT("Second default parallel query accepted"), Component->RequestTargets(Candidates));
				Subsystem->Tick(0);
				Equipment->OnEquipmentEquipped.Broadcast(EProject_JEquipmentSlot::None, nullptr);
				bTestingCancellation = true;
				return false;
			}
			Test->TestNull(TEXT("Equipment change suppresses the dispatched parallel result"), Component->GetLastScoredTarget());
			return true;
		}
	private:
		FAutomationTestBase* Test;
		double Started;
		UWorld* World = nullptr;
		UProject_JTargetScoringSubsystem* Subsystem = nullptr;
		UProject_JTargetScoringComponent* Component = nullptr;
		UProject_JEquipmentManagerComponent* Equipment = nullptr;
		TArray<AActor*> Candidates;
		bool bTestingCancellation = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJDefaultParallelComponentTest, "ProjectJ.AsyncTargeting.ComponentDefaultParallel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJDefaultParallelComponentTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FProjectJDefaultParallelComponentCommand(this));
	return true;
}
#endif
