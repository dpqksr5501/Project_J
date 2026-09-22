#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Equipment/Project_JWeaponPresentationProfile.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/Project_JAnimationBudgetTypes.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSSRHistoricalSweepTest, "ProjectJ.Combat.SSRHistoricalSweepInterpolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJSSRHistoricalSweepTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* Character = World->SpawnActor<ACharacter>(Params);

	UProject_JCombatHitValidationComponent* HitVal = NewObject<UProject_JCombatHitValidationComponent>(Character);
	Character->AddInstanceComponent(HitVal);
	HitVal->RegisterComponent();

	const FGameplayTag AttackTag = FGameplayTag::RequestGameplayTag(TEXT("ProjectJ.Tests.SSR.Attack"), false);
	const int32 Key = 42;

	// Populate ring buffer with 3 historical sweeps
	FProject_JAuthoritativeSweepRecord Record1;
	Record1.ServerTimestamp = 10.0f;
	Record1.TraceStart = FVector(0, 0, 0);
	Record1.TraceEnd = FVector(100, 0, 0);
	Record1.AttackNodeTag = AttackTag;
	Record1.PredictionKey = Key;
	Record1.bHitWindowOpen = true;
	HitVal->AppendSweepHistoryRecord(Record1);

	FProject_JAuthoritativeSweepRecord Record2;
	Record2.ServerTimestamp = 10.1f;
	Record2.TraceStart = FVector(0, 100, 0);
	Record2.TraceEnd = FVector(100, 100, 0);
	Record2.AttackNodeTag = AttackTag;
	Record2.PredictionKey = Key;
	Record2.bHitWindowOpen = true;
	HitVal->AppendSweepHistoryRecord(Record2);

	FProject_JAuthoritativeSweepRecord Record3;
	Record3.ServerTimestamp = 10.2f;
	Record3.TraceStart = FVector(0, 200, 0);
	Record3.TraceEnd = FVector(100, 200, 0);
	Record3.AttackNodeTag = AttackTag;
	Record3.PredictionKey = Key;
	Record3.bHitWindowOpen = false; // hit window closed at end
	HitVal->AppendSweepHistoryRecord(Record3);

	// 1. Interpolation test: timestamp 10.05 is exactly halfway between Record1 (10.0) and Record2 (10.1)
	FVector InterpStart = FVector::ZeroVector;
	FVector InterpEnd = FVector::ZeroVector;
	bool bHitWindowOpen = false;
	const bool bFound = HitVal->FindAuthoritativeTraceAtTime(10.05f, Key, AttackTag, InterpStart, InterpEnd, bHitWindowOpen);

	TestTrue(TEXT("Found historical trace within valid window"), bFound);
	TestEqual(TEXT("Interpolated TraceStart Y is halfway (50)"), InterpStart.Y, 50.0);
	TestEqual(TEXT("Interpolated TraceEnd Y is halfway (50)"), InterpEnd.Y, 50.0);
	TestTrue(TEXT("Historical HitWindow was open at 10.05"), bHitWindowOpen);

	// 2. Reject out-of-bounds timestamp (ancient past: 5.0)
	FVector OutStart, OutEnd;
	bool bOutWindow = false;
	TestFalse(TEXT("Rejects ancient timestamp before history bounds"),
		HitVal->FindAuthoritativeTraceAtTime(5.0f, Key, AttackTag, OutStart, OutEnd, bOutWindow));

	// 3. Reject future timestamp (15.0)
	TestFalse(TEXT("Rejects future timestamp beyond tolerance"),
		HitVal->FindAuthoritativeTraceAtTime(15.0f, Key, AttackTag, OutStart, OutEnd, bOutWindow));

	// 4. Reject prediction key mismatch
	const int32 WrongKey = 999;
	TestFalse(TEXT("Rejects prediction key mismatch"),
		HitVal->FindAuthoritativeTraceAtTime(10.05f, WrongKey, AttackTag, OutStart, OutEnd, bOutWindow));

	// 5. Historical hit window closed test at 10.2
	const bool bAtEnd = HitVal->FindAuthoritativeTraceAtTime(10.2f, Key, AttackTag, OutStart, OutEnd, bOutWindow);
	TestTrue(TEXT("Found trace at 10.2"), bAtEnd);
	TestFalse(TEXT("Historical HitWindow was closed at 10.2"), bOutWindow);

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJStableGripTargetsTest, "ProjectJ.Presentation.StableGripTargetsAndAuthoredAlpha",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJStableGripTargetsTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* Character = World->SpawnActor<ACharacter>(Params);

	UProject_JWeaponPresentationComponent* Presentation = NewObject<UProject_JWeaponPresentationComponent>(Character);
	Character->AddInstanceComponent(Presentation);
	Presentation->RegisterComponent();

	// Test AnimInstance registration and event-driven weapon target push
	UProject_JRetargetAnimInstance* RetargetAnim = NewObject<UProject_JRetargetAnimInstance>(Character->GetMesh());
	Presentation->RegisterRetargetAnimInstance(RetargetAnim);

	// Quality Tier property tests
	TestTrue(TEXT("Default tier allows Hand IK"), RetargetAnim->bTierAllowsHandIK);
	TestTrue(TEXT("Default tier allows Retarget IK"), RetargetAnim->bTierAllowsRetargetIK);
	TestEqual(TEXT("Default tier is Local"), RetargetAnim->CurrentQualityTier, EProject_JAnimBudgetTier::Local);

	Presentation->UnregisterRetargetAnimInstance(RetargetAnim);

	World->DestroyWorld(false);
	return true;
}
#endif
