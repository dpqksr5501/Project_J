#include "Testing/Project_JLoopbackCohort.h"
#include "Components/Project_JCombatHitValidationComponent.h"

#include "AbilitySystemComponent.h"
#include "Algo/AllOf.h"
#include "Algo/AnyOf.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/PendingNetGame.h"
#include "Engine/World.h"
#include "GameFramework/GameSession.h"
#include "GameplayEffect.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "Misc/StringBuilder.h"
#include "Network/Project_JIpNetDriver.h"
#include "Project_JAttributeSet.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Game/Project_JPlayerState.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJLoopbackCohort, Log, All);

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
static TAutoConsoleVariable<int32> GCohortResult(TEXT("ProjectJ.Cohort.Result"), 0,
	TEXT("Local PIE test result. Zero while running, one for success and minus one for failure."));

namespace
{
constexpr int32 CombatLanes = 5;
constexpr int32 SustainedRounds = 40;
constexpr double LaneInterval = .1;
constexpr double IdleSeconds = 10.;
constexpr double BurstSeconds = 5.;
constexpr double DrainSeconds = 5.;
constexpr double BossClientLaunchInterval = .25;
constexpr double BossCohortTimeout = 360.;
constexpr double CloseTimeout = 25.;
// Keep the final witness outside the native pawn's default health of 100.
constexpr float FinalBossHealth = 731.f;
bool bCohortGeneratorStarted = false;

constexpr int32 MeasuredRounds(bool bAuthoredBoss)
{
	return bAuthoredBoss ? 0 : SustainedRounds;
}
}
#endif

AProject_JLoopbackCohort::AProject_JLoopbackCohort()
{
	PlayerControllerClass = AProject_JCohortController::StaticClass();
	DefaultPawnClass = nullptr;
}

AProject_JCohortTarget::AProject_JCohortTarget()
{
	bAlwaysRelevant = true;
}

void AProject_JLoopbackCohort::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	bBossCombat = FParse::Param(FCommandLine::Get(), TEXT("ProjectJCohortBossCombat"))
		|| FParse::Param(FCommandLine::Get(), TEXT("ProjectJCohortBossClient"));
	bOwnerOnlyPeers = FParse::Param(FCommandLine::Get(), TEXT("ProjectJCohortOwnerOnlyPeers"));
	if (bBossCombat) PlayerStateClass = AProject_JPlayerState::StaticClass();
#endif
	Super::InitGame(MapName, Options, ErrorMessage);
}

void AProject_JLoopbackCohort::BeginPlay()
{
	Super::BeginPlay();
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	const TCHAR* CommandLine = FCommandLine::Get();
	bFixtureEnabled = FParse::Value(CommandLine, TEXT("ProjectJCohort="), Expected) && Expected > 0 && Expected <= 200;
	if (!bFixtureEnabled) return;
	FParse::Value(CommandLine, TEXT("ProjectJCohortMemoryReserveMiB="), MemoryReserveMiB);
	if (MemoryReserveMiB < 0 || MemoryReserveMiB > 65536) { FinishCohort(false); return; }
	if (GetNetMode() == NM_DedicatedServer)
	{
		if (!GameSession) { FinishCohort(false); return; }
		const auto* Driver = Cast<UProject_JIpNetDriver>(GetWorld()->GetNetDriver());
		if (!Driver) { FinishCohort(false); return; }
		const int32 Capacity = Driver->GetIncomingCapacity();
		if (Expected > Capacity)
		{
			UE_LOG(LogProjectJLoopbackCohort, Display, TEXT("COHORT_CAPACITY_REJECTED requested=%d capacity=%d"), Expected, Capacity);
			FinishCohort(false);
			return;
		}
		FParse::Value(CommandLine, TEXT("ProjectJCohortCombatMetrics="), MetricsPath);
		GameSession->MaxPlayers = Expected;
		if (bBossCombat)
		{
			if (MetricsPath.IsEmpty() || !PrepareBossCombat())
			{
				UE_LOG(LogProjectJLoopbackCohort, Error, TEXT("Could not prepare the authored boss cohort"));
				FinishCohort(false);
				return;
			}
		}
		else
		{
			auto* TargetClass = MetricsPath.IsEmpty() ? AProject_JNPCCharacter::StaticClass() : AProject_JCohortTarget::StaticClass();
			Boss = GetWorld()->SpawnActor<AProject_JNPCCharacter>(TargetClass, FVector(0., 0., 1000.), FRotator::ZeroRotator);
			if (!Boss || !Boss->GetAbilitySystemComponent()) { FinishCohort(false); return; }
			Boss->SetActorEnableCollision(false);
			Damage = NewObject<UGameplayEffect>(this);
			Damage->DurationPolicy = EGameplayEffectDurationType::Instant;
			auto& Modifier = Damage->Modifiers.AddDefaulted_GetRef();
			Modifier.Attribute = UProject_JAttributeSet::GetHealthAttribute();
			Modifier.ModifierOp = EGameplayModOp::Additive;
			Modifier.ModifierMagnitude = FScalableFloat(-1.f);
		}
		if (!MetricsPath.IsEmpty())
		{
			int32 RequestedBatch = Expected;
			FParse::Value(CommandLine, TEXT("ProjectJCohortCloseBatchSize="), RequestedBatch);
			CloseBatchSize = FMath::Clamp(RequestedBatch, 1, Expected);
			TickSamples.Reserve(2000);
			StrikeLatenciesMs.Reserve(Expected * (1 + MeasuredRounds(bBossCombat)));
			TickStartHandle = FWorldDelegates::OnWorldTickStart.AddUObject(this, &ThisClass::BeginMeasuredTick);
			TickEndHandle = FWorldDelegates::OnWorldTickEnd.AddUObject(this, &ThisClass::EndMeasuredTick);
		}
		UE_LOG(LogProjectJLoopbackCohort, Display, TEXT("COHORT_READY count=%d capacity=%d"), Expected, Capacity);
	}
	else if (!FParse::Value(CommandLine, TEXT("ProjectJCohortEndpoint="), Endpoint) || !Endpoint.StartsWith(TEXT("127.0.0.1:")))
	{
		FinishCohort(false);
		return;
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		// A failed child can browse to the default map and construct another fixture GameMode.
		if (bCohortGeneratorStarted) return;
		bCohortGeneratorStarted = true;
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddWeakLambda(this,
			[this](UWorld* World, UNetDriver* Driver, ENetworkFailure::Type, const FString& Error)
			{
				if (!Algo::AnyOf(Clients, [World, Driver](const auto& Client)
				{
					const auto* Context = Client->GetWorldContext();
					return (World && Client->GetWorld() == World)
						|| (Driver && Context && Context->PendingNetGame && Context->PendingNetGame->NetDriver == Driver);
				})) return;
				UE_LOG(LogProjectJLoopbackCohort, Error, TEXT("Cohort connection failed: %s"), *Error);
				bChildNetworkFailed = true;
			});
	}
	bMeasuredClient = FParse::Param(CommandLine, TEXT("ProjectJCohortMeasuredClient"));
	Started = FPlatformTime::Seconds();
	Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::AdvanceCohort), MetricsPath.IsEmpty() ? .1f : 0.f);
#endif
}

void AProject_JLoopbackCohort::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& Error)
{
	Super::PreLogin(Options, Address, UniqueId, Error);
	if (!bFixtureEnabled || (Address != TEXT("127.0.0.1") && !Address.StartsWith(TEXT("127.0.0.1:")))) Error = TEXT("LoopbackCohortOnly");
}

void AProject_JLoopbackCohort::Logout(AController* Exiting)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	if (auto* Player = Cast<AProject_JCohortController>(Exiting))
	{
		if (auto* Participant = Participants.Find(Player))
		{
			if (CombatPhase == ECombatPhase::Close && Participant->bReleaseSent && !Participant->bLoggedOut)
			{
				Participant->bLoggedOut = true;
				++ClosedClients;
			}
			else ++StrikeViolations;
		}
		else if (bBossCombat && CombatPhase == ECombatPhase::Waiting)
		{
			++StrikeViolations;
		}
	}
#endif
	Super::Logout(Exiting);
}

bool AProject_JLoopbackCohort::AdvanceCohort(float)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	const double Now = FPlatformTime::Seconds();
	if (MemoryReserveMiB > 0 && Now >= NextMemoryCheck)
	{
		NextMemoryCheck = Now + 1.;
		const FPlatformMemoryStats Memory = FPlatformMemory::GetStats();
		MinFreePhysical = FMath::Min(MinFreePhysical, Memory.AvailablePhysical);
		MinFreeCommit = FMath::Min(MinFreeCommit, Memory.AvailableVirtual);
		if (FMath::Min(MinFreePhysical, MinFreeCommit) < uint64(MemoryReserveMiB) * 1024 * 1024)
		{
			UE_LOG(LogProjectJLoopbackCohort, Error, TEXT("COHORT_MEMORY_STOP launched=%d peak=%d"), LaunchedClients, Peak);
			FinishCohort(false);
			return false;
		}
	}
	if (bChildNetworkFailed || Now - Started > (bBossCombat ? BossCohortTimeout : 120.))
	{
		if (!bChildNetworkFailed)
			UE_LOG(LogProjectJLoopbackCohort, Error, TEXT("Cohort exceeded its overall deadline (phase=%d, peak=%d, launched=%d)."),
				static_cast<int32>(CombatPhase), Peak, LaunchedClients);
		FinishCohort(false);
		return false;
	}
	if (GetNetMode() == NM_DedicatedServer)
	{
		const auto* Driver = GetWorld()->GetNetDriver();
		if (!Driver) return true;
		int32 Ready = 0;
		for (const auto& Connection : Driver->ClientConnections)
			if (Connection && Cast<AProject_JCohortController>(Connection->PlayerController)) ++Ready;
		Peak = FMath::Max(Peak, Ready);
		if (!MetricsPath.IsEmpty()) return AdvanceMeasuredCombat(*Driver, Ready, Now);
		if (Ready == Expected && !bSentStrike)
		{
			bSentStrike = true;
			auto* ASC = Boss->GetAbilitySystemComponent();
			ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(), static_cast<float>(Expected));
			ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), static_cast<float>(Expected));
			for (const auto& Connection : Driver->ClientConnections)
				CastChecked<AProject_JCohortController>(Connection->PlayerController)->ClientBeginCohortStrike();
		}
		if (bSentStrike && Strikes.Num() == Expected)
		{
			const float Health = Boss->GetAbilitySystemComponent()->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute());
			if (Ready != Expected || Health != 0.f) { FinishCohort(false); return false; }
			for (const auto& Connection : Driver->ClientConnections)
				CastChecked<AProject_JCohortController>(Connection->PlayerController)->ClientConfirmCohortFinished();
			FTimerHandle Delay;
			GetWorldTimerManager().SetTimer(Delay, FTimerDelegate::CreateUObject(this, &ThisClass::FinishCohort, true), 3.f, false);
			return false;
		}
	}
	else
	{
		const uint64 AvailableMemory = FPlatformMemory::GetStats().AvailablePhysical;
		if (AvailableMemory < 1536ull * 1024 * 1024)
		{
			UE_LOG(LogProjectJLoopbackCohort, Error, TEXT("The client host reached its minimum available memory reserve (launched=%d expected=%d available_mib=%llu)."),
				LaunchedClients, Expected, AvailableMemory / (1024 * 1024));
			FinishCohort(false);
			return false;
		}
		// Spread full map and avatar initialization across ticks on the local generator.
		const int32 LaunchBudget = bBossCombat ? (Now >= NextBossClientLaunch ? 1 : 0) : 4;
		if (bBossCombat && LaunchBudget) NextBossClientLaunch = Now + BossClientLaunchInterval;
		for (int32 Added = 0; Added < LaunchBudget && LaunchedClients < Expected; ++Added)
		{
			UWorld* PreviousWorld = GWorld;
			const int32 PreviousPIEInstance = UE::GetPlayInEditorID();
			ON_SCOPE_EXIT { GWorld = PreviousWorld; UE::SetPlayInEditorID(PreviousPIEInstance); };
			auto* Client = NewObject<UGameInstance>(GEngine);
			Clients.Add(Client);
			const int32 PIEInstance = 2000 + ++LaunchedClients;
			FGameInstancePIEParameters Parameters;
			Parameters.NetMode = EPlayNetMode::PIE_Client;
			const FGameInstancePIEResult Initialized = Client->InitializeForPlayInEditor(PIEInstance, Parameters);
			if (!Initialized.IsSuccess())
			{
				FinishCohort(false);
				return false;
			}
			auto* Context = Client->GetWorldContext();
			auto* LocalPlayer = NewObject<ULocalPlayer>(GEngine, GEngine->LocalPlayerClass);
			LocalPlayer->SetConnectionIdentifier(PIEInstance);
			FString Error;
			if (Client->AddLocalPlayer(LocalPlayer, FPlatformUserId::CreateFromInternalId(0)) == INDEX_NONE
				|| GEngine->Browse(*Context, FURL(nullptr, *Endpoint, TRAVEL_Absolute), Error) != EBrowseReturnVal::Pending)
			{
				UE_LOG(LogProjectJLoopbackCohort, Error, TEXT("Cohort client launch failed: %s"), *Error);
				FinishCohort(false);
				return false;
			}
		}
		if (bMeasuredClient)
		{
			TArray<TObjectPtr<UGameInstance>, TInlineAllocator<16>> Retiring;
			for (int32 Index = Clients.Num() - 1; Index >= 0; --Index)
			{
				auto* Player = Cast<AProject_JCohortController>(Clients[Index]->GetFirstLocalPlayerController());
				if (!Player || !Player->PollMeasuredRelease()) continue;
				Retiring.Add(Clients[Index]);
				Clients.RemoveAtSwap(Index, EAllowShrinking::No);
			}
			RetireClientContexts(Retiring);
			ClosedClients += Retiring.Num();
			if (ClosedClients == Expected) { FinishCohort(true); return false; }
		}
		else if (Clients.Num() == Expected && Algo::AllOf(Clients, [](const auto& Client)
			{
				const auto* Player = Cast<AProject_JCohortController>(Client->GetFirstLocalPlayerController());
				return Player && Player->HasCohortCompleted();
			}))
		{
			FinishCohort(true);
			return false;
		}
	}
#endif
	return true;
}

void AProject_JLoopbackCohort::ApplyCohortStrike(AProject_JCohortController* Player)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	if (!bFixtureEnabled || !bSentStrike || !Player || !Player->GetNetConnection() || Strikes.Contains(Player)) return;
	Strikes.Add(Player);
	auto* ASC = Boss->GetAbilitySystemComponent();
	ASC->ApplyGameplayEffectToSelf(Damage, 1.f, ASC->MakeEffectContext());
#endif
}

void AProject_JLoopbackCohort::ApplyMeasuredStrike(AProject_JCohortController* Player, int32 Sequence)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	if (MetricsPath.IsEmpty() || bBossCombat) return;
	auto* Participant = Participants.Find(Player);
	if (!Participant || !Player->GetNetConnection() || Sequence != Participant->Received + 1 || !Participant->SentAt.IsValidIndex(Sequence - 1))
	{
		++StrikeViolations;
		return;
	}
	StrikeLatenciesMs.Add((FPlatformTime::Seconds() - Participant->SentAt[Sequence - 1]) * 1000.);
	++Participant->Received;
	auto* ASC = Boss->GetAbilitySystemComponent();
	const auto Health = UProject_JAttributeSet::GetHealthAttribute();
	const float Before = ASC->GetNumericAttribute(Health);
	ASC->ApplyGameplayEffectToSelf(Damage, 1.f, ASC->MakeEffectContext());
	StrikeViolations += ASC->GetNumericAttribute(Health) != Before - 1.f;
	++MeasuredStrikes;
#endif
}

void AProject_JLoopbackCohort::AcknowledgeMeasuredHealth(AProject_JCohortController* Player,
	TConstArrayView<FProject_JCohortHealthObservation> Observations)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	auto* Participant = Participants.Find(Player);
	if (CombatPhase != ECombatPhase::Verify || !Participant || !Player->GetNetConnection() || Participant->bAcknowledgedHealth)
	{
		++StrikeViolations;
		return;
	}
	if (BossStrikeTimings.IsValidIndex(Participant->BossStrikeIndex) && Observations.Num() <= 512)
	{
		auto& Timing = BossStrikeTimings[Participant->BossStrikeIndex];
		const double Now = FPlatformTime::Seconds();
		for (const auto& Observation : Observations)
		{
			// All participating processes share the host's QPC clock. A coalesced
			// health update may cross several thresholds in the same replication.
			if (Observation.Health <= Timing.ConfirmedHealth && Observation.ObservedAt >= Timing.ConfirmedAt
				&& Observation.ObservedAt <= Now)
			{
				Timing.ReplicationMs = (Observation.ObservedAt - Timing.ConfirmedAt) * 1000.;
				break;
			}
		}
	}
	Participant->bAcknowledgedHealth = true;
	++ConfirmedHealth;
#endif
}

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
bool AProject_JLoopbackCohort::AdvanceMeasuredCombat(const UNetDriver& Driver, int32 Ready, double Now)
{
	const auto SetPhase = [this, Now](ECombatPhase Phase)
	{
		CombatPhase = Phase;
		PhaseStarted = Now;
	};
	const auto Send = [Now](AProject_JCohortController& Player, FParticipant& Participant)
	{
		if (APawn* Pawn = Player.GetPawn())
			if (auto* Validation = Pawn->FindComponentByClass<UProject_JCombatHitValidationComponent>())
				Validation->ArmHitWindowTelemetry();
		Player.ClientBeginMeasuredStrike(Participant.SentAt.Add(Now) + 1);
	};
	const auto Fail = [this, Ready](const TCHAR* Reason)
	{
		UE_LOG(LogProjectJLoopbackCohort, Error, TEXT("Measured cohort %s (phase=%d, ready=%d, strikes=%d, violations=%d, batch=%d)"),
			Reason, static_cast<int32>(CombatPhase), Ready, MeasuredStrikes, StrikeViolations, NextBatch);
		FinishCohort(false);
		return false;
	};
	const int32 Rounds = MeasuredRounds(bBossCombat);
	const int32 TotalStrikes = Expected * (1 + Rounds);
	if (StrikeViolations) return Fail(TEXT("observed an invalid reply or disconnect"));
	if (CombatPhase == ECombatPhase::Waiting)
	{
		if (bBossCombat)
		{
			int32 SpawnedThisTick = 0;
			for (const auto& Connection : Driver.ClientConnections)
			{
				auto* Player = Connection ? Cast<AProject_JCohortController>(Connection->PlayerController) : nullptr;
				if (!Player || Player->GetPawn()) continue;
				if (!SpawnBossParticipant(*Player, BossPawns)) return Fail(TEXT("could not possess and equip an authored participant"));
				++BossPawns;
				if (++SpawnedThisTick == 4) break;
			}
			if (BossPawns != Expected) return true;
		}
		if (Ready != Expected) return true;
		for (const auto& Connection : Driver.ClientConnections)
		{
			if (auto* Player = Connection ? Cast<AProject_JCohortController>(Connection->PlayerController) : nullptr)
			{
				const int32 Slot = Participants.Num();
				auto& Participant = Participants.Add(Player);
				Participant.Lane = Slot % CombatLanes;
				Participant.SentAt.Reserve(1 + Rounds);
			}
		}
		ServerTickRate = Driver.GetNetServerMaxTickRate();
		auto* ASC = Boss->GetAbilitySystemComponent();
		const float StartingHealth = bBossCombat ? BossStartingHealth : FinalBossHealth + TotalStrikes;
		ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(), StartingHealth);
		ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), StartingHealth);
		SetPhase(ECombatPhase::Idle);
		return true;
	}
	if (CombatPhase != ECombatPhase::Close && Ready != Expected)
		return Fail(TEXT("lost a participant"));
	const double Elapsed = Now - PhaseStarted;
	if (CombatPhase == ECombatPhase::Idle && bBossCombat && BossReadyPlayers != Expected)
	{
		if (Elapsed > 45.) return Fail(TEXT("timed out preparing client-owned avatars"));
		if (Now >= NextBossPrepare)
		{
			// Generic can initially decode a reliable RPC's actor parameter as null while its actor channel opens.
			for (const auto& [Player, Participant] : Participants)
				if (!Participant.bBossReady && Player.IsValid()) Player->ClientPrepareBossCombat(Boss);
			NextBossPrepare = Now + 1.;
		}
		return true;
	}
	if (CombatPhase == ECombatPhase::Idle && Elapsed >= IdleSeconds)
	{
		if (bBossCombat)
		{
			const int32 Aimed = CountBossCombatAimedParticipants();
			if (Aimed != Expected)
			{
				if (Elapsed <= 45.) return true;
				UE_LOG(LogProjectJLoopbackCohort, Warning, TEXT("COHORT_BOSS_AIM_TIMEOUT accepted=%d expected=%d"), Aimed, Expected);
				return Fail(TEXT("timed out waiting for authoritative Mover aim"));
			}
			UE_LOG(LogProjectJLoopbackCohort, Display, TEXT("COHORT_BOSS_AIM accepted=%d expected=%d"), Aimed, Expected);
		}
		if (bBossCombat && bOwnerOnlyPeers && !CheckBossMulticastAudience(Driver))
			return Fail(TEXT("owner-only participants would replicate combat multicasts to other clients"));
		SetPhase(ECombatPhase::Burst);
		for (auto& [Player, Participant] : Participants)
		{
			if (!Player.IsValid()) return Fail(TEXT("lost a participant before the burst"));
			Send(*Player.Get(), Participant);
		}
	}
	else if (CombatPhase == ECombatPhase::Burst && Elapsed >= BurstSeconds)
	{
		if (MeasuredStrikes != Expected) return Fail(TEXT("burst did not finish in its measured window"));
		// Authored attacks move the pawns. Measure one burst without teleporting them back.
		SetPhase(bBossCombat ? ECombatPhase::Drain : ECombatPhase::Sustained);
	}
	else if (CombatPhase == ECombatPhase::Sustained)
	{
		const int32 TotalBatches = CombatLanes * Rounds;
		while (NextBatch < TotalBatches)
		{
			const double Due = NextBatch * LaneInterval;
			if (Elapsed < Due) break;
			for (auto& [Player, Participant] : Participants)
			{
				if (Participant.Lane != NextBatch % CombatLanes) continue;
				if (!Player.IsValid()) return Fail(TEXT("lost a participant during sustained combat"));
				Send(*Player.Get(), Participant);
			}
			++NextBatch;
		}
		if (Elapsed >= TotalBatches * LaneInterval) SetPhase(ECombatPhase::Drain);
	}
	else if (CombatPhase == ECombatPhase::Drain && Elapsed >= DrainSeconds)
	{
		const float Health = Boss->GetAbilitySystemComponent()->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute());
		const bool bVerifiedHealth = bBossCombat
			? ConfirmedBossDamage > 0.f && FMath::IsNearlyEqual(Health, BossStartingHealth - ConfirmedBossDamage, .01f)
			  && Boss->GetAttributeSet()->GetHealthGEObservationFailures() == BossObservationFailures
			: Health == FinalBossHealth;
		if (MeasuredStrikes != TotalStrikes || !bVerifiedHealth || !Algo::AllOf(Participants,
			[Rounds](const auto& Entry) { return Entry.Value.Received == 1 + Rounds; }))
			return Fail(TEXT("did not settle all strikes and boss health"));
		SetPhase(ECombatPhase::Verify);
		for (const auto& [Player, Participant] : Participants)
		{
			if (!Player.IsValid()) return Fail(TEXT("lost a participant before health verification"));
			Player->ClientConfirmMeasuredHealth(Boss, Health);
		}
	}
	else if (CombatPhase == ECombatPhase::Verify)
	{
		if (ConfirmedHealth == Expected && Elapsed >= .25) SetPhase(ECombatPhase::Close);
		else if (Elapsed > 10.) return Fail(TEXT("timed out waiting for replicated health receipts"));
	}
	else if (CombatPhase == ECombatPhase::Close)
	{
		const double Deadline = bBossCombat
			? FMath::Max(CloseTimeout, 5. + 2. * FMath::DivideAndRoundUp(Expected, CloseBatchSize))
			: CloseTimeout;
		if (Elapsed > Deadline) return Fail(TEXT("timed out waiting for native logout"));
		if (ClosedClients == Expected && Driver.ClientConnections.IsEmpty())
		{
			FinishCohort(true);
			return false;
		}
		if (ClosedClients == ReleasesSent && Ready == Expected - ClosedClients)
		{
			int32 Batch = 0;
			for (auto& [Player, Participant] : Participants)
			{
				if (Participant.bReleaseSent) continue;
				if (!Player.IsValid() || !Participant.bAcknowledgedHealth) return Fail(TEXT("participant changed before release"));
				Participant.bReleaseSent = true;
				++ReleasesSent;
				Player->ClientReleaseMeasuredConnection();
				if (++Batch == CloseBatchSize) break;
			}
		}
	}
	return true;
}

void AProject_JLoopbackCohort::BeginMeasuredTick(UWorld* World, ELevelTick, float)
{
	if (World != GetWorld() || CombatPhase == ECombatPhase::Waiting) return;
	const auto* Driver = World->GetNetDriver();
	if (!Driver) return;
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_CohortMeasuredTickStart);
	const double Now = FPlatformTime::Seconds();
	const double IdleSecondsThisFrame = FApp::GetIdleTime();
	CurrentTick = {CombatPhase, Now, PreviousTickStart > 0. ? (Now - PreviousTickStart) * 1000. : 0., 0.,
		IdleSecondsThisFrame * 1000., IdleSecondsThisFrame > 0. ? FApp::GetIdleTimeOvershoot() * 1000. : 0.,
		Driver->InTotalBytes, Driver->OutTotalBytes, MeasuredStrikes, ClosedClients};
	PreviousTickStart = Now;
}

void AProject_JLoopbackCohort::EndMeasuredTick(UWorld* World, ELevelTick, float)
{
	if (World != GetWorld() || CurrentTick.Started == 0.) return;
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_CohortMeasuredTickEnd);
	const double End = FPlatformTime::Seconds();
	if (bBossHitDiagnostics && CombatPhase == ECombatPhase::Burst && IsValid(Boss))
	{
		const auto* Capsule = Boss->GetCapsuleComponent();
		UE_LOG(LogProjectJLoopbackCohort, Display, TEXT("COHORT_TARGET frame=%llu actor=%s center=%s radius=%.3f half_height=%.3f query=%d object_type=%d actor_collision=%d speed=%.3f"),
			GFrameCounter, *GetNameSafe(Boss), *Capsule->GetComponentLocation().ToCompactString(),
			Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight(), Capsule->IsQueryCollisionEnabled(),
			static_cast<int32>(Capsule->GetCollisionObjectType()), Boss->GetActorEnableCollision(), Boss->GetVelocity().Size());
	}
	if (const auto* Driver = World->GetNetDriver())
	{
		CurrentTick.WorkMs = (End - CurrentTick.Started) * 1000.;
		CurrentTick.InBytes = Driver->InTotalBytes - CurrentTick.InBytes;
		CurrentTick.OutBytes = Driver->OutTotalBytes - CurrentTick.OutBytes;
		CurrentTick.Strikes = MeasuredStrikes - CurrentTick.Strikes;
		CurrentTick.Closed = ClosedClients - CurrentTick.Closed;
		TickSamples.Add(CurrentTick);
	}
	CurrentTick.Started = 0.;
}

bool AProject_JLoopbackCohort::WriteCombatMetrics() const
{
	static const TCHAR* const PhaseNames[] = { TEXT("Waiting"), TEXT("Idle"), TEXT("Burst"), TEXT("Sustained"), TEXT("Drain"), TEXT("Verify"), TEXT("Close") };
	TStringBuilder<4096> Ticks;
	Ticks.Append(TEXT("phase,seconds,tick_interval_ms,world_tick_ms,engine_idle_ms,engine_idle_overshoot_ms,in_bytes,out_bytes,strikes,closed\n"));
	for (const auto& Sample : TickSamples)
		Ticks.Appendf(TEXT("%s,%.6f,%.6f,%.6f,%.6f,%.6f,%u,%u,%d,%d\n"), PhaseNames[static_cast<int32>(Sample.Phase)], Sample.Started - Started,
			Sample.IntervalMs, Sample.WorkMs, Sample.EngineIdleMs, Sample.EngineIdleOvershootMs,
			Sample.InBytes, Sample.OutBytes, Sample.Strikes, Sample.Closed);
	TStringBuilder<4096> StrikesCsv;
	StrikesCsv.Append(TEXT("phase,round_trip_ms,attack_to_hit_window_ms,server_authority_hit_ms,damage_replication_ms\n"));
	for (int32 Index = 0; Index < StrikeLatenciesMs.Num(); ++Index)
	{
		StrikesCsv.Appendf(TEXT("%s,%.6f"), Index < Expected ? TEXT("Burst") : TEXT("Sustained"), StrikeLatenciesMs[Index]);
		if (BossStrikeTimings.IsValidIndex(Index))
		{
			const auto& Timing = BossStrikeTimings[Index];
			StrikesCsv.Appendf(TEXT(",%.6f,%.6f,%.6f\n"), Timing.HitWindowMs, Timing.AuthorityMs, Timing.ReplicationMs);
		}
		else StrikesCsv.Append(TEXT(",,,\n"));
	}
	const bool bTicksWritten = FFileHelper::SaveStringToFile(Ticks.ToView(), *(MetricsPath / TEXT("combat-ticks.csv")));
	const bool bStrikesWritten = FFileHelper::SaveStringToFile(StrikesCsv.ToView(), *(MetricsPath / TEXT("combat-strikes.csv")));
	const auto* ASC = Boss ? Boss->GetAbilitySystemComponent() : nullptr;
	UE_LOG(LogProjectJLoopbackCohort, Display, TEXT("COHORT_MEASURED accepted=%d expected=%d violations=%d net_tick_rate=%d samples=%d boss_health=%.0f"),
		MeasuredStrikes, Expected * (1 + MeasuredRounds(bBossCombat)), StrikeViolations, ServerTickRate, TickSamples.Num(),
		ASC ? ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()) : -1.f);
	UE_LOG(LogProjectJLoopbackCohort, Display, TEXT("COHORT_CLOSED confirmed=%d released=%d closed=%d batch=%d"),
		ConfirmedHealth, ReleasesSent, ClosedClients, CloseBatchSize);
	if (bBossCombat)
		UE_LOG(LogProjectJLoopbackCohort, Display, TEXT("COHORT_BOSS pawns=%d ready=%d owner_only_peers=%d confirmed_damage=%.3f"),
			BossPawns, BossReadyPlayers, bOwnerOnlyPeers, ConfirmedBossDamage);
	return bTicksWritten && bStrikesWritten && !TickSamples.IsEmpty();
}

void AProject_JLoopbackCohort::RetireClientContexts(TConstArrayView<TObjectPtr<UGameInstance>> Retiring)
{
	if (Retiring.IsEmpty()) return;
	// The custom child contexts have no PIE windows. Retire them before the editor
	// closes its normal PIE session and starts the teardown for registered windows.
	UWorld* PreviousWorld = GWorld;
	const int32 PreviousPIEInstance = UE::GetPlayInEditorID();
	FWorldContext* PrimaryContext = GEngine->GetWorldContextFromWorld(GetWorld());
	TArray<TObjectPtr<UWorld>*> SharedReferences;
	for (const auto& Client : Retiring)
	{
		UWorld* ClientWorld = Client->GetWorld();
		if (!ClientWorld) continue;
		const auto* ClientContext = Client->GetWorldContext();
		if (!ClientContext) continue;
		if (PrimaryContext)
			for (auto* Reference : ClientContext->ExternalReferences)
				if (Reference && PrimaryContext->ExternalReferences.Contains(Reference)) SharedReferences.AddUnique(Reference);
		GWorld = ClientWorld;
		UE::SetPlayInEditorID(ClientContext->PIEInstance);
		GEngine->CancelPending(ClientWorld);
		GEngine->ShutdownWorldNetDriver(ClientWorld);
		ClientWorld->EndPlay(EEndPlayReason::EndPlayInEditor);
		Client->Shutdown();
		ClientWorld->DestroyWorld(false);
		GEngine->DestroyWorldContext(ClientWorld);
	}
	// InitializeForPlayInEditor shares the editor play-world pointer across contexts.
	// Restore that registered reference to the primary session before EndPlayMap.
	if (PrimaryContext)
		for (auto* Reference : SharedReferences) PrimaryContext->AddRef(*Reference);
	GWorld = PreviousWorld;
	UE::SetPlayInEditorID(PreviousPIEInstance);
}
#endif

void AProject_JLoopbackCohort::FinishCohort(bool bSuccess)
{
	int32 Confirmed = Strikes.Num();
	int32 ReportedClients = Clients.Num();
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	if (MemoryReserveMiB > 0 && MinFreePhysical != MAX_uint64)
	{
		UE_LOG(LogProjectJLoopbackCohort, Display, TEXT("COHORT_MEMORY reserve_mib=%d min_physical_mib=%llu min_commit_mib=%llu"),
			MemoryReserveMiB, MinFreePhysical / (1024 * 1024), MinFreeCommit / (1024 * 1024));
	}
	if (!MetricsPath.IsEmpty())
	{
		FWorldDelegates::OnWorldTickStart.Remove(TickStartHandle);
		FWorldDelegates::OnWorldTickEnd.Remove(TickEndHandle);
		bSuccess = WriteCombatMetrics() && bSuccess && StrikeViolations == 0;
		Confirmed = MeasuredStrikes;
	}
	if (bMeasuredClient) ReportedClients = ClosedClients;
#endif
	UE_LOG(LogProjectJLoopbackCohort, Display, TEXT("COHORT_FINISHED passed=%d peak=%d strikes=%d clients=%d"),
		bSuccess, Peak, Confirmed, ReportedClients);
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	if (GEngine) GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
	RetireClientContexts(Clients);
	Clients.Reset();
	if (GetWorld()->WorldType == EWorldType::PIE)
	{
		// Let the editor restore the level viewport before requesting process exit.
		GCohortResult.AsVariable()->Set(bSuccess ? 1 : -1, ECVF_SetByCode);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[World = TWeakObjectPtr<UWorld>(GetWorld()), bSuccess](float)
			{
				if (World.IsValid() && GEngine->GetWorldContextFromWorld(World.Get())) return true;
				FPlatformMisc::RequestExitWithStatus(false, bSuccess ? 0 : 8);
				return false;
			}), .25f);
		return;
	}
#endif
	FPlatformMisc::RequestExitWithStatus(false, bSuccess ? 0 : 8);
}

void AProject_JLoopbackCohort::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Ticker.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	if (GEngine) GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
	FWorldDelegates::OnWorldTickStart.Remove(TickStartHandle);
	FWorldDelegates::OnWorldTickEnd.Remove(TickEndHandle);
	if (auto* Attributes = Boss ? Boss->GetAttributeSet() : nullptr)
		Attributes->OnConfirmedHealthGELoss.Remove(BossDamageHandle);
#endif
	Super::EndPlay(Reason);
}

void AProject_JCohortController::ClientBeginCohortStrike_Implementation()
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	ServerCohortStrike();
#endif
}

void AProject_JCohortController::ServerCohortStrike_Implementation()
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	if (auto* Mode = GetWorld()->GetAuthGameMode<AProject_JLoopbackCohort>()) Mode->ApplyCohortStrike(this);
#endif
}

void AProject_JCohortController::ClientConfirmCohortFinished_Implementation()
{
	bCohortCompleted = true;
}

void AProject_JCohortController::ClientBeginMeasuredStrike_Implementation(int32 Sequence)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	if (bBossCombatPrepared)
	{
		MarkRenderedStrike();
		PressBossCombatAttack();
	}
	else ServerMeasuredStrike(Sequence);
#endif
}

void AProject_JCohortController::ServerMeasuredStrike_Implementation(int32 Sequence)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	if (auto* Mode = GetWorld()->GetAuthGameMode<AProject_JLoopbackCohort>()) Mode->ApplyMeasuredStrike(this, Sequence);
#endif
}

void AProject_JCohortController::ClientConfirmMeasuredHealth_Implementation(AProject_JNPCCharacter* Target, float Health)
{
	if (!HasExpectedBossAudience()) return;
	if (!Target)
		UE_LOG(LogProjectJLoopbackCohort, Warning, TEXT("Owner health receipt did not resolve its target actor"));
	ObservedBoss = Target;
	ExpectedHealth = Health;
	bCheckReplicatedHealth = true;
	bCohortCompleted = true;
}

bool AProject_JCohortController::HasCohortCompleted() const
{
	if (!bCohortCompleted || !bCheckReplicatedHealth) return bCohortCompleted;
	const auto* Target = ObservedBoss.Get();
	const auto* ASC = Target ? Target->GetAbilitySystemComponent() : nullptr;
	return ASC && ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()) == ExpectedHealth;
}

bool AProject_JCohortController::PollMeasuredRelease()
{
	PollBossReadiness();
	if (!bAcknowledgedHealth && bCheckReplicatedHealth && HasCohortCompleted())
	{
		bAcknowledgedHealth = true;
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
		ServerAcknowledgeMeasuredHealth(BossHealthObservations);
#else
		ServerAcknowledgeMeasuredHealth({});
#endif
	}
	return bAcknowledgedHealth && bReleaseMeasuredConnection;
}

void AProject_JCohortController::ServerAcknowledgeMeasuredHealth_Implementation(
	const TArray<FProject_JCohortHealthObservation>& Observations)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	if (auto* Mode = GetWorld()->GetAuthGameMode<AProject_JLoopbackCohort>()) Mode->AcknowledgeMeasuredHealth(this, Observations);
#endif
}

void AProject_JCohortController::ClientReleaseMeasuredConnection_Implementation()
{
	bReleaseMeasuredConnection = true;
}
