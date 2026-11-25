#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "AI/Project_JThreatComponent.h"
#include "Camera/Project_JCameraContextProfile.h"
#include "Combat/Project_JCombatQueryTypes.h"
#include "Combat/Project_JGameplayAbility_Melee.h"
#include "Combat/Project_JGameplayAbility_Sprint.h"
#include "Combat/Project_JMultiPartHitComponent.h"
#include "Combat/Project_JProjectileManagerSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/Project_JPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Project_JDeathReviveSubsystem.h"
#include "Gameplay/Project_JLootSubsystem.h"
#include "GameplayEffect.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/WorldSettings.h"
#include "Mass/Project_JNPCPoolSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "MoverDataModelTypes.h"
#include "Mount/Project_JMountMoverComponent.h"
#include "Network/Project_JMMOClientBridgeComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Project_JCCResistanceComponent.h"
#include "Project_JGameplayTags.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Project_JNPCCharacter.h"
#include "Project_JSpellbladeCharacter.h"
#include "World/Project_JDungeonInstanceSubsystem.h"

namespace ProjectJPhase2Tests
{
struct FWorldScope
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldScope()
	{
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->GetWorldSettings()->NotifyBeginPlay();
		World->GetWorldSettings()->NotifyMatchStarted();
	}
	~FWorldScope()
	{
		World->EndPlay(EEndPlayReason::LevelTransition);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
	template<typename T>
	T* Spawn(const FVector& Loc = FVector::ZeroVector)
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<T>(Loc, FRotator::ZeroRotator, P);
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPhase2MountMoverEnergyTest, "ProjectJ.Systems.Phase2.MountMoverEnergyFlight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPhase2MountMoverEnergyTest::RunTest(const FString&)
{
	FProject_JMountMotionProfile Profile;
	Profile.bCanFly = true;
	FProject_JMountMoverSyncState State;
	State.AirSpeed = 1600.f;
	State.Stamina = 100.f;

	// Diving increases airspeed without draining stamina.
	FProject_JMountMoverInputs DiveInput;
	DiveInput.PitchInput = -1.0f;
	DiveInput.ThrottleTurn = FVector2D(1.0, 0.0);
	for (int32 i = 0; i < 30; ++i)
	{
		Project_J::Mount::StepFlyingEnergyModel(DiveInput, Profile, 0.05f, State);
	}
	TestTrue(TEXT("Diving accelerates airspeed above cruise"), State.AirSpeed > Profile.FlightCruiseSpeed);
	TestTrue(TEXT("Diving does not drain stamina"), State.Stamina >= 99.f);
	FProject_JMountMoverSyncState BoostedDive = State;
	DiveInput.bBoostHeld = true;
	Project_J::Mount::StepFlyingEnergyModel(DiveInput, Profile, 0.05f, BoostedDive);
	TestTrue(TEXT("Holding boost while diving still consumes stamina"), BoostedDive.Stamina < State.Stamina);

	// Sustained climb + boost drains stamina and automatically transitions into Gliding without stalling.
	FProject_JMountMoverInputs ClimbInput;
	ClimbInput.PitchInput = 1.0f;
	ClimbInput.bBoostHeld = true;
	ClimbInput.ThrottleTurn = FVector2D(1.0, 0.0);
	for (int32 i = 0; i < 120; ++i)
	{
		Project_J::Mount::StepFlyingEnergyModel(ClimbInput, Profile, 0.05f, State);
	}
	TestTrue(TEXT("Exhausting stamina automatically transitions to gliding"), State.bIsGliding);
	TestTrue(TEXT("Airspeed never drops below stall speed"), State.AirSpeed >= Profile.FlightStallSpeed);

	ProjectJPhase2Tests::FWorldScope Scope;
	auto* Mount = Scope.Spawn<AProject_JMountMoverPawn>(FVector(0, 0, 1000));
	if (!TestNotNull(TEXT("Mover mount"), Mount)) return false;
	auto* Mover = Mount->GetMountMover();
	Mover->MotionProfile.bCanFly = true;
	const auto* Settings = Mover->FindSharedSettings<UCommonLegacyMovementSettings>();
	if (!TestNotNull(TEXT("Falling movement settings"), Settings)) return false;
	TestEqual(TEXT("Touchdown resolves to the mount ground mode"), Settings->GroundMovementModeName, Mover->GroundMountModeName);

	auto* GroundMode = Cast<UProject_JGroundMountMode>(Mover->FindMovementModeByName(Mover->GroundMountModeName));
	auto* FlyingMode = Cast<UProject_JFlyingMountMode>(Mover->FindMovementModeByName(Mover->FlyingMountModeName));
	if (!TestNotNull(TEXT("Ground mount mode"), GroundMode) || !TestNotNull(TEXT("Flying mount mode"), FlyingMode)) return false;
	FSimulationTickParams Params;
	Params.TimeStep.StepMs = 16.0f;
	Params.StartState.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>();
	auto& FlightInput = Params.StartState.InputCmd.InputCollection.FindOrAddMutableDataByType<FProject_JMountMoverInputs>();
	FlightInput.bTakeOffPressed = true;
	FMoverTickEndData Takeoff;
	GroundMode->SimulationTick_Implementation(Params, Takeoff);
	TestEqual(TEXT("Recorded takeoff input selects the flying mode"), Takeoff.MovementEndState.NextModeName, Mover->FlyingMountModeName);
	TestEqual(TEXT("Takeoff forwards the unconsumed tick"), Takeoff.MovementEndState.RemainingMs, Params.TimeStep.StepMs);
	FlightInput.bTakeOffPressed = false;
	FlightInput.bLandPressed = true;
	FMoverTickEndData Landing;
	FlyingMode->SimulationTick_Implementation(Params, Landing);
	TestEqual(TEXT("Recorded landing input hands control to collision-based falling"), Landing.MovementEndState.NextModeName, DefaultModeNames::Falling);
	TestTrue(TEXT("The landing flag participates in reconciliation"), FlightInput.ShouldReconcile(FProject_JMountMoverInputs{}));
	FProject_JMountMoverInputs FullSpeed, SlowFlight;
	SlowFlight.FlightStreamingSpeedScale = 128;
	TestTrue(TEXT("Local streaming can only request a bounded slowdown"), SlowFlight.GetFlightStreamingSpeedScale() < 1.f);
	TestTrue(TEXT("Streaming slowdown is included in Mover reconciliation"), SlowFlight.ShouldReconcile(FullSpeed));
	SlowFlight.FlightStreamingSpeedScale = 0;
	TestEqual(TEXT("A malformed zero speed scale still respects the lower bound"), SlowFlight.GetFlightStreamingSpeedScale(), 0.1f);

	auto* Controller = Scope.Spawn<APlayerController>();
	auto* Player = Scope.Spawn<AProject_JPlayerState>();
	auto* Rider = Scope.Spawn<AProject_JSpellbladeCharacter>(Mount->GetActorLocation() + FVector(100, 0, 0));
	if (!Controller || !Player || !Rider) return false;
	Player->SetOwner(Controller);
	Controller->SetPlayerState(Player);
	Rider->SetPlayerState(Player);
	Controller->Possess(Rider);
	auto* RiderASC = Player->GetProjectJAbilitySystemComponent();
	auto* RiderAttributes = Player->GetProjectJAttributeSet();
	RiderAttributes->InitMaxHealth(100.0f);
	RiderAttributes->SetHealth(100.0f);
	if (!TestTrue(TEXT("Player character mounts after its ASC avatar is bound"), Mount->TryMountRider(Rider))) return false;
	TestTrue(TEXT("Player's original ASC remains accessible from the attached rider"), Rider->GetAbilitySystemComponent() == RiderASC);
	TestTrue(TEXT("The correct rider ASC owns the mounted tag"), RiderASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Mounted));
	RiderAttributes->SetHealth(0.0f);
	TestFalse(TEXT("Player death forces dismount"), Mount->IsMounted());
	TestTrue(TEXT("Player death restores rider possession"), Controller->GetPawn() == Rider);
	TestFalse(TEXT("The mounted tag is released on the original ASC"), RiderASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Mounted));
	auto* Revive = Scope.World->GetSubsystem<UProject_JDeathReviveSubsystem>();
	TestTrue(TEXT("Health reaching zero records a player death without manual notification"),
		Revive->ShouldAutoReleaseCorpse(Rider, Scope.World->GetTimeSeconds() + 181.0));

	// Dynamic camera presets for giant boss & flying dive.
	UProject_JCameraPolicyAsset* Policy = NewObject<UProject_JCameraPolicyAsset>();
	const auto OnFoot = Policy->ResolvePreset(EProject_JCameraContextMode::OnFoot);
	const auto Boss = Policy->ResolvePreset(EProject_JCameraContextMode::GiantBossZoomOut);
	const auto Dive = Policy->ResolvePreset(EProject_JCameraContextMode::FlyingMountDive);
	TestTrue(TEXT("Giant boss preset zooms out further than OnFoot"), Boss.TargetArmLength > OnFoot.TargetArmLength);
	TestTrue(TEXT("Flying dive preset widens FOV above OnFoot"), Dive.FieldOfView > OnFoot.FieldOfView);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPhase2DungeonFlowTest, "ProjectJ.Systems.Phase2.DungeonPortalMenuAndReturn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPhase2DungeonFlowTest::RunTest(const FString&)
{
	UGameInstance* GI = NewObject<UGameInstance>(GEngine);
	GI->Init();
	auto* Dungeon = GI->GetSubsystem<UProject_JDungeonInstanceSubsystem>();
	if (!TestNotNull(TEXT("DungeonInstanceSubsystem"), Dungeon)) { GI->Shutdown(); return false; }

	// Register open-world channels: Channel 1 (full), Channel 2 (available fallback).
	const double HeartbeatUtc = UProject_JDungeonInstanceSubsystem::GetCurrentUtcEpochSeconds();
	const double StaleHeartbeatUtc = HeartbeatUtc - UProject_JDungeonInstanceSubsystem::ChannelHeartbeatTimeoutSeconds - 1.0;
	FProject_JChannelInfo Ch1{ TEXT("MainWorld"), 1, TEXT("127.0.0.1:7777"), 200, 200, false, HeartbeatUtc };
	FProject_JChannelInfo Ch2{ TEXT("MainWorld"), 2, TEXT("127.0.0.1:7779"), 45, 200, false, HeartbeatUtc };
	FProject_JChannelInfo StaleSibling{ TEXT("MainWorld"), 3, TEXT("127.0.0.1:7776"), 0, 200, false, StaleHeartbeatUtc };
	Dungeon->RegisterChannelHeartbeat(Ch1);
	Dungeon->RegisterChannelHeartbeat(Ch2);
	Dungeon->RegisterChannelHeartbeat(StaleSibling);
	auto OlderCh2 = Ch2;
	OlderCh2.LastHeartbeatSeconds = StaleHeartbeatUtc;
	OlderCh2.bDraining = true;
	Dungeon->RegisterChannelHeartbeat(OlderCh2);
	Dungeon->RegisterWarmPoolServer(TEXT("127.0.0.1:7788"));

	const FGuid CharA = FGuid::NewGuid();
	TMap<FGuid, FProject_JReturnAnchor> Anchors;
	Anchors.Add(CharA, FProject_JReturnAnchor{ TEXT("MainWorld"), 1, FVector(1000, 2000, 300), FRotator::ZeroRotator });

	FGuid InstanceId;
	TArray<FProject_JDungeonJoinTicket> Tickets;
	TestTrue(TEXT("Menu UI dungeon entry allocates Warm Pool server"),
		Dungeon->RequestDungeonEntry(EProject_JDungeonEntrySource::MenuUI, TEXT("CryptOfShadows"), { CharA }, Anchors, 100.0, InstanceId, Tickets));
	if (!TestEqual(TEXT("One ticket minted"), Tickets.Num(), 1)) { GI->Shutdown(); return false; }
	TestEqual(TEXT("Warm pool endpoint used"), Tickets[0].ServerEndpoint, FString(TEXT("127.0.0.1:7788")));
	UGameInstance* DestinationGI = NewObject<UGameInstance>(GEngine);
	DestinationGI->Init();
	auto* Destination = DestinationGI->GetSubsystem<UProject_JDungeonInstanceSubsystem>();
	FProject_JDungeonInstanceRecord Admission;
	TestTrue(TEXT("Source exposes the allocator admission record"), Dungeon->GetInstanceAdmission(InstanceId, Admission));
	const FString Travel = UProject_JMMOClientBridgeComponent::BuildDungeonTravelURL(Tickets[0]);
	const FString Options = Travel.Mid(Travel.Find(TEXT("?")));
	FProject_JDungeonJoinTicket Decoded;
	TestTrue(TEXT("Travel options round-trip through the same strict parser used at login"),
		Dungeon->ParseJoinTicketOptions(Options, Decoded) && Decoded.Nonce == Tickets[0].Nonce && Decoded.ServerEndpoint == Tickets[0].ServerEndpoint);
	TestFalse(TEXT("Malformed issue time is rejected instead of truncating a prefix"),
		Dungeon->ParseJoinTicketOptions(Options.Replace(TEXT("TicketIssued=100.000"), TEXT("TicketIssued=100.000junk")), Decoded));
	if (TestNotNull(TEXT("Distinct destination admission subsystem"), Destination))
	{
		TestFalse(TEXT("A valid ticket is unusable before destination admission is installed"), Destination->VerifyAndConsumeJoinTicket(Tickets[0], 105.0));
		TestTrue(TEXT("Destination installs trusted allocation"), Destination->InstallInstanceAdmission(Admission, 105.0));
		TestTrue(TEXT("Destination preflight checks can repeat without consuming the nonce"),
			Destination->CanAdmitJoinTicket(Tickets[0], 105.0) && Destination->CanAdmitJoinTicket(Tickets[0], 105.0));
		Destination->RegisterChannelHeartbeat(Ch2);
		FProject_JReturnAnchor DestinationAnchor;
		FString DestinationEndpoint;
		int32 DestinationChannel = 0;
		TestTrue(TEXT("Destination resolves the original return anchor through its own registry"),
			Destination->ResolveCharacterReturn(CharA, DestinationAnchor, DestinationEndpoint, DestinationChannel)
			&& DestinationAnchor.Location == Anchors.FindChecked(CharA).Location);
		auto ChangedAdmission = Admission;
		ChangedAdmission.ReturnAnchors.FindChecked(CharA).Location.Z += 10.0;
		TestFalse(TEXT("An installation retry cannot override the original anchor"),
			Destination->InstallInstanceAdmission(ChangedAdmission, 105.0));
		TestTrue(TEXT("Destination verifies the source ticket"), Destination->VerifyAndConsumeJoinTicket(Tickets[0], 105.0));
		TestTrue(TEXT("Retrying the allocator acknowledgement preserves admission state"), Destination->InstallInstanceAdmission(Admission, 106.0));
		TestFalse(TEXT("Admission replay cannot reset nonce or character state"), Destination->VerifyAndConsumeJoinTicket(Tickets[0], 106.0));
		Destination->PruneExpiredState(Admission.ExpiresAtSeconds);
		TestFalse(TEXT("Expired allocations retire character bindings"), Destination->HasActiveDungeonInstance(CharA));
	}
	DestinationGI->Shutdown();

	// Ticket verification is single-use and tamper-evident.
	FProject_JDungeonJoinTicket Tampered = Tickets[0];
	Tampered.CharacterId = FGuid::NewGuid();
	TestFalse(TEXT("Tampered ticket rejected"), Dungeon->VerifyAndConsumeJoinTicket(Tampered, 105.0));
	TestTrue(TEXT("Valid ticket accepted once"), Dungeon->VerifyAndConsumeJoinTicket(Tickets[0], 105.0));
	TestFalse(TEXT("Replayed ticket nonce rejected"), Dungeon->VerifyAndConsumeJoinTicket(Tickets[0], 106.0));

	// 180s disconnect grace window allows reconnect before expiry, rejects after expiry.
	TestTrue(TEXT("Disconnect recorded"), Dungeon->RecordDisconnectInDungeon(CharA, 200.0));
	FProject_JDungeonJoinTicket ReconnectTicket;
	TestTrue(TEXT("Reconnect ticket issued within 180s"), Dungeon->TryIssueReconnectTicket(CharA, 350.0, ReconnectTicket));
	FProject_JDungeonJoinTicket Redispatched;
	TestTrue(TEXT("A lost response can request the same reconnect ticket until it is used"),
		Dungeon->TryIssueReconnectTicket(CharA, 351.0, Redispatched) && Redispatched.Nonce == ReconnectTicket.Nonce);
	TestTrue(TEXT("Reconnect ticket verifies cleanly"), Dungeon->VerifyAndConsumeJoinTicket(ReconnectTicket, 355.0));

	// Idempotent clear reward claim.
	TestTrue(TEXT("First clear reward claim succeeds"), Dungeon->ClaimDungeonClearReward(InstanceId, CharA));
	TestFalse(TEXT("Duplicate clear reward claim rejected"), Dungeon->ClaimDungeonClearReward(InstanceId, CharA));

	// Safe open-world return falls back from full Channel 1 to healthy Channel 2.
	FProject_JReturnAnchor OutAnchor;
	FString OutEndpoint;
	int32 OutChannelId = 0;
	TestTrue(TEXT("Return anchor resolved"), Dungeon->ResolveCharacterReturn(CharA, OutAnchor, OutEndpoint, OutChannelId));
	TestEqual(TEXT("Full Channel 1 fell back to sibling Channel 2"), OutChannelId, 2);
	TestEqual(TEXT("Fallback endpoint matches Channel 2"), OutEndpoint, FString(TEXT("127.0.0.1:7779")));
	FProject_JReturnAnchor StaleAnchor = OutAnchor;
	StaleAnchor.ChannelId = StaleSibling.ChannelId;
	TestTrue(TEXT("Stale original channel also falls back to the live sibling"),
		Dungeon->ResolveReturnChannelEndpoint(StaleAnchor, OutEndpoint, OutChannelId));
	TestEqual(TEXT("Stale original cannot receive the return"), OutChannelId, Ch2.ChannelId);
	Ch2.LastHeartbeatSeconds = UProject_JDungeonInstanceSubsystem::GetCurrentUtcEpochSeconds();
	Ch2.bDraining = true;
	Dungeon->RegisterChannelHeartbeat(Ch2);
	TestFalse(TEXT("Full, stale and draining channels cannot receive the return"),
		Dungeon->ResolveCharacterReturn(CharA, OutAnchor, OutEndpoint, OutChannelId));
	TestTrue(TEXT("Field boss crowd throttle triggers above 40 players"), UProject_JDungeonInstanceSubsystem::ShouldEnableFieldBossCrowdThrottle(55));

	GI->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPhase2CombatAndCrowdTest, "ProjectJ.Systems.Phase2.CombatA09AndCrowdA03",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPhase2CombatAndCrowdTest::RunTest(const FString&)
{
	// 1. Generalized AoE geometry (Circle, Cone, Rect, Donut) & Motion Warp clamp.
	FProject_JHitQuerySpec Donut;
	Donut.Kind = EProject_JHitQueryKind::AoEDonut;
	Donut.Origin = FVector::ZeroVector;
	Donut.InnerRadius = 200.f;
	Donut.OuterRadius = 500.f;
	TestFalse(TEXT("Donut safe center excluded"), Project_J::Combat::IsPointInsideAoESpec(Donut, FVector(50, 0, 0)));
	TestTrue(TEXT("Donut ring hit included"), Project_J::Combat::IsPointInsideAoESpec(Donut, FVector(350, 0, 0)));

	FProject_JHitQuerySpec Cone;
	Cone.Kind = EProject_JHitQueryKind::AoECone;
	Cone.Origin = FVector::ZeroVector;
	Cone.ForwardDir = FVector::ForwardVector;
	Cone.OuterRadius = 400.f;
	Cone.HalfAngleDeg = 45.f;
	TestTrue(TEXT("Cone forward target hit"), Project_J::Combat::IsPointInsideAoESpec(Cone, FVector(300, 50, 0)));
	TestFalse(TEXT("Cone behind target missed"), Project_J::Combat::IsPointInsideAoESpec(Cone, FVector(-200, 0, 0)));

	FProject_JAttackWarpTarget WarpReq;
	WarpReq.RequestedLocation = FVector(1000, 0, 0);
	WarpReq.MaxAllowedWarpDistanceCm = 250.f;
	const FTransform ClampedWarp = Project_J::Combat::ClampAttackWarpTarget(FTransform::Identity, WarpReq);
	TestTrue(TEXT("Server clamps Motion Warp distance to 250cm"), FMath::IsNearlyEqual(ClampedWarp.GetLocation().X, 250.0, 1.0));

	// 2. Remote MM LOD & MandatoryWarning Crowd FX budget.
	TestEqual(TEXT("Local uses Full MM"), Project_J::Combat::ResolveRemoteLocomotionLOD(true, 0.0), EProject_JRemoteLocomotionLOD::LocalFullMM);
	TestEqual(TEXT("Mid remote switches to BlendSpace"), Project_J::Combat::ResolveRemoteLocomotionLOD(false, 3500.0), EProject_JRemoteLocomotionLOD::MidBlendSpace);
	TestTrue(TEXT("Mandatory boss warning is never culled"), Project_J::Combat::AdmitCombatFX(EProject_JCombatFXOwnerTier::MandatoryBossWarning, 999, 10000.0, true));
	TestFalse(TEXT("Other player FX throttled during boss fight"), Project_J::Combat::AdmitCombatFX(EProject_JCombatFXOwnerTier::OtherPlayer, 5, 500.0, true));

	// 3. CC Diminishing Returns (100% -> 50% -> 25% -> Immune) & Boss Stagger.
	UProject_JCCResistanceComponent* CC = NewObject<UProject_JCCResistanceComponent>();
	TestTrue(TEXT("1st Stun 100%"), FMath::IsNearlyEqual(CC->TryApplyCC(EProject_JCCCategory::Stun, 4.0f, 10.0), 4.0f));
	TestTrue(TEXT("2nd Stun 50%"), FMath::IsNearlyEqual(CC->TryApplyCC(EProject_JCCCategory::Stun, 4.0f, 15.0), 2.0f));
	TestTrue(TEXT("3rd Stun 25%"), FMath::IsNearlyEqual(CC->TryApplyCC(EProject_JCCCategory::Stun, 4.0f, 18.0), 1.0f));
	TestTrue(TEXT("4th Stun Immune"), FMath::IsNearlyEqual(CC->TryApplyCC(EProject_JCCCategory::Stun, 4.0f, 20.0), 0.0f));

	CC->ResetAll();
	CC->bIsBossCCImmune = true;
	CC->MaxStagger = 500.f;
	CC->CurrentStagger = 500.f;
	TestEqual(TEXT("Boss ignores raw stun"), CC->TryApplyCC(EProject_JCCCategory::Stun, 3.0f, 30.0), 0.0f);
	TestFalse(TEXT("Partial stagger does not break yet"), CC->ApplyStaggerDamage(300.f, 30.0));
	TestTrue(TEXT("Full stagger triggers Groggy"), CC->ApplyStaggerDamage(250.f, 31.0));
	TestTrue(TEXT("Boss is Groggy inside window"), CC->IsGroggy(33.0));

	// 4. Multi-Part Boss SSR, Pooled Projectile & Mass 3-Tier Pool.
	ProjectJPhase2Tests::FWorldScope Scope;
	AProject_JNPCCharacter* Boss = Scope.Spawn<AProject_JNPCCharacter>(FVector::ZeroVector);
	if (!TestNotNull(TEXT("Boss spawned"), Boss)) return false;

	auto* MultiPart = NewObject<UProject_JMultiPartHitComponent>(Boss);
	MultiPart->HitParts.Add({ TEXT("Body"), NAME_None, FVector(0, 0, 100), 120.f, 1.0f, false });
	MultiPart->HitParts.Add({ TEXT("Head"), NAME_None, FVector(300, 0, 200), 80.f, 2.0f, true });
	Boss->AddInstanceComponent(MultiPart);
	MultiPart->RegisterComponent();
	MultiPart->RecordSample(1.0);
	Boss->SetActorLocation(FVector(500, 0, 0));
	MultiPart->RecordSample(1.1);

	FName HitPart;
	float Mult = 0.f;
	FVector HitCenter;
	TestTrue(TEXT("Multi-part SSR rewinds boss head weakpoint at T=1.0"),
		MultiPart->VerifyRewoundSegmentHit(1.0, FVector(300, -100, 200), FVector(300, 100, 200), 20.f, HitPart, Mult, HitCenter));
	TestEqual(TEXT("Head part selected"), HitPart, FName(TEXT("Head")));
	TestTrue(TEXT("Weakpoint 2x multiplier reported"), FMath::IsNearlyEqual(Mult, 2.0f));

	auto* Pool = Scope.World->GetSubsystem<UProject_JNPCPoolSubsystem>();
	TestEqual(TEXT("Combat NPC never demotes below Tier 0"),
		UProject_JNPCPoolSubsystem::EvaluateTargetTier(EProject_JNPCCrowdTier::Tier0_NearFullActor, 9000.0, true),
		EProject_JNPCCrowdTier::Tier0_NearFullActor);
	TestEqual(TEXT("Non-combat far NPC demotes to Tier 2 Mass VAT"),
		UProject_JNPCPoolSubsystem::EvaluateTargetTier(EProject_JNPCCrowdTier::Tier0_NearFullActor, 8000.0, false),
		EProject_JNPCCrowdTier::Tier2_FarMassVAT);
	TestTrue(TEXT("Pool stores released NPC"), Pool->ReleaseNPCToPool(Boss));
	TestEqual(TEXT("Reacquired NPC reuses pooled instance"), Pool->AcquireNPC(AProject_JNPCCharacter::StaticClass(), FTransform::Identity), Boss);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPhase2PvEThreatReviveLootTest, "ProjectJ.Systems.Phase2.PvEThreatReviveAndLoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPhase2PvEThreatReviveLootTest::RunTest(const FString&)
{
	ProjectJPhase2Tests::FWorldScope Scope;
	AProject_JNPCCharacter* Tank = Scope.Spawn<AProject_JNPCCharacter>(FVector(200, 0, 0));
	AProject_JNPCCharacter* RangedDPS = Scope.Spawn<AProject_JNPCCharacter>(FVector(1500, 0, 0));
	AProject_JNPCCharacter* Boss = Scope.Spawn<AProject_JNPCCharacter>(FVector::ZeroVector);
	if (!Tank || !RangedDPS || !Boss) return false;

	auto* Threat = NewObject<UProject_JThreatComponent>(Boss);
	Threat->HomeLocation = FVector::ZeroVector;
	const FGuid TankId = FGuid::NewGuid();
	const FGuid DpsId = FGuid::NewGuid();

	Threat->AddDamageThreat(Tank, TankId, 1000.0, 1.0, 1.0);
	Threat->AddDamageThreat(RangedDPS, DpsId, 1200.0, 1.0, 1.0); // 120% < 130% ranged hysteresis!
	TestEqual(TEXT("Ranged 120% threat does NOT steal aggro (130% hysteresis)"), Threat->ResolvePrimaryTarget(FVector::ZeroVector, 2.0), static_cast<AActor*>(Tank));

	Threat->AddDamageThreat(RangedDPS, DpsId, 200.0, 1.0, 2.5); // 140% > 130% -> pulls aggro
	TestEqual(TEXT("Ranged 140% threat pulls aggro"), Threat->ResolvePrimaryTarget(FVector::ZeroVector, 3.0), static_cast<AActor*>(RangedDPS));

	Threat->ApplyTaunt(Tank, 4.0, 3.5);
	TestEqual(TEXT("Taunt forces boss onto tank"), Threat->ResolvePrimaryTarget(FVector::ZeroVector, 5.0), static_cast<AActor*>(Tank));

	// Player death removes active aggro but preserves boss ContributionMap.
	Threat->RemoveFromActiveAggro(Tank);
	const TArray<FGuid> Contributors = Threat->GetEligibleContributors(100000.0, 0.001);
	TestTrue(TEXT("Dead tank remains eligible in boss ContributionMap"), Contributors.Contains(TankId));
	TestTrue(TEXT("DPS is eligible in boss ContributionMap"), Contributors.Contains(DpsId));

	// Boss phases & leash/evade.
	TestEqual(TEXT("Boss transitions to Phase 2 at 65% HP"), Threat->EvaluateBossPhase(650.f, 1000.f), 2);
	TestEqual(TEXT("Boss transitions to Phase 3 at 30% HP"), Threat->EvaluateBossPhase(300.f, 1000.f), 3);
	TestTrue(TEXT("Boss enters Evade when dragged beyond LeashRadius"), Threat->EvaluateLeashAndEvade(FVector(5000, 0, 0), 0.f, true));
	Boss->GetAttributeSet()->InitMaxHealth(1000.f);
	Boss->GetAttributeSet()->SetHealth(300.f);
	Threat->FinishEvadeReset();
	TestEqual(TEXT("Evade reset restores Phase 1"), Threat->CurrentBossPhase, 1);
	TestEqual(TEXT("Authority evade reset restores the boss to full health"), Boss->GetAttributeSet()->GetHealth(), 1000.f);

	// Death & Revive lifecycle.
	auto* Revive = Scope.World->GetSubsystem<UProject_JDeathReviveSubsystem>();
	Revive->RegisterGraveyard({ TEXT("TownNorth"), FVector(1000, 1000, 100), FRotator::ZeroRotator });
	Tank->GetAttributeSet()->InitMaxHealth(100.f);
	Tank->GetAttributeSet()->SetHealth(100.f);
	Tank->GetAttributeSet()->SetHealth(0.f);
	Revive->NotifyCharacterDied(Tank, 10.0);
	TestFalse(TEXT("Dead character cannot perform alive actions"), UProject_JDeathReviveSubsystem::CanCharacterPerformAliveAction(Tank));
	Revive->SetDungeonBossFightActive(true);
	FVector RevLoc;
	TestFalse(TEXT("An unprovisioned revive item cannot revive for free"), Revive->ExecuteRevive(Tank, EProject_JRevivePath::ReviveItem, 12.0, RevLoc));
	TestFalse(TEXT("Graveyard revive blocked during dungeon boss fight"), Revive->ExecuteRevive(Tank, EProject_JRevivePath::Graveyard, 12.0, RevLoc));
	Revive->SetDungeonBossFightActive(false);
	TestTrue(TEXT("Graveyard revive succeeds outside boss fight"), Revive->ExecuteRevive(Tank, EProject_JRevivePath::Graveyard, 15.0, RevLoc));
	TestTrue(TEXT("Revived character can act again"), UProject_JDeathReviveSubsystem::CanCharacterPerformAliveAction(Tank));
	TestTrue(TEXT("Post-revive 3s invulnerability active"), Revive->HasPostReviveInvulnerability(Tank, 16.5));
	auto* ASC = Cast<UProject_JAbilitySystemComponent>(Tank->GetAbilitySystemComponent());
	if (!TestNotNull(TEXT("Revived target ASC"), ASC)) return false;
	auto* Damage = NewObject<UGameplayEffect>();
	auto& HealthLoss = Damage->Modifiers.AddDefaulted_GetRef();
	HealthLoss.Attribute = UProject_JAttributeSet::GetHealthAttribute();
	HealthLoss.ModifierOp = EGameplayModOp::Additive;
	HealthLoss.ModifierMagnitude = FScalableFloat(-10.0f);
	const float RestoredHealth = Tank->GetAttributeSet()->GetHealth();
	ASC->ApplyGameplayEffectToSelf(Damage, 1.0f, ASC->MakeEffectContext());
	TestEqual(TEXT("Revive protection rejects instant Health losses"), Tank->GetAttributeSet()->GetHealth(), RestoredHealth);
	ASC->ProtectAvatarHealthLoss(Tank, Scope.World->GetTimeSeconds());
	ASC->ApplyGameplayEffectToSelf(Damage, 1.0f, ASC->MakeEffectContext());
	TestEqual(TEXT("Instant Health losses resume at the protection deadline"), Tank->GetAttributeSet()->GetHealth(), RestoredHealth - 10.0f);
	Tank->GetAttributeSet()->SetHealth(0.0f);
	Revive->NotifyCharacterDied(Tank, Scope.World->GetTimeSeconds() - 181.0);
	Revive->SetDungeonBossFightActive(true);
	TestFalse(TEXT("A due corpse remains dead while a boss locks graveyard release"), Revive->CanCharacterPerformAliveAction(Tank));
	Revive->SetDungeonBossFightActive(false);
	// A timer installed between frames enters the pending set before its first active tick.
	++GFrameCounter;
	Scope.World->GetTimerManager().Tick(0.0f);
	++GFrameCounter;
	Scope.World->GetTimerManager().Tick(1.25f);
	TestTrue(TEXT("The world timer releases a due corpse after the boss lock clears"), Revive->CanCharacterPerformAliveAction(Tank));

	// Boss contribution loot with mail fallback & idempotency.
	UGameInstance* GI = NewObject<UGameInstance>(GEngine);
	GI->Init();
	auto* Loot = GI->GetSubsystem<UProject_JLootSubsystem>();
	auto* Table = NewObject<UProject_JLootTableDefinition>();
	Table->Entries.Add({ TEXT("DragonScale"), 1, 2, 1.0f, true });
	Table->MinGold = 500;
	Table->MaxGold = 500;
	const auto Grants = Loot->DistributeBossContributionLoot(
		TEXT("FieldBoss_Dragon"), 1, Table, { TankId, DpsId },
		[&](const FGuid& Id, int32) { return Id == TankId; }, // DPS bag full -> mail fallback!
		42);
	TestEqual(TEXT("Both eligible contributors receive personal loot"), Grants.Num(), 2);
	if (Grants.Num() == 2)
	{
		TestFalse(TEXT("Tank with bag space receives direct loot"), Grants[0].bRoutedToMailFallback);
		TestTrue(TEXT("DPS with full bag routes to MAIL_ESCROW fallback"), Grants[1].bRoutedToMailFallback);
	}
	const auto DupGrants = Loot->DistributeBossContributionLoot(
		TEXT("FieldBoss_Dragon"), 1, Table, { TankId, DpsId },
		[](const FGuid&, int32) { return true; }, 42);
	TestEqual(TEXT("Duplicate boss reward call produces 0 duplicate grants"), DupGrants.Num(), 0);

	GI->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAuditCombatAuthorityTest, "ProjectJ.AuditFollowup.CombatAuthority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAuditCombatAuthorityTest::RunTest(const FString&)
{
	ProjectJPhase2Tests::FWorldScope Scope;
	auto* PvP = IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Combat.OpenWorldPvP"));
	if (!TestNotNull(TEXT("Open world PvP policy"), PvP)) return false;
	const int32 PreviousPvP = PvP->GetInt();
	const EConsoleVariableFlags PreviousPriority = static_cast<EConsoleVariableFlags>(PvP->GetFlags() & ECVF_SetByMask);
	PvP->Set(0, PreviousPriority);
	ON_SCOPE_EXIT { PvP->Set(PreviousPvP, PreviousPriority); };

	auto* Player = Scope.Spawn<AProject_JSpellbladeCharacter>();
	auto* OtherPlayer = Scope.Spawn<AProject_JSpellbladeCharacter>(FVector(200, 0, 0));
	auto* SourceNPC = Scope.Spawn<AProject_JNPCCharacter>(FVector(0, 1000, 0));
	auto* TargetNPC = Scope.Spawn<AProject_JNPCCharacter>(FVector(200, 1000, 0));
	auto* Encounter = Scope.Spawn<AActor>();
	if (!Player || !OtherPlayer || !SourceNPC || !TargetNPC || !Encounter) return false;
	using Project_J::Combat::CanAffectCombatTarget;
	TestFalse(TEXT("Default PvE protects unrelated player characters"), CanAffectCombatTarget(Player, OtherPlayer, nullptr));
	TestTrue(TEXT("Unowned test NPCs can still hit each other"), CanAffectCombatTarget(SourceNPC, TargetNPC, nullptr));
	SourceNPC->SetOwner(Encounter);
	TargetNPC->SetOwner(Encounter);
	TestFalse(TEXT("NPCs belonging to the same encounter cannot hit each other"), CanAffectCombatTarget(SourceNPC, TargetNPC, nullptr));

	auto* Controller = Scope.Spawn<APlayerController>();
	auto* State = Scope.Spawn<AProject_JPlayerState>();
	if (!Controller || !State) return false;
	State->SetOwner(Controller);
	Controller->SetPlayerState(State);
	Player->SetPlayerState(State);
	Controller->Possess(Player);
	auto* ASC = State->GetProjectJAbilitySystemComponent();
	if (!TestNotNull(TEXT("Player ASC"), ASC)) return false;
	Player->GetLocomotionAnimStateComponent()->SetMoveInput(FVector2D(0, 1));
	FMoverInputCmdContext Command;
	Player->ProduceInput_Implementation(16, Command);
	const auto* Inputs = Command.InputCollection.FindDataByType<FCharacterDefaultInputs>();
	if (!TestNotNull(TEXT("Alive input was published"), Inputs)) return false;
	TestFalse(TEXT("Alive movement control produces nonzero intent"), Inputs->GetMoveInput().IsNearlyZero());
	const auto* Melee = GetDefault<UProject_JGameplayAbility_Melee>();
	const auto* Sprint = GetDefault<UProject_JGameplayAbility_Sprint>();
	ASC->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
	TestTrue(TEXT("Melee and sprint tag gates allow an alive player"),
		Melee->DoesAbilitySatisfyTagRequirements(*ASC) && Sprint->DoesAbilitySatisfyTagRequirements(*ASC));
	ASC->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_Dead);
	TestFalse(TEXT("Dead attacker cannot affect an enemy"), CanAffectCombatTarget(Player, TargetNPC, nullptr));
	TestFalse(TEXT("Melee rejects the dead state in GAS activation tags"), Melee->DoesAbilitySatisfyTagRequirements(*ASC));
	TestFalse(TEXT("Sprint rejects the dead state in GAS activation tags"), Sprint->DoesAbilitySatisfyTagRequirements(*ASC));
	Player->ProduceInput_Implementation(16, Command);
	Inputs = Command.InputCollection.FindDataByType<FCharacterDefaultInputs>();
	TestTrue(TEXT("Dead mover clears existing movement intent"), Inputs && Inputs->GetMoveInput().IsNearlyZero());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAuditProjectileBlockTest, "ProjectJ.AuditFollowup.ProjectileCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAuditProjectileBlockTest::RunTest(const FString&)
{
	auto* Async = IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Projectile.UseAsyncSweep"));
	auto* Threshold = IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Projectile.AsyncSweepThreshold"));
	if (!Async || !Threshold) return false;
	const int32 PreviousAsync = Async->GetInt();
	const int32 PreviousThreshold = Threshold->GetInt();
	ON_SCOPE_EXIT { Async->Set(PreviousAsync, ECVF_SetByCode); Threshold->Set(PreviousThreshold, ECVF_SetByCode); };
	Threshold->Set(1, ECVF_SetByCode);
	for (const int32 Mode : {0, 1})
	{
		Async->Set(Mode, ECVF_SetByCode);
		ProjectJPhase2Tests::FWorldScope Scope;
		auto* Encounter = Scope.Spawn<AActor>();
		auto* Attacker = Scope.Spawn<AProject_JNPCCharacter>(FVector(0, 0, 100));
		auto* Ally = Scope.Spawn<AProject_JNPCCharacter>(FVector(150, 0, 100));
		auto* Enemy = Scope.Spawn<AProject_JNPCCharacter>(FVector(450, 0, 100));
		auto* Wall = Scope.Spawn<AActor>();
		auto* Floor = Scope.Spawn<AActor>();
		auto* Projectiles = Scope.World->GetSubsystem<UProject_JProjectileManagerSubsystem>();
		if (!Encounter || !Attacker || !Ally || !Enemy || !Wall || !Floor || !Projectiles) return false;
		Attacker->SetOwner(Encounter);
		Ally->SetOwner(Encounter);
		for (auto* NPC : {Ally, Enemy})
			NPC->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		const auto AddBlocker = [](AActor* Actor, const FVector& Extent, const FVector& Location)
		{
			auto* Component = NewObject<UBoxComponent>(Actor);
			Actor->SetRootComponent(Component);
			Component->SetBoxExtent(Extent);
			Component->SetWorldLocation(Location);
			Component->SetCollisionObjectType(ECC_WorldStatic);
			Component->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Component->SetCollisionResponseToAllChannels(ECR_Block);
			Component->RegisterComponent();
			return Component;
		};
		auto* Box = AddBlocker(Wall, FVector(10, 150, 150), FVector(300, 0, 100));
		AddBlocker(Floor, FVector(1000, 300, 30), FVector(300, 0, -30));
		const auto StepToContact = [&]()
		{
			const int32 DeliveryTicks = Mode == 0 ? 1 : 2;
			for (int32 Tick = 0; Tick < DeliveryTicks; ++Tick)
			{
				++GFrameCounter;
				Scope.World->Tick(LEVELTICK_All, 0.1f);
			}
		};
		TArray<AActor*> Hits;
		const auto Launch = [&]()
		{
			return Projectiles->SpawnProjectile(Attacker, FVector(0, 0, 100), FVector(6000, 0, 0), 8.f, 1.f, 2, 0.f,
				FProjectJOnProjectileHit::CreateLambda([&Hits](uint64, AActor* Target, const FHitResult&) { Hits.Add(Target); }));
		};
		if (!TestTrue(TEXT("Piercing projectile starts"), Launch() != 0)) return false;
		StepToContact();
		TestTrue(TEXT("The same sweep passes the ally and stops on the wall"), Hits.Num() == 1 && Hits[0] == Wall);
		TestEqual(TEXT("A wall stops the projectile even with remaining pierces"), Projectiles->GetActiveProjectileCount(), 0);
		Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		StepToContact();
		Hits.Reset();
		if (!TestTrue(TEXT("Control projectile starts"), Launch() != 0)) return false;
		StepToContact();
		TestTrue(FString::Printf(TEXT("Mode %d passes the ally and reaches the enemy with the wall disabled (hits %d)"), Mode, Hits.Num()),
			Hits.Num() == 1 && Hits[0] == Enemy);
		StepToContact();
		TestTrue(TEXT("The pierced enemy does not dispatch a duplicate hit"), Hits.Num() == 1 && Hits[0] == Enemy);
	}
	return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCorpseEndPlayTrackingTest, "ProjectJ.Systems.Phase2.CorpseEndPlayTracking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCorpseEndPlayTrackingTest::RunTest(const FString&)
{
	ProjectJPhase2Tests::FWorldScope Scope;
	auto* Revive = Scope.World->GetSubsystem<UProject_JDeathReviveSubsystem>();
	auto* Character = Scope.Spawn<AProject_JNPCCharacter>();
	if (!Revive || !Character) return false;
	Revive->SetDungeonBossFightActive(true);
	Revive->NotifyCharacterDied(Character, 10.0);
	Revive->NotifyCharacterDied(Character, 20.0);
	TestEqual(TEXT("Duplicate notifications keep a single death"), Revive->DeathTimes.Num(), 1);
	TestEqual(TEXT("The original death timestamp is retained"), Revive->DeathTimes.FindRef(Character), 10.0);
	Character->Destroy();
	TestEqual(TEXT("EndPlay removes a corpse while release is suspended"), Revive->DeathTimes.Num(), 0);
	return !HasAnyErrors();
}
#endif
