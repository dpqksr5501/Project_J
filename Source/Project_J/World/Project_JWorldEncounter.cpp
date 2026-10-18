#include "World/Project_JWorldEncounter.h"
#include "World/Project_JWorldEventRegistry.h"
#include "Project_JNPCCharacter.h"
#include "Project_JCombatInterface.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Components/SceneComponent.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"

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
    bRegistered = GetWorld()->GetSubsystem<UProject_JWorldEventRegistry>()->Register(EncounterId, this);
    if (!bRegistered || NPCClass.IsNull()) { Phase = EProject_JEncounterPhase::Disabled; return; }
    GetWorldTimerManager().SetTimer(UpdateTimer, this, &ThisClass::UpdateEncounter, 1.f, true);
}
void AProject_JWorldEncounter::UpdateEncounter()
{
    if (bStopping || !HasAuthority()) { return; }
    if (Phase == EProject_JEncounterPhase::Active)
    {
        if (NPC.IsValid() && IProject_JCombatInterface::Execute_IsDead(NPC.Get())) { EnterCooldown(true); }
        return;
    }
    if (Phase == EProject_JEncounterPhase::Cooldown && GetWorld()->GetTimeSeconds() >= NextSpawnServerTime) { Phase = EProject_JEncounterPhase::Dormant; }
    if (Phase != EProject_JEncounterPhase::Dormant) { return; }
    bool bNearby = false;
    for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        const APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr;
        if (Pawn && FVector::DistSquared(Pawn->GetActorLocation(), GetActorLocation()) <= FMath::Square(ActivationRadius)) { bNearby = true; break; }
    }
    if (!bNearby) { return; }
    Phase = EProject_JEncounterPhase::Preparing;
    ForceNetUpdate();
    LoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(NPCClass.ToSoftObjectPath(), FStreamableDelegate::CreateUObject(this, &ThisClass::CompletePreparation));
    if (!LoadHandle) { EnterCooldown(false); }
}
void AProject_JWorldEncounter::CompletePreparation()
{
    if (bStopping || !HasAuthority() || Phase != EProject_JEncounterPhase::Preparing) { return; }
    UClass* Class = NPCClass.Get();
    FHitResult Ground;
    const FVector Origin = GetActorLocation();
    // Wait for server-side ground collision; a completed asset request is not cell readiness.
    if (!Class || !GetWorld()->LineTraceSingleByChannel(Ground, Origin+FVector(0,0,5000), Origin-FVector(0,0,15000), ECC_WorldStatic))
    { EnterCooldown(false); return; }
    const FTransform Transform(GetActorRotation(), Ground.ImpactPoint+FVector(0,0,200));
    AProject_JNPCCharacter* Spawned = GetWorld()->SpawnActorDeferred<AProject_JNPCCharacter>(Class, Transform, this, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding);
    if (!Spawned) { EnterCooldown(false); return; }
    NPC = Spawned;
    Spawned->OnDestroyed.AddDynamic(this, &ThisClass::OnNPCDestroyed);
    Spawned->FinishSpawning(Transform);
    if (!IsValid(Spawned) || Spawned->IsActorBeingDestroyed()) { EnterCooldown(false); return; }
    // The project's Iris grid supports at most 1.5 km; activation and network relevance are separate budgets.
    Spawned->SetNetCullDistanceSquared(FMath::Square(FMath::Clamp(ActivationRadius, 50000.f, 150000.f)));
    Phase = EProject_JEncounterPhase::Active;
    ++SpawnRevision;
    LoadHandle.Reset();
    ForceNetUpdate();
}
void AProject_JWorldEncounter::EnterCooldown(bool bDefeated)
{
    if (Phase == EProject_JEncounterPhase::Cooldown || bStopping) { return; }
    const bool bWasActive = Phase == EProject_JEncounterPhase::Active;
    Phase = EProject_JEncounterPhase::Cooldown;
    NextSpawnServerTime = GetWorld()->GetTimeSeconds() + (bDefeated ? FMath::Max(1.f, RespawnSeconds) : 10.f);
    if (NPC.IsValid())
    {
        NPC->OnDestroyed.RemoveDynamic(this, &ThisClass::OnNPCDestroyed);
        NPC->Destroy();
    }
    NPC.Reset();
    LoadHandle.Reset();
    ForceNetUpdate();
    if (bDefeated && bWasActive) { OnResolved.Broadcast(EncounterId, SpawnRevision); }
}
void AProject_JWorldEncounter::OnNPCDestroyed(AActor* Actor)
{
    if (!bStopping && Phase == EProject_JEncounterPhase::Active) { EnterCooldown(false); }
}
void AProject_JWorldEncounter::EndPlay(EEndPlayReason::Type Reason)
{
    bStopping = true;
    GetWorldTimerManager().ClearTimer(UpdateTimer);
    if (LoadHandle) { LoadHandle->CancelHandle(); LoadHandle.Reset(); }
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
