#include "Project_JProjectileTestDamage.h"
#include "Project_JAttributeSet.h"
UProject_JProjectileTestDamage::UProject_JProjectileTestDamage()
{
 DurationPolicy = EGameplayEffectDurationType::Instant;
 auto& Modifier = Modifiers.AddDefaulted_GetRef();
 Modifier.Attribute = UProject_JAttributeSet::GetHealthAttribute();
 Modifier.ModifierOp = EGameplayModOp::Additive;
 Modifier.ModifierMagnitude = FScalableFloat(-10.f);
}
