#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/Project_JPlayerMaturityFixtures.h"
#include "Tests/Project_JMountLifecycleFixture.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "AIController.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Animation/Project_JCharacterAnimProfile.h"
#include "Animation/Project_JCombatAnimProfile.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "Components/Project_JCombatStateComponent.h"
#include "Components/Project_JPlayerInputBindingComponent.h"
#include "Mount/Project_JMountComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include <limits>

namespace
{
struct FFixWorld
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FFixWorld() { GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World); }
	~FFixWorld() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMountedInteractInputTest, "ProjectJ.MountStrafeFix.Mount.InteractInputOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMountedInteractInputTest::RunTest(const FString&)
{
	FFixWorld W;
	auto* Player = W.World->SpawnActor<AProject_JGreatswordCharacter>();
	auto* Mount = W.World->SpawnActor<AProject_JMountLifecycleFixture>();
	if (!TestNotNull(TEXT("Player fixture"), Player) || !TestNotNull(TEXT("Mount fixture"), Mount)) { return false; }
	auto* Action = NewObject<UInputAction>();
	auto* PlayerInput = NewObject<UEnhancedInputComponent>(Player);
	FProject_JPlayerInputActionSet Actions; Actions.InteractAction = Action;
	auto* Binding = Player->FindComponentByClass<UProject_JPlayerInputBindingComponent>();
	TestTrue(TEXT("Player binds effective interaction action"), Binding->BindInput(PlayerInput, Player, Actions));
	TestTrue(TEXT("Rider component retains the effective action"), Player->GetMountComponent()->GetRiderInteractAction() == Action);
	Binding->UnbindInput();
	TestTrue(TEXT("UnPossessed input cleanup preserves the mount hand-off action"), Player->GetMountComponent()->GetRiderInteractAction() == Action);
	auto* Input = NewObject<UEnhancedInputComponent>(Mount);
	Input->BindAction(Action, ETriggerEvent::Completed, Mount, &AProject_JMountLifecycleFixture::UnrelatedInput);
	Mount->SetupPlayerInputComponent(Input);
	TestEqual(TEXT("No rider yet leaves unrelated binding intact"), Input->GetActionEventBindings().Num(), 1);
	Mount->SetTestRider(Player);
	TestEqual(TEXT("Late Rider replication supplies dismount binding"), Input->GetActionEventBindings().Num(), 2);
	Mount->SetTestInteractAction(Action); Mount->SetupPlayerInputComponent(Input);
	TestEqual(TEXT("Identical authored/rider action binds only once"), Input->GetActionEventBindings().Num(), 2);
	auto* Alternate = NewObject<UInputAction>(); Mount->SetTestInteractAction(Alternate); Mount->SetupPlayerInputComponent(Input);
	TestEqual(TEXT("Distinct authored action remains supported alongside rider F"), Input->GetActionEventBindings().Num(), 3);
	auto* Replacement = NewObject<UInputAction>(); Player->GetMountComponent()->SetRiderInteractAction(Replacement);
	// SetTestRider models only the mount-side replication; rebuild on input setup as well.
	Mount->SetupPlayerInputComponent(Input);
	TestEqual(TEXT("Repeated setup never accumulates native bindings"), Input->GetActionEventBindings().Num(), 3);
	Mount->SetTestRider(nullptr);
	TestEqual(TEXT("Rider loss retires fallback but preserves authored/unrelated bindings"), Input->GetActionEventBindings().Num(), 2);
	return true;
}





IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMountedInteractDispatchTest, "ProjectJ.MountStrafeFix.Mount.InteractDispatchRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMountedInteractDispatchTest::RunTest(const FString&)
{
	FFixWorld W;
	FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Rider = W.World->SpawnActor<AProject_JPlayerMaturityASCFixture>(Params);
	auto* Mount = W.World->SpawnActor<AProject_JMountLifecycleFixture>(Params);
	auto* Controller = W.World->SpawnActor<AAIController>(); Controller->Possess(Rider);
	auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Rider); ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Rider, Rider); Rider->CurrentASC = ASC;
	auto* RiderMount = NewObject<UProject_JMountComponent>(Rider); RiderMount->RegisterComponent();
	auto* Action = NewObject<UInputAction>(); RiderMount->SetRiderInteractAction(Action);
	TestTrue(TEXT("Native mount possesses the rider controller"), Mount->TryMountRider(Rider));
	auto* Input = NewObject<UEnhancedInputComponent>(Mount); Mount->SetupPlayerInputComponent(Input);
	if (!TestEqual(TEXT("Actual session inherits exactly one Started action"), Input->GetActionEventBindings().Num(), 1)) { return false; }
	// Enhanced Input dispatches cloned bindings, so removing the owned binding
	// during UnPossessed cannot destroy the delegate currently executing.
	auto Dispatch = Input->GetActionEventBindings()[0]->Clone();
	FInputActionInstance Instance(Action); Dispatch->Execute(Instance);
	TestTrue(TEXT("Interaction Started restores controller possession"), Controller->GetPawn() == Rider);
	TestFalse(TEXT("Input dismount retires the mounted tag"), ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Mounted));
	TestFalse(TEXT("Input dismount clears the rider relationship"), RiderMount->IsMounted());
	TestEqual(TEXT("UnPossessed retires owned action bindings"), Input->GetActionEventBindings().Num(), 0);
	Dispatch->Execute(Instance);
	TestTrue(TEXT("A queued duplicate cannot steal rider possession"), Controller->GetPawn() == Rider);
	TestTrue(TEXT("Same action supports remount"), Mount->TryMountRider(Rider));
	Mount->SetupPlayerInputComponent(Input);
	if (!TestEqual(TEXT("Remount recreates a single owned binding"), Input->GetActionEventBindings().Num(), 1)) { return false; }
	auto Next = Input->GetActionEventBindings()[0]->Clone(); Next->Execute(Instance);
	TestTrue(TEXT("Second interaction cycle also restores possession"), Controller->GetPawn() == Rider);
	return true;
}
#endif
