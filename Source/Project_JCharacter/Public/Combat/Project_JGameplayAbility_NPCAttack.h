#pragma once
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Abilities/GameplayAbility.h"
#include "Project_JGameplayAbility_NPCAttack.generated.h"

class UProject_JAttackDefinition;
class USkeletalMeshComponent;
class ACharacter;
class AProject_JNPCCharacter;
/** In-place or unwarped ground root-motion attack using engine CharacterMovement and existing hit/VFX notifies. */
UCLASS()
class PROJECT_JCHARACTER_API UProject_JGameplayAbility_NPCAttack : public UGameplayAbility
{
	GENERATED_BODY()
public:
	UProject_JGameplayAbility_NPCAttack();
	/** Configure the granted server instance without editing the ability asset/CDO. SourceObject supplies AttackDefinition. */
	UFUNCTION(BlueprintCallable, Category="NPC|Attack")
	bool ConfigureHitEvent(FGameplayTag EventTag);
	virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
protected:
	/** Alternatively supply the existing AttackDefinition as the granted spec's SourceObject. */
	UPROPERTY(EditDefaultsOnly, Category="NPC|Attack")
	TObjectPtr<UProject_JAttackDefinition> AttackDefinition;
	UPROPERTY(EditDefaultsOnly, Category="NPC|Attack")
	FGameplayTag HitEventTag;
private:
	friend class FProjectJNPCAttackMovementPolicyTest;
	static bool SupportsMovementPolicy(const UProject_JAttackDefinition& Definition);
	FName ValidateAttackContext(const AProject_JNPCCharacter* NPC, const UProject_JAttackDefinition* Definition) const;
	void LogAttackRejection(FName Reason) const;
	UProject_JAttackDefinition* ResolveAttack(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo) const;
	UFUNCTION() void OnCompleted();
	UFUNCTION() void OnInterrupted();
	UFUNCTION() void OnHit(FGameplayEventData Payload);
	UFUNCTION() void OnMovementModeChanged(ACharacter* Character, EMovementMode PreviousMode, uint8 PreviousCustomMode);
	UPROPERTY(Transient) TObjectPtr<UProject_JAttackDefinition> ActiveDefinition;
	TWeakObjectPtr<AActor> LockedTarget;
	TWeakObjectPtr<USkeletalMeshComponent> AttackMesh;
	TWeakObjectPtr<ACharacter> AttackCharacter;
	uint8 SavedVisibility = 0;
	bool bSavedURO = false, bChangedMeshPolicy = false;
	bool bEndingAttack = false;
};
