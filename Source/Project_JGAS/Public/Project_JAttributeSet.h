// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "Project_JAttributeSet.generated.h"

/** Immutable identity snapshot of one native GE Health execution. No spec/context pointer escapes. */
struct FProject_JHealthGEExecutionProof
{
	TWeakObjectPtr<UAbilitySystemComponent> TargetASC, SourceASC;
	TWeakObjectPtr<AActor> TargetOwner, TargetAvatar, SourceOwner, SourceAvatar, Instigator, EffectCauser;
	TWeakObjectPtr<const UGameplayEffect> EffectDefinition;
	uint64 TargetEpoch = 0, SourceEpoch = 0;
};
struct FProject_JConfirmedHealthGELoss
{
	FProject_JHealthGEExecutionProof Proof;
	float OldValue = 0, NewValue = 0;
};
enum class EProject_JHealthGEObservationFailure : uint8 { ScopeLimit, UnobservedExecution, UnsupportedAttributeSet };

// Uses macros from AttributeSet.h
#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

/**
 * Base Attribute Set for characters in Project J.
 */
UCLASS()
class PROJECT_JGAS_API UProject_JAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UProject_JAttributeSet();

	// Initialization belongs to the persistent set, not to a replaceable avatar.
	bool HasInitializedDefaults() const { return bDefaultsInitialized; }
	void MarkDefaultsInitialized() { bDefaultsInitialized = true; }
	/** Emitted only after a producer completes direct resource Init writes. */
	DECLARE_MULTICAST_DELEGATE(FOnInitializedResources);
	FOnInitializedResources OnInitializedResources;
	void PublishInitializedResources();

	/** Confirmed loss caused by an executed GE Health modifier, not general damage/contribution classification. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnConfirmedHealthGELoss, const FProject_JConfirmedHealthGELoss&);
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnHealthGEObservationFailed, const FProject_JHealthGEExecutionProof&, EProject_JHealthGEObservationFailure);
	FOnConfirmedHealthGELoss OnConfirmedHealthGELoss;
	FOnHealthGEObservationFailed OnHealthGEObservationFailed;
	static constexpr int32 MaxObservedHealthGEExecutions = 8;
	uint64 GetHealthGEObservationFailures() const { return HealthGEObservationFailures; }
	void ReleaseUnusedHealthGEObservationStorage();
	/** Current exact native Pre/Post and clamp-only PreAttributeChange contract; derived overrides need their own proof. */
	bool CanBeginHealthGEObservation() const;
#if WITH_DEV_AUTOMATION_TESTS
	int32 GetActiveHealthGEExecutionsForTest() const { return HealthGEExecutions.Num(); }
	uint32 GetSuppressedHealthGEDepthForTest() const { return SuppressedHealthGEDepth; }
	int32 GetHealthGEObservationCapacityForTest() const { return HealthGEExecutions.Max(); }
#endif

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;
	virtual void PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue) override;
	virtual bool PreGameplayEffectExecute(struct FGameplayEffectModCallbackData& Data) override;
	virtual void PostGameplayEffectExecute(const struct FGameplayEffectModCallbackData& Data) override;

	// Health Attribute
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_Health)
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS(UProject_JAttributeSet, Health)

	UFUNCTION()
	virtual void OnRep_Health(const FGameplayAttributeData& OldHealth);

	// MaxHealth Attribute
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_MaxHealth)
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS(UProject_JAttributeSet, MaxHealth)

	UFUNCTION()
	virtual void OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth);

	// Mana Attribute
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_Mana)
	FGameplayAttributeData Mana;
	ATTRIBUTE_ACCESSORS(UProject_JAttributeSet, Mana)

	UFUNCTION()
	virtual void OnRep_Mana(const FGameplayAttributeData& OldMana);

	// MaxMana Attribute
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_MaxMana)
	FGameplayAttributeData MaxMana;
	ATTRIBUTE_ACCESSORS(UProject_JAttributeSet, MaxMana)

	UFUNCTION()
	virtual void OnRep_MaxMana(const FGameplayAttributeData& OldMaxMana);

	// AttackPower Attribute
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_AttackPower)
	FGameplayAttributeData AttackPower;
	ATTRIBUTE_ACCESSORS(UProject_JAttributeSet, AttackPower)

	UFUNCTION()
	virtual void OnRep_AttackPower(const FGameplayAttributeData& OldAttackPower);

	// Defense Attribute
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_Defense)
	FGameplayAttributeData Defense;
	ATTRIBUTE_ACCESSORS(UProject_JAttributeSet, Defense)

	UFUNCTION()
	virtual void OnRep_Defense(const FGameplayAttributeData& OldDefense);

private:
	UPROPERTY(Transient)
	bool bDefaultsInitialized = false;

	struct FHealthGEExecution
	{
		const FGameplayEffectModCallbackData* Identity = nullptr; // Synchronous native Pre/Post pairing only.
		FProject_JHealthGEExecutionProof Proof;
		TArray<TStrongObjectPtr<const UObject>, TInlineAllocator<10>> Pins;
		bool bObserve = false, bConsumed = false;
	};
	// Default OFF stays empty; active observation reserves once, never allocates per ordinary hit.
	TArray<FHealthGEExecution> HealthGEExecutions;
	FProject_JHealthGEExecutionProof SuppressedHealthGEProof;
	EProject_JHealthGEObservationFailure SuppressedHealthGEReason = EProject_JHealthGEObservationFailure::ScopeLimit;
	uint32 SuppressedHealthGEDepth = 0;
	uint64 HealthGEObservationFailures = 0;
	void RecordHealthGEObservationFailure();
	void FinalizeExecutedAttribute(const FGameplayEffectModCallbackData& Data);
	void ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const;
};
