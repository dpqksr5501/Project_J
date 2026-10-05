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
#include "Animation/Project_JMotionMatchingAssetSet.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCombatFacingTransitionTest, "ProjectJ.MountStrafeFix.Combat.GradualFacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCombatFacingTransitionTest::RunTest(const FString&)
{
	FFixWorld W;
	auto* Player = W.World->SpawnActor<AProject_JCombatFacingFixture>();
	auto* Controller = W.World->SpawnActor<AAIController>(); Controller->Possess(Player);
	auto* Profile = NewObject<UProject_JCharacterAnimProfile>();
	auto* Combat = NewObject<UProject_JCombatAnimProfile>(); Profile->CombatAnimProfile = Combat; Player->CharacterAnimProfile = Profile;
	auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Player); ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Player, Player);
	Player->FindComponentByClass<UProject_JCombatStateComponent>()->BindToAbilitySystem(ASC);
	const auto CombatTag = FProject_JGameplayTags::Get().State_CombatMode;
	ASC->AddProjectJLooseGameplayTag(CombatTag, false);
	auto* Movement = Player->GetCharacterMovement(); Movement->SetMovementMode(MOVE_Walking);
	Movement->Velocity = FVector(-300, 0, 0);
	Combat->CombatFacingRotationRateYaw = 360.0f;
	Player->UpdateTestMovementPolicy();
	for (const int32 FPS : {30, 60, 120})
	{
		const float Delta = 1.0f / FPS;
		Player->SetActorRotation(FRotator(0, 180, 0)); Controller->SetControlRotation(FRotator::ZeroRotator);
		Player->ApplyTestRotationMode(true);
		TestFalse(TEXT("Combat does not enable snapping Pawn::FaceRotation"), Player->bUseControllerRotationYaw);
		TestTrue(TEXT("Moving Strafe uses bounded CMC desired rotation"), Movement->bUseControllerDesiredRotation && !Movement->bOrientRotationToMovement);
		Player->FaceRotation(Controller->GetControlRotation(), Delta);
		TestTrue(TEXT("Entering combat preserves the initial actor yaw"), FMath::Abs(FMath::FindDeltaAngleDegrees(180, Player->GetActorRotation().Yaw)) < 0.01f);
		float PreviousRemaining = 180.0f;
		for (int32 Frame = 0; Frame < FPS / 2; ++Frame)
		{
			const float BeforeYaw = Player->GetActorRotation().Yaw;
			Movement->PhysicsRotation(Delta);
			const float Step = FMath::Abs(FMath::FindDeltaAngleDegrees(BeforeYaw, Player->GetActorRotation().Yaw));
			const float Remaining = FMath::Abs(FMath::FindDeltaAngleDegrees(Player->GetActorRotation().Yaw, 0));
			TestTrue(TEXT("Per-frame yaw respects the authored angular rate"), Step <= 360.0f * Delta + 0.01f);
			TestTrue(TEXT("Actor approaches camera yaw without overshoot"), Remaining <= PreviousRemaining + 0.01f);
			PreviousRemaining = Remaining;
		}
		TestTrue(TEXT("180 degree turn completes in half a second at every cadence"), PreviousRemaining < 0.1f);
	}
	Player->SetActorRotation(FRotator(0, 170, 0)); Controller->SetControlRotation(FRotator(0, -170, 0));
	Movement->PhysicsRotation(1.0f / 60.0f);
	TestTrue(TEXT("Crossing +/-180 takes the short angular path"), FMath::Abs(FMath::FindDeltaAngleDegrees(Player->GetActorRotation().Yaw, -170)) < 20.0f);
	TestFalse(TEXT("Combat never enables remote straight-running trajectory repair"), Player->AllowsStraightRunningTrajectoryRepair());
	Combat->CombatFacingRotationRateYaw = std::numeric_limits<float>::quiet_NaN(); Player->UpdateTestMovementPolicy();
	TestEqual(TEXT("Invalid authored rotation rate uses a finite default"), Movement->RotationRate.Yaw, 360.0);
	Movement->Velocity = FVector::ZeroVector; Player->ApplyTestRotationMode(true);
	TestFalse(TEXT("Idle TIP retains authored yaw ownership"), Movement->bUseControllerDesiredRotation);
	const float IdleYaw = Player->GetActorRotation().Yaw; Movement->PhysicsRotation(0.1f);
	TestTrue(TEXT("CMC does not compete with idle TIP"), FMath::Abs(FMath::FindDeltaAngleDegrees(IdleYaw, Player->GetActorRotation().Yaw)) < 0.01f);
	Movement->Velocity = FVector(-300, 0, 0); Movement->SetMovementMode(MOVE_Falling); Player->ApplyTestRotationMode(true);
	TestFalse(TEXT("Air interpolation keeps its separate owner"), Movement->bUseControllerDesiredRotation);
	Movement->SetMovementMode(MOVE_Walking); ASC->RemoveProjectJLooseGameplayTag(CombatTag, false);
	Player->ApplyTestRotationMode(false); Player->UpdateTestMovementPolicy();
	TestTrue(TEXT("Leaving combat restores OTM rotation"), Movement->bOrientRotationToMovement && !Movement->bUseControllerDesiredRotation);
	TestEqual(TEXT("OTM retains its original movement rotation tuning"), Movement->RotationRate.Yaw, static_cast<double>(Player->WalkRotationRateYaw));
	TestTrue(TEXT("OTM can still repair remote straight-running trajectory"), Player->AllowsStraightRunningTrajectoryRepair());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCombatFacingSettledCycleTest, "ProjectJ.MountStrafeFix.Combat.SettleAfterFacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCombatFacingSettledCycleTest::RunTest(const FString&)
{
	FFixWorld W;
	auto* Player = W.World->SpawnActor<AProject_JCombatFacingFixture>();
	auto* Controller = W.World->SpawnActor<AAIController>(); Controller->Possess(Player);
	auto* Profile = NewObject<UProject_JCharacterAnimProfile>();
	auto* Combat = NewObject<UProject_JCombatAnimProfile>(); Profile->CombatAnimProfile = Combat; Player->CharacterAnimProfile = Profile;
	auto* Set = NewObject<UProject_JMotionMatchingAssetSet>(); Combat->CombatStrafeMotionMatchingAssetSet = Set;
	Set->RunDatabases.Cycle = NewObject<UPoseSearchDatabase>(); Set->RunDatabases.SettledCycle = NewObject<UPoseSearchDatabase>();
	auto* State = Player->GetLocomotionAnimStateComponent();
	State->bUsingLocalInputState = true;
	State->AuthoritativeContext.bCombatMode = true;
	State->AuthoritativeContext.RotationMode = EProject_JLocomotionRotationMode::Strafe;
	State->AuthoritativeContext.GaitIntent = EProject_JLocomotionGaitIntent::Run;
	State->DerivedLocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
	State->KinematicContext.bHasMoveInput = true; State->KinematicContext.GroundSpeed = 300;
	Player->SetActorRotation(FRotator(0, 180, 0)); Controller->SetControlRotation(FRotator::ZeroRotator);
	for (int32 Frame = 0; Frame < 10; ++Frame) { State->UpdateCombatStrafeCycleSelection(0.1f, *Player); }
	TestFalse(TEXT("Stable camera/input cannot settle while actor is still facing away"), State->bUseCombatStrafeSettledCycle);
	TestEqual(TEXT("Turn catch-up does not accumulate settled time"), State->CombatStrafeSettledCycleElapsedTime, 0.0f);
	Player->SetActorRotation(FRotator::ZeroRotator);
	State->UpdateCombatStrafeCycleSelection(0.15f, *Player);
	TestFalse(TEXT("Alignment still observes the authored settle delay"), State->bUseCombatStrafeSettledCycle);
	State->UpdateCombatStrafeCycleSelection(0.15f, *Player);
	TestTrue(TEXT("Aligned stable facing can enter the loop-only cycle"), State->bUseCombatStrafeSettledCycle);
	Player->SetActorRotation(FRotator(0, 20, 0)); State->UpdateCombatStrafeCycleSelection(0.1f, *Player);
	TestFalse(TEXT("Losing facing alignment immediately restores Dynamic Cycle"), State->bUseCombatStrafeSettledCycle);
	Player->SetActorRotation(FRotator(0, 179, 0)); Controller->SetControlRotation(FRotator(0, -179, 0));
	State->UpdateCombatStrafeCycleSelection(0.1f, *Player);
	State->UpdateCombatStrafeCycleSelection(0.15f, *Player); State->UpdateCombatStrafeCycleSelection(0.15f, *Player);
	TestTrue(TEXT("Facing tolerance handles the +/-180 wrap"), State->bUseCombatStrafeSettledCycle);
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
