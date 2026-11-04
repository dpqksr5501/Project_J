#include "Gathering/Project_JGatheringNode.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Interaction/Project_JInteractionTargetComponent.h"
#include "Game/Project_JPlayerState.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

AProject_JGatheringNode::AProject_JGatheringNode()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true; SetNetUpdateFrequency(2); SetMinNetUpdateFrequency(1);
    InteractionBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionBounds")); SetRootComponent(InteractionBounds);
    InteractionBounds->InitBoxExtent(FVector(45)); InteractionBounds->SetCollisionObjectType(ECC_WorldDynamic);
    InteractionBounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly); InteractionBounds->SetCollisionResponseToAllChannels(ECR_Overlap);
    InteractionBounds->SetGenerateOverlapEvents(false);
    NodeVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("NodeVisual")); NodeVisual->SetupAttachment(InteractionBounds);
    NodeVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    InteractionTarget = CreateDefaultSubobject<UProject_JInteractionTargetComponent>(TEXT("InteractionTarget"));
    InteractionTarget->InteractionRange = 250; InteractionTarget->bExclusive = true;
    InteractionTarget->PromptText = NSLOCTEXT("Gathering", "GatherPrompt", "Gather");
}
bool AProject_JGatheringNode::HasValidDefinition() const
{ return !NodeId.IsNone() && !YieldItemId.IsNone() && YieldItemVersion >= 1 && YieldCount > 0 && FMath::IsFinite(CooldownSeconds) && CooldownSeconds > 0; }
bool AProject_JGatheringNode::EnsureRegistration()
{
    auto* Ledger = GetWorld() ? GetWorld()->GetSubsystem<UProject_JGatheringSubsystem>() : nullptr;
    if (!Ledger) return false;
    if (RegisteredId != NodeId)
    { Ledger->UnregisterNode(RegisteredId, this, Registration); Registration = 0; RegisteredId = NAME_None; }
    if (!Registration) { Registration = Ledger->RegisterNode(this); if (Registration) RegisteredId = NodeId; }
    return Registration != 0;
}
void AProject_JGatheringNode::BeginPlay() { Super::BeginPlay(); if (HasAuthority()) EnsureRegistration(); }
void AProject_JGatheringNode::EndPlay(EEndPlayReason::Type Reason)
{
    if (auto* Ledger = GetWorld() ? GetWorld()->GetSubsystem<UProject_JGatheringSubsystem>() : nullptr)
        Ledger->UnregisterNode(RegisteredId, this, Registration);
    Registration = 0; RegisteredId = NAME_None;
    Super::EndPlay(Reason);
}
EProject_JGatheringPhase AProject_JGatheringNode::GetPhase() const
{
    if (HasAuthority())
        if (const auto* Ledger = GetWorld() ? GetWorld()->GetSubsystem<UProject_JGatheringSubsystem>() : nullptr)
            return Ledger->GetState(NodeId, this).Phase;
    const auto* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
    const double Now = GameState ? GameState->GetServerWorldTimeSeconds() : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0);
    return Phase == EProject_JGatheringPhase::Cooldown && FMath::IsFinite(Now) && Now >= NextAvailableServerTime ? EProject_JGatheringPhase::Available : Phase;
}
double AProject_JGatheringNode::GetNextAvailableServerTime() const
{
    if (HasAuthority())
        if (const auto* Ledger = GetWorld() ? GetWorld()->GetSubsystem<UProject_JGatheringSubsystem>() : nullptr)
            return Ledger->GetState(NodeId, this).NextAvailable;
    return NextAvailableServerTime;
}
void AProject_JGatheringNode::ApplyLedgerState(const FProject_JGatheringView& View)
{ Phase = View.Phase; NextAvailableServerTime = View.NextAvailable; ForceNetUpdate(); }
bool AProject_JGatheringNode::IsSpatiallyAvailable(const APawn* Interactor) const
{
    if (!IsValid(Interactor) || Interactor->IsActorBeingDestroyed() || IsActorBeingDestroyed() || !HasValidDefinition() || !GetWorld()
        || Interactor->GetWorld() != GetWorld() || !InteractionTarget || !InteractionTarget->IsAvailable()) return false;
    const double Range = InteractionTarget->InteractionRange;
    const FVector Origin = Interactor->GetActorLocation(), Target = GetActorLocation();
    if (!FMath::IsFinite(Range) || Range <= 0 || Range > 300 || Origin.ContainsNaN() || Target.ContainsNaN()
        || FVector::DistSquared(Origin, Target) > FMath::Square(Range)) return false;
    if (InteractionTarget->bRequireLineOfSight)
    {
        FCollisionQueryParams Query(SCENE_QUERY_STAT(ProjectJGatheringVisibility), false, Interactor); Query.AddIgnoredActor(this);
        if (GetWorld()->LineTraceTestByChannel(Origin, Target, ECC_Visibility, Query)) return false;
    }
    return true;
}
bool AProject_JGatheringNode::CanInteract_Implementation(APawn* Interactor) const
{ return IsSpatiallyAvailable(Interactor) && GetPhase() == EProject_JGatheringPhase::Available; }
void AProject_JGatheringNode::Interact_Implementation(APawn* Interactor) { TryGather(Interactor); }
bool AProject_JGatheringNode::TryGather(APawn* Interactor)
{
    check(IsInGameThread());
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    const TStrongObjectPtr<APawn> KeepInteractor(Interactor);
    if (!HasAuthority() || GetNetMode() != NM_Standalone || !IsSpatiallyAvailable(Interactor) || !Interactor->HasAuthority()
        || GetPhase() != EProject_JGatheringPhase::Available) return false;
    auto* Player = Interactor->GetPlayerState<AProject_JPlayerState>();
    auto* ASC = Player ? Player->GetProjectJAbilitySystemComponent() : nullptr;
    if (!IsValid(Player) || Player->IsActorBeingDestroyed() || Player->GetPawn() != Interactor || Player->GetWorld() != GetWorld()
        || !ASC || ASC->IsBeingDestroyed() || ASC->GetAvatarActor() != Interactor || !Player->GetProjectJAttributeSet()
        || !(Player->GetProjectJAttributeSet()->GetHealth() > 0) || !EnsureRegistration()) return false;
    return GetWorld()->GetSubsystem<UProject_JGatheringSubsystem>()->SubmitGather(this, Registration, Player);
}
void AProject_JGatheringNode::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AProject_JGatheringNode, Phase); DOREPLIFETIME(AProject_JGatheringNode, NextAvailableServerTime);
}
#if WITH_EDITOR
EDataValidationResult AProject_JGatheringNode::IsDataValid(FDataValidationContext& Context) const
{
    const auto ParentResult = Super::IsDataValid(Context);
    if (IsTemplate() && YieldItemId.IsNone()) return ParentResult == EDataValidationResult::Invalid ? ParentResult : EDataValidationResult::Valid;
    if ((!IsTemplate() && NodeId.IsNone()) || YieldItemId.IsNone() || YieldItemVersion < 1 || YieldCount < 1
        || !FMath::IsFinite(CooldownSeconds) || CooldownSeconds <= 0)
    { Context.AddError(NSLOCTEXT("Gathering", "InvalidNode", "Gathering nodes require a stable instance NodeId, canonical yield ID/version/count and finite positive cooldown.")); return EDataValidationResult::Invalid; }
    return ParentResult == EDataValidationResult::Invalid ? ParentResult : EDataValidationResult::Valid;
}
#endif
