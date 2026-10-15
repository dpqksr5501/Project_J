#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Project_JAnimationContinuationFixture.h"
#include "Project_JGreatswordCharacter.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Animation/Project_JCharacterAnimProfile.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Animation/Project_JCombatAnimProfile.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchNormalizationSet.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNullableRefaceCycleTest,
	"ProjectJ.Continuation.NullableRefaceCycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNullableRefaceCycleTest::RunTest(const FString&)
{
	TStrongObjectPtr<UWorld> World(UWorld::CreateWorld(EWorldType::Game, false));
	FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Player = World->SpawnActor<AProject_JGreatswordCharacter>(FVector::ZeroVector, FRotator(0, 180, 0), Spawn);
	if (!TestNotNull(TEXT("Native player fixture"), Player)) { World->DestroyWorld(false); return false; }
	auto* Profile = NewObject<UProject_JCharacterAnimProfile>(Player);
	Profile->LocomotionProfile = NewObject<UProject_JLocomotionProfile>(Profile);
	Profile->LocomotionProfile->bEnableBackwardRefaceCandidates = true;
	Profile->CombatAnimProfile = NewObject<UProject_JCombatAnimProfile>(Profile);
	auto* Set = NewObject<UProject_JMotionMatchingAssetSet>(Profile);
	Profile->CombatAnimProfile->CombatStrafeMotionMatchingAssetSet = Set;
	auto* Property = FindFProperty<FObjectProperty>(Player->GetClass(), TEXT("CharacterAnimProfile"));
	if (!TestNotNull(TEXT("Authored profile property"), Property)) { World->DestroyWorld(false); return false; }
	Property->SetObjectPropertyValue_InContainer(Player, Profile);
	auto* Controller = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Facing controller"), Controller)) { World->DestroyWorld(false); return false; }
	Controller->Possess(Player);
	Controller->SetControlRotation(FRotator(0, 180, 0));
	TestTrue(TEXT("Backward facing fixture"), FMath::Abs(FMath::FindDeltaAngleDegrees(Player->GetControlRotation().Yaw, 180.0f)) < .1f);
	auto* Cycle = NewObject<UPoseSearchDatabase>(Set);
	auto* Reface = NewObject<UPoseSearchDatabase>(Set);
	Cycle->Schema = Reface->Schema = NewObject<UPoseSearchSchema>(Set);
	auto* Normalization = NewObject<UPoseSearchNormalizationSet>(Set);
	Normalization->Databases = {Cycle, Reface};
	Cycle->NormalizationSet = Reface->NormalizationSet = Normalization;
	Set->RunDatabases.BackwardRefaceCycle = Cycle; Set->RunDatabases.BackwardReface = Reface;
	TestNull(TEXT("Ordinary cycle is deliberately absent"), Set->RunDatabases.Cycle.Get());
	FProject_JAnimThreadSafeData Data;
	auto& Context = Data.MotionMatching.SelectionContext;
	Context.RotationMode = EProject_JLocomotionRotationMode::Strafe;
	Context.GaitIntent = EProject_JLocomotionGaitIntent::Run;
	Context.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
	Context.bUseGenericFamiliesForNonOrientToMovement = true; Context.bBackwardRefaceCandidates = true;
	TestTrue(TEXT("Reface pair is independently valid"), Set->FindTurnCycleCompanion(Context, Cycle) == Reface);
	Context.bBackwardRefaceCandidates = Context.bMovingTurn180 = Context.bGeneralTurnCandidates = false;
	Data.OneShotPresentation.bShouldOverrideMotionMatching = false;
	Data.Combat.bIsCombatMode = Data.bLocomotionSteeringEnabled = true;
	Data.LocomotionContext.RequestedMoveWorldDirection = FVector::ForwardVector;
	Data.Movement.GroundSpeed = 300; Data.Movement.Velocity = FVector(-300, 0, 0);
	FProject_JTurnSelectionFeedback Feedback;
	Feedback.Frame = Feedback.VisualFrame = GFrameCounter;
	Feedback.Seconds = World->GetTimeSeconds(); Feedback.Mode = Context.RotationMode; Feedback.Gait = Context.GaitIntent;
	Feedback.bRelevant = Feedback.bVisualFacingValid = true; Feedback.VisualFacingYaw = 0;
	Feedback.SelectedDatabase = Cycle->GetFName();
	TestTrue(TEXT("Completed selection reaches fresh native branch"), Feedback.IsFresh(GFrameCounter, Feedback.Seconds, Context.RotationMode));
	TestTrue(TEXT("Candidate cvar enabled"), IConsoleManager::Get().FindConsoleVariable(TEXT("p.ProjectJ.BackwardRefaceCandidates"))->GetInt() != 0);
	TestTrue(TEXT("Companion cvar enabled"), IConsoleManager::Get().FindConsoleVariable(TEXT("p.ProjectJ.TurnCycleCandidates"))->GetInt() != 0);
	TStrongObjectPtr<UProject_JAnimationContinuationFixture> Anim(NewObject<UProject_JAnimationContinuationFixture>(Player->GetMesh()));
	Anim->EvaluateBackward(Player, Feedback, Data);
	TestTrue(FString::Printf(TEXT("Valid backward reversal without ordinary cycle: %s"), *Data.BackwardRefaceReason.ToString()), Context.bBackwardRefaceCandidates);
	Feedback.bVisualFacingValid = false;
	Anim->EvaluateBackward(Player, Feedback, Data);
	TestFalse(TEXT("Invalid visual feedback still retires the candidate"), Context.bBackwardRefaceCandidates);
	World->DestroyWorld(false);
	return true;
}
#endif
