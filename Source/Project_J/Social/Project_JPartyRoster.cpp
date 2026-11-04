#include "Social/Project_JPartyRoster.h"
#include "Social/Project_JSocialSubsystem.h"
#include "Game/Project_JPlayerState.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/StrongObjectPtr.h"

void FProject_JPartyRosterArray::PostReplicatedReceive(const FPostReplicatedReceiveParameters&)
{
    if (IsValid(OwnerState.Get())) OwnerState->NotifyPartyRosterChanged();
}

TArray<FProject_JPartyMemberSummary> AProject_JPlayerState::GetPartyRoster() const
{
    TArray<FProject_JPartyMemberSummary> Result;
    if (bPartyRosterEnding || IsActorBeingDestroyed() || PartyRoster.Items.Num() > ProjectJMaxPartyRosterMembers) return Result;
    Result.Reserve(PartyRoster.Items.Num());
    for (const auto& Item : PartyRoster.Items) Result.Add(Item.Summary);
    Result.Sort([](const auto& A, const auto& B) { return A.Order < B.Order; });
    return Result;
}

void AProject_JPlayerState::NotifyPartyRosterChanged()
{
    if (bPartyRosterEnding || IsActorBeingDestroyed() || PartyRoster.Items.Num() > ProjectJMaxPartyRosterMembers) return;
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    if (++PartyRosterRevision == 0) ++PartyRosterRevision;
    OnPartyRosterChanged.Broadcast();
}

bool AProject_JPlayerState::ApplyPartyRoster(const TArray<FProject_JPartyMemberSummary>& Desired)
{
    if (!HasAuthority() || bPartyRosterEnding || IsActorBeingDestroyed() || Desired.Num() > ProjectJMaxPartyRosterMembers) return false;
    TSet<FGuid> Ids;
    FName Group;
    for (int32 Index = 0; Index < Desired.Num(); ++Index)
    {
        const auto& Row = Desired[Index];
        if (!Row.CharacterId.IsValid() || Row.PartyId.IsNone() || Row.Order != Index || Ids.Contains(Row.CharacterId)
            || Row.DisplayName.Len() > 64 || Row.HealthPermille < 0 || Row.HealthPermille > 1000 || Row.Level < 0
            || (!Row.bAvailableHere && (Row.bHealthKnown || Row.Level != 0 || !Row.DisplayName.IsEmpty()))
            || (!Row.bHealthKnown && Row.HealthPermille != 0) || (!Group.IsNone() && Group != Row.PartyId)) return false;
        Group = Row.PartyId; Ids.Add(Row.CharacterId);
    }
    bool bChanged = PartyRoster.Items.RemoveAllSwap([&](const auto& Item) { return !Ids.Contains(Item.Summary.CharacterId); }) > 0;
    if (bChanged) PartyRoster.MarkArrayDirty();
    for (const auto& Row : Desired)
    {
        auto* Existing = PartyRoster.Items.FindByPredicate([&](const auto& Item) { return Item.Summary.CharacterId == Row.CharacterId; });
        if (!Existing)
        {
            auto& Added = PartyRoster.Items.AddDefaulted_GetRef();
            Added.Summary = Row; PartyRoster.MarkItemDirty(Added); bChanged = true;
        }
        else if (!(Existing->Summary == Row))
        {
            Existing->Summary = Row; PartyRoster.MarkItemDirty(*Existing); bChanged = true;
        }
    }
    if (bChanged)
    {
        ForceNetUpdate();
        NotifyPartyRosterChanged(); // The complete array is installed before any local UI callback.
    }
    return bChanged;
}

void AProject_JPlayerState::StopPartySummaryObservation()
{
    bPartySummaryObserving = false;
    if (++PartySummaryGeneration == 0) ++PartySummaryGeneration;
    if (auto* TimerWorld = PartySummaryTimerWorld.Get()) TimerWorld->GetTimerManager().ClearTimer(PartySummaryTimer);
    PartySummaryTimer.Invalidate(); PartySummaryTimerWorld.Reset();
    const auto PreviousASC = PartySummaryASC;
    const auto PreviousAttributes = PartySummaryAttributes;
    const auto Health = PartyHealthHandle, Maximum = PartyMaxHealthHandle, Defaults = PartyDefaultsHandle;
    PartyHealthHandle.Reset(); PartyMaxHealthHandle.Reset(); PartyDefaultsHandle.Reset();
    PartySummaryASC.Reset(); PartySummaryAttributes.Reset(); PartySummaryAuthority.Reset();
    OnPawnSet.RemoveDynamic(this, &ThisClass::OnPartySummaryPawnSet);
    if (auto* ASC = PreviousASC.Get())
    {
        ASC->GetGameplayAttributeValueChangeDelegate(UProject_JAttributeSet::GetHealthAttribute()).Remove(Health);
        ASC->GetGameplayAttributeValueChangeDelegate(UProject_JAttributeSet::GetMaxHealthAttribute()).Remove(Maximum);
    }
    if (auto* Attributes = PreviousAttributes.Get()) Attributes->OnInitializedResources.Remove(Defaults);
}

void AProject_JPlayerState::SetPartySummaryObservation(UProject_JSocialSubsystem* Authority, bool bEnable)
{
    if (!bEnable || !IsValid(Authority) || Authority->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) || bPartyRosterEnding || bSocialSessionRevoked || IsActorBeingDestroyed() || !HasAuthority())
    {
        StopPartySummaryObservation(); return;
    }
    auto* ASC = GetProjectJAbilitySystemComponent();
    auto* Attributes = GetProjectJAttributeSet();
    if (bPartySummaryObserving && PartySummaryAuthority == Authority && PartySummaryASC == ASC && PartySummaryAttributes == Attributes) return;
    StopPartySummaryObservation();
    if (!IsValid(ASC) || !IsValid(Attributes)) return;
    bPartySummaryObserving = true; PartySummaryAuthority = Authority; PartySummaryASC = ASC; PartySummaryAttributes = Attributes;
    const uint64 Generation = PartySummaryGeneration;
    PartyHealthHandle = ASC->GetGameplayAttributeValueChangeDelegate(UProject_JAttributeSet::GetHealthAttribute()).AddUObject(this, &ThisClass::OnPartyHealthChanged, Generation);
    PartyMaxHealthHandle = ASC->GetGameplayAttributeValueChangeDelegate(UProject_JAttributeSet::GetMaxHealthAttribute()).AddUObject(this, &ThisClass::OnPartyHealthChanged, Generation);
    PartyDefaultsHandle = Attributes->OnInitializedResources.AddUObject(this, &ThisClass::OnPartyDefaultsInitialized, Generation);
    OnPawnSet.AddUniqueDynamic(this, &ThisClass::OnPartySummaryPawnSet);
    QueuePartySummary();
}

void AProject_JPlayerState::OnPartyHealthChanged(const FOnAttributeChangeData&, uint64 Generation)
{
    if (Generation == PartySummaryGeneration) QueuePartySummary();
}
void AProject_JPlayerState::OnPartyDefaultsInitialized(uint64 Generation)
{
    // Deterministic producer completion: InitHealth/InitMaxHealth do not emit ASC changes.
    if (Generation == PartySummaryGeneration) QueuePartySummary();
}
void AProject_JPlayerState::OnPartySummaryPawnSet(APlayerState*, APawn*, APawn*)
{
    SetPartySummaryObservation(PartySummaryAuthority.Get(), true); QueuePartySummary();
}
void AProject_JPlayerState::QueuePartySummary()
{
    auto* World = GetWorld();
    if (!bPartySummaryObserving || bPartyRosterEnding || bSocialSessionRevoked || IsActorBeingDestroyed()
        || !HasAuthority() || !World || World->bIsTearingDown) return;
    if (PartySummaryTimer.IsValid())
    {
        if (PartySummaryTimerWorld == World) return;
        if (auto* PreviousWorld = PartySummaryTimerWorld.Get()) PreviousWorld->GetTimerManager().ClearTimer(PartySummaryTimer);
        PartySummaryTimer.Invalidate(); PartySummaryTimerWorld.Reset();
    }
    if (PartySummaryASC != GetProjectJAbilitySystemComponent() || PartySummaryAttributes != GetProjectJAttributeSet())
    {
        SetPartySummaryObservation(PartySummaryAuthority.Get(), true); return;
    }
    const uint64 Generation = PartySummaryGeneration;
    PartySummaryTimerWorld = World;
    World->GetTimerManager().SetTimer(PartySummaryTimer, FTimerDelegate::CreateWeakLambda(this, [this, Generation]()
    {
        if (Generation != PartySummaryGeneration) return;
        const bool bSameWorld = PartySummaryTimerWorld == GetWorld();
        PartySummaryTimer.Invalidate(); PartySummaryTimerWorld.Reset();
        if (!bPartySummaryObserving || bPartyRosterEnding || bSocialSessionRevoked || IsActorBeingDestroyed()
            || !HasAuthority() || !bSameWorld || !GetWorld() || GetWorld()->bIsTearingDown) return;
        if (PartySummaryASC != GetProjectJAbilitySystemComponent() || PartySummaryAttributes != GetProjectJAttributeSet())
        {
            SetPartySummaryObservation(PartySummaryAuthority.Get(), true); return;
        }
        if (auto* Social = PartySummaryAuthority.Get()) Social->PublishPartyMemberSummary(this);
    }), 0.2f, false);
}
void AProject_JPlayerState::RetirePartyRoster()
{
    bPartyRosterEnding = true;
    StopPartySummaryObservation();
    PartyRoster.Items.Reset(); PartyRoster.OwnerState = nullptr;
    OnPartyRosterChanged.Clear();
}
