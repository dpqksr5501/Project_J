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
#include "Components/SkeletalMeshComponent.h"
#include "Components/Project_JModularMeshComponent.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "GameFramework/GameStateBase.h"
#include "GameplayTagsManager.h"

class FGameStateTestAccessor : public AGameStateBase
{
public:
	using AGameStateBase::ServerWorldTimeSecondsDelta;
};

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
	TestEqual(TEXT("Default: RetargetLODThreshold is -1"), RetargetAnim->RetargetLODThreshold, -1);
	TestEqual(TEXT("Default: Quality Tier is Local"), RetargetAnim->CurrentQualityTier, EProject_JAnimBudgetTier::Local);

	// 2. Verify Policy structures for each tier
	FProject_JAnimOptimizationPolicy PolicyLocal;
	PolicyLocal.Tier = EProject_JAnimBudgetTier::Local;
	PolicyLocal.bEnableHandIK = true;
	PolicyLocal.bEnableFootIK = true;
	PolicyLocal.bEnableRetargetIK = true;
	PolicyLocal.bEnableFollowerRetarget = true;
	PolicyLocal.RetargetIKLODThreshold = 0;
	PolicyLocal.RetargetLODThreshold = -1;
	TestTrue(TEXT("Local policy: Hand IK on"), PolicyLocal.bEnableHandIK);
	TestTrue(TEXT("Local policy: Follower retarget on"), PolicyLocal.bEnableFollowerRetarget);
	TestEqual(TEXT("Local policy: RetargetLODThreshold is -1"), PolicyLocal.RetargetLODThreshold, -1);

	FProject_JAnimOptimizationPolicy PolicyNear;
	PolicyNear.Tier = EProject_JAnimBudgetTier::Near;
	PolicyNear.bEnableHandIK = true;
	PolicyNear.bEnableFootIK = true;
	PolicyNear.bEnableRetargetIK = true;
	PolicyNear.bEnableFollowerRetarget = true;
	PolicyNear.RetargetIKLODThreshold = 1;
	PolicyNear.RetargetLODThreshold = -1;
	TestTrue(TEXT("Near policy: Retarget IK on"), PolicyNear.bEnableRetargetIK);
	TestEqual(TEXT("Near policy: RetargetLODThreshold is -1"), PolicyNear.RetargetLODThreshold, -1);

	FProject_JAnimOptimizationPolicy PolicyMid;
	PolicyMid.Tier = EProject_JAnimBudgetTier::Mid;
	PolicyMid.bEnableHandIK = false;
	PolicyMid.bEnableFootIK = true;
	PolicyMid.bEnableRetargetIK = false;
	PolicyMid.bEnableFollowerRetarget = true;
	PolicyMid.RetargetIKLODThreshold = 0;
	PolicyMid.RetargetLODThreshold = -1;
	TestFalse(TEXT("Mid policy: Hand IK off"), PolicyMid.bEnableHandIK);
	TestFalse(TEXT("Mid policy: Retarget IK off"), PolicyMid.bEnableRetargetIK);
	TestTrue(TEXT("Mid policy: Follower retarget on"), PolicyMid.bEnableFollowerRetarget);
	TestEqual(TEXT("Mid policy: RetargetLODThreshold is -1"), PolicyMid.RetargetLODThreshold, -1);

	FProject_JAnimOptimizationPolicy PolicyFar;
	PolicyFar.Tier = EProject_JAnimBudgetTier::Far;
	PolicyFar.bEnableHandIK = false;
	PolicyFar.bEnableFootIK = false;
	PolicyFar.bEnableRetargetIK = false;
	PolicyFar.bEnableFollowerRetarget = true;
	PolicyFar.RetargetIKLODThreshold = 0;
	PolicyFar.RetargetLODThreshold = 2;
	TestFalse(TEXT("Far policy: Foot IK off"), PolicyFar.bEnableFootIK);
	TestFalse(TEXT("Far policy: Hand IK off"), PolicyFar.bEnableHandIK);
	TestEqual(TEXT("Far policy: RetargetLODThreshold is 2"), PolicyFar.RetargetLODThreshold, 2);

	FProject_JAnimOptimizationPolicy PolicyHidden;
	PolicyHidden.Tier = EProject_JAnimBudgetTier::Hidden;
	PolicyHidden.bEnableHandIK = false;
	PolicyHidden.bEnableFootIK = false;
	PolicyHidden.bEnableRetargetIK = false;
	PolicyHidden.bEnableFollowerRetarget = false;
	PolicyHidden.RetargetIKLODThreshold = 0;
	PolicyHidden.RetargetLODThreshold = 0;
	TestFalse(TEXT("Hidden policy: Follower retarget off"), PolicyHidden.bEnableFollowerRetarget);
	TestFalse(TEXT("Hidden policy: Hand IK off"), PolicyHidden.bEnableHandIK);
	TestEqual(TEXT("Hidden policy: RetargetLODThreshold is 0"), PolicyHidden.RetargetLODThreshold, 0);

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJHiddenLeaderVisibleFollowerTest, "ProjectJ.Animation.HiddenLeaderVisibleFollower",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJHiddenLeaderVisibleFollowerTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* Character = World->SpawnActor<ACharacter>(Params);

	USkeletalMeshComponent* LeaderMesh = Character->GetMesh();
	LeaderMesh->SetHiddenInGame(true);

	// Create FollowerMesh as child of LeaderMesh with RetargetAnimInstance
	USkeletalMeshComponent* FollowerMesh = NewObject<USkeletalMeshComponent>(Character, TEXT("FollowerVisualMesh"));
	FollowerMesh->SetupAttachment(LeaderMesh);
	FollowerMesh->RegisterComponent();
	FollowerMesh->SetHiddenInGame(false);

	UProject_JRetargetAnimInstance* RetargetAnim = NewObject<UProject_JRetargetAnimInstance>(FollowerMesh);
	FollowerMesh->AnimScriptInstance = RetargetAnim;

	UProject_JCharacterAnimInstance* LeaderAnim = NewObject<UProject_JCharacterAnimInstance>(LeaderMesh);
	LeaderMesh->AnimScriptInstance = LeaderAnim;
	LeaderAnim->InitializeAnimation();

	// 1. Verify FollowerMesh is properly identified by LeaderAnim
	TestEqual(TEXT("FollowerMesh identified by GetRuntimeRetargetFollowerMesh"),
		LeaderAnim->GetRuntimeRetargetFollowerMesh(), FollowerMesh);

	// 2. Mark FollowerMesh as recently rendered
	const float Now = World->GetTimeSeconds();
	FollowerMesh->SetLastRenderTime(Now);

	// LeaderMesh is bHiddenInGame = true, but FollowerMesh is rendered -> WasOwnerVisualRecentlyRendered must be true!
	TestTrue(TEXT("WasOwnerVisualRecentlyRendered is true when Follower is rendered"),
		LeaderAnim->WasOwnerVisualRecentlyRendered(1.0f));

	FProject_JAnimOptimizationPolicy Policy = LeaderAnim->BuildOptimizationPolicy();
	TestNotEqual(TEXT("Tier is NOT Hidden when Follower is rendered"),
		Policy.Tier, EProject_JAnimBudgetTier::Hidden);
	TestTrue(TEXT("Policy enables Follower Retarget"), Policy.bEnableFollowerRetarget);

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJHiddenToVisibleFollowerRestoreTest, "ProjectJ.Animation.HiddenToVisibleFollowerRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJHiddenToVisibleFollowerRestoreTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* Character = World->SpawnActor<ACharacter>(Params);

	USkeletalMeshComponent* LeaderMesh = Character->GetMesh();
	LeaderMesh->SetHiddenInGame(true);

	USkeletalMeshComponent* FollowerMesh = NewObject<USkeletalMeshComponent>(Character, TEXT("FollowerVisualMesh"));
	FollowerMesh->SetupAttachment(LeaderMesh);
	FollowerMesh->RegisterComponent();
	FollowerMesh->SetHiddenInGame(false);

	UProject_JRetargetAnimInstance* RetargetAnim = NewObject<UProject_JRetargetAnimInstance>(FollowerMesh);
	FollowerMesh->AnimScriptInstance = RetargetAnim;

	UProject_JCharacterAnimInstance* LeaderAnim = NewObject<UProject_JCharacterAnimInstance>(LeaderMesh);
	LeaderMesh->AnimScriptInstance = LeaderAnim;
	FollowerMesh->SetLastRenderTime(World->GetTimeSeconds());
	LeaderAnim->InitializeAnimation();
	FollowerMesh->SetComponentTickEnabled(true);

	TestTrue(TEXT("Initial: Follower tick enabled"), FollowerMesh->IsComponentTickEnabled());

	// 1. Transition to Hidden: Follower not rendered (far past time)
	FollowerMesh->SetLastRenderTime(-100.0f);

	TestFalse(TEXT("WasOwnerVisualRecentlyRendered is false when off-screen"),
		LeaderAnim->WasOwnerVisualRecentlyRendered(1.0f));

	FProject_JAnimOptimizationPolicy HiddenPolicy = LeaderAnim->BuildOptimizationPolicy();
	TestEqual(TEXT("Tier becomes Hidden"), HiddenPolicy.Tier, EProject_JAnimBudgetTier::Hidden);
	TestFalse(TEXT("HiddenPolicy disables follower retarget"), HiddenPolicy.bEnableFollowerRetarget);

	LeaderAnim->ApplyOptimizationPolicy(HiddenPolicy);
	TestFalse(TEXT("Follower tick disabled after entering Hidden tier"), FollowerMesh->IsComponentTickEnabled());

	// 2. Transition back to Visible: camera looks at character, Follower becomes rendered
	const float Now = World->GetTimeSeconds();
	FollowerMesh->SetLastRenderTime(Now);

	TestTrue(TEXT("WasOwnerVisualRecentlyRendered recovers to true"),
		LeaderAnim->WasOwnerVisualRecentlyRendered(1.0f));

	FProject_JAnimOptimizationPolicy RestoredPolicy = LeaderAnim->BuildOptimizationPolicy();
	TestNotEqual(TEXT("Restored tier is not Hidden"), RestoredPolicy.Tier, EProject_JAnimBudgetTier::Hidden);
	TestTrue(TEXT("Restored policy enables follower retarget"), RestoredPolicy.bEnableFollowerRetarget);

	LeaderAnim->ApplyOptimizationPolicy(RestoredPolicy);
	TestTrue(TEXT("Follower tick restored after exiting Hidden tier"), FollowerMesh->IsComponentTickEnabled());

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJModularMeshIsolationTest, "ProjectJ.Animation.ModularMeshIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJModularMeshIsolationTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* Character = World->SpawnActor<ACharacter>(Params);

	USkeletalMeshComponent* LeaderMesh = Character->GetMesh();

	// 1. Create Follower Mesh
	USkeletalMeshComponent* FollowerMesh = NewObject<USkeletalMeshComponent>(Character, TEXT("FollowerVisualMesh"));
	FollowerMesh->SetupAttachment(LeaderMesh);
	FollowerMesh->RegisterComponent();
	UProject_JRetargetAnimInstance* RetargetAnim = NewObject<UProject_JRetargetAnimInstance>(FollowerMesh);
	FollowerMesh->AnimScriptInstance = RetargetAnim;

	// 2. Create Modular Mesh (e.g. Armor/Helmet)
	UProject_JModularMeshComponent* ModularArmorMesh = NewObject<UProject_JModularMeshComponent>(Character, TEXT("ModularArmorMesh"));
	ModularArmorMesh->SetupAttachment(LeaderMesh);
	ModularArmorMesh->RegisterComponent();

	UProject_JCharacterAnimInstance* LeaderAnim = NewObject<UProject_JCharacterAnimInstance>(LeaderMesh);
	LeaderMesh->AnimScriptInstance = LeaderAnim;
	LeaderAnim->InitializeAnimation();

	ModularArmorMesh->PrimaryComponentTick.bCanEverTick = true;
	FollowerMesh->SetComponentTickEnabled(true);
	ModularArmorMesh->SetComponentTickEnabled(true);
	TestTrue(TEXT("Initial: FollowerMesh tick enabled"), FollowerMesh->IsComponentTickEnabled());
	TestTrue(TEXT("Initial: ModularArmorMesh tick enabled"), ModularArmorMesh->IsComponentTickEnabled());

	// 3. Apply Hidden policy (bEnableFollowerRetarget = false)
	FProject_JAnimOptimizationPolicy HiddenPolicy;
	HiddenPolicy.Tier = EProject_JAnimBudgetTier::Hidden;
	HiddenPolicy.bEnableFollowerRetarget = false;
	LeaderAnim->ApplyOptimizationPolicy(HiddenPolicy);

	// FollowerMesh must be disabled, BUT ModularArmorMesh must NOT be disabled!
	TestFalse(TEXT("FollowerMesh tick disabled in Hidden tier"), FollowerMesh->IsComponentTickEnabled());
	TestTrue(TEXT("ModularArmorMesh tick preserved in Hidden tier (isolated)"), ModularArmorMesh->IsComponentTickEnabled());

	// 4. Restore to Visible policy
	FProject_JAnimOptimizationPolicy VisiblePolicy;
	VisiblePolicy.Tier = EProject_JAnimBudgetTier::Near;
	VisiblePolicy.bEnableFollowerRetarget = true;
	LeaderAnim->ApplyOptimizationPolicy(VisiblePolicy);

	TestTrue(TEXT("FollowerMesh tick restored in Visible tier"), FollowerMesh->IsComponentTickEnabled());
	TestTrue(TEXT("ModularArmorMesh tick still enabled"), ModularArmorMesh->IsComponentTickEnabled());

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSSRHitWindowProductionTransitionTest, "ProjectJ.Combat.SSRHitWindowProductionTransition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJSSRHitWindowProductionTransitionTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* Character = World->SpawnActor<ACharacter>(Params);

	AGameStateBase* GameState = World->SpawnActor<AGameStateBase>(Params);
	World->SetGameState(GameState);
	auto* GameStateAccess = static_cast<FGameStateTestAccessor*>(GameState);

	UProject_JCombatHitValidationComponent* HitVal = NewObject<UProject_JCombatHitValidationComponent>(Character);
	Character->AddInstanceComponent(HitVal);
	HitVal->RegisterComponent();

	const FGameplayTag AttackTag = UGameplayTagsManager::Get().AddNativeGameplayTag(TEXT("ProjectJ.Tests.SSR.ProductionAttack"));
	const int32 Key = 101;
	const float WorldTime = World->GetTimeSeconds();

	// 1. Begin Attack Node at 10.0s
	GameStateAccess->ServerWorldTimeSecondsDelta = 10.0f - WorldTime;
	HitVal->BeginAttackNode(AttackTag, nullptr, Key);

	// 2. Open HitWindow at 10.0s (records open transition boundary)
	HitVal->SetHitWindowOpen(true);

	// 3. Record authoritative traces within hit window
	GameStateAccess->ServerWorldTimeSecondsDelta = 10.1f - WorldTime;
	HitVal->RecordAuthoritativeTrace(FVector(0, 0, 0), FVector(100, 0, 0));

	GameStateAccess->ServerWorldTimeSecondsDelta = 10.2f - WorldTime;
	HitVal->RecordAuthoritativeTrace(FVector(0, 100, 0), FVector(100, 100, 0));

	// 4. Close HitWindow at 10.3s (records close transition boundary)
	GameStateAccess->ServerWorldTimeSecondsDelta = 10.3f - WorldTime;
	HitVal->SetHitWindowOpen(false);

	// 5. Test SSR verification via FindAuthoritativeTraceAtTime
	FVector OutStart = FVector::ZeroVector;
	FVector OutEnd = FVector::ZeroVector;
	bool bOutHitWindowOpen = false;

	// Sub-frame inside hit window between Trace1 (10.1) and Trace2 (10.2) at 10.15s
	const bool bValidMid = HitVal->FindAuthoritativeTraceAtTime(10.15f, Key, AttackTag, OutStart, OutEnd, bOutHitWindowOpen);
	TestTrue(TEXT("Production path: Sub-frame inside hit window is valid"), bValidMid);
	TestTrue(TEXT("Production path: HitWindow was open"), bOutHitWindowOpen);
	TestNearlyEqual(TEXT("Production path: Interpolated Y is 50"), OutStart.Y, 50.0, 0.1);

	// Timestamp before hit window opened (9.95s)
	const bool bBeforeOpen = HitVal->FindAuthoritativeTraceAtTime(9.95f, Key, AttackTag, OutStart, OutEnd, bOutHitWindowOpen);
	TestFalse(TEXT("Production path: Sample before open boundary is rejected"), bBeforeOpen);

	// Timestamp after hit window closed (10.32s within tolerance): trace found, but window is closed
	const bool bAfterClose = HitVal->FindAuthoritativeTraceAtTime(10.32f, Key, AttackTag, OutStart, OutEnd, bOutHitWindowOpen);
	TestTrue(TEXT("Production path: Trace after close found within tolerance"), bAfterClose);
	TestFalse(TEXT("Production path: HitWindow is closed after close boundary"), bOutHitWindowOpen);

	// Timestamp beyond future tolerance (10.45s) is rejected
	const bool bBeyondFuture = HitVal->FindAuthoritativeTraceAtTime(10.45f, Key, AttackTag, OutStart, OutEnd, bOutHitWindowOpen);
	TestFalse(TEXT("Production path: Sample beyond future tolerance is rejected"), bBeyondFuture);

	// Mismatched PredictionKey
	const bool bWrongKey = HitVal->FindAuthoritativeTraceAtTime(10.15f, 9999, AttackTag, OutStart, OutEnd, bOutHitWindowOpen);
	TestFalse(TEXT("Production path: Mismatched key rejected"), bWrongKey);

	// Active attack convenience overload succeeds during active combo node
	TestTrue(TEXT("Production path: Active attack convenience overload succeeds"),
		HitVal->FindAuthoritativeTraceAtTime(10.15f, OutStart, OutEnd));

	// 6. Calling EndAttack() clears active attack context (Option A active attack context)
	HitVal->EndAttack();
	const bool bAfterEndAttack = HitVal->FindAuthoritativeTraceAtTime(10.15f, OutStart, OutEnd);
	TestFalse(TEXT("Production path: Convenience overload after EndAttack is rejected (Option A active attack context)"), bAfterEndAttack);
	TestFalse(TEXT("Production path: Active tag is cleared"), HitVal->GetActiveAttackNodeTag().IsValid());

	FProject_JCombatHitRequest Request;
	Request.PredictionKey = Key;
	Request.AttackNodeTag = AttackTag;
	TestEqual(TEXT("Production path: ValidateActiveAttack rejects with NoActiveAttack after EndAttack"),
		HitVal->ValidateActiveAttack(Request), EProject_JCombatHitValidationFailure::NoActiveAttack);

	World->DestroyWorld(false);
	return true;
}

#include "Animation/Project_JAnimNotifyState_TwoHandIK.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJTwoHandIKTransitionAndCurveTest, "ProjectJ.Animation.TwoHandIKTransitionAndCurve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJTwoHandIKTransitionAndCurveTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* Character = World->SpawnActor<ACharacter>(Params);

	UProject_JWeaponPresentationComponent* Presentation = NewObject<UProject_JWeaponPresentationComponent>(Character);
	Character->AddInstanceComponent(Presentation);
	Presentation->RegisterComponent();

	// 1. Mock Weapon Actor with Right and Left Grip Sockets
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

	// 2. Setup WeaponPresentationProfile with Greatsword settings (Secondary = 0 in drawn idle)
	UProject_JWeaponPresentationProfile* Profile = NewObject<UProject_JWeaponPresentationProfile>(Character);
	Profile->MotionPresentation.PrimaryGripSocketName = TEXT("WeaponGrip_R");
	Profile->MotionPresentation.SecondaryGripSocketName = TEXT("WeaponGrip_L");
	Profile->MotionPresentation.DefaultDrawnPrimaryIKAlpha = 1.0f;
	Profile->MotionPresentation.DefaultDrawnSecondaryIKAlpha = 0.0f; // Greatsword default: right hand only
	Profile->MotionPresentation.DefaultSheathedPrimaryIKAlpha = 0.0f;
	Profile->MotionPresentation.DefaultSheathedSecondaryIKAlpha = 0.0f;

	Presentation->AppliedProfile = Profile;
	Presentation->SpawnedWeapon = MockWeapon;
	Presentation->UpdateSocketComponentCache();

	// 3. Setup Follower Mesh and UProject_JRetargetAnimInstance
	USkeletalMeshComponent* FollowerMesh = NewObject<USkeletalMeshComponent>(Character);
	FollowerMesh->SetupAttachment(Character->GetMesh());
	FollowerMesh->RegisterComponent();

	UProject_JRetargetAnimInstance* RetargetAnim = NewObject<UProject_JRetargetAnimInstance>(FollowerMesh);
	FollowerMesh->AnimScriptInstance = RetargetAnim;
	Presentation->RegisterRetargetAnimInstance(RetargetAnim);

	// Test A: Sheathed / Non-combat
	Presentation->CurrentPresentationSocket = EProject_JWeaponPresentationSocket::Sheathed;
	Presentation->UpdateGripTargets();
	FProject_JWeaponGripTargets Targets = Presentation->GetWeaponGripTargets();
	TestEqual(TEXT("Sheathed: Primary IK Alpha is 0.0"), Targets.PrimaryIKAlpha, 0.0f);
	TestEqual(TEXT("Sheathed: Secondary IK Alpha is 0.0"), Targets.SecondaryIKAlpha, 0.0f);

	// Test B: Drawn Combat Stance (Idle/Run outside attack) -> Right only
	Presentation->CurrentPresentationSocket = EProject_JWeaponPresentationSocket::Drawn;
	Presentation->UpdateGripTargets();
	Targets = Presentation->GetWeaponGripTargets();
	TestEqual(TEXT("Combat Idle: Primary IK Alpha is 1.0"), Targets.PrimaryIKAlpha, 1.0f);
	TestEqual(TEXT("Combat Idle: Secondary IK Alpha is 0.0 (Left hand free)"), Targets.SecondaryIKAlpha, 0.0f);

	// Test C: Begin TwoHandGrip (Attack swing starts) -> Left hand engages
	Presentation->BeginTwoHandGrip(1.0f);
	TestTrue(TEXT("TwoHandGrip is active"), Presentation->IsTwoHandGripActive());
	Targets = Presentation->GetWeaponGripTargets();
	TestEqual(TEXT("Attack Swing: Secondary IK Alpha is 1.0 (Left hand grips hilt)"), Targets.SecondaryIKAlpha, 1.0f);
	TestEqual(TEXT("Attack Swing: Primary IK Alpha remains 1.0"), Targets.PrimaryIKAlpha, 1.0f);

	// Test D: Combo overlap (Second attack begins before first ends) -> Ref count is 2
	Presentation->BeginTwoHandGrip(1.0f);
	TestTrue(TEXT("TwoHandGrip remains active during combo"), Presentation->IsTwoHandGripActive());
	Presentation->EndTwoHandGrip(); // First attack notify ends
	TestTrue(TEXT("TwoHandGrip still active after first notify ends (ref-count > 0)"), Presentation->IsTwoHandGripActive());
	Targets = Presentation->GetWeaponGripTargets();
	TestEqual(TEXT("Combo transition: Secondary IK Alpha stays 1.0 without popping"), Targets.SecondaryIKAlpha, 1.0f);

	Presentation->EndTwoHandGrip(); // Second attack notify ends
	TestFalse(TEXT("TwoHandGrip inactive after second notify ends"), Presentation->IsTwoHandGripActive());
	Targets = Presentation->GetWeaponGripTargets();
	TestEqual(TEXT("Combo finished: Secondary IK Alpha returns to 0.0"), Targets.SecondaryIKAlpha, 0.0f);

	// Test E: UProject_JAnimNotifyState_TwoHandIK triggers presentation methods
	UProject_JAnimNotifyState_TwoHandIK* NotifyState = NewObject<UProject_JAnimNotifyState_TwoHandIK>();
	NotifyState->SecondaryIKAlpha = 0.9f;
	FAnimNotifyEventReference EventRef;
	NotifyState->NotifyBegin(Character->GetMesh(), nullptr, 1.0f, EventRef);
	TestTrue(TEXT("NotifyBegin activated TwoHandGrip"), Presentation->IsTwoHandGripActive());
	Targets = Presentation->GetWeaponGripTargets();
	TestEqual(TEXT("Notify target alpha applied (0.9)"), Targets.SecondaryIKAlpha, 0.9f);

	NotifyState->NotifyEnd(Character->GetMesh(), nullptr, EventRef);
	TestFalse(TEXT("NotifyEnd deactivated TwoHandGrip"), Presentation->IsTwoHandGripActive());
	Targets = Presentation->GetWeaponGripTargets();
	TestEqual(TEXT("NotifyEnd restored Secondary Alpha to 0.0"), Targets.SecondaryIKAlpha, 0.0f);

	// Test F: ExitCombatPresentation clears TwoHandGrip count
	Presentation->BeginTwoHandGrip(1.0f);
	TestTrue(TEXT("Active before ExitCombat"), Presentation->IsTwoHandGripActive());
	Presentation->ExitCombatPresentation();
	TestFalse(TEXT("ExitCombatPresentation reset TwoHandGrip state count"), Presentation->IsTwoHandGripActive());

	World->DestroyWorld(false);
	return true;
}

#endif
