#pragma once
#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Project_JMountAttributeSet.generated.h"
#define MOUNT_ATTR(Class, Name) GAMEPLAYATTRIBUTE_PROPERTY_GETTER(Class, Name) GAMEPLAYATTRIBUTE_VALUE_GETTER(Name) GAMEPLAYATTRIBUTE_VALUE_SETTER(Name) GAMEPLAYATTRIBUTE_VALUE_INITTER(Name)
UCLASS()
class PROJECT_JMOUNT_API UProject_JMountAttributeSet : public UAttributeSet
{
	GENERATED_BODY()
public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Health) FGameplayAttributeData Health; MOUNT_ATTR(UProject_JMountAttributeSet, Health)
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_MaxHealth) FGameplayAttributeData MaxHealth; MOUNT_ATTR(UProject_JMountAttributeSet, MaxHealth)
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Stamina) FGameplayAttributeData Stamina; MOUNT_ATTR(UProject_JMountAttributeSet, Stamina)
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_MaxStamina) FGameplayAttributeData MaxStamina; MOUNT_ATTR(UProject_JMountAttributeSet, MaxStamina)
	UFUNCTION() void OnRep_Health(const FGameplayAttributeData& Old); UFUNCTION() void OnRep_MaxHealth(const FGameplayAttributeData& Old); UFUNCTION() void OnRep_Stamina(const FGameplayAttributeData& Old); UFUNCTION() void OnRep_MaxStamina(const FGameplayAttributeData& Old);
};
