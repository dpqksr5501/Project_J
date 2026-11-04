#include "Combat/Project_JAuthoritativeDamage.h"
#include "Combat/Project_JCombatHitValidation.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Project_JAbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "GameFramework/Actor.h"
#include "UObject/StrongObjectPtr.h"

bool Project_J::Combat::ApplyAuthoritativeDamage(AActor* InSource, AActor* InTarget,
 TSubclassOf<UGameplayEffect> Effect, TFunctionRef<bool()> IsCurrent)
{
 check(IsInGameThread());
 TStrongObjectPtr<AActor> Source(InSource), Target(InTarget);
 const auto Eligible = [&]()
 {
  return IsCurrent() && IsValid(Source.Get()) && Source->HasAuthority() && IsValid(Target.Get())
   && ValidatePlayerMeleeTarget(Source.Get(), Target.Get()) == EProject_JCombatHitValidationFailure::None
   && IsCurrent();
 };
 if (!Effect || !Eligible()) return false;
 TStrongObjectPtr<UAbilitySystemComponent> SourceASC(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Source.Get()));
 if (!Eligible() || !IsValid(SourceASC.Get())) return false;
 TStrongObjectPtr<UAbilitySystemComponent> TargetASC(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target.Get()));
 if (!Eligible() || !IsValid(TargetASC.Get())) return false;
 const auto* ProjectSource = Cast<UProject_JAbilitySystemComponent>(SourceASC.Get());
 const auto* ProjectTarget = Cast<UProject_JAbilitySystemComponent>(TargetASC.Get());
 const uint64 SourceEpoch = ProjectSource ? ProjectSource->GetActorInfoRevision() : 0;
 const uint64 TargetEpoch = ProjectTarget ? ProjectTarget->GetActorInfoRevision() : 0;
 const TWeakObjectPtr<AActor> SourceAvatar = SourceASC->GetAvatarActor(), TargetAvatar = TargetASC->GetAvatarActor();
 const TWeakObjectPtr<AActor> SourceOwner = SourceASC->GetOwnerActor(), TargetOwner = TargetASC->GetOwnerActor();
 const auto Current = [&]()
 {
  return Eligible() && UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Source.Get()) == SourceASC.Get()
   && UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target.Get()) == TargetASC.Get()
   && SourceASC->GetAvatarActor() == SourceAvatar.Get() && TargetASC->GetAvatarActor() == TargetAvatar.Get()
   && SourceASC->GetOwnerActor() == SourceOwner.Get() && TargetASC->GetOwnerActor() == TargetOwner.Get()
   && (!ProjectSource || ProjectSource->GetActorInfoRevision() == SourceEpoch)
   && (!ProjectTarget || ProjectTarget->GetActorInfoRevision() == TargetEpoch) && Eligible();
 };
 FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
 if (!Current()) return false;
 Context.AddInstigator(Source.Get(), Source.Get());
 FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(Effect, 1.f, Context);
 if (!Spec.IsValid() || !Current()) return false;
 SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC.Get());
 return true;
}
