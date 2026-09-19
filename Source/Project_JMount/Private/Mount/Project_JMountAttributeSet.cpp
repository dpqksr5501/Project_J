#include "Mount/Project_JMountAttributeSet.h"

#include "GameFramework/Actor.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"

void UProject_JMountAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UProject_JMountAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UProject_JMountAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UProject_JMountAttributeSet, Stamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UProject_JMountAttributeSet, MaxStamina, COND_None, REPNOTIFY_Always);
}

void UProject_JMountAttributeSet::ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const
{
	if (!FMath::IsFinite(NewValue)) NewValue = 0.0f;
	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, FMath::Max(0.0f, GetMaxHealth()));
	}
	else if (Attribute == GetStaminaAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, FMath::Max(0.0f, GetMaxStamina()));
	}
	else
	{
		NewValue = FMath::Max(Attribute == GetMaxHealthAttribute() ? 1.0f : 0.0f, NewValue);
	}
}

void UProject_JMountAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

void UProject_JMountAttributeSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

void UProject_JMountAttributeSet::PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
	Super::PostAttributeChange(Attribute, OldValue, NewValue);
	if (GetOwningActor() && GetOwningActor()->HasAuthority())
	{
		// A lower maximum must constrain the current resource even without a damage effect.
		if (Attribute == GetMaxHealthAttribute() && GetHealth() > NewValue) SetHealth(NewValue);
		else if (Attribute == GetMaxStaminaAttribute() && GetStamina() > NewValue) SetStamina(NewValue);
	}
}

bool UProject_JMountAttributeSet::PreGameplayEffectExecute(FGameplayEffectModCallbackData& Data)
{
	return Super::PreGameplayEffectExecute(Data) && FMath::IsFinite(Data.EvaluatedData.Magnitude);
}

void UProject_JMountAttributeSet::OnRep_Health(const FGameplayAttributeData& Old)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UProject_JMountAttributeSet, Health, Old);
}

void UProject_JMountAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& Old)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UProject_JMountAttributeSet, MaxHealth, Old);
}

void UProject_JMountAttributeSet::OnRep_Stamina(const FGameplayAttributeData& Old)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UProject_JMountAttributeSet, Stamina, Old);
}

void UProject_JMountAttributeSet::OnRep_MaxStamina(const FGameplayAttributeData& Old)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UProject_JMountAttributeSet, MaxStamina, Old);
}
