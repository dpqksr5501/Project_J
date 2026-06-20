#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Animation/Project_JLocomotionProfile.h"
#include "Combat/Project_JCombatHitValidation.h"
#include "Equipment/Project_JEquipmentTypes.h"

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
	TestEqual(
		TEXT("Suppressed airborne search uses policy throttle"),
		Policy.ResolveSearchThrottleTime(EProject_JLocomotionPhaseFamily::Fall, false, 0.0f, false),
		120.0f);
	TestEqual(
		TEXT("Database changes retain immediate search"),
		Policy.ResolveSearchThrottleTime(EProject_JLocomotionPhaseFamily::Fall, false, 0.0f, true),
		0.0f);

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

#endif
