#include "Project_JLocomotionAnimStateComponent.h"
#include "AbilitySystemComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Movement/Project_JMovementQueries.h"
#include "Project_JGameplayTags.h"
#include "Project_JPlayerCharacter.h"
#include "TimerManager.h"

FString UProject_JLocomotionAnimStateComponent::GetDebugSummary() const
{
 return FString::Printf(TEXT("Mover Gameplay Local=%d Input=(%.2f,%.2f) Speed=%.1f Sprint=%d PhysicalAir=%d Jump=%d FallOff=%d Landing=%d LandingRevision=%d"),
  bUsingLocalInputState,CachedMoveInput.X,CachedMoveInput.Y,GroundSpeed,bWantsSprint,bIsPhysicallyInAir,bIsJumping,bIsFallOffStart,bIsLanding,LandingPresentationRevision);
}









void UProject_JLocomotionAnimStateComponent::OnCachedAbilitySystemChanged()
{
    const auto& Tags = FProject_JGameplayTags::Get();
    ReconcileMovementGameplayTag(Tags.State_Movement_InAir, OwnedInAirTagASC, bInAirGameplayTagRequested);
    ReconcileMovementGameplayTag(Tags.State_Movement_Landing, OwnedLandingTagASC, bLandingGameplayTagRequested);
}

void UProject_JLocomotionAnimStateComponent::ReconcileMovementGameplayTag(
    const FGameplayTag& Tag, TWeakObjectPtr<UAbilitySystemComponent>& Lease, const bool& bRequested)
{
    UAbilitySystemComponent* DesiredASC = GetCachedAbilitySystemComponent();
    if (DesiredASC && DesiredASC->GetAvatarActor() != GetPlayerOwner()) { DesiredASC = nullptr; }
    if (Lease.Get() != DesiredASC || !bRequested || bMovementTagOwnershipEnding)
    { ReleaseMovementGameplayTag(Tag, Lease); }
    // Release delegates can rebind the ASC, change demand, or end this owner.
    // Read live demand and cache again; never overwrite a nested acquired lease.
    if (bMovementTagOwnershipEnding || !bRequested || Lease.IsValid()) { return; }
    UAbilitySystemComponent* ASC = GetCachedAbilitySystemComponent();
    if (!IsValid(ASC) || ASC->GetAvatarActor() != GetPlayerOwner()) { return; }
    Lease = ASC; // Publish before the synchronous gameplay-tag callbacks.
    if (auto* ProjectJASC = Cast<UProject_JAbilitySystemComponent>(ASC)) { ProjectJASC->AddProjectJLooseGameplayTag(Tag); }
    else { ASC->AddLooseGameplayTag(Tag); }
}

void UProject_JLocomotionAnimStateComponent::ReleaseMovementGameplayTag(
    const FGameplayTag& Tag, TWeakObjectPtr<UAbilitySystemComponent>& Lease)
{
    UAbilitySystemComponent* ASC = Lease.Get();
    Lease.Reset(); // Detach before callbacks so recursive release cannot decrement twice.
    if (!IsValid(ASC)) { return; }
    if (auto* ProjectJASC = Cast<UProject_JAbilitySystemComponent>(ASC)) { ProjectJASC->RemoveProjectJLooseGameplayTag(Tag); }
    else { ASC->RemoveLooseGameplayTag(Tag); }
}

void UProject_JLocomotionAnimStateComponent::AddOwnedInAirGameplayTag()
{
    if (bMovementTagOwnershipEnding) { return; }
    bInAirGameplayTagRequested = true;
    ReconcileMovementGameplayTag(FProject_JGameplayTags::Get().State_Movement_InAir, OwnedInAirTagASC, bInAirGameplayTagRequested);
}

void UProject_JLocomotionAnimStateComponent::RemoveOwnedInAirGameplayTag()
{
    bInAirGameplayTagRequested = false;
    ReleaseMovementGameplayTag(FProject_JGameplayTags::Get().State_Movement_InAir, OwnedInAirTagASC);
}

void UProject_JLocomotionAnimStateComponent::AddOwnedLandingGameplayTag()
{
    if (bMovementTagOwnershipEnding) { return; }
    bLandingGameplayTagRequested = true;
    ReconcileMovementGameplayTag(FProject_JGameplayTags::Get().State_Movement_Landing, OwnedLandingTagASC, bLandingGameplayTagRequested);
}

void UProject_JLocomotionAnimStateComponent::RemoveOwnedLandingGameplayTag()
{
    bLandingGameplayTagRequested = false;
    ReleaseMovementGameplayTag(FProject_JGameplayTags::Get().State_Movement_Landing, OwnedLandingTagASC);
}

void UProject_JLocomotionAnimStateComponent::ClearOwnedMovementGameplayTags()
{
    bInAirGameplayTagRequested = bLandingGameplayTagRequested = false;
    RemoveOwnedInAirGameplayTag();
    RemoveOwnedLandingGameplayTag();
}
