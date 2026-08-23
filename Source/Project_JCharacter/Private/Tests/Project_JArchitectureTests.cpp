#if WITH_DEV_AUTOMATION_TESTS

#include <limits>

#include "Misc/AutomationTest.h"

#include "Animation/Project_JLocomotionProfile.h"
#include "Animation/Project_JReplicatedAnimEventTypes.h"
#include "Animation/Project_JReplicatedJumpState.h"
#include "Components/Project_JAnimationUpdateCoordinatorComponent.h"
#include "Combat/Project_JCombatCommandSet.h"
#include "Combat/Project_JCombatHitValidation.h"
#include "Equipment/Project_JEquipmentTypes.h"
#include "GameFramework/Actor.h"
#include "Network/Project_JNetObjectFilter_Distance.h"
#include "Project_JMMOTypes.h"
#include "Project_JGameplayTags.h"
#include "Project_JLocomotionAnimTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectJMotionMatchingSearchPolicyTest,
	"ProjectJ.Architecture.MotionMatching.SearchPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJMotionMatchingSearchPolicyTest::RunTest(const FString& Parameters)
{
	FProject_JMotionMatchingSearchPolicy Policy;
	Policy.SuppressedSearchThrottleTime = 120.0f;

	TestTrue(
		TEXT("Ground locomotion keeps regular searching"),
		Policy.ShouldSearchEveryUpdate(EProject_JLocomotionPhaseFamily::Cycle, false));
	TestFalse(
		TEXT("In-air loop defaults to continuing-pose only"),
		Policy.ShouldSearchEveryUpdate(EProject_JLocomotionPhaseFamily::Fall, false));
	TestFalse(
		TEXT("Landing defaults to continuing-pose only"),
		Policy.ShouldSearchEveryUpdate(EProject_JLocomotionPhaseFamily::Landing, false));
	TestFalse(
		TEXT("Stop defaults to continuing-pose only"),
		Policy.ShouldSearchEveryUpdate(EProject_JLocomotionPhaseFamily::Stop, false));
	TestEqual(
		TEXT("Suppressed airborne search uses policy throttle"),
		Policy.ResolveSearchThrottleTime(EProject_JLocomotionPhaseFamily::Fall, false, 0.0f, false),
		120.0f);
	TestEqual(
		TEXT("Database changes retain immediate search"),
		Policy.ResolveSearchThrottleTime(EProject_JLocomotionPhaseFamily::Fall, false, 0.0f, true),
		0.0f);
	TestEqual(
		TEXT("Landing suppresses repeat one-shot searches until its database changes"),
		Policy.ResolveSearchThrottleTime(EProject_JLocomotionPhaseFamily::Landing, false, 0.0f, false),
		120.0f);
	TestEqual(
		TEXT("Stop suppresses repeat one-shot searches until its database changes"),
		Policy.ResolveSearchThrottleTime(EProject_JLocomotionPhaseFamily::Stop, false, 0.0f, false),
		120.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectJCombatHitValidationPolicyTest,
	"ProjectJ.Architecture.Combat.HitValidationPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJCombatHitValidationPolicyTest::RunTest(const FString& Parameters)
{
	FProject_JCombatHitValidationPolicy Policy;
	Policy.MaxRequestAge = 1.0f;
	Policy.MaxFutureTolerance = 0.05f;
	Policy.MaxTraceLength = 200.0f;

	FProject_JCombatHitRequest Request;
	Request.ClientTimestamp = 9.5f;
	Request.TraceStart = FVector::ZeroVector;
	Request.TraceEnd = FVector(100.0f, 0.0f, 0.0f);

	TestEqual(
		TEXT("Valid request data is accepted"),
		Policy.ValidateRequestData(Request, 10.0f),
		EProject_JCombatHitValidationFailure::None);

	Request.ClientTimestamp = 8.0f;
	TestEqual(
		TEXT("Old request is rejected"),
		Policy.ValidateRequestData(Request, 10.0f),
		EProject_JCombatHitValidationFailure::RequestTooOld);

	Request.ClientTimestamp = 10.1f;
	TestEqual(
		TEXT("Future request outside tolerance is rejected"),
		Policy.ValidateRequestData(Request, 10.0f),
		EProject_JCombatHitValidationFailure::RequestFromFuture);

	Request.ClientTimestamp = 9.5f;
	Request.TraceEnd = FVector(250.0f, 0.0f, 0.0f);
	TestEqual(
		TEXT("Excessive trace length is rejected"),
		Policy.ValidateRequestData(Request, 10.0f),
		EProject_JCombatHitValidationFailure::TraceTooLong);

	Policy.MaxTargetDistance = 600.0f;
	Policy.MaxTraceStartDistance = 100.0f;
	Policy.MinTargetFacingDot = 0.0f;
	Request.TraceStart = FVector(50.0f, 0.0f, 0.0f);
	Request.TraceEnd = FVector(150.0f, 0.0f, 0.0f);
	TestEqual(
		TEXT("Nearby forward target passes spatial validation"),
		Policy.ValidateSpatialData(
			FVector::ZeroVector,
			FVector::ForwardVector,
			FVector(200.0f, 0.0f, 0.0f),
			Request),
		EProject_JCombatHitValidationFailure::None);

	Request.TraceStart = FVector(150.0f, 0.0f, 0.0f);
	TestEqual(
		TEXT("Trace origin too far from requester is rejected"),
		Policy.ValidateSpatialData(
			FVector::ZeroVector,
			FVector::ForwardVector,
			FVector(200.0f, 0.0f, 0.0f),
			Request),
		EProject_JCombatHitValidationFailure::TraceOriginTooFar);

	Request.TraceStart = FVector::ZeroVector;
	TestEqual(
		TEXT("Target behind requester is rejected"),
		Policy.ValidateSpatialData(
			FVector::ZeroVector,
			FVector::ForwardVector,
			FVector(-200.0f, 0.0f, 0.0f),
			Request),
		EProject_JCombatHitValidationFailure::TargetOutsideAttackArc);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectJCombatCommandResolutionTest,
	"ProjectJ.Architecture.Combat.CommandResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJCombatCommandResolutionTest::RunTest(const FString& Parameters)
{
	UProject_JCombatCommandSet* CommandSet = NewObject<UProject_JCombatCommandSet>(GetTransientPackage());
	const FProject_JGameplayTags& Tags = FProject_JGameplayTags::Get();

	FProject_JCombatCommandDefinition& Command = CommandSet->Commands.AddDefaulted_GetRef();
	Command.CommandTag = Tags.InputTag_Weapon_RMB;
	Command.OrderedInputSequence = {
		Tags.InputTag_Weapon_LMB,
		Tags.InputTag_Weapon_RMB,
		Tags.InputTag_Weapon_LMB};
	Command.ResultInputTag = Tags.InputTag_Weapon_RMB;
	Command.MaxTimeBetweenInputs = 0.45f;

	TArray<FProject_JCombatCommandInputEntry> History;
	History.Add({Tags.InputTag_Weapon_LMB, 10.0});
	History.Add({Tags.InputTag_Weapon_RMB, 10.2});
	History.Add({Tags.InputTag_Weapon_LMB, 10.4});

	const FGameplayTagContainer OwnerTags;
	TestNotNull(TEXT("Light-heavy-light resolves the authored command"), CommandSet->FindBestMatch(History, OwnerTags));

	History[2].TimestampSeconds = 10.8;
	TestNull(TEXT("A gap beyond the command window does not resolve"), CommandSet->FindBestMatch(History, OwnerTags));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectJEquipmentOperationResultTest,
	"ProjectJ.Architecture.Equipment.OperationResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJEquipmentOperationResultTest::RunTest(const FString& Parameters)
{
	const FGuid InstanceId = FGuid::NewGuid();
	const FProject_JEquipmentOperationResult Success =
		FProject_JEquipmentOperationResult::Success(InstanceId, EProject_JEquipmentSlot::Weapon);
	TestTrue(TEXT("Success result is marked successful"), Success.bSucceeded);
	TestEqual(TEXT("Success preserves instance id"), Success.ItemInstanceId, InstanceId);
	TestEqual(TEXT("Success preserves slot"), Success.Slot, EProject_JEquipmentSlot::Weapon);

	const FProject_JEquipmentOperationResult Failure =
		FProject_JEquipmentOperationResult::FailureResult(
			EProject_JEquipmentOperationFailure::ItemLocked,
			InstanceId);
	TestFalse(TEXT("Failure result is not successful"), Failure.bSucceeded);
	TestEqual(TEXT("Failure preserves reason"), Failure.Failure, EProject_JEquipmentOperationFailure::ItemLocked);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectJReplicationOwnerRelevanceTest,
	"ProjectJ.Architecture.Replication.OwnerRelevance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJReplicationOwnerRelevanceTest::RunTest(const FString& Parameters)
{
	UProject_JNetObjectFilter_Distance* Filter =
		NewObject<UProject_JNetObjectFilter_Distance>(GetTransientPackage());
	AActor* Viewer = NewObject<AActor>(GetTransientPackage());
	AActor* UnrelatedOwner = NewObject<AActor>(GetTransientPackage());
	AActor* Target = NewObject<AActor>(GetTransientPackage());

	FProject_JReplicationPolicySettings Settings;
	Settings.MaxReplicationDistance = 1.0f;
	Settings.bAlwaysReplicateOwnerOrInstigator = true;

	Target->SetOwner(UnrelatedOwner);
	const FProject_JReplicationPolicyDecision UnrelatedDecision =
		Filter->BuildReplicationDecisionWithSettings(Target, FVector(1000.0f), Settings, Viewer);
	TestFalse(TEXT("An unrelated owner does not make the target relevant"), UnrelatedDecision.bShouldReplicate);
	TestFalse(
		TEXT("An unrelated owner does not add the owner relevance reason"),
		UnrelatedDecision.HasReason(EProject_JReplicationRelevanceReason::Owner));

	Target->SetOwner(Viewer);
	const FProject_JReplicationPolicyDecision OwnedDecision =
		Filter->BuildReplicationDecisionWithSettings(Target, FVector(1000.0f), Settings, Viewer);
	TestTrue(TEXT("The viewer's owned target remains relevant outside distance"), OwnedDecision.bShouldReplicate);
	TestTrue(
		TEXT("The viewer's owned target records the owner relevance reason"),
		OwnedDecision.HasReason(EProject_JReplicationRelevanceReason::Owner));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectJReplicationPolicySettingsSanitizationTest,
	"ProjectJ.Architecture.Replication.PolicySettingsSanitization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJReplicationPolicySettingsSanitizationTest::RunTest(const FString& Parameters)
{
	FProject_JReplicationPolicySettings Settings;
	Settings.MaxReplicationDistance = -100.0f;
	Settings.PartyPriorityMultiplier = 0.0f;
	Settings.GuildPriorityMultiplier = std::numeric_limits<float>::quiet_NaN();

	TestEqual(TEXT("Negative replication distance is clamped to zero"), Settings.GetMaxReplicationDistanceSquared(), 0.0f);
	TestEqual(TEXT("Party priority never drops below one"), Settings.GetPartyPriorityMultiplier(), 1.0f);
	TestEqual(TEXT("Non-finite guild priority falls back to one"), Settings.GetGuildPriorityMultiplier(), 1.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectJRemoteJumpPredictionPolicyTest,
	"ProjectJ.Architecture.Animation.RemoteJumpPrediction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJRemoteJumpPredictionPolicyTest::RunTest(const FString& Parameters)
{
	FProject_JRemoteJumpPredictionPolicy Policy;
	Policy.MinUpwardSpeed = 80.0f;

	TestTrue(
		TEXT("Ground-to-falling transition with upward velocity predicts jump start"),
		Policy.ShouldPredict(false, true, 300.0f, false, false, false));
	TestFalse(
		TEXT("Negative vertical velocity remains a fall-off"),
		Policy.ShouldPredict(false, true, -300.0f, false, false, false));
	TestFalse(
		TEXT("An already airborne proxy does not retrigger jump start"),
		Policy.ShouldPredict(true, true, 300.0f, false, false, false));
	TestFalse(
		TEXT("Server-confirmed or predicted jump state suppresses duplicate prediction"),
		Policy.ShouldPredict(false, true, 300.0f, true, false, false));
	TestFalse(
		TEXT("Landing state suppresses remote jump prediction"),
		Policy.ShouldPredict(false, true, 300.0f, false, true, false));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectJRemoteOneShotSemanticPolicyTest,
	"ProjectJ.Architecture.Animation.RemoteOneShotSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJRemoteOneShotSemanticPolicyTest::RunTest(const FString& Parameters)
{
	const FProject_JRemoteVisualLocomotionPolicy Policy;
	TestTrue(TEXT("Far Start and Stop remain budgeted by default"), Policy.bDisableStartStopChooserBeyondFarDistance);
	TestFalse(TEXT("Rare Land one-shots remain visible by default"), Policy.bDisableLandChooserBeyondFarDistance);
	TestTrue(TEXT("Urgent one-shot URO bypass is short and enabled"),
		Policy.UrgentOneShotAnimationUpdateDuration > 0.0f &&
		Policy.UrgentOneShotAnimationUpdateDuration <= 0.25f);

	const FProject_JReplicatedAnimEventState State;
	TestEqual(TEXT("Move semantic sequence starts empty"), State.MoveSequence, 0);
	TestEqual(TEXT("Landing semantic revision starts empty"), State.LandingRevision, 0);
	TestFalse(TEXT("A default replicated snapshot cannot start a stale landing"), State.bLandingActive);
	TestFalse(TEXT("Stop gait is not inferred as Sprint by default"), State.bIsSprinting);

	const UProject_JAnimationUpdateCoordinatorComponent* UpdateCoordinatorDefaults =
		GetDefault<UProject_JAnimationUpdateCoordinatorComponent>();
	TestFalse(TEXT("The presentation update coordinator adds no replication state"),
		UpdateCoordinatorDefaults->GetIsReplicated());
	TestFalse(TEXT("The presentation update coordinator is event-driven and never ticks"),
		UpdateCoordinatorDefaults->PrimaryComponentTick.bCanEverTick);

	using Project_J::Locomotion::ResolveLandingGaitIntent;
	TestEqual(TEXT("A standing landing stays Walk regardless of a stale Sprint bit"),
		ResolveLandingGaitIntent(false, true), EProject_JLocomotionGaitIntent::Walk);
	TestEqual(TEXT("A moving non-Sprint landing resolves to Run"),
		ResolveLandingGaitIntent(true, false), EProject_JLocomotionGaitIntent::Run);
	TestEqual(TEXT("A moving Sprint landing resolves to Sprint"),
		ResolveLandingGaitIntent(true, true), EProject_JLocomotionGaitIntent::Sprint);
	return true;
}

#endif
