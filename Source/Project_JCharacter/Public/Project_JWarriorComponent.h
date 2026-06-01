// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Project_JCombatComponent.h"
#include "Project_JWarriorComponent.generated.h"

class UAnimMontage;
class UProject_JWeaponAnimProfile;
struct FGameplayTag;
struct FProject_JWeaponAttackSpec;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JWarriorComponent : public UProject_JCombatComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UProject_JWarriorComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

	// Called when the game ends
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	/** Spawns and equips the sword actor onto the owner character */
	virtual void EquipWeapon() override;

	/** Unequips and destroys the sword actor */
	virtual void UnequipWeapon() override;

	/** Triggers a sword combo attack */
	virtual void Attack() override;

	/** Performs the sphere collision trace for the sword attack */
	UFUNCTION(BlueprintCallable, Category = "Warrior|Combat")
	void DoAttackTrace(FName DamageSourceBone);

	/** Moves the combo to the next section */
	UFUNCTION(BlueprintCallable, Category = "Warrior|Combat")
	void CheckCombo();

	/** Resets the combo count and states */
	UFUNCTION(BlueprintCallable, Category = "Warrior|Combat")
	void ResetCombo();

	UFUNCTION(BlueprintPure, Category = "Warrior|Combat")
	bool IsPrototypeAttacking() const { return bIsAttacking; }

protected:
	/** Callback when the attack montage completes or is interrupted */
	void OnAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted);
	bool TryActivatePrimaryAttackAbility() const;
	bool CanStartPrototypeAttack(const ACharacter* Owner, const UAnimMontage* AttackMontage, const TArray<FName>& ComboSections) const;
	void QueuePrototypeComboInput(float CurrentTime);
	void BeginPrototypeAttack(UAnimMontage* AttackMontage, const TArray<FName>& ComboSections);
	void ClearPrototypeAttackState();
	void EndPrototypeAttack(bool bStopMontage);
	const UProject_JWeaponAnimProfile* GetCurrentWeaponAnimProfile() const;
	TSubclassOf<AActor> GetEffectiveWeaponClass() const;
	FName GetEffectiveWeaponSocketName() const;
	UAnimMontage* GetEffectiveAttackMontage() const;
	const TArray<FName>& GetEffectiveComboSectionNames() const;
	FGameplayTag GetEffectivePrimaryAttackAbilityTag() const;
	FProject_JWeaponAttackSpec GetEffectivePrimaryAttackSpec() const;

protected:
	/** Weapon Actor class to spawn */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Warrior|Weapon")
	TSubclassOf<AActor> WeaponClass;

	/** Socket to attach the weapon to */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Warrior|Weapon")
	FName WeaponSocketName = FName("WeaponSocket_R");

	/** Spawned Weapon Actor reference */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Warrior|Weapon")
	AActor* SpawnedWeapon = nullptr;

	/** Combo attack montage for the sword */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Warrior|Attack")
	UAnimMontage* SwordComboMontage = nullptr;

	/** List of section names in the combo montage */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Warrior|Attack")
	TArray<FName> ComboSectionNames;

	/** Max amount of time that may elapse for a non-combo attack input to not be considered stale */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Warrior|Attack")
	float AttackInputCacheTimeTolerance = 1.0f;

	/** Time at which an attack button was last pressed */
	float CachedAttackInputTime = 0.0f;

	/** Base damage dealt by sword attacks */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Warrior|Attack", meta = (ClampMin = 0.0f))
	float MeleeDamage = 1.5f;

	/** Range of the attack trace */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Warrior|Attack", meta = (ClampMin = 0.0f))
	float MeleeTraceDistance = 100.0f;

	/** Radius of the attack trace sphere */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Warrior|Attack", meta = (ClampMin = 0.0f))
	float MeleeTraceRadius = 50.0f;

	/** Knockback impulse applied to hit targets */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Warrior|Attack")
	float MeleeKnockbackImpulse = 200.0f;

	/** Launch impulse applied to hit targets */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Warrior|Attack")
	float MeleeLaunchImpulse = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Warrior|Attack", meta = (ToolTip = "Keep enabled for MMORPG-style authority. Disable only for local prototype experiments."))
	bool bRequireAuthorityForDamageTrace = true;

private:
	/** Current index of the combo attack */
	int32 ComboCount = 0;

	/** True if the character is currently attacking */
	bool bIsAttacking = false;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveAttackMontage = nullptr;

	TArray<FName> ActiveComboSectionNames;

	/** Delegate for montage end */
	FOnMontageEnded OnAttackMontageEndedDelegate;
};
