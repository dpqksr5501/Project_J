#include "Project_JPlayerCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Project_JGameplayTags.h"
#include "System/Project_JNPCDecisionSubsystem.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Components/Project_JSkillInputExecutionComponent.h"
#include "Mount/Project_JMountComponent.h"
#include "Mount/Project_JMountCharacter.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

bool AProject_JPlayerCharacter::IsPlayerLifeActive() const
{
    const auto* Attributes = GetAttributeSet();
    return !bPlayerDead && Attributes && FMath::IsFinite(Attributes->GetHealth()) && Attributes->GetHealth() > 0;
}

void AProject_JPlayerCharacter::StopPlayerLifeBinding()
{
    const TStrongObjectPtr<UProject_JAbilitySystemComponent> OldASC(LifeASC.Get());
    LifeASC.Reset();
    const bool bRemoveTag = bOwnsDeadTag;
    bOwnsDeadTag = false;
    if (OldASC.IsValid())
    {
        OldASC->GetGameplayAttributeValueChangeDelegate(UProject_JAttributeSet::GetHealthAttribute()).Remove(LifeHealthHandle);
        OldASC->OnActorInfoSettled.Remove(LifeActorInfoHandle);
    }
    LifeHealthHandle.Reset(); LifeActorInfoHandle.Reset();
    if (auto* Registry = GetWorld() ? GetWorld()->GetSubsystem<UProject_JNPCDecisionSubsystem>() : nullptr)
        Registry->UnregisterTarget(this);
    if (bRemoveTag && OldASC.IsValid())
        OldASC->RemoveProjectJLooseGameplayTag(FProject_JGameplayTags::Get().State_Dead, true);
}

void AProject_JPlayerCharacter::RefreshPlayerLifeBinding()
{
    if (!HasAuthority() || bCreationDependencyEnding || (!HasActorBegunPlay() && !IsActorBeginningPlay())) return;
    auto* ASC = Cast<UProject_JAbilitySystemComponent>(GetAbilitySystemComponent());
    if (LifeASC.Get() != ASC)
    {
        StopPlayerLifeBinding();
        if (bCreationDependencyEnding || LifeASC.IsValid() || !IsValid(ASC) || ASC->IsBeingDestroyed()
            || GetAbilitySystemComponent() != ASC) return;
        LifeASC = ASC;
        LifeHealthHandle = ASC->GetGameplayAttributeValueChangeDelegate(UProject_JAttributeSet::GetHealthAttribute()).AddUObject(this, &ThisClass::OnPlayerLifeHealthChanged);
        LifeActorInfoHandle = ASC->OnActorInfoSettled.AddUObject(this, &ThisClass::OnPlayerLifeActorInfoSettled);
    }
    RefreshPlayerLifeState();
}

void AProject_JPlayerCharacter::OnPlayerLifeHealthChanged(const FOnAttributeChangeData&)
{
    RefreshPlayerLifeState();
}

void AProject_JPlayerCharacter::OnPlayerLifeActorInfoSettled(UProject_JAbilitySystemComponent* Source)
{
    if (Source == LifeASC.Get()) RefreshPlayerLifeState();
}

void AProject_JPlayerCharacter::RefreshPlayerLifeState()
{
    if (!HasAuthority() || bCreationDependencyEnding || bApplyingDeath || !GetWorld() || GetWorld()->bIsTearingDown) return;
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    const TStrongObjectPtr<UProject_JAbilitySystemComponent> ASC(LifeASC.Get());
    auto* Registry = GetWorld()->GetSubsystem<UProject_JNPCDecisionSubsystem>();
    const uint64 Revision = ASC.IsValid() ? ASC->GetActorInfoRevision() : 0;
    const auto IsCurrent = [&] { return IsValid(this) && !IsActorBeingDestroyed() && !bCreationDependencyEnding
        && ASC.IsValid() && !ASC->IsBeingDestroyed() && LifeASC.Get() == ASC.Get() && GetAbilitySystemComponent() == ASC.Get()
        && !ASC->IsActorInfoChanging() && ASC->GetAvatarActor() == this && ASC->GetActorInfoRevision() == Revision; };
    if (!IsCurrent())
    {
        // Disarm before registry callbacks can publish another binding/tag receipt.
        const bool bRemoveTag = bOwnsDeadTag; bOwnsDeadTag = false;
        if (Registry) Registry->UnregisterTarget(this);
        // Remove only this Avatar's captured contribution, never a replacement's.
        if (bRemoveTag && ASC.IsValid()) ASC->RemoveProjectJLooseGameplayTag(FProject_JGameplayTags::Get().State_Dead, true);
        return;
    }
    const auto* Attributes = GetAttributeSet();
    if (!Attributes) return;
    if (FMath::IsFinite(Attributes->GetHealth()) && Attributes->GetHealth() > 0 && !bPlayerDead)
    {
        // Existing encounter defaults use team 1; players occupy the friendly PvE team 0.
        if (Registry) Registry->RegisterTarget(this, 0);
        return;
    }
    if (Registry) Registry->UnregisterTarget(this);
    if (!IsCurrent()) return;
    if (bPlayerDead)
    {
        if (!bOwnsDeadTag)
        {
            bOwnsDeadTag = true;
            ASC->AddProjectJLooseGameplayTag(FProject_JGameplayTags::Get().State_Dead, true);
        }
        return;
    }
    bPlayerDead = true;
    ForceNetUpdate();
    // Settled callbacks during cancellation are deferred by bApplyingDeath. Replay
    // once after the guard drops so a replaced ASC cannot retain our dead tag.
    ON_SCOPE_EXIT { RefreshPlayerLifeState(); };
    TGuardValue<bool> ApplyingDeath(bApplyingDeath, true);
    bOwnsDeadTag = true;
    ASC->AddProjectJLooseGameplayTag(FProject_JGameplayTags::Get().State_Dead, true);
    if (!IsCurrent()) return;
    StopJumping(); ConsumeMovementInputVector(); bSprintInputHeld = false;
    if (SkillInputExecutionComponent) SkillInputExecutionComponent->ReleaseLifeInputs();
    if (!IsCurrent()) return;
    CancelCombatIntroMontage();
    if (!IsCurrent()) return;
    CancelCombatOutroMontage();
    if (!IsCurrent()) return;
    if (CombatHitValidationComponent) CombatHitValidationComponent->EndAttack();
    if (!IsCurrent()) return;
    ResetCombatSpatialHistory();
    if (!IsCurrent()) return;
    TArray<FGameplayAbilitySpecHandle> Executions;
    for (const auto& Spec : ASC->GetActivatableAbilities()) if (Spec.IsActive()) Executions.Add(Spec.Handle);
    for (const auto Handle : Executions)
    {
        if (!IsCurrent()) return;
        ASC->CancelAbilityHandle(Handle);
    }
    if (!IsCurrent()) return;
    if (auto* Mount = MountComponent ? MountComponent->GetMountedMount() : nullptr)
        Mount->DismountRider(true);
}

bool AProject_JPlayerCharacter::ReviveAuthority(float HealthFraction)
{
    auto* ASC = LifeASC.Get();
    auto* Attributes = GetAttributeSet();
    if (!HasAuthority() || bCreationDependencyEnding || bApplyingDeath || !bPlayerDead
        || !IsValid(ASC) || GetAbilitySystemComponent() != ASC || ASC->IsActorInfoChanging() || ASC->GetAvatarActor() != this
        || !Attributes || !FMath::IsFinite(HealthFraction) || HealthFraction <= 0 || HealthFraction > 1
        || !FMath::IsFinite(Attributes->GetMaxHealth()) || Attributes->GetMaxHealth() <= 0) return false;
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    const TStrongObjectPtr<UProject_JAbilitySystemComponent> KeepASC(ASC);
    const uint64 Revision = ASC->GetActorInfoRevision();
    ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), Attributes->GetMaxHealth() * HealthFraction);
    if (bCreationDependencyEnding || !IsValid(this) || IsActorBeingDestroyed() || LifeASC.Get() != ASC || GetAbilitySystemComponent() != ASC
        || ASC->GetAvatarActor() != this || ASC->GetActorInfoRevision() != Revision || Attributes->GetHealth() <= 0) return false;
    bPlayerDead = false;
    const bool bRemoveTag = bOwnsDeadTag; bOwnsDeadTag = false;
    if (bRemoveTag) ASC->RemoveProjectJLooseGameplayTag(FProject_JGameplayTags::Get().State_Dead, true);
    if (bCreationDependencyEnding || GetAbilitySystemComponent() != ASC || ASC->GetAvatarActor() != this || ASC->GetActorInfoRevision() != Revision) return false;
    ResetCombatSpatialHistory(); ForceNetUpdate(); RefreshPlayerLifeState();
    return IsPlayerLifeActive();
}
