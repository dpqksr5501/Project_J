#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "System/Project_JTargetScoringSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"

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
#endif
