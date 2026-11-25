#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "AI/Project_JThreatComponent.h"
#include "Audio/Project_JAudioBudgetSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Camera/Project_JCameraComponent.h"
#include "Combat/Project_JMultiPartHitComponent.h"
#include "Combat/Project_JProjectileManagerSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Economy/Project_JEconomySubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/Project_JGameMode.h"
#include "Game/Project_JPlayerState.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/Project_JDeathReviveSubsystem.h"
#include "Gameplay/Project_JLootSubsystem.h"
#include "Gameplay/Project_JProgressionAndLifeSubsystem.h"
#include "GameplayEffect.h"
#include "Gathering/Project_JGatheringNode.h"
#include "Kismet/GameplayStatics.h"
#include "Mass/Project_JNPCPoolSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Mount/Project_JMountMoverComponent.h"
#include "Network/Project_JMMOClientBridgeComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Project_JCCResistanceComponent.h"
#include "Project_JNPCCharacter.h"
#include "Security/Project_JServerIntegritySubsystem.h"
#include "Social/Project_JGuildAndSocialExtensions.h"
#include "Social/Project_JSocialSubsystem.h"
#include "UI/Project_JUIWindowStackSubsystem.h"
#include "World/Project_JDungeonInstanceSubsystem.h"
#include "World/Project_JWorldPortal.h"
#include "World/Project_JWorldStreamingComponent.h"

namespace ProjectJWiringTests
{
struct FWorldScope
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UGameInstance* GI = NewObject<UGameInstance>(GEngine);
	FWorldScope()
	{
		GI->Init();
		World->SetGameInstance(GI);
		auto& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
		Ctx.OwningGameInstance = GI;
		Ctx.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->GetWorldSettings()->NotifyBeginPlay();
		World->GetWorldSettings()->NotifyMatchStarted();
	}
	~FWorldScope()
	{
		World->EndPlay(EEndPlayReason::LevelTransition);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		GI->Shutdown();
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJBossCameraSourceTest, "ProjectJ.Systems.Wiring.BossCameraSources",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJBossCameraSourceTest::RunTest(const FString&)
{
	ProjectJWiringTests::FWorldScope Scope;
	auto* Player = Scope.Spawn<AActor>();
	auto* First = Scope.Spawn<AActor>();
	auto* Second = Scope.Spawn<AActor>();
	if (!Player || !First || !Second) return false;
	auto* Camera = NewObject<UProject_JCameraComponent>(Player);
	Player->AddInstanceComponent(Camera);
	Camera->RegisterComponent();

	Camera->SetBossCameraSource(First, true);
	Camera->SetBossCameraSource(Second, true);
	Camera->SetBossCameraSource(First, false);
	TestEqual(TEXT("One encounter releasing the camera preserves the other"), Camera->GetActiveCameraContextMode(),
		EProject_JCameraContextMode::GiantBossZoomOut);
	Camera->SetCameraContextMode(EProject_JCameraContextMode::OnFoot);
	TestEqual(TEXT("Explicit camera override takes priority"), Camera->GetActiveCameraContextMode(),
		EProject_JCameraContextMode::OnFoot);
	Camera->ClearCameraContextOverride();
	TestEqual(TEXT("Clearing the explicit override resumes the active encounter"), Camera->GetActiveCameraContextMode(),
		EProject_JCameraContextMode::GiantBossZoomOut);
	Camera->SetBossCameraSource(Second, false);
	TestEqual(TEXT("The final release restores the normal camera"), Camera->GetActiveCameraContextMode(),
		EProject_JCameraContextMode::OnFoot);
	Camera->SetBossCameraSource(First, true);
	First->Destroy();
	TestEqual(TEXT("An encounter destroyed before releasing its request cannot leave the camera zoomed out"),
		Camera->GetActiveCameraContextMode(), EProject_JCameraContextMode::OnFoot);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJWiringEndToEndTest, "ProjectJ.Systems.Wiring.EndToEndRuntimeIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJWiringEndToEndTest::RunTest(const FString&)
{
	ProjectJWiringTests::FWorldScope Scope;

	// 1. Live AProject_JMountMoverPawn mount -> fly -> fatal damage -> auto ForceStop + Dismount + possession restore.
	APlayerController* PC = Scope.Spawn<APlayerController>();
	ACharacter* Rider = Scope.Spawn<ACharacter>(FVector(50, 0, 100));
	AProject_JMountMoverPawn* MoverMount = Scope.Spawn<AProject_JMountMoverPawn>(FVector(0, 0, 100));
	if (!TestNotNull(TEXT("PC"), PC) || !TestNotNull(TEXT("Rider"), Rider) || !TestNotNull(TEXT("MoverMount"), MoverMount))
		return false;

	PC->Possess(Rider);
	MoverMount->GetMountMover()->MotionProfile.bCanFly = true;
	TestTrue(TEXT("Rider mounts AProject_JMountMoverPawn"), MoverMount->TryMountRider(Rider));
	TestTrue(TEXT("Controller now possesses MoverMount"), PC->GetPawn() == MoverMount);
	TestTrue(TEXT("Mount enters FlyingMountMode"), MoverMount->GetMountMover()->TryEnterFlightMode());
	TestTrue(TEXT("Fatal damage accepted on MoverMount"), MoverMount->ApplyMountDamage(1000.f));
	TestFalse(TEXT("Fatal damage auto-dismounted rider"), MoverMount->IsMounted());
	TestTrue(TEXT("Controller restored to original rider after fatal dismount"), PC->GetPawn() == Rider);

	// 2. Live UProject_JCameraComponent context mode override (GiantBossZoomOut) interpolation.
	auto* Boom = NewObject<USpringArmComponent>(Rider);
	auto* Cam = NewObject<UCameraComponent>(Rider);
	auto* JCam = NewObject<UProject_JCameraComponent>(Rider);
	Rider->AddInstanceComponent(Boom); Boom->RegisterComponent();
	Rider->AddInstanceComponent(Cam); Cam->RegisterComponent();
	Rider->AddInstanceComponent(JCam); JCam->RegisterComponent();
	JCam->Initialize(Boom, Cam);
	JCam->SetCameraContextMode(EProject_JCameraContextMode::GiantBossZoomOut);
	TestEqual(TEXT("Camera context mode set to GiantBossZoomOut"), JCam->GetActiveCameraContextMode(), EProject_JCameraContextMode::GiantBossZoomOut);

	// 3. Live AProject_JWorldPortal (DungeonId) -> UProject_JDungeonInstanceSubsystem -> TravelURL -> Ticket Verify.
	AProject_JPlayerState* PS = Scope.Spawn<AProject_JPlayerState>();
	PC->PlayerState = PS;
	PS->SetOwner(PC);
	const FGuid CharId = FGuid::NewGuid();
	PS->SetIdentity(FProject_JAccountId{ FGuid::NewGuid() }, FProject_JCharacterId{ CharId });

	auto* Bridge = NewObject<UProject_JMMOClientBridgeComponent>(PC);
	PC->AddInstanceComponent(Bridge); Bridge->RegisterComponent();
	FProject_JDungeonJoinTicket ReceivedTicket;
	AProject_JWorldPortal* DungeonPortal = Scope.Spawn<AProject_JWorldPortal>(Rider->GetActorLocation() + FVector(80, 0, 0));
	DungeonPortal->DungeonId = TEXT("Dungeon_Catacombs");
	TestTrue(TEXT("Dungeon portal is spatially available"), DungeonPortal->IsSpatiallyAvailable(Rider));
	IProject_JInteractable::Execute_Interact(DungeonPortal, Rider);
	auto* DungeonSub = Scope.GI->GetSubsystem<UProject_JDungeonInstanceSubsystem>();
	TestEqual(TEXT("Interacting with DungeonPortal allocated 1 dungeon instance"), DungeonSub->GetActiveInstanceCount(), 1);

	// Build Travel URL & verify ticket signing + replay defense.
	FGuid InstId;
	TArray<FProject_JDungeonJoinTicket> Tickets;
	TMap<FGuid, FProject_JReturnAnchor> Anchors;
	const FGuid MenuCharId = FGuid::NewGuid();
	Anchors.Add(MenuCharId, FProject_JReturnAnchor{ TEXT("MainWorld"), 1, FVector::ZeroVector, FRotator::ZeroRotator });
	TestTrue(TEXT("Menu entry allocates second instance"),
		DungeonSub->RequestDungeonEntry(EProject_JDungeonEntrySource::MenuUI, TEXT("Dungeon_Spire"), { MenuCharId }, Anchors, 10.0, InstId, Tickets));
	if (!TestEqual(TEXT("Menu entry issues one ticket"), Tickets.Num(), 1)) return false;
	const FString TravelUrl = UProject_JMMOClientBridgeComponent::BuildDungeonTravelURL(Tickets[0]);
	TestTrue(TEXT("Travel URL carries signed ticket params"), TravelUrl.Contains(TEXT("TicketSig=")) && TravelUrl.Contains(TEXT("TicketNonce=")));
	TestTrue(TEXT("PreLogin ticket verification accepts valid ticket once"), DungeonSub->VerifyAndConsumeJoinTicket(Tickets[0], 15.0));
	TestFalse(TEXT("PreLogin ticket verification rejects replay"), DungeonSub->VerifyAndConsumeJoinTicket(Tickets[0], 16.0));

	// 4. Live UProject_JCombatHitValidationComponent generalized AoE + MultiPartBoss + CC DR wiring.
	AProject_JNPCCharacter* Attacker = Scope.Spawn<AProject_JNPCCharacter>(FVector::ZeroVector);
	AProject_JNPCCharacter* Boss = Scope.Spawn<AProject_JNPCCharacter>(FVector(250, 0, 0));
	auto* HitVal = NewObject<UProject_JCombatHitValidationComponent>(Attacker);
	Attacker->AddInstanceComponent(HitVal); HitVal->RegisterComponent();
	auto* MultiPart = NewObject<UProject_JMultiPartHitComponent>(Boss);
	MultiPart->HitParts.Add({ TEXT("Head"), NAME_None, FVector(0, 0, 150), 90.f, 2.0f, true });
	Boss->AddInstanceComponent(MultiPart); MultiPart->RegisterComponent();
	MultiPart->RecordSample(Scope.World->GetTimeSeconds());

	FProject_JHitQuerySpec BossQuery;
	BossQuery.Kind = EProject_JHitQueryKind::MultiPartBoss;
	BossQuery.Origin = FVector(0, 0, 150);
	BossQuery.ForwardDir = FVector::ForwardVector;
	BossQuery.OuterRadius = 350.f;
	BossQuery.InnerRadius = 25.f;
	BossQuery.bRequireLineOfSight = false;
	TArray<AActor*> HitActors;
	TArray<float> Mults;
	const int32 Hits = HitVal->ServerExecuteHitQuery(BossQuery, { Boss }, HitActors, Mults);
	TestEqual(TEXT("Generalized ServerExecuteHitQuery hits boss weakpoint"), Hits, 1);
	if (Hits == 1)
	{
		TestTrue(TEXT("Weakpoint 2x multiplier returned"), FMath::IsNearlyEqual(Mults[0], 2.0f));
	}

	TestNotNull(TEXT("Boss automatically includes default CCResistanceComponent"), Boss->GetCCResistanceComponent());
	TestTrue(TEXT("ApplyValidatedCC 1st stun full"), FMath::IsNearlyEqual(HitVal->ApplyValidatedCC(Boss, EProject_JCCCategory::Stun, 2.0f), 2.0f));
	TestTrue(TEXT("ApplyValidatedCC 2nd stun 50% DR"), FMath::IsNearlyEqual(HitVal->ApplyValidatedCC(Boss, EProject_JCCCategory::Stun, 2.0f), 1.0f));

	// 5. Live UProject_JNPCPoolSubsystem VAT Proxy -> ISM batch sync.
	auto* Pool = Scope.World->GetSubsystem<UProject_JNPCPoolSubsystem>();
	auto* ISM = NewObject<UInstancedStaticMeshComponent>(Attacker);
	Attacker->AddInstanceComponent(ISM); ISM->RegisterComponent();
	Pool->UpsertVATProxy({ 101, TEXT("Goblin"), FTransform(FVector(1000, 0, 0)), 1 });
	Pool->UpsertVATProxy({ 102, TEXT("Goblin"), FTransform(FVector(2000, 0, 0)), 2 });
	TestEqual(TEXT("SyncVATProxiesToISM pushes 2 GPU instances"), Pool->SyncVATProxiesToISM(ISM, TEXT("Goblin")), 2);
	TestEqual(TEXT("ISM instance count matches"), ISM->GetInstanceCount(), 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAutoWiringParentContractsTest, "ProjectJ.Systems.Wiring.AutoWiringParentContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAutoWiringParentContractsTest::RunTest(const FString&)
{
	ProjectJWiringTests::FWorldScope Scope;

	// 1. Field Boss / NPC default components + BrainDefinition auto-sync + GAS HealthLoss -> Threat & Boss Phase.
	AProject_JNPCCharacter* Boss = Scope.Spawn<AProject_JNPCCharacter>(FVector(300, 0, 0));
	if (!TestNotNull(TEXT("Boss spawned"), Boss)) return false;
	TestNotNull(TEXT("NPC auto-creates ThreatComponent"), Boss->GetThreatComponent());
	TestNotNull(TEXT("NPC auto-creates CCResistanceComponent"), Boss->GetCCResistanceComponent());

	UProject_JNPCBrainDefinition* Brain = NewObject<UProject_JNPCBrainDefinition>();
	Brain->BrainId = TEXT("Brain_TestDragon");
	Brain->bIsGiantBoss = true;
	Brain->BossPhaseHealthThresholds = { 0.70f, 0.35f };
	Boss->BrainDefinition = Brain;
	Boss->GetThreatComponent()->BrainDefinition = Brain;
	Boss->GetCCResistanceComponent()->bIsBossCCImmune = true;
	Boss->EnableThreatObservation();

	AProject_JPlayerState* AttackerPS = Scope.Spawn<AProject_JPlayerState>();
	const FGuid AttackerCharId = FGuid::NewGuid();
	AttackerPS->SetIdentity(FProject_JAccountId{ FGuid::NewGuid() }, FProject_JCharacterId{ AttackerCharId });
	TestEqual(TEXT("PlayerState exposes CharacterGuid via Project_JCore interface"),
		static_cast<const IProject_JSocialStateInterface*>(AttackerPS)->GetAuthoritativeCharacterGuid(), AttackerCharId);

	Boss->GetAttributeSet()->SetMaxHealth(1000.f);
	Boss->GetAttributeSet()->SetHealth(1000.f);
	FProject_JConfirmedHealthGELoss Loss;
	Loss.Proof.SourceOwner = AttackerPS;
	Loss.OldValue = 1000.f;
	Loss.NewValue = 650.f;
	Boss->GetAttributeSet()->OnConfirmedHealthGELoss.Broadcast(Loss);
	TestTrue(TEXT("Confirmed Health GE loss auto-recorded damage threat"),
		Boss->GetThreatComponent()->GetThreatValue(AttackerPS) >= 350.0);
	TestEqual(TEXT("Boss auto-advanced to Phase 2 below 70% HP"), Boss->GetThreatComponent()->CurrentBossPhase, 2);
	TestTrue(TEXT("Attacker automatically eligible for boss personal loot"),
		Boss->GetThreatComponent()->GetEligibleContributors(1000.0, 0.001).Contains(AttackerCharId));

	// 2. Boss reward item mail minting into MailEscrow + atomic ClaimMailAttachments.
	auto* Econ = Scope.GI->GetSubsystem<UProject_JEconomySubsystem>();
	const FGuid RewardMailId = Econ->SendSystemRewardItemMail(
		AttackerCharId, TEXT("MAIL_FIELDBOSS_REWARD"), TEXT("GOLD"), 250, TEXT("Item_DragonScale"), 1);
	TestTrue(TEXT("SendSystemRewardItemMail succeeded"), RewardMailId.IsValid());
	TestFalse(TEXT("Mail claim rolls back when inventory full"), Econ->ClaimMailAttachments(RewardMailId, AttackerCharId, false));
	TestTrue(TEXT("Mail claim succeeds when inventory has space"), Econ->ClaimMailAttachments(RewardMailId, AttackerCharId, true));
	TestEqual(TEXT("Gold credited from boss reward mail"), Econ->GetLedger().GetBalance(AttackerCharId, TEXT("GOLD")), 250ll);

	// 3. MountMoverPawn IMoverInputProducerInterface::ProduceInput + wheel zoom.
	AProject_JMountMoverPawn* Mount = Scope.Spawn<AProject_JMountMoverPawn>(FVector(0, 0, 100));
	Mount->SetMountControlInput(FVector2D(1.0, 0.5), -0.75f, true, true);
	FMoverInputCmdContext InputCmd;
	IMoverInputProducerInterface::Execute_ProduceInput(Mount, 16, InputCmd);
	const auto* MountIn = InputCmd.InputCollection.FindDataByType<FProject_JMountMoverInputs>();
	TestNotNull(TEXT("Mount ProduceInput populated FProject_JMountMoverInputs"), MountIn);
	if (MountIn)
	{
		TestTrue(TEXT("Throttle/Turn propagated"), MountIn->ThrottleTurn.Equals(FVector2D(1.0, 0.5), 0.01));
		TestTrue(TEXT("Pitch & Boost/Glide propagated"), FMath::IsNearlyEqual(MountIn->PitchInput, -0.75f) && MountIn->bBoostHeld && MountIn->bGlideHeld);
	}

	// 4. DungeonDefinition + Party Group Auto-Entry via SocialSubsystem + Disconnect Grace.
	auto* Dungeon = Scope.GI->GetSubsystem<UProject_JDungeonInstanceSubsystem>();
	auto* Social = Scope.GI->GetSubsystem<UProject_JSocialSubsystem>();
	UProject_JDungeonDefinition* Def = NewObject<UProject_JDungeonDefinition>();
	Def->DungeonId = TEXT("Dungeon_FireTemple");
	Def->MinPartySize = 2;
	Def->MaxPartySize = 4;
	Def->DungeonMap = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/Dungeon_FireTemple.Dungeon_FireTemple")));
	Dungeon->RegisterDungeonDefinition(Def);

	APlayerController* PC1 = Scope.Spawn<APlayerController>();
	APlayerController* PC2 = Scope.Spawn<APlayerController>();
	AProject_JPlayerState* PS1 = Scope.Spawn<AProject_JPlayerState>();
	AProject_JPlayerState* PS2 = Scope.Spawn<AProject_JPlayerState>();
	const FGuid Char1 = FGuid::NewGuid();
	const FGuid Char2 = FGuid::NewGuid();
	PS1->SetIdentity(FProject_JAccountId{ FGuid::NewGuid() }, FProject_JCharacterId{ Char1 });
	PS2->SetIdentity(FProject_JAccountId{ FGuid::NewGuid() }, FProject_JCharacterId{ Char2 });
	PC1->PlayerState = PS1; PS1->SetOwner(PC1);
	PC2->PlayerState = PS2; PS2->SetOwner(PC2);
	auto* Bridge1 = NewObject<UProject_JMMOClientBridgeComponent>(PC1); PC1->AddInstanceComponent(Bridge1); Bridge1->RegisterComponent();
	auto* Bridge2 = NewObject<UProject_JMMOClientBridgeComponent>(PC2); PC2->AddInstanceComponent(Bridge2); Bridge2->RegisterComponent();

	// Solo rejected when MinPartySize == 2.
	TestFalse(TEXT("Solo entry rejected by DungeonDefinition MinPartySize=2"),
		UProject_JMMOClientBridgeComponent::DispatchDungeonEntryForPlayer(PC1, EProject_JDungeonEntrySource::WorldPortal, Def->DungeonId, true));

	// Form 2-player party -> both automatically get tickets in shared instance!
	const FProject_JSocialOperationResult PartyRes = Social->CreateParty(PS1);
	TestTrue(TEXT("Party created"), PartyRes.bSucceeded);
	TestTrue(TEXT("Player 2 joined party"), Social->JoinParty(PS2, PartyRes.GroupId).bSucceeded);
	TestTrue(TEXT("Party dungeon dispatch succeeded for both members"),
		UProject_JMMOClientBridgeComponent::DispatchDungeonEntryForPlayer(PC1, EProject_JDungeonEntrySource::WorldPortal, Def->DungeonId, true));
	TestTrue(TEXT("Player 1 tracked in active dungeon instance"), Dungeon->HasActiveDungeonInstance(Char1));
	TestTrue(TEXT("Player 2 tracked in active dungeon instance"), Dungeon->HasActiveDungeonInstance(Char2));

	// The source tracks the allocation but only an admitted destination session can disconnect from it.
	TestFalse(TEXT("Leaving the source before dungeon admission cannot open reconnect grace"),
		Dungeon->RecordDisconnectInDungeon(Char1, UProject_JDungeonInstanceSubsystem::GetCurrentUtcEpochSeconds()));

	// 5. Generalized AProject_JGatheringNode personal-phase life skill (Fishing spot reuse).
	AProject_JGatheringNode* FishingSpot = Scope.Spawn<AProject_JGatheringNode>(FVector(0, 0, 0));
	FishingSpot->NodeId = TEXT("FishingSpot_River01");
	FishingSpot->YieldItemId = TEXT("Item_RiverSalmon");
	FishingSpot->ActivityType = EProject_JLifeSkillActivity::Fishing;
	FishingSpot->bUsePersonalPhaseLifeSkill = true;
	FishingSpot->ResourceNodeMode = EProject_JResourceNodeMode::PersonalPhase;
	FishingSpot->CooldownSeconds = 60.0;
	auto* Life = Scope.GI->GetSubsystem<UProject_JProgressionAndLifeSubsystem>();
	TestTrue(TEXT("Personal-phase fishing node admits player 1"),
		Life->TryGatherResourceNode(FGuid::NewGuid(), EProject_JResourceNodeMode::PersonalPhase, Char1, 1, 1, 60.0, 10.0));
	TestTrue(TEXT("Fishing spot activity enum persisted"), FishingSpot->ActivityType == EProject_JLifeSkillActivity::Fishing);

	// 6. Audio budget subsystem 48-voice & 25m cull sanity.
	auto* Audio = Scope.GI->GetSubsystem<UProject_JAudioBudgetSubsystem>();
	Audio->ResetAllVoices();
	TestFalse(TEXT("AudioBudget rejects OtherPlayerSFX beyond 25m"), Audio->RequestVoice(EProject_JAudioGroup::OtherPlayerSFX, 3000.0).bAdmitted);
	const auto BossSfx = Audio->RequestVoice(EProject_JAudioGroup::BossSFX, 1500.0);
	TestTrue(TEXT("AudioBudget admits BossSFX"), BossSfx.bAdmitted);
	Audio->ReleaseVoice(EProject_JAudioGroup::BossSFX);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJBugHuntRegressionsTest, "ProjectJ.Systems.Wiring.BugHuntEdgeCaseRegressions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJBugHuntRegressionsTest::RunTest(const FString&)
{
	ProjectJWiringTests::FWorldScope Scope;

	// Bug #1: BuildDungeonTravelURL carries TicketEndpoint so PreLogin SHA-1 signature matches.
	auto* Dungeon = Scope.GI->GetSubsystem<UProject_JDungeonInstanceSubsystem>();
	const FGuid CharA = FGuid::NewGuid();
	TMap<FGuid, FProject_JReturnAnchor> Anchors;
	Anchors.Add(CharA, FProject_JReturnAnchor{ TEXT("MainWorld"), 1, FVector::ZeroVector, FRotator::ZeroRotator });
	FGuid InstId;
	TArray<FProject_JDungeonJoinTicket> Tickets;
	TestTrue(TEXT("Dungeon ticket minted"), Dungeon->RequestDungeonEntry(
		EProject_JDungeonEntrySource::WorldPortal, TEXT("Dungeon_Crypt"), { CharA }, Anchors, 50.0, InstId, Tickets));
	const FString Url = UProject_JMMOClientBridgeComponent::BuildDungeonTravelURL(Tickets[0]);
	const int32 OptStart = Url.Find(TEXT("?"));
	TestTrue(TEXT("Travel URL contains ? options"), OptStart != INDEX_NONE);
	const FString Options = (OptStart != INDEX_NONE) ? Url.Mid(OptStart) : FString();
	const FString ParsedEndpoint = UGameplayStatics::ParseOption(Options, TEXT("TicketEndpoint"));
	TestEqual(TEXT("Travel URL carries exact TicketEndpoint for PreLogin verification"), ParsedEndpoint, Tickets[0].ServerEndpoint);

	// Full PreLogin parse -> VerifyAndConsumeJoinTicket round-trip proof.
	FProject_JDungeonJoinTicket ParsedTicket;
	FGuid::Parse(UGameplayStatics::ParseOption(Options, TEXT("TicketInstance")), ParsedTicket.InstanceId);
	ParsedTicket.DungeonId = FName(*UGameplayStatics::ParseOption(Options, TEXT("TicketDungeon")));
	FGuid::Parse(UGameplayStatics::ParseOption(Options, TEXT("TicketChar")), ParsedTicket.CharacterId);
	FGuid::Parse(UGameplayStatics::ParseOption(Options, TEXT("TicketNonce")), ParsedTicket.Nonce);
	ParsedTicket.ServerEndpoint = ParsedEndpoint;
	ParsedTicket.IssuedAtSeconds = FCString::Atod(*UGameplayStatics::ParseOption(Options, TEXT("TicketIssued")));
	ParsedTicket.ExpiresAtSeconds = FCString::Atod(*UGameplayStatics::ParseOption(Options, TEXT("TicketExp")));
	ParsedTicket.SignatureHex = UGameplayStatics::ParseOption(Options, TEXT("TicketSig"));
	TestTrue(TEXT("Parsed PreLogin ticket passes SHA-1 signature & single-use verification"),
		Dungeon->VerifyAndConsumeJoinTicket(ParsedTicket, 60.0));

	// Bug #3: ClaimMailAttachments pre-validates missing item BEFORE crediting gold (no partial-commit gold exploit).
	auto* Econ = Scope.GI->GetSubsystem<UProject_JEconomySubsystem>();
	const FGuid MissingItemGuid = FGuid::NewGuid();
	const FGuid BrokenMail = Econ->SendSystemMail(CharA, TEXT("MAIL_BROKEN_ITEM"), TEXT("GOLD"), 5000, MissingItemGuid);
	TestFalse(TEXT("Mail with unescrowed item rejected"), Econ->ClaimMailAttachments(BrokenMail, CharA, true));
	TestEqual(TEXT("Zero gold credited on rejected mail claim"), Econ->GetLedger().GetBalance(CharA, TEXT("GOLD")), 0ll);

	// Bug #3b: Negative auction deposit rejected & duplicate trade items rejected.
	FGuid OutListing;
	TestFalse(TEXT("Negative auction deposit fee rejected"),
		Econ->CreateAuctionListing(FGuid::NewGuid(), CharA, MissingItemGuid, 100, -50, 3600.0, 10.0, OutListing));
	const FGuid CharB = FGuid::NewGuid();
	const FGuid TradeId = Econ->StartDirectTrade(CharA, CharB);
	TestFalse(TEXT("Duplicate item ID in direct trade offer rejected"),
		Econ->UpdateTradeOffer(TradeId, CharA, 0, { MissingItemGuid, MissingItemGuid }));

	// Bug #7: NaN threat ignored, unsorted boss phase thresholds work, NaN speed rejected, chat survives clock reset.
	AProject_JNPCCharacter* Boss = Scope.Spawn<AProject_JNPCCharacter>();
	auto* Threat = Boss->GetThreatComponent();
	Threat->AddDamageThreat(Boss, CharA, std::numeric_limits<double>::quiet_NaN(), 1.0, 10.0);
	TestEqual(TEXT("NaN damage threat ignored"), Threat->GetActiveThreatCount(), 0);
	UProject_JNPCBrainDefinition* UnsortedBrain = NewObject<UProject_JNPCBrainDefinition>();
	UnsortedBrain->BossPhaseHealthThresholds = { 0.35f, 0.70f }; // intentionally unsorted
	Threat->BrainDefinition = UnsortedBrain;
	TestEqual(TEXT("Unsorted thresholds still resolve Phase 2 at 60% HP"), Threat->EvaluateBossPhase(600.f, 1000.f), 2);
	TestEqual(TEXT("Unsorted thresholds resolve Phase 3 at 30% HP"), Threat->EvaluateBossPhase(300.f, 1000.f), 3);

	auto* Integrity = Scope.GI->GetSubsystem<UProject_JServerIntegritySubsystem>();
	TestEqual(TEXT("NaN DeltaSeconds rejected with SnapBack"),
		Integrity->ValidateMovementStep(CharA, FVector::ZeroVector, FVector(5000, 0, 0), 600.f, std::numeric_limits<double>::quiet_NaN(), 10.0),
		EProject_JIntegrityAction::SnapBack);

	auto* SocialExt = Scope.GI->GetSubsystem<UProject_JGuildAndSocialExtensionsSubsystem>();
	TestTrue(TEXT("Chat accepted before map travel"), SocialExt->AdmitChatMessage(CharA, FGuid(), EProject_JChatChannel::World, TEXT("Hi"), 500.0));
	TestTrue(TEXT("Chat not locked out after map-travel clock reset (Now < LastTime)"),
		SocialExt->AdmitChatMessage(CharA, FGuid(), EProject_JChatChannel::World, TEXT("After travel"), 2.0));

	// Bug #8: WeeklyMonUtc00 stays identical across Dec 31 -> Sun Jan 3 and flips on Mon Jan 4.
	const FString ThuDec31 = UProject_JProgressionAndLifeSubsystem::ComputeUtcPeriodKey(FDateTime(2026, 12, 31, 23, 59, 0), EProject_JQuestResetPolicy::WeeklyMonUtc00);
	const FString SunJan03 = UProject_JProgressionAndLifeSubsystem::ComputeUtcPeriodKey(FDateTime(2027, 1, 3, 23, 59, 0), EProject_JQuestResetPolicy::WeeklyMonUtc00);
	const FString MonJan04 = UProject_JProgressionAndLifeSubsystem::ComputeUtcPeriodKey(FDateTime(2027, 1, 4, 0, 0, 0), EProject_JQuestResetPolicy::WeeklyMonUtc00);
	TestEqual(TEXT("Dec 31 Thu and Jan 3 Sun share the same Monday UTC week"), ThuDec31, SunJan03);
	TestNotEqual(TEXT("Monday Jan 4 00:00 UTC starts a new weekly period"), SunJan03, MonJan04);

	// Bug #5: AudioBudget & UIWindowStack cleanly reset on world teardown.
	auto* Audio = Scope.GI->GetSubsystem<UProject_JAudioBudgetSubsystem>();
	auto* UIStack = Scope.GI->GetSubsystem<UProject_JUIWindowStackSubsystem>();
	Audio->RequestVoice(EProject_JAudioGroup::BossSFX, 500.0);
	UIStack->PushModalWindow(TEXT("AuctionHouse"));
	TestEqual(TEXT("Audio voice active before cleanup"), Audio->GetActiveTotalVoices(), 1);
	TestTrue(TEXT("UI window open before cleanup"), UIStack->HasOpenModalWindow());
	FWorldDelegates::OnWorldCleanup.Broadcast(Scope.World, true, true);
	TestEqual(TEXT("Audio voice budget reset on world cleanup"), Audio->GetActiveTotalVoices(), 0);
	TestFalse(TEXT("UI modal stack cleared on world cleanup"), UIStack->HasOpenModalWindow());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCrossDomainPerfBugRegressionsTest, "ProjectJ.Systems.Wiring.CrossDomainPerfAndBugRegressions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCrossDomainPerfBugRegressionsTest::RunTest(const FString&)
{
	ProjectJWiringTests::FWorldScope Scope;

	// 1. MultiPartHitComponent: NaN QueryTimeSeconds with 1 history frame must NOT crash OOB; ring buffer wraps cleanly.
	AProject_JNPCCharacter* Boss = Scope.Spawn<AProject_JNPCCharacter>(FVector(300, 0, 0));
	auto* MultiPart = NewObject<UProject_JMultiPartHitComponent>(Boss);
	MultiPart->HitParts.Add({ TEXT("Head"), NAME_None, FVector(0, 0, 150), 90.f, 2.0f, true });
	Boss->AddInstanceComponent(MultiPart); MultiPart->RegisterComponent();
	MultiPart->RecordSample(1.0);
	FName PartName;
	float Mult = 0.f;
	FVector HitCenter;
	TestFalse(TEXT("MultiPart rejects NaN QueryTimeSeconds without OOB crash"),
		MultiPart->VerifyRewoundSegmentHit(std::numeric_limits<double>::quiet_NaN(), FVector(0, 0, 150), FVector(400, 0, 150), 25.f, PartName, Mult, HitCenter));
	TestFalse(TEXT("MultiPart rejects +Inf AttackRadius"),
		MultiPart->VerifyRewoundSegmentHit(1.0, FVector(0, 0, 150), FVector(400, 0, 150), std::numeric_limits<float>::infinity(), PartName, Mult, HitCenter));
	for (int32 i = 0; i < 30; ++i)
	{
		MultiPart->RecordSample(2.0 + i * 0.033);
	}
	TestTrue(TEXT("MultiPart ring buffer still hits after wrapping > MaxHistoryFrames"),
		MultiPart->VerifyRewoundSegmentHit(2.5, FVector(0, 0, 150), FVector(400, 0, 150), 25.f, PartName, Mult, HitCenter));

	// 2. CCResistanceComponent: NaN duration & stagger must not permanently poison DR bucket or Boss stagger.
	auto* CC = Boss->GetCCResistanceComponent();
	TestEqual(TEXT("CC rejects NaN duration"), CC->TryApplyCC(EProject_JCCCategory::Stun, std::numeric_limits<float>::quiet_NaN(), 10.0), 0.0f);
	TestTrue(TEXT("CC still applies full duration after rejected NaN"), FMath::IsNearlyEqual(CC->TryApplyCC(EProject_JCCCategory::Stun, 2.0f, 11.0), 2.0f));
	CC->bIsBossCCImmune = true;
	CC->MaxStagger = 100.f;
	CC->CurrentStagger = 100.f;
	TestFalse(TEXT("Boss stagger ignores NaN"), CC->ApplyStaggerDamage(std::numeric_limits<float>::quiet_NaN(), 12.0));
	TestTrue(TEXT("Boss can still be staggered cleanly after NaN"), CC->ApplyStaggerDamage(100.f, 13.0));

	// 3. NPCPoolSubsystem: duplicate ReleaseNPCToPool must be rejected to prevent double-acquire aliasing.
	auto* Pool = Scope.World->GetSubsystem<UProject_JNPCPoolSubsystem>();
	AProject_JNPCCharacter* Mob = Scope.Spawn<AProject_JNPCCharacter>(FVector(1000, 0, 0));
	TestTrue(TEXT("First pool release succeeds"), Pool->ReleaseNPCToPool(Mob));
	TestFalse(TEXT("Duplicate pool release rejected"), Pool->ReleaseNPCToPool(Mob));
	TestEqual(TEXT("Pooled count stays 1 (no aliasing)"), Pool->GetPooledCount(AProject_JNPCCharacter::StaticClass()), 1);

	// 4. ProjectileManagerSubsystem: NaN multi-hit cooldown rejected, NaN gravity rejected.
	auto* ProjSub = Scope.World->GetSubsystem<UProject_JProjectileManagerSubsystem>();
	TestEqual(TEXT("SpawnProjectile rejects NaN gravity"),
		ProjSub->SpawnProjectile(Boss, FVector::ZeroVector, FVector(1000, 0, 0), 20.f, 2.f, 0, std::numeric_limits<float>::quiet_NaN(), {}), 0ull);
	TestFalse(TEXT("AdmitMultiHitTarget rejects NaN cooldown"),
		ProjSub->AdmitMultiHitTarget(77, Mob, 10.0, std::numeric_limits<float>::quiet_NaN()));

	// 5. GuildBank: AuditLogs capped at 500 & failed Initiate withdraw does not pollute counts.
	auto* GuildExt = Scope.GI->GetSubsystem<UProject_JGuildAndSocialExtensionsSubsystem>();
	const FGuid GuildId = FGuid::NewGuid();
	const FGuid InitiateId = FGuid::NewGuid();
	GuildExt->SetGuildMemberRank(GuildId, InitiateId, EProject_JGuildRank::Initiate);
	FGuid OutItem;
	TestFalse(TEXT("Initiate cannot withdraw"), GuildExt->WithdrawFromGuildBank(GuildId, InitiateId, 0, 1, 20261123, OutItem));
	for (int32 i = 0; i < 600; ++i)
	{
		GuildExt->DepositToGuildBank(GuildId, InitiateId, i % 256, FGuid::NewGuid(), TEXT("Item_Ore"));
	}
	TestTrue(TEXT("Guild audit logs stay bounded <= 500"), GuildExt->GetGuildBankAuditLogCount(GuildId) <= 500);

	// 6. Dungeon UTC epoch seconds & nonce pruning.
	const double UtcNow = UProject_JDungeonInstanceSubsystem::GetCurrentUtcEpochSeconds();
	TestTrue(TEXT("UTC epoch seconds is valid (> 1.7e9)"), UtcNow > 1700000000.0);

	return true;
}
#endif
