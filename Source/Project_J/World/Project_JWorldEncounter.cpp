#include "World/Project_JWorldEncounter.h"
#include "World/Project_JWorldEventRegistry.h"
#include "World/Project_JDungeonInstanceSubsystem.h"
#include "AI/Project_JThreatComponent.h"
#include "Backend/Project_JOnlineQuests.h"
#include "Backend/Project_JOnlineWorldSubsystem.h"
#include "Camera/Project_JCameraComponent.h"
#include "Economy/Project_JEconomySubsystem.h"
#include "Dom/JsonObject.h"
#include "Game/Project_JGameMode.h"
#include "Game/Project_JPlayerState.h"
#include "Game/Project_JQuestComponent.h"
#include "Gameplay/Project_JLootSubsystem.h"
#include "Gameplay/Project_JDeathReviveSubsystem.h"
#include "Social/Project_JSocialSubsystem.h"
#include "Project_JAttributeSet.h"
#include "Project_JNPCCharacter.h"
#include "Project_JPlayerCharacter.h"
#include "Components/Project_JNPCActivationComponent.h"
#include "Project_JCombatInterface.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Net/UnrealNetwork.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

struct AProject_JWorldEncounter::FBossRewardManifest
{
    FString Key;
    FName EncounterId;
    int32 SpawnRevision = 0;
    FGuid DefeatId;
    double CooldownSeconds = 0.0;
    TArray<FProject_JRolledLootGrant> Rewards;
    TMap<FGuid, TSharedPtr<FJsonObject>> EarnedQuestCredits;
    uint8 FailedAttempts = 0;
};

AProject_JWorldEncounter::AProject_JWorldEncounter()
{
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("SpawnOrigin")));
    bReplicates = true;
    SetNetCullDistanceSquared(FMath::Square(150000.f));
    SetNetUpdateFrequency(1);
#if WITH_EDITORONLY_DATA
    bIsSpatiallyLoaded = false;
#endif
}
void AProject_JWorldEncounter::BeginPlay()
{
    Super::BeginPlay();
    if (!HasAuthority()) { return; }
    // Disabled owners still claim their stable ID, but must not enter runnable scheduling.
    if (NPCClass.IsNull() || !FMath::IsFinite(ActivationRadius) || ActivationRadius <= 0 || ActivationRadius > 1.e9 ||
        !FMath::IsFinite(RespawnSeconds) || RespawnSeconds < 1 || (bUseSchedule && EndUtc <= StartUtc) || !HasValidInitialNavigation() || !HasValidCombatConfiguration())
    { Phase = EProject_JEncounterPhase::Disabled; }
    auto* Registry = GetWorld()->GetSubsystem<UProject_JWorldEventRegistry>();
    bRegistered = Registry && Registry->Register(EncounterId, this);
    if (!bRegistered) { Phase = EProject_JEncounterPhase::Disabled; }
}
bool AProject_JWorldEncounter::HasValidInitialNavigation() const
{
    return !bUseInitialNavigation || (!InitialNavigationDestination.ContainsNaN() && InitialNavigationDestination.GetAbsMax() <= 1.e9 &&
        FMath::IsFinite(InitialNavigationAcceptanceRadius) && InitialNavigationAcceptanceRadius >= 1 && InitialNavigationAcceptanceRadius <= 2000);
}
bool AProject_JWorldEncounter::HasValidCombatConfiguration() const
{
    return CombatDefinition.IsNull() || (!bUseInitialNavigation && CombatDefinition.ToSoftObjectPath().IsValid() && CombatTeamId >= 0);
}
AProject_JNPCCharacter* AProject_JWorldEncounter::GetActiveNPC() const
{
    return Phase == EProject_JEncounterPhase::Active ? NPC.Get() : nullptr;
}
bool AProject_JWorldEncounter::IsScheduleOpen(FDateTime NowUtc) const
{ return !bUseSchedule || (StartUtc < EndUtc && NowUtc >= StartUtc && NowUtc < EndUtc); }
bool AProject_JWorldEncounter::HasActivationDemand(bool& bKnown) const
{
    bKnown = false;
    if (bStopping || IsActorBeingDestroyed() || !HasAuthority() || !GetWorld() || GetWorld()->bIsTearingDown) return false;
    if (!IsScheduleOpen(FDateTime::UtcNow())) { bKnown = true; return false; }
    auto* Registry = GetWorld()->GetSubsystem<UProject_JWorldEventRegistry>();
    bool bFound = false;
    bKnown = Registry && Registry->TryQueryNearbyPlayer(GetActorLocation(), ActivationRadius, bFound);
    return bKnown && bFound;
}
void AProject_JWorldEncounter::UpdateEncounter()
{
    UWorld* World = GetWorld();
    if (bStopping || !HasAuthority() || !World || World->bIsTearingDown) { return; }
    if (Phase == EProject_JEncounterPhase::SettlingRewards)
    {
        TrySettleBossRewards();
        return;
    }
    if (Phase == EProject_JEncounterPhase::Dormant && !bTriggerDungeonClearOnDefeat && BossLootTable && !bBossCooldownLoaded
        && UProject_JOnlineWorldSubsystem::UsesOnlineBackend(World))
    {
        auto* GI = World->GetGameInstance();
        const auto* Online = GI ? GI->GetSubsystem<UProject_JOnlineWorldSubsystem>() : nullptr;
        double Remaining = 0.0;
        if (!Online || !Online->TryGetBossRewardCooldown(GetBossSettlementKey(), Remaining)) return;
        bBossCooldownLoaded = true;
        if (Remaining > 0.0)
        {
            NextSpawnServerTime = World->GetTimeSeconds() + Remaining;
            Phase = EProject_JEncounterPhase::Cooldown;
            ForceNetUpdate();
            return;
        }
    }
    if (Phase == EProject_JEncounterPhase::Active)
    {
        const TStrongObjectPtr<ThisClass> KeepSelf(this);
        const TStrongObjectPtr<AProject_JNPCCharacter> CurrentNPC(NPC.Get());
        const int32 Revision = SpawnRevision;
        if (!IsValid(CurrentNPC.Get())) { EnterCooldown(false); return; }
        const bool bDefeated = IProject_JCombatInterface::Execute_IsDead(CurrentNPC.Get());
        // Life queries are extensible: destruction may resolve this exact spawn synchronously.
        if (!bStopping && !IsActorBeingDestroyed() && HasAuthority() && GetWorld() && !GetWorld()->bIsTearingDown
            && Phase == EProject_JEncounterPhase::Active && SpawnRevision == Revision && NPC.Get() == CurrentNPC.Get()
            && bDefeated) { EnterCooldown(true); }
        return;
    }
    if (Phase == EProject_JEncounterPhase::Cooldown && GetWorld()->GetTimeSeconds() >= NextSpawnServerTime)
    {
        Phase = EProject_JEncounterPhase::Dormant;
        ForceNetUpdate();
    }
    if (Phase != EProject_JEncounterPhase::Dormant) { return; }
    bool bDemandKnown = false;
    if (!HasActivationDemand(bDemandKnown)) return;
    auto* Registry = GetWorld()->GetSubsystem<UProject_JWorldEventRegistry>();
    if (!Registry) return;
    PreparationToken = Registry->RequestPreparation(this, NPCClass.ToSoftObjectPath());
    if (!PreparationToken) return;
    Phase = EProject_JEncounterPhase::Preparing;
    ForceNetUpdate();
}

FString AProject_JWorldEncounter::GetBossSettlementKey() const
{
    const UWorld* World = GetWorld();
    const auto* Mode = World->GetAuthGameMode<AProject_JGameMode>();
    return FString::Printf(TEXT("%s|%d|%s"), *World->GetPackage()->GetName(), Mode ? Mode->GetOnlineChannelId() : 0,
        *EncounterId.ToString()).ToLower();
}
void AProject_JWorldEncounter::CompletePreparation(uint64 Token)
{
    if (bStopping || !HasAuthority() || IsActorBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown ||
        Phase != EProject_JEncounterPhase::Preparing || !Token || Token != PreparationToken) return;
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    const TStrongObjectPtr<UWorld> KeepWorld(GetWorld());
    const TStrongObjectPtr<UProject_JWorldEventRegistry> Registry(GetWorld()->GetSubsystem<UProject_JWorldEventRegistry>());
    const int32 Revision = SpawnRevision;
    const FName Id = EncounterId;
    const FSoftObjectPath ExpectedClassPath = NPCClass.ToSoftObjectPath();
    UClass* const Class = NPCClass.Get();
    const auto PreviousNPC = NPC;
    const bool bNavigate = bUseInitialNavigation;
    const FVector NavigationDestination = InitialNavigationDestination;
    const double NavigationRadius = InitialNavigationAcceptanceRadius;
    const FSoftObjectPath ExpectedDefinitionPath = CombatDefinition.ToSoftObjectPath();
    const int32 ExpectedCombatTeam = CombatTeamId;
    const bool bCombat = !ExpectedDefinitionPath.IsNull();
    // The registry's completed group handle is alive through this callback. No synchronous resolution/load.
    const TStrongObjectPtr<const UProject_JNPCActivationDefinition> KeepDefinition(CombatDefinition.Get());
    const auto HasSamePreparation = [&]
    {
        return !bStopping && !IsActorBeingDestroyed() && HasAuthority() && GetWorld() == KeepWorld.Get() &&
            !KeepWorld->bIsTearingDown && Phase == EProject_JEncounterPhase::Preparing &&
            PreparationToken == Token && SpawnRevision == Revision;
    };
    const auto IsPreparing = [&]
    {
        return HasSamePreparation() && Registry.IsValid() && Registry->IsCurrent(this) && EncounterId == Id &&
            NPCClass.ToSoftObjectPath() == ExpectedClassPath && NPCClass.Get() == Class && bUseInitialNavigation == bNavigate &&
            (!bNavigate || (HasValidInitialNavigation() && InitialNavigationDestination == NavigationDestination && InitialNavigationAcceptanceRadius == NavigationRadius)) &&
            HasValidCombatConfiguration() && CombatDefinition.ToSoftObjectPath() == ExpectedDefinitionPath &&
            (!bCombat || (KeepDefinition.IsValid() && CombatDefinition.Get() == KeepDefinition.Get() && CombatTeamId == ExpectedCombatTeam));
    };
    const auto RetireCandidate = [&](AProject_JNPCCharacter* Candidate, const TWeakObjectPtr<AProject_JNPCCharacter>& ExpectedNPC)
    {
        // Registration retirement must not strand the detached old token. A
        // replacement token/phase/NPC owns its state and is never rolled back.
        if (HasSamePreparation() && NPC == ExpectedNPC) EnterCooldown(false);
        if (IsValid(Candidate) && !Candidate->IsActorBeingDestroyed())
        {
            Candidate->OnDestroyed.RemoveDynamic(this, &ThisClass::OnNPCDestroyed);
            Candidate->Destroy();
        }
    };
    if (!IsPreparing()) { RetireCandidate(nullptr, PreviousNPC); return; }
    bool bDemandKnown = false;
    if (!HasActivationDemand(bDemandKnown)) { RetireCandidate(nullptr, PreviousNPC); return; }
    FHitResult Ground;
    const FVector Origin = GetActorLocation();
    // Preserve blocking static/dynamic support; a player/NPC body is not spawn ground.
    // This collision projection still does not certify WP, Nav or GPU readiness.
    FCollisionResponseParams GroundResponses(FCollisionResponseParams::DefaultResponseParam);
    GroundResponses.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
    if (!Class || !GetWorld()->LineTraceSingleByChannel(Ground, Origin+FVector(0,0,5000), Origin-FVector(0,0,15000), ECC_WorldStatic,
        FCollisionQueryParams::DefaultQueryParam, GroundResponses))
    { RetireCandidate(nullptr, PreviousNPC); return; }
    if (!IsPreparing()) { RetireCandidate(nullptr, PreviousNPC); return; }
    const auto* DefaultNPC = Class->GetDefaultObject<AProject_JNPCCharacter>();
    const float SpawnZOffset = (DefaultNPC && DefaultNPC->GetCapsuleComponent())
        ? FMath::Max(DefaultNPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.0f, 50.0f)
        : 100.0f;
    const FTransform Transform(GetActorRotation(), Ground.ImpactPoint + FVector(0, 0, SpawnZOffset));
    AProject_JNPCCharacter* Spawned = GetWorld()->SpawnActorDeferred<AProject_JNPCCharacter>(Class, Transform, this, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding);
    if (!Spawned) { RetireCandidate(nullptr, PreviousNPC); return; }
    const TStrongObjectPtr<AProject_JNPCCharacter> KeepSpawned(Spawned);
    const TWeakObjectPtr<AProject_JNPCCharacter> CandidateNPC(Spawned);
    if (!IsPreparing() || NPC != PreviousNPC) { RetireCandidate(Spawned, PreviousNPC); return; }
    NPC = Spawned;
    Spawned->OnDestroyed.AddDynamic(this, &ThisClass::OnNPCDestroyed);
    Spawned->FinishSpawning(Transform);
    if (!IsPreparing() || NPC != CandidateNPC) { RetireCandidate(Spawned, CandidateNPC); return; }
    if (!IsValid(Spawned) || Spawned->IsActorBeingDestroyed()) { RetireCandidate(Spawned, CandidateNPC); return; }
    // The registry detached the ready reservation before this callback. If
    // spawn callbacks invalidate demand, retire this candidate rather than
    // leave Preparing without a reservation or publish an unqualified NPC.
    const bool bDemand = HasActivationDemand(bDemandKnown);
    if (!IsPreparing() || NPC != CandidateNPC) { RetireCandidate(Spawned, CandidateNPC); return; }
    if (!bDemand) { RetireCandidate(Spawned, CandidateNPC); return; }
    if (bNavigate || bCombat)
    {
        // This is command/capability admission, not arrival or defeat. Later explicit
        // navigation/combat retirement must not destroy this otherwise live spawn.
        const bool bAdmitted = bCombat ? Spawned->StartCombatActions(KeepDefinition.Get(), ExpectedCombatTeam)
            : Spawned->StartNavigationTo(NavigationDestination, NavigationRadius);
        if (!IsPreparing() || NPC != CandidateNPC || !IsValid(Spawned) || Spawned->IsActorBeingDestroyed() || !bAdmitted)
        { RetireCandidate(Spawned, CandidateNPC); return; }
        // The helper may spawn/possess/register and query extensible life/ASC providers.
        // Requalify the existing shared demand/schedule contract after those callbacks.
        const bool bStillDemand = HasActivationDemand(bDemandKnown);
        if (!IsPreparing() || NPC != CandidateNPC || !IsValid(Spawned) || Spawned->IsActorBeingDestroyed() || !bStillDemand)
        { RetireCandidate(Spawned, CandidateNPC); return; }
    }
    // The project's Iris grid supports at most 1.5 km; activation and network relevance are separate budgets.
    Spawned->SetNetCullDistanceSquared(FMath::Square(FMath::Clamp(ActivationRadius, 50000.f, 150000.f)));
    if (BossLootTable != nullptr || !QuestKillEventId.IsNone() || !EncounterId.IsNone())
    {
        Spawned->EnableThreatObservation();
    }
    if (bTriggerDungeonClearOnDefeat)
    {
        if (auto* Revive = GetWorld()->GetSubsystem<UProject_JDeathReviveSubsystem>())
        {
            Revive->SetDungeonBossFightActive(true);
        }
    }
    Phase = EProject_JEncounterPhase::Active;
    ++SpawnRevision;
    PreparationToken = 0; // Registry releases the completed load after this bounded application.
    ForceNetUpdate();
    OnRep_Phase();
}
void AProject_JWorldEncounter::EnterCooldown(bool bDefeated)
{
    if (Phase == EProject_JEncounterPhase::Cooldown || Phase == EProject_JEncounterPhase::SettlingRewards
        || bStopping || IsActorBeingDestroyed() || !HasAuthority()
        || !GetWorld() || GetWorld()->bIsTearingDown) return;
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    const TStrongObjectPtr<AProject_JNPCCharacter> OldNPC(NPC.Get());
    const bool bWasActive = Phase == EProject_JEncounterPhase::Active;
    const FName ResolvedId = EncounterId;
    const int32 ResolvedRevision = SpawnRevision;
    const uint64 OldPreparation = PreparationToken;
    // Detach this spawn before actor/ability destruction can run arbitrary callbacks.
    NPC.Reset(); PreparationToken = 0;
    Phase = EProject_JEncounterPhase::Cooldown;
    NextSpawnServerTime = GetWorld()->GetTimeSeconds() + (bDefeated ? FMath::Max(1.f, RespawnSeconds) : 10.f);
    if (auto* Registry = GetWorld()->GetSubsystem<UProject_JWorldEventRegistry>()) Registry->ReleasePreparation(OldPreparation, this);
    if (bTriggerDungeonClearOnDefeat)
    {
        if (auto* Revive = GetWorld()->GetSubsystem<UProject_JDeathReviveSubsystem>())
        {
            Revive->SetDungeonBossFightActive(false);
        }
    }
    if (bDefeated && bWasActive && IsValid(OldNPC.Get()))
    {
        if (bTriggerDungeonClearOnDefeat)
        {
            auto* GI = GetWorld()->GetGameInstance();
            auto* Dungeon = GI ? GI->GetSubsystem<UProject_JDungeonInstanceSubsystem>() : nullptr;
            if (Dungeon && DungeonDefinition && Dungeon->ResolveFinalBossDefeat(*DungeonDefinition))
                Phase = EProject_JEncounterPhase::Disabled;
            else
                UE_LOG(LogTemp, Warning, TEXT("Dungeon completion was not admitted for encounter %s"), *ResolvedId.ToString());
        }
        if (auto* Threat = OldNPC->FindComponentByClass<UProject_JThreatComponent>())
        {
            UGameInstance* GI = GetWorld()->GetGameInstance();
            const bool bOnline = UProject_JOnlineWorldSubsystem::UsesOnlineBackend(GetWorld());
            const TArray<FGuid> Contributors = CollectDefeatContributors(*OldNPC.Get(), *Threat, bOnline);
            auto* Loot = GI ? GI->GetSubsystem<UProject_JLootSubsystem>() : nullptr;
            auto* Econ = GI ? GI->GetSubsystem<UProject_JEconomySubsystem>() : nullptr;
            const FName EffectiveQuestEvent = !QuestKillEventId.IsNone() ? QuestKillEventId
                : (!OldNPC->QuestKillEventId.IsNone() ? OldNPC->QuestKillEventId : ResolvedId);
            if (!bTriggerDungeonClearOnDefeat && BossLootTable && Loot && bOnline)
            {
                auto Manifest = MakeShared<FBossRewardManifest>();
                Manifest->Key = GetBossSettlementKey();
                Manifest->EncounterId = ResolvedId;
                Manifest->SpawnRevision = ResolvedRevision;
                Manifest->DefeatId = FGuid::NewGuid();
                Manifest->CooldownSeconds = FMath::Max(1.f, RespawnSeconds);
                Manifest->Rewards.Reserve(Contributors.Num());
                for (const FGuid& Contributor : Contributors)
                {
                    const int32 Seed = static_cast<int32>(HashCombineFast(GetTypeHash(Manifest->DefeatId), GetTypeHash(Contributor)));
                    Manifest->Rewards.Add(Loot->RollPersonalLoot(BossLootTable, Contributor, Seed));
                }
                CaptureBossQuestCredits(EffectiveQuestEvent, Contributors, Manifest->EarnedQuestCredits);
                PendingBossRewards = MoveTemp(Manifest);
                NextBossSettlementAttempt = 0.0;
                NextSpawnServerTime = 0.0;
                Phase = EProject_JEncounterPhase::SettlingRewards;
            }
            else
            {
                if (!bTriggerDungeonClearOnDefeat && BossLootTable && Loot && Econ)
                {
                    for (const FGuid& Contributor : Contributors)
                    {
                        const int32 Seed = static_cast<int32>(HashCombineFast(GetTypeHash(ResolvedId) ^ uint32(ResolvedRevision), GetTypeHash(Contributor)));
                        if (!Econ->SendBossRewardMail(Loot->RollPersonalLoot(BossLootTable, Contributor, Seed)))
                        {
                            UE_LOG(LogTemp, Warning, TEXT("Boss loot mail rejected for encounter %s, spawn %d"), *ResolvedId.ToString(), ResolvedRevision);
                        }
                    }
                }
                CreditBossQuest(EffectiveQuestEvent, Contributors);
            }
        }

    }
    if (IsValid(OldNPC.Get()))
    {
        OldNPC->OnDestroyed.RemoveDynamic(this, &ThisClass::OnNPCDestroyed);
        if (!OldNPC->IsActorBeingDestroyed()) OldNPC->Destroy();
    }
    if (bStopping || IsActorBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown) return;
    ForceNetUpdate();
    OnRep_Phase();
    if (Phase == EProject_JEncounterPhase::SettlingRewards) { TrySettleBossRewards(); return; }
    if (bDefeated && bWasActive) { OnResolved.Broadcast(ResolvedId, ResolvedRevision); }
}

bool AProject_JWorldEncounter::HasValidPartyRewardRadius() const
{
    return FMath::IsFinite(PartyRewardRadiusCm) && PartyRewardRadiusCm >= 0.0f && PartyRewardRadiusCm <= 30000.0f;
}

TArray<FGuid> AProject_JWorldEncounter::CollectDefeatContributors(const AProject_JNPCCharacter& DefeatedNPC,
    const UProject_JThreatComponent& Threat, bool bOnline) const
{
    const UWorld* World = GetWorld();
    if (!HasAuthority() || !World || World->bIsTearingDown || DefeatedNPC.GetWorld() != World || Threat.GetOwner() != &DefeatedNPC)
        return {};
    const auto* Attributes = DefeatedNPC.GetAttributeSet();
    TArray<FGuid> Contributors = Threat.GetEligibleContributors(Attributes ? Attributes->GetMaxHealth() : 1000.0);
    const auto* GI = World->GetGameInstance();
    const auto* Social = GI ? GI->GetSubsystem<UProject_JSocialSubsystem>() : nullptr;
    if (Contributors.IsEmpty() || !Social) return Contributors;

    const bool bField = !bTriggerDungeonClearOnDefeat;
    const FVector DefeatLocation = DefeatedNPC.GetActorLocation();
    if (bField && (!HasValidPartyRewardRadius() || PartyRewardRadiusCm == 0.0f || DefeatLocation.ContainsNaN())) return Contributors;
    const auto* OnlineMode = bOnline && bField ? World->GetAuthGameMode<AProject_JGameMode>() : nullptr;
    if (bOnline && bField && !OnlineMode) return Contributors;

    TSet<FGuid> Recipients(Contributors);
    TSet<FName> ContributingParties;
    TArray<TPair<FGuid, FName>, TInlineAllocator<16>> Candidates;
    const double RadiusSq = FMath::Square(static_cast<double>(PartyRewardRadiusCm));
    if (!bField)
    {
        // Keep the existing dungeon kill-event projection independent of the field proximity policy.
        for (const FGuid& Character : Contributors)
        {
            const FName Party = Social->GetPartyIdForCharacter(Character);
            if (!Party.IsNone()) ContributingParties.Add(Party);
        }
    }
    for (auto It = World->GetPlayerControllerIterator(); It; ++It)
    {
        const APlayerController* Controller = It->Get();
        const auto* State = IsValid(Controller) ? Controller->GetPlayerState<AProject_JPlayerState>() : nullptr;
        if (!IsValid(State) || !State->GetCharacterId().IsValid()) continue;
        const FGuid Character = State->GetCharacterId().Value;
        const FName Party = Social->GetPartyIdForCharacter(Character);
        if (Party.IsNone()) continue;

        if (bField)
        {
            const APawn* Pawn = Controller->GetPawn();
            if (Controller->IsActorBeingDestroyed() || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || Pawn->GetWorld() != World
                || Pawn->GetController() != Controller || Pawn->GetPlayerState() != State || State->GetOwner() != Controller
                || State->IsOnlyASpectator() || !State->CanAcceptSessionCommand() || (OnlineMode && OnlineMode->GetOnlineEpoch(State) <= 0))
                continue;
            const FVector Position = Pawn->GetActorLocation();
            if (Position.ContainsNaN() || FVector::DistSquared(DefeatLocation, Position) > RadiusSq) continue;
            if (Recipients.Contains(Character)) ContributingParties.Add(Party);
        }
        if (!Recipients.Contains(Character)) Candidates.Emplace(Character, Party);
    }
    for (const auto& [Character, Party] : Candidates)
    {
        if (!ContributingParties.Contains(Party) || Recipients.Contains(Character)) continue;
        Recipients.Add(Character);
        Contributors.Add(Character);
    }
    return Contributors;
}

void AProject_JWorldEncounter::TrySettleBossRewards()
{
    if (bBossSettlementInFlight || !PendingBossRewards || FPlatformTime::Seconds() < NextBossSettlementAttempt) return;
    const TSharedPtr<FBossRewardManifest> Manifest = PendingBossRewards;
    bBossSettlementInFlight = UProject_JOnlineWorldSubsystem::EnqueueBossRewards(GetWorld(), Manifest->Key, Manifest->DefeatId,
        Manifest->CooldownSeconds, Manifest->Rewards,
        [WeakThis = TWeakObjectPtr<ThisClass>(this), Manifest](bool bCommitted, double RemainingCooldown)
        {
            const TStrongObjectPtr<ThisClass> Self(WeakThis.Get());
            UWorld* World = Self.IsValid() ? Self->GetWorld() : nullptr;
            if (!Self.IsValid() || !World || World->bIsTearingDown || Self->bStopping || Self->IsActorBeingDestroyed()
                || Self->Phase != EProject_JEncounterPhase::SettlingRewards || Self->PendingBossRewards != Manifest) return;
            Self->bBossSettlementInFlight = false;
            if (!bCommitted)
            {
                Self->DelayBossRewardSettlement();
                return;
            }
            Self->PendingBossRewards.Reset();
            Self->Phase = EProject_JEncounterPhase::Cooldown;
            Self->NextSpawnServerTime = World->GetTimeSeconds() + RemainingCooldown;
            Self->ForceNetUpdate();
            Self->OnRep_Phase();
            if (!Self->bStopping && !Self->IsActorBeingDestroyed() && !World->bIsTearingDown)
                Self->OnResolved.Broadcast(Manifest->EncounterId, Manifest->SpawnRevision);
        }, Manifest->EarnedQuestCredits);
    if (!bBossSettlementInFlight) DelayBossRewardSettlement();
}

void AProject_JWorldEncounter::DelayBossRewardSettlement()
{
    const auto& Manifest = *PendingBossRewards;
    if (Manifest.FailedAttempts == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("Field boss rewards waiting for durable settlement: %s, defeat %s, recipients %d"),
            *Manifest.Key, *UProject_JOnlineWorldSubsystem::GuidText(Manifest.DefeatId), Manifest.Rewards.Num());
    }
    PendingBossRewards->FailedAttempts = FMath::Min<uint8>(Manifest.FailedAttempts + 1, 8);
    NextBossSettlementAttempt = FPlatformTime::Seconds() + FMath::Min(30.0, 0.25 * (1 << Manifest.FailedAttempts));
}

void AProject_JWorldEncounter::CreditBossQuest(FName Event, const TArray<FGuid>& Contributors)
{
    if (Event.IsNone() || Contributors.IsEmpty()) return;
    const TSet<FGuid> ContributorSet(Contributors);
    int32 RejectedProgress = 0;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();
        AProject_JPlayerState* PS = PC ? PC->GetPlayerState<AProject_JPlayerState>() : nullptr;
        if (PS && PS->GetCharacterId().IsValid() && ContributorSet.Contains(PS->GetCharacterId().Value))
        {
            if (auto* Quests = PS->FindComponentByClass<UProject_JQuestComponent>())
            {
                RejectedProgress += !Quests->TryRecordObjective(Event, 1);
            }
        }
    }
    if (RejectedProgress > 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("Boss quest progress was not admitted for %d contributors: encounter %s, spawn %d"),
            RejectedProgress, *EncounterId.ToString(), SpawnRevision);
    }
}

void AProject_JWorldEncounter::CaptureBossQuestCredits(FName Event, const TArray<FGuid>& Contributors,
    TMap<FGuid, TSharedPtr<FJsonObject>>& Credits) const
{
    if (Event.IsNone() || Contributors.IsEmpty()) return;
    const UWorld* World = GetWorld();
    const auto* Mode = World->GetAuthGameMode<AProject_JGameMode>();
    if (!Mode) return;
    const TSet<FGuid> Eligible(Contributors);
    const FString EarnedAt = LexToString(FDateTime::UtcNow().ToUnixTimestamp());
    int32 RejectedProgress = 0;
    for (auto It = World->GetPlayerControllerIterator(); It; ++It)
    {
        auto* Player = It->Get() ? It->Get()->GetPlayerState<AProject_JPlayerState>() : nullptr;
        if (!Player || !Eligible.Contains(Player->GetCharacterId().Value) || Credits.Contains(Player->GetCharacterId().Value)) continue;
        const auto* Quests = Player->FindComponentByClass<UProject_JQuestComponent>();
        const int64 Epoch = Mode->GetOnlineEpoch(Player);
        TArray<FProject_JQuestProgressInput> Inputs;
        if (!Quests || Epoch <= 0 || !Quests->CaptureObjectiveProgress(Event, 1, Inputs)) { ++RejectedProgress; continue; }
        if (Inputs.IsEmpty()) continue;
        auto Body = MakeShared<FJsonObject>();
        if (!ProjectJ::OnlineQuests::PrepareProgress(*Quests, Inputs, *Body)) { ++RejectedProgress; continue; }
        const FGuid Character = Player->GetCharacterId().Value;
        Body->SetStringField(TEXT("character_id"), UProject_JOnlineWorldSubsystem::GuidText(Character));
        Body->SetStringField(TEXT("session_epoch"), LexToString(Epoch));
        Body->SetStringField(TEXT("earned_at"), EarnedAt);
        Credits.Add(Character, Body);
    }
    if (RejectedProgress > 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("Could not capture online boss quest progress for %d contributors: %s spawn %d"),
            RejectedProgress, *EncounterId.ToString(), SpawnRevision);
    }
}
void AProject_JWorldEncounter::OnNPCDestroyed(AActor* Actor)
{
    if (bStopping || IsActorBeingDestroyed() || !HasAuthority() || !GetWorld() || GetWorld()->bIsTearingDown
        || Phase != EProject_JEncounterPhase::Active || Actor != NPC.Get() || !IsValid(Actor)) return;
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    const TStrongObjectPtr<AActor> KeepNPC(Actor);
    const int32 Revision = SpawnRevision;
    // OnDestroyed runs before Actor/components are marked garbage. Use the same
    // authoritative life contract as polling; destruction alone is not defeat.
    const bool bDefeated = IProject_JCombatInterface::Execute_IsDead(Actor);
    if (!bStopping && !IsActorBeingDestroyed() && HasAuthority() && GetWorld() && !GetWorld()->bIsTearingDown
        && Phase == EProject_JEncounterPhase::Active && SpawnRevision == Revision && NPC.Get() == Actor)
        EnterCooldown(bDefeated);
}

void AProject_JWorldEncounter::OnRep_Phase()
{
    UWorld* World = GetWorld();
    if (!World || World->GetNetMode() == NM_DedicatedServer || World->bIsTearingDown)
    {
        return;
    }
    const bool bShouldMonitorBossCamera = (bEnableBossCameraZoomOut || BossLootTable != nullptr)
        && Phase == EProject_JEncounterPhase::Active;
    if (bShouldMonitorBossCamera)
    {
        if (!World->GetTimerManager().IsTimerActive(BossCameraTimerHandle))
        {
            World->GetTimerManager().SetTimer(BossCameraTimerHandle, this, &ThisClass::UpdateLocalBossCameraState, 0.25f, true);
        }
        UpdateLocalBossCameraState();
    }
    else
    {
        ClearLocalBossCameraState();
    }
}

void AProject_JWorldEncounter::UpdateLocalBossCameraState()
{
    UWorld* World = GetWorld();
    if (!World || World->bIsTearingDown || Phase != EProject_JEncounterPhase::Active)
    {
        ClearLocalBossCameraState();
        return;
    }
    const FVector BossLoc = NPC.IsValid() ? NPC->GetActorLocation() : GetActorLocation();
    const double RadiusSq = FMath::Square(static_cast<double>(FMath::Max(500.0f, BossCameraRadiusCm)));
    decltype(ActiveBossCameras) DesiredCameras;
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();
        if (!PC || !PC->IsLocalController())
        {
            continue;
        }
        auto* Player = Cast<AProject_JPlayerCharacter>(PC->GetPawn());
        auto* Cam = Player ? Player->GetCameraComponent() : nullptr;
        if (IsValid(Cam) && PC->GetViewTarget() == Player && FVector::DistSquared(Player->GetActorLocation(), BossLoc) <= RadiusSq)
        {
            DesiredCameras.AddUnique(Cam);
        }
    }

    for (const auto& Camera : ActiveBossCameras)
    {
        if (Camera.IsValid() && !DesiredCameras.Contains(Camera))
        {
            Camera->SetBossCameraSource(this, false);
        }
    }
    for (const auto& Camera : DesiredCameras)
    {
        if (!ActiveBossCameras.Contains(Camera))
        {
            Camera->SetBossCameraSource(this, true);
        }
    }
    ActiveBossCameras = MoveTemp(DesiredCameras);
}

void AProject_JWorldEncounter::ClearLocalBossCameraState()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(BossCameraTimerHandle);
    }
    for (const auto& Camera : ActiveBossCameras)
    {
        if (Camera.IsValid()) Camera->SetBossCameraSource(this, false);
    }
    ActiveBossCameras.Reset();
}

void AProject_JWorldEncounter::EndPlay(EEndPlayReason::Type Reason)
{
    bStopping = true;
    PendingBossRewards.Reset();
    if (bTriggerDungeonClearOnDefeat && HasAuthority())
    {
        if (auto* Revive = GetWorld() ? GetWorld()->GetSubsystem<UProject_JDeathReviveSubsystem>() : nullptr)
        {
            Revive->SetDungeonBossFightActive(false);
        }
    }
    ClearLocalBossCameraState();
    if (auto* Registry = GetWorld()->GetSubsystem<UProject_JWorldEventRegistry>()) Registry->ReleasePreparation(PreparationToken, this);
    PreparationToken = 0;
    if (NPC.IsValid()) { NPC->OnDestroyed.RemoveDynamic(this, &ThisClass::OnNPCDestroyed); NPC->Destroy(); }
    NPC.Reset();
    if (bRegistered) { if (auto* Registry = GetWorld()->GetSubsystem<UProject_JWorldEventRegistry>()) { Registry->Unregister(EncounterId, this); } }
    Super::EndPlay(Reason);
}
void AProject_JWorldEncounter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AProject_JWorldEncounter, Phase);
    DOREPLIFETIME(AProject_JWorldEncounter, SpawnRevision);
    DOREPLIFETIME(AProject_JWorldEncounter, NextSpawnServerTime);
}
#if WITH_EDITOR
EDataValidationResult AProject_JWorldEncounter::IsDataValid(FDataValidationContext& Context) const
{
    const auto Base = Super::IsDataValid(Context);
    bool bValid = Base != EDataValidationResult::Invalid;
    if (!IsTemplate() && EncounterId.IsNone()) { Context.AddError(FText::FromString(TEXT("Encounter instance requires a stable EncounterId."))); bValid = false; }
    if (bUseSchedule && EndUtc <= StartUtc) { Context.AddError(FText::FromString(TEXT("UTC event end must be later than start."))); bValid = false; }
    if (!FMath::IsFinite(ActivationRadius) || ActivationRadius <= 0 || ActivationRadius > 1.e9 || !FMath::IsFinite(RespawnSeconds) || RespawnSeconds < 1)
    { Context.AddError(FText::FromString(TEXT("Encounter radius and respawn interval must be finite and positive."))); bValid = false; }
    if (!HasValidInitialNavigation())
    { Context.AddError(FText::FromString(TEXT("Enabled initial navigation requires a finite world-space destination (absolute coordinates <= 1e9) and acceptance radius 1..2000."))); bValid = false; }
    if (!HasValidCombatConfiguration())
    { Context.AddError(FText::FromString(TEXT("Selected encounter combat requires a valid definition path, a nonnegative authoritative team, and initial navigation disabled."))); bValid = false; }
    if (!bTriggerDungeonClearOnDefeat && !HasValidPartyRewardRadius())
    { Context.AddError(FText::FromString(TEXT("Field encounter party reward radius must be finite and within 0..30000 cm."))); bValid = false; }
    if (bTriggerDungeonClearOnDefeat && (!DungeonDefinition || DungeonDefinition->DungeonId.IsNone()))
    { Context.AddError(FText::FromString(TEXT("A dungeon final boss requires its dungeon definition."))); bValid = false; }
    return bValid ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif
