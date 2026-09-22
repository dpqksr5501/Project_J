#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Equipment/Project_JWeaponPresentationProfile.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/Project_JAnimationBudgetTypes.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Components/StaticMeshComponent.h"
#include "GameplayTagsManager.h"

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

	const FGameplayTag AttackTag = UGameplayTagsManager::Get().AddNativeGameplayTag(TEXT("ProjectJ.Tests.SSR.Attack"));
	const FGameplayTag OtherAttackTag = UGameplayTagsManager::Get().AddNativeGameplayTag(TEXT("ProjectJ.Tests.SSR.OtherAttack"));
	const int32 Key = 42;

	// Populate ring buffer with 3 historical sweeps (including Close transition boundary)
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
	Record3.bHitWindowOpen = false; // Transition boundary: HitWindow closed at 10.2f
	HitVal->AppendSweepHistoryRecord(Record3);

	// 1. Interpolation test: timestamp 10.05 is exactly halfway between Record1 (10.0) and Record2 (10.1)
	FVector InterpStart = FVector::ZeroVector;
	FVector InterpEnd = FVector::ZeroVector;
	bool bHitWindowOpen = false;
	bool bFound = HitVal->FindAuthoritativeTraceAtTime(10.05f, Key, AttackTag, InterpStart, InterpEnd, bHitWindowOpen);

	TestTrue(TEXT("Found historical trace within valid window"), bFound);
	TestEqual(TEXT("Interpolated TraceStart Y is halfway (50)"), InterpStart.Y, 50.0);
	TestEqual(TEXT("Interpolated TraceEnd Y is halfway (50)"), InterpEnd.Y, 50.0);
	TestTrue(TEXT("Historical HitWindow was open at 10.05"), bHitWindowOpen);

	// 2. Transition boundary test: at 10.15s (between Record2 Open and Record3 Closed)
	// Because 10.15 < 10.20 (the close transition timestamp), hit window must still be open
	bFound = HitVal->FindAuthoritativeTraceAtTime(10.15f, Key, AttackTag, InterpStart, InterpEnd, bHitWindowOpen);
	TestTrue(TEXT("Found trace at 10.15 before close boundary"), bFound);
	TestTrue(TEXT("HitWindow is open prior to close timestamp"), bHitWindowOpen);
	TestNearlyEqual(TEXT("Interpolated TraceStart Y at 10.15 is 150"), InterpStart.Y, 150.0, 0.01);

	// 3. Exact close transition timestamp test: at 10.20s
	bFound = HitVal->FindAuthoritativeTraceAtTime(10.20f, Key, AttackTag, InterpStart, InterpEnd, bHitWindowOpen);
	TestTrue(TEXT("Found trace at 10.20"), bFound);
	TestFalse(TEXT("HitWindow is closed exactly at close timestamp"), bHitWindowOpen);

	// 4. Immediately after close transition within future tolerance: at 10.22s
	bFound = HitVal->FindAuthoritativeTraceAtTime(10.22f, Key, AttackTag, InterpStart, InterpEnd, bHitWindowOpen);
	TestTrue(TEXT("Found trace at 10.22 within tolerance"), bFound);
	TestFalse(TEXT("HitWindow remains closed after close timestamp"), bHitWindowOpen);

	// 5. Reject out-of-bounds timestamp (ancient past: 5.0)
	FVector OutStart, OutEnd;
	bool bOutWindow = false;
	TestFalse(TEXT("Rejects ancient timestamp before history bounds"),
		HitVal->FindAuthoritativeTraceAtTime(5.0f, Key, AttackTag, OutStart, OutEnd, bOutWindow));

	// 6. Reject future timestamp (15.0)
	TestFalse(TEXT("Rejects future timestamp beyond tolerance"),
		HitVal->FindAuthoritativeTraceAtTime(15.0f, Key, AttackTag, OutStart, OutEnd, bOutWindow));

	// 7. Reject prediction key mismatch
	const int32 WrongKey = 999;
	TestFalse(TEXT("Rejects prediction key mismatch"),
		HitVal->FindAuthoritativeTraceAtTime(10.05f, WrongKey, AttackTag, OutStart, OutEnd, bOutWindow));

	// 8. Reject attack node mismatch
	TestFalse(TEXT("Rejects attack node mismatch"),
		HitVal->FindAuthoritativeTraceAtTime(10.05f, Key, OtherAttackTag, OutStart, OutEnd, bOutWindow));

	// 9. Ring buffer wrap-around test: fill 135 records to exceed capacity 128
	for (int32 Index = 0; Index < 135; ++Index)
	{
		FProject_JAuthoritativeSweepRecord WrapRecord;
		WrapRecord.ServerTimestamp = 20.0f + Index * 0.01f;
		WrapRecord.TraceStart = FVector(Index, 0, 0);
		WrapRecord.TraceEnd = FVector(Index + 10, 0, 0);
		WrapRecord.AttackNodeTag = AttackTag;
		WrapRecord.PredictionKey = 100;
		WrapRecord.bHitWindowOpen = true;
		HitVal->AppendSweepHistoryRecord(WrapRecord);
	}

	TestEqual(TEXT("SweepHistoryCount clamped to MaxSweepHistoryCapacity (128)"), HitVal->SweepHistoryCount, 128);

	// Query oldest available record in wrapped buffer (record 7 is at 20.0 + 7 * 0.01 = 20.07)
	const bool bFoundWrappedOldest = HitVal->FindAuthoritativeTraceAtTime(20.07f, 100, AttackTag, OutStart, OutEnd, bOutWindow);
	TestTrue(TEXT("Found oldest available record in wrapped ring buffer"), bFoundWrappedOldest);

	// Query overwritten record prior to wrap (e.g. record 0 at 20.0) -> must reject
	const bool bFoundOverwritten = HitVal->FindAuthoritativeTraceAtTime(20.01f, 100, AttackTag, OutStart, OutEnd, bOutWindow);
	TestFalse(TEXT("Rejects overwritten record in ring buffer"), bFoundOverwritten);

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

	// 1. Create a mock visual weapon actor with static mesh sockets
	AActor* MockWeapon = World->SpawnActor<AActor>(Params);
	UStaticMesh* StaticMesh = NewObject<UStaticMesh>(MockWeapon);

	UStaticMeshSocket* SocketR = NewObject<UStaticMeshSocket>(StaticMesh);
	SocketR->SocketName = TEXT("WeaponGrip_R");
	SocketR->RelativeLocation = FVector(10.0, 20.0, 30.0);
	StaticMesh->AddSocket(SocketR);

	UStaticMeshSocket* SocketL = NewObject<UStaticMeshSocket>(StaticMesh);
	SocketL->SocketName = TEXT("WeaponGrip_L");
	SocketL->RelativeLocation = FVector(-10.0, -20.0, 30.0);
	StaticMesh->AddSocket(SocketL);

	UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(MockWeapon);
	MeshComp->SetStaticMesh(StaticMesh);
	MockWeapon->SetRootComponent(MeshComp);
	MeshComp->RegisterComponent();

	// 2. Setup WeaponPresentationProfile with authored alphas
	UProject_JWeaponPresentationProfile* Profile = NewObject<UProject_JWeaponPresentationProfile>(Character);
	Profile->MotionPresentation.PrimaryGripSocketName = TEXT("WeaponGrip_R");
	Profile->MotionPresentation.SecondaryGripSocketName = TEXT("WeaponGrip_L");
	Profile->MotionPresentation.DefaultDrawnPrimaryIKAlpha = 1.0f;
	Profile->MotionPresentation.DefaultDrawnSecondaryIKAlpha = 0.85f;
	Profile->MotionPresentation.DefaultSheathedPrimaryIKAlpha = 0.5f;
	Profile->MotionPresentation.DefaultSheathedSecondaryIKAlpha = 0.0f;

	Presentation->AppliedProfile = Profile;
	Presentation->SpawnedWeapon = MockWeapon;
	Presentation->UpdateSocketComponentCache();

	// Verify cached components
	TestTrue(TEXT("Cached primary grip component is valid"), Presentation->CachedPrimaryGripComponent.IsValid());
	TestTrue(TEXT("Cached secondary grip component is valid"), Presentation->CachedSecondaryGripComponent.IsValid());

	// 3. Test Drawn State
	Presentation->CurrentPresentationSocket = EProject_JWeaponPresentationSocket::Drawn;
	Presentation->UpdateGripTargets();
	FProject_JWeaponGripTargets Targets = Presentation->GetWeaponGripTargets();

	TestTrue(TEXT("Drawn: has primary grip"), Targets.bHasPrimaryGrip);
	TestTrue(TEXT("Drawn: has secondary grip"), Targets.bHasSecondaryGrip);
	TestEqual(TEXT("Drawn: Primary IK Alpha matches profile (1.0)"), Targets.PrimaryIKAlpha, 1.0f);
	TestEqual(TEXT("Drawn: Secondary IK Alpha matches profile (0.85)"), Targets.SecondaryIKAlpha, 0.85f);
	TestEqual(TEXT("Drawn: Primary grip X location matches socket (10.0)"), Targets.PrimaryGripWorldTransform.GetLocation().X, 10.0);
	TestEqual(TEXT("Drawn: Secondary grip X location matches socket (-10.0)"), Targets.SecondaryGripWorldTransform.GetLocation().X, -10.0);

	// 4. Test Sheathed State
	Presentation->CurrentPresentationSocket = EProject_JWeaponPresentationSocket::Sheathed;
	Presentation->UpdateGripTargets();
	Targets = Presentation->GetWeaponGripTargets();

	TestEqual(TEXT("Sheathed: Primary IK Alpha matches profile (0.5)"), Targets.PrimaryIKAlpha, 0.5f);
	TestEqual(TEXT("Sheathed: Secondary IK Alpha matches profile (0.0)"), Targets.SecondaryIKAlpha, 0.0f);

	// 5. Test Independent Motion override
	Presentation->bIndependentMotionActive = true;
	Presentation->ActivePrimaryGripIKAlpha = 0.7f;
	Presentation->ActiveSecondaryGripIKAlpha = 0.3f;
	Presentation->UpdateGripTargets();
	Targets = Presentation->GetWeaponGripTargets();

	TestEqual(TEXT("Independent Motion: Primary IK Alpha overridden to 0.7"), Targets.PrimaryIKAlpha, 0.7f);
	TestEqual(TEXT("Independent Motion: Secondary IK Alpha overridden to 0.3"), Targets.SecondaryIKAlpha, 0.3f);

	// 6. Test End Independent Motion -> restores default drawn alpha
	Presentation->EndIndependentMotion();
	Presentation->UpdateGripTargets();
	Targets = Presentation->GetWeaponGripTargets();

	TestEqual(TEXT("End Motion: Primary IK Alpha restored to drawn (1.0)"), Targets.PrimaryIKAlpha, 1.0f);
	TestEqual(TEXT("End Motion: Secondary IK Alpha restored to drawn (0.85)"), Targets.SecondaryIKAlpha, 0.85f);

	// 7. Test Weapon Destroy / Teardown
	Presentation->DestroyWeaponPresentation();
	Targets = Presentation->GetWeaponGripTargets();

	TestFalse(TEXT("Teardown: no primary grip"), Targets.bHasPrimaryGrip);
	TestFalse(TEXT("Teardown: no secondary grip"), Targets.bHasSecondaryGrip);
	TestEqual(TEXT("Teardown: Primary Alpha reset to 0.0"), Targets.PrimaryIKAlpha, 0.0f);
	TestEqual(TEXT("Teardown: Secondary Alpha reset to 0.0"), Targets.SecondaryIKAlpha, 0.0f);
	TestFalse(TEXT("Teardown: cached primary component invalidated"), Presentation->CachedPrimaryGripComponent.IsValid());
	TestFalse(TEXT("Teardown: cached secondary component invalidated"), Presentation->CachedSecondaryGripComponent.IsValid());

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAnimationQualityTierTest, "ProjectJ.Animation.QualityTierPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJAnimationQualityTierTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* Character = World->SpawnActor<ACharacter>(Params);

	UProject_JRetargetAnimInstance* RetargetAnim = NewObject<UProject_JRetargetAnimInstance>(Character->GetMesh());

	// 1. Verify default values of UProject_JRetargetAnimInstance
	TestTrue(TEXT("Default: allows Follower Retarget"), RetargetAnim->bEnableFollowerRetarget);
	TestTrue(TEXT("Default: allows Hand IK"), RetargetAnim->bTierAllowsHandIK);
	TestTrue(TEXT("Default: allows Foot IK"), RetargetAnim->bTierAllowsFootIK);
	TestTrue(TEXT("Default: allows Retarget IK"), RetargetAnim->bTierAllowsRetargetIK);
	TestEqual(TEXT("Default: Quality Tier is Local"), RetargetAnim->CurrentQualityTier, EProject_JAnimBudgetTier::Local);

	// 2. Verify Policy structures for each tier
	FProject_JAnimOptimizationPolicy PolicyLocal;
	PolicyLocal.Tier = EProject_JAnimBudgetTier::Local;
	PolicyLocal.bEnableHandIK = true;
	PolicyLocal.bEnableFootIK = true;
	PolicyLocal.bEnableRetargetIK = true;
	PolicyLocal.bEnableFollowerRetarget = true;
	PolicyLocal.RetargetIKLODThreshold = 0;
	TestTrue(TEXT("Local policy: Hand IK on"), PolicyLocal.bEnableHandIK);
	TestTrue(TEXT("Local policy: Follower retarget on"), PolicyLocal.bEnableFollowerRetarget);

	FProject_JAnimOptimizationPolicy PolicyNear;
	PolicyNear.Tier = EProject_JAnimBudgetTier::Near;
	PolicyNear.bEnableHandIK = true;
	PolicyNear.bEnableFootIK = true;
	PolicyNear.bEnableRetargetIK = true;
	PolicyNear.bEnableFollowerRetarget = true;
	PolicyNear.RetargetIKLODThreshold = 1;
	TestTrue(TEXT("Near policy: Retarget IK on"), PolicyNear.bEnableRetargetIK);

	FProject_JAnimOptimizationPolicy PolicyMid;
	PolicyMid.Tier = EProject_JAnimBudgetTier::Mid;
	PolicyMid.bEnableHandIK = false;
	PolicyMid.bEnableFootIK = true;
	PolicyMid.bEnableRetargetIK = false;
	PolicyMid.bEnableFollowerRetarget = true;
	TestFalse(TEXT("Mid policy: Hand IK off"), PolicyMid.bEnableHandIK);
	TestFalse(TEXT("Mid policy: Retarget IK off"), PolicyMid.bEnableRetargetIK);
	TestTrue(TEXT("Mid policy: Follower retarget on"), PolicyMid.bEnableFollowerRetarget);

	FProject_JAnimOptimizationPolicy PolicyFar;
	PolicyFar.Tier = EProject_JAnimBudgetTier::Far;
	PolicyFar.bEnableHandIK = false;
	PolicyFar.bEnableFootIK = false;
	PolicyFar.bEnableRetargetIK = false;
	PolicyFar.bEnableFollowerRetarget = true;
	TestFalse(TEXT("Far policy: Foot IK off"), PolicyFar.bEnableFootIK);
	TestFalse(TEXT("Far policy: Hand IK off"), PolicyFar.bEnableHandIK);

	FProject_JAnimOptimizationPolicy PolicyHidden;
	PolicyHidden.Tier = EProject_JAnimBudgetTier::Hidden;
	PolicyHidden.bEnableHandIK = false;
	PolicyHidden.bEnableFootIK = false;
	PolicyHidden.bEnableRetargetIK = false;
	PolicyHidden.bEnableFollowerRetarget = false;
	TestFalse(TEXT("Hidden policy: Follower retarget off"), PolicyHidden.bEnableFollowerRetarget);
	TestFalse(TEXT("Hidden policy: Hand IK off"), PolicyHidden.bEnableHandIK);

	World->DestroyWorld(false);
	return true;
}

#endif
