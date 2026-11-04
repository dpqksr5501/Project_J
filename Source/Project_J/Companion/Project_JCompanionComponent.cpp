#include "Companion/Project_JCompanionComponent.h"
#include "Companion/Project_JCompanionItemDefinition.h"
#include "Game/Project_JPlayerState.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JNPCCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Project_JCombatInterface.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JCommandBudgetComponent.h"
#include "Components/Project_JNPCActionComponent.h"
#include "World/Project_JWorldEventRegistry.h"
#include "AIController.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "UObject/StrongObjectPtr.h"

UProject_JCompanionComponent::UProject_JCompanionComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}
void UProject_JCompanionComponent::BeginPlay()
{
    Super::BeginPlay();
    State = Cast<AProject_JPlayerState>(GetOwner());
    if (!State.IsValid() || !State->HasAuthority()) return;
    if (State->GetCompanionComponent() != this) { State.Reset(); return; }
    State->OnPawnSet.AddUniqueDynamic(this, &ThisClass::OnPawnSet);
    IdentityHandle = State->OnCharacterIdentityChanged.AddUObject(this, &ThisClass::DismissCompanionAuthority);
    if (auto* Inventory = State->GetInventoryComponent())
    {
        Inventory->OnItemChanged.AddUniqueDynamic(this, &ThisClass::OnOwnedItemChanged);
        Inventory->OnItemRemoved.AddUniqueDynamic(this, &ThisClass::OnOwnedItemRemoved);
    }
    OwnerHealthHandle = State->GetAbilitySystemComponent()->GetGameplayAttributeValueChangeDelegate(
        UProject_JAttributeSet::GetHealthAttribute()).AddUObject(this, &ThisClass::OnOwnerHealthChanged);
}
void UProject_JCompanionComponent::RequestCompanion(FGuid ItemInstanceId)
{
    if (GetOwner() && GetOwner()->HasAuthority()) UseCompanionAuthority(ItemInstanceId);
    else ServerRequestCompanion(ItemInstanceId);
}
void UProject_JCompanionComponent::ServerRequestCompanion_Implementation(FGuid ItemInstanceId)
{
    if (!UProject_JCommandBudgetComponent::AdmitClientCommand(GetOwner(), NextClientRequest, 0.2)) return;
    UseCompanionAuthority(ItemInstanceId);
}
bool UProject_JCompanionComponent::IsCurrent(uint64 ExpectedGeneration) const
{
    auto* OwnerState = State.Get();
    auto* CurrentAvatar = Avatar.Get();
    auto* ASC = OwnerState ? OwnerState->GetProjectJAbilitySystemComponent() : nullptr;
    FProject_JItemInstanceData Item;
    return !bEnding && !IsBeingDestroyed() && IsRegistered() && Generation == ExpectedGeneration
        && GetWorld() && !GetWorld()->bIsTearingDown && IsValid(OwnerState) && !OwnerState->IsActorBeingDestroyed()
        && GetOwner() == OwnerState && OwnerState->GetCompanionComponent() == this && OwnerState->HasAuthority() && IsValid(CurrentAvatar) && !CurrentAvatar->IsActorBeingDestroyed()
        && CurrentAvatar->GetWorld() == GetWorld()
        && ASC && !ASC->IsBeingDestroyed() && ASC->GetOwnerActor() == OwnerState && ASC->GetAvatarActor() == CurrentAvatar
        && OwnerState->GetProjectJAttributeSet() && OwnerState->GetProjectJAttributeSet()->GetHealth() > 0
        && IsValid(Definition) && OwnerState->GetInventoryComponent()
        && OwnerState->GetInventoryComponent()->FindItemInstance(OwnedItem, Item) && !Item.bIsLocked && Item.ItemDef.Get() == Definition.Get();
}
bool UProject_JCompanionComponent::UseCompanionAuthority(FGuid ItemInstanceId)
{
    check(IsInGameThread());
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    const TStrongObjectPtr<AProject_JPlayerState> KeepState(State.Get());
    if (bEnding || IsBeingDestroyed() || !IsRegistered() || !KeepState || !KeepState->HasAuthority()
        || KeepState->IsActorBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown) return false;
    if (!ItemInstanceId.IsValid()) { DismissCompanionAuthority(); return true; }
    if (bChanging) return false;
    auto* ASC = KeepState->GetProjectJAbilitySystemComponent();
    auto* CurrentAvatar = ASC ? Cast<AProject_JPlayerCharacter>(ASC->GetAvatarActor()) : nullptr;
    const TStrongObjectPtr<UProject_JAbilitySystemComponent> KeepASC(ASC);
    FProject_JItemInstanceData Item;
    if (!IsValid(CurrentAvatar) || CurrentAvatar->IsActorBeingDestroyed() || !IsValid(ASC) || ASC->IsBeingDestroyed() || ASC->GetOwnerActor() != KeepState.Get() || ASC->GetAvatarActor() != CurrentAvatar
        || !KeepState->GetInventoryComponent()->FindItemInstance(ItemInstanceId, Item) || Item.bIsLocked) return false;
    auto* Selected = Cast<UProject_JCompanionItemDefinition>(Item.ItemDef);
    if (!IsValid(Selected) || !Selected->IsFollowDefinitionValid()) return false;
    if (OwnedItem == ItemInstanceId && IsCurrent(Generation) && (PreparationToken || GetActiveCompanion())) return true;
    const TStrongObjectPtr<AProject_JPlayerCharacter> KeepAvatar(CurrentAvatar);
    const TStrongObjectPtr<UProject_JCompanionItemDefinition> KeepDefinition(Selected);
    TGuardValue<bool> ChangingGuard(bChanging, true);
    const uint64 BeforeRetirement = Generation;
    RetireLease();
    // Old actor destruction may end the owner or replace its Avatar. It must not install stale work.
    if (Generation != BeforeRetirement + 1 || bEnding || IsBeingDestroyed() || !IsValid(KeepState.Get()) || KeepState->IsActorBeingDestroyed()
        || !IsValid(CurrentAvatar) || CurrentAvatar->IsActorBeingDestroyed()
        || !IsValid(ASC) || ASC->IsBeingDestroyed() || ASC != KeepState->GetProjectJAbilitySystemComponent() || ASC->GetAvatarActor() != CurrentAvatar) return false;
    const uint64 Lease = ++Generation;
    OwnedItem = ItemInstanceId; Definition = Selected; Avatar = CurrentAvatar;
    if (!IsCurrent(Lease)) { RetireLease(); return false; }
    CurrentAvatar->OnEndPlay.AddUniqueDynamic(this, &ThisClass::OnLeaseActorEnded);
    const TWeakObjectPtr<ThisClass> WeakSelf(this);
    auto* Registry = GetWorld()->GetSubsystem<UProject_JWorldEventRegistry>();
    PreparationToken = Registry ? Registry->RequestComponentPreparation(this, Selected->CompanionClass.ToSoftObjectPath(),
        [WeakSelf, Lease](uint64 Token, bool bCompleted)
        { if (auto* Self = WeakSelf.Get()) Self->CompletePreparation(Token, bCompleted, Lease); }) : 0;
    if (!PreparationToken) { RetireLease(); return false; }
    return true;
}
void UProject_JCompanionComponent::CompletePreparation(uint64 Token, bool bLoadCompleted, uint64 Lease)
{
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    const TStrongObjectPtr<AProject_JPlayerState> KeepState(State.Get());
    const TStrongObjectPtr<AProject_JPlayerCharacter> KeepAvatar(Avatar.Get());
    const TStrongObjectPtr<const UProject_JCompanionItemDefinition> KeepDefinition(Definition.Get());
    if (Token != PreparationToken || Generation != Lease) return;
    PreparationToken = 0;
    if (!bLoadCompleted || !IsCurrent(Lease) || bChanging) { RetireLease(); return; }
    TGuardValue<bool> ChangingGuard(bChanging, true);
    UClass* Class = Definition->CompanionClass.Get();
    if (!Class || Class->HasAnyClassFlags(CLASS_Abstract) || !Class->IsChildOf(AProject_JNPCCharacter::StaticClass()))
    { RetireLease(); return; }
    // This slice exposes only the existing public NPC attributes. The session pointer
    // does not grant the client an owning connection to the NPC ASC or its GAS RPCs.
    const auto* Defaults = Class->GetDefaultObject<AProject_JNPCCharacter>();
    if (!Defaults || Defaults->AutoPossessPlayer != EAutoReceiveInput::Disabled) { RetireLease(); return; }
    const FTransform Transform(KeepAvatar->GetActorRotation(),
        KeepAvatar->GetActorLocation() - KeepAvatar->GetActorForwardVector() * Definition->SpawnDistance);
    auto* Spawned = GetWorld()->SpawnActorDeferred<AProject_JNPCCharacter>(Class, Transform, nullptr, KeepAvatar.Get(),
        ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding);
    if (!Spawned) { RetireLease(); return; }
    const TStrongObjectPtr<AProject_JNPCCharacter> KeepSpawned(Spawned);
    if (!IsCurrent(Lease)) { if (IsValid(Spawned) && !Spawned->IsActorBeingDestroyed()) Spawned->Destroy(); return; }
    ActiveCompanion = Spawned;
    const auto IsSpawnCurrent = [&] { return IsCurrent(Lease) && ActiveCompanion.Get() == Spawned && IsValid(Spawned) && !Spawned->IsActorBeingDestroyed(); };
    Spawned->OnEndPlay.AddUniqueDynamic(this, &ThisClass::OnLeaseActorEnded);
    Spawned->FinishSpawning(Transform);
    if (!IsSpawnCurrent()) { RetireLease(); return; }
    if (!Spawned->GetController())
    {
        auto* AI = GetWorld()->SpawnActor<AAIController>();
        if (!AI) { RetireLease(); return; }
        const TStrongObjectPtr<AAIController> KeepAI(AI);
        CreatedController = AI;
        if (!IsSpawnCurrent() || !IsValid(AI) || AI->IsActorBeingDestroyed()) { RetireLease(); return; }
        AI->Possess(Spawned);
        if (!IsSpawnCurrent()) { RetireLease(); return; }
    }
    auto* Following = NewObject<UProject_JNPCActionComponent>(Spawned);
    const TStrongObjectPtr<UProject_JNPCActionComponent> KeepFollowing(Following);
    Spawned->AddInstanceComponent(Following); Following->RegisterComponent(); Action = Following;
    const TWeakObjectPtr<ThisClass> WeakSelf(this);
    if (!IsSpawnCurrent() || !IsValid(Following) || Following->IsBeingDestroyed() || !Following->StartFollowing(KeepAvatar.Get(), KeepState->GetAbilitySystemComponent(), Definition->FollowDistance, [WeakSelf, Lease]
        { if (auto* Self = WeakSelf.Get(); Self && Self->Generation == Lease) Self->DismissCompanionAuthority(); }))
    { RetireLease(); return; }
    ActionToken = Following->GetOwnershipToken();
    auto* NPCASC = Spawned->GetAbilitySystemComponent();
    if (!IsSpawnCurrent() || !IsValid(NPCASC) || NPCASC->IsBeingDestroyed() || NPCASC->GetAvatarActor() != Spawned) { RetireLease(); return; }
    CompanionASC = NPCASC;
    CompanionHealthHandle = NPCASC->GetGameplayAttributeValueChangeDelegate(UProject_JAttributeSet::GetHealthAttribute()).AddUObject(
        this, &ThisClass::OnCompanionHealthChanged);
    if (!IsCurrent(Lease) || !Spawned->GetAttributeSet() || Spawned->GetAttributeSet()->GetHealth() <= 0) { RetireLease(); return; }
    KeepState->ForceNetUpdate();
}
AProject_JNPCCharacter* UProject_JCompanionComponent::GetActiveCompanion() const
{ return IsValid(ActiveCompanion) && !ActiveCompanion->IsActorBeingDestroyed() ? ActiveCompanion.Get() : nullptr; }
void UProject_JCompanionComponent::RetireLease()
{
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    const TStrongObjectPtr<AProject_JNPCCharacter> OldNPC(ActiveCompanion.Get());
    const TStrongObjectPtr<AProject_JPlayerCharacter> OldAvatar(Avatar.Get());
    const TStrongObjectPtr<AAIController> OldController(CreatedController.Get());
    const TStrongObjectPtr<UProject_JNPCActionComponent> OldAction(Action.Get());
    const TStrongObjectPtr<UAbilitySystemComponent> OldASC(CompanionASC.Get());
    const uint64 OldPreparation = PreparationToken, OldActionToken = ActionToken;
    ++Generation; PreparationToken = ActionToken = 0; ActiveCompanion = nullptr; Avatar.Reset();
    Definition = nullptr; OwnedItem.Invalidate(); CreatedController.Reset(); Action.Reset(); CompanionASC.Reset();
    if (IsValid(OldAvatar.Get())) OldAvatar->OnEndPlay.RemoveDynamic(this, &ThisClass::OnLeaseActorEnded);
    if (IsValid(OldNPC.Get())) OldNPC->OnEndPlay.RemoveDynamic(this, &ThisClass::OnLeaseActorEnded);
    if (IsValid(OldASC.Get())) OldASC->GetGameplayAttributeValueChangeDelegate(UProject_JAttributeSet::GetHealthAttribute()).Remove(CompanionHealthHandle);
    CompanionHealthHandle.Reset();
    if (auto* Registry = GetWorld() ? GetWorld()->GetSubsystem<UProject_JWorldEventRegistry>() : nullptr)
        Registry->ReleasePreparation(OldPreparation, this);
    if (IsValid(OldAction.Get()) && !OldAction->IsBeingDestroyed()) OldAction->StopActionsIfOwned(OldActionToken);
    if (IsValid(OldNPC.Get()) && !OldNPC->IsActorBeingDestroyed()) OldNPC->Destroy();
    if (IsValid(OldController.Get()) && !OldController->IsActorBeingDestroyed()
        && (!OldController->GetPawn() || OldController->GetPawn() == OldNPC.Get())) OldController->Destroy();
    if (State.IsValid() && !State->IsActorBeingDestroyed()) State->ForceNetUpdate();
}
void UProject_JCompanionComponent::DismissCompanionAuthority()
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;
    RetireLease();
}
void UProject_JCompanionComponent::OnPawnSet(APlayerState*, APawn*, APawn*)
{
    // Mount possession changes PlayerState.Pawn while the gameplay ASC still owns the rider.
    // A real Avatar replacement is also checked by the existing follow action cadence.
    if (Avatar.IsValid() && !IsCurrent(Generation)) DismissCompanionAuthority();
}
void UProject_JCompanionComponent::OnLeaseActorEnded(AActor* Actor, EEndPlayReason::Type)
{ if (Actor == Avatar.Get() || Actor == ActiveCompanion.Get()) DismissCompanionAuthority(); }
void UProject_JCompanionComponent::OnOwnedItemChanged(const FProject_JItemInstanceData& Item)
{ if (Item.InstanceId == OwnedItem && !IsCurrent(Generation)) DismissCompanionAuthority(); }
void UProject_JCompanionComponent::OnOwnedItemRemoved(const FProject_JItemInstanceData& Item)
{ if (Item.InstanceId == OwnedItem) DismissCompanionAuthority(); }
void UProject_JCompanionComponent::OnOwnerHealthChanged(const FOnAttributeChangeData& Data)
{ if (Data.NewValue <= 0) DismissCompanionAuthority(); }
void UProject_JCompanionComponent::OnCompanionHealthChanged(const FOnAttributeChangeData& Data)
{ if (Data.NewValue <= 0) DismissCompanionAuthority(); }
void UProject_JCompanionComponent::Stop()
{
    bEnding = true;
    if (auto* OwnerState = State.Get())
    {
        OwnerState->OnPawnSet.RemoveDynamic(this, &ThisClass::OnPawnSet);
        OwnerState->OnCharacterIdentityChanged.Remove(IdentityHandle);
        if (auto* ASC = OwnerState->GetAbilitySystemComponent())
            ASC->GetGameplayAttributeValueChangeDelegate(UProject_JAttributeSet::GetHealthAttribute()).Remove(OwnerHealthHandle);
        if (auto* Inventory = OwnerState->GetInventoryComponent())
        {
            Inventory->OnItemChanged.RemoveDynamic(this, &ThisClass::OnOwnedItemChanged);
            Inventory->OnItemRemoved.RemoveDynamic(this, &ThisClass::OnOwnedItemRemoved);
        }
    }
    RetireLease(); State.Reset();
}
void UProject_JCompanionComponent::EndPlay(EEndPlayReason::Type Reason) { Stop(); Super::EndPlay(Reason); }
void UProject_JCompanionComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{ Stop(); Super::OnComponentDestroyed(bDestroyingHierarchy); }
void UProject_JCompanionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(UProject_JCompanionComponent, ActiveCompanion, COND_OwnerOnly);
}
