#include "Combat/Project_JGameplayEffect_SpellbladeFocus.h"

#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "NativeGameplayTags.h"
#include "Project_JAttributeSet.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(SpellbladeFocus, "State.Spellblade.Focus");
UE_DEFINE_GAMEPLAY_TAG_STATIC(SpellbladeMend, "State.Spellblade.Mend");
UE_DEFINE_GAMEPLAY_TAG_STATIC(SpellbladeHaste, "State.Spellblade.Haste");

UProject_JGameplayEffect_SpellbladeBuff::UProject_JGameplayEffect_SpellbladeBuff()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(6.f));
	GEComponents.Add(CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("BuffTag")));
}

void UProject_JGameplayEffect_SpellbladeBuff::SetRecovery(FGameplayAttribute Attribute, float PerSecond)
{
	Period = FScalableFloat(1.f);
	bExecutePeriodicEffectOnApplication = false;
	auto& Modifier = Modifiers.AddDefaulted_GetRef();
	Modifier.Attribute = Attribute;
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(PerSecond));
}

void UProject_JGameplayEffect_SpellbladeBuff::SetBuffTag(const FGameplayTag& Tag)
{
	FInheritedTagContainer Tags;
	Tags.AddTag(Tag);
	CastChecked<UTargetTagsGameplayEffectComponent>(GEComponents[0])->SetAndApplyTargetTagChanges(Tags);
}

UProject_JGameplayEffect_SpellbladeFocus::UProject_JGameplayEffect_SpellbladeFocus()
{
	SetBuffTag(SpellbladeFocus);
	SetRecovery(UProject_JAttributeSet::GetManaAttribute(), 5.f);
}

UProject_JGameplayEffect_SpellbladeMend::UProject_JGameplayEffect_SpellbladeMend()
{
	SetBuffTag(SpellbladeMend);
	SetRecovery(UProject_JAttributeSet::GetHealthAttribute(), 4.f);
}

UProject_JGameplayEffect_SpellbladeHaste::UProject_JGameplayEffect_SpellbladeHaste()
{
	SetBuffTag(SpellbladeHaste);
}
