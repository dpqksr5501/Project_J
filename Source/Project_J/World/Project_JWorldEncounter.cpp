#include "World/Project_JWorldEncounter.h"
#include "World/Project_JWorldEventRegistry.h"
#include "World/Project_JDungeonInstanceSubsystem.h"
#include "AI/Project_JThreatComponent.h"
#include "Backend/Project_JOnlineWorldSubsystem.h"
#include "Camera/Project_JCameraComponent.h"
#include "Economy/Project_JEconomySubsystem.h"
#include "Dom/JsonObject.h"
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
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

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
    if (bStopping || !HasAuthority()) { return; }
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
    if (Phase == EProject_JEncounterPhase::Cooldown || bStopping || IsActorBeingDestroyed() || !HasAuthority()
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
            const double MaxHP = OldNPC->GetAttributeSet() ? static_cast<double>(OldNPC->GetAttributeSet()->GetMaxHealth()) : 1000.0;
            TArray<FGuid> Contributors = Threat->GetEligibleContributors(MaxHP, 0.001);
            UGameInstance* GI = GetWorld()->GetGameInstance();
            // Share boss eligibility with same-world online party members (so healers/supports earn loot & quest credit).
            if (const auto* Social = GI ? GI->GetSubsystem<UProject_JSocialSubsystem>() : nullptr; Social && !Contributors.IsEmpty())
            {
                TSet<FName> EligibleParties;
                TSet<FGuid> ContributorSet(Contributors);
                for (const FGuid& CharId : Contributors)
                {
                    const FName PartyId = Social->GetPartyIdForCharacter(CharId);
                    if (!PartyId.IsNone()) EligibleParties.Add(PartyId);
                }
                if (!EligibleParties.IsEmpty())
                {
                    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
                    {
                        const APlayerController* PC = It->Get();
                        const AProject_JPlayerState* PS = PC ? PC->GetPlayerState<AProject_JPlayerState>() : nullptr;
                        if (PS && PS->GetCharacterId().IsValid())
                        {
                            const FGuid MemberId = PS->GetCharacterId().Value;
                            if (!ContributorSet.Contains(MemberId) && EligibleParties.Contains(Social->GetPartyIdForCharacter(MemberId)))
                            {
                                ContributorSet.Add(MemberId);
                                Contributors.Add(MemberId);
                            }
                        }
                    }
                }
            }
            auto* Loot = GI ? GI->GetSubsystem<UProject_JLootSubsystem>() : nullptr;
            auto* Econ = GI ? GI->GetSubsystem<UProject_JEconomySubsystem>() : nullptr;
            const bool bOnline = UProject_JOnlineWorldSubsystem::UsesOnlineBackend(GetWorld());
            if (!bTriggerDungeonClearOnDefeat && BossLootTable && Loot && (bOnline || Econ))
            {
                const FGuid Defeat = FGuid::NewGuid();
                for (const FGuid& Contributor : Contributors)
                {
                    const int32 Seed = static_cast<int32>(HashCombineFast(GetTypeHash(ResolvedId) ^ uint32(ResolvedRevision), GetTypeHash(Contributor)));
                    const auto Grant = Loot->RollPersonalLoot(BossLootTable, Contributor, Seed);
                    if (bOnline)
                    {
                        auto Body = UProject_JOnlineWorldSubsystem::WriteLoot(Grant);
                        Body->SetStringField(TEXT("defeat_id"), UProject_JOnlineWorldSubsystem::GuidText(Defeat));
                        Body->SetStringField(TEXT("character_id"), UProject_JOnlineWorldSubsystem::GuidText(Contributor));
                        const FString Context = FString::Printf(TEXT("encounter=%s spawn=%d char=%s"),
                            *ResolvedId.ToString(), ResolvedRevision, *UProject_JOnlineWorldSubsystem::GuidText(Contributor));
                        if (!UProject_JOnlineWorldSubsystem::EnqueueDurableMutation(
                            GetWorld(),
                            TEXT("Reward"),
                            TEXT("Boss"),
                            Body,
                            FGuid::NewGuid(),
                            Context,
                            [ResolvedId, ResolvedRevision](bool bConfirmed, const TSharedPtr<FJsonObject>&)
                            {
                                if (!bConfirmed)
                                {
                                    UE_LOG(LogTemp, Warning, TEXT("Online field boss mail unconfirmed after outbox retries for encounter %s, spawn %d"),
                                        *ResolvedId.ToString(), ResolvedRevision);
                                }
                            }))
                        {
                            UE_LOG(LogTemp, Warning, TEXT("Online field boss mail admission rejected: %s"), *Context);
                        }
                    }
                    else if (!Econ->SendBossRewardMail(Grant))
                    {
                        UE_LOG(LogTemp, Warning, TEXT("Boss loot mail rejected for encounter %s, spawn %d"), *ResolvedId.ToString(), ResolvedRevision);
                    }
                }
            }

            // Credit authoritative kill quest objectives to all online contributors.
            const FName EffectiveQuestEvent = !QuestKillEventId.IsNone()
                ? QuestKillEventId
                : (!OldNPC->QuestKillEventId.IsNone() ? OldNPC->QuestKillEventId : ResolvedId);
            if (!EffectiveQuestEvent.IsNone() && !Contributors.IsEmpty())
            {
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
                            RejectedProgress += !Quests->TryRecordObjective(EffectiveQuestEvent, 1);
                        }
                    }
                }
                if (RejectedProgress > 0)
                {
                    UE_LOG(LogTemp, Warning, TEXT("Boss quest progress was not admitted for %d contributors: encounter %s, spawn %d"),
                        RejectedProgress, *ResolvedId.ToString(), ResolvedRevision);
                }
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
    if (bDefeated && bWasActive) { OnResolved.Broadcast(ResolvedId, ResolvedRevision); }
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
    if (bTriggerDungeonClearOnDefeat && (!DungeonDefinition || DungeonDefinition->DungeonId.IsNone()))
    { Context.AddError(FText::FromString(TEXT("A dungeon final boss requires its dungeon definition."))); bValid = false; }
    return bValid ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif
