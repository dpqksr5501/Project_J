#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Project_JCombatStateComponent.generated.h"

class UAbilitySystemComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FProject_JCombatStateTagChangedSignature, FGameplayTag, StateTag, int32, NewCount);

UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JCombatStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JCombatStateComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

	UPROPERTY(BlueprintAssignable, Category = "Combat|State")
	FProject_JCombatStateTagChangedSignature OnCombatStateTagChanged;

	void BindToAbilitySystem(UAbilitySystemComponent* InAbilitySystemComponent);
	void ClearAbilitySystemBinding();

	bool HasStateTag(const FGameplayTag& StateTag) const;
	bool TryActivateAbilityByTag(const FGameplayTag& AbilityTag) const;
	void CancelAbilitiesByTag(const FGameplayTag& AbilityTag) const;

	UFUNCTION(BlueprintPure, Category = "Combat|State")
	bool IsCombatModeActive() const;

	UFUNCTION(BlueprintPure, Category = "Combat|State")
	bool IsAttacking() const;

	UFUNCTION(BlueprintPure, Category = "Combat|State")
	bool IsDodging() const;

	UFUNCTION(BlueprintPure, Category = "Combat|State")
	bool IsHitReacting() const;

	UFUNCTION(BlueprintPure, Category = "Movement|Sprint")
	bool IsSprintTagActive() const;

private:
	UAbilitySystemComponent* GetCurrentAvatarAbilitySystem() const;
	void RegisterStateTagEvents();
	void UnregisterStateTagEvents();
	void HandleStateTagChanged(const FGameplayTag CallbackTag, int32 NewCount);

	UPROPERTY(Transient)
	TWeakObjectPtr<UAbilitySystemComponent> BoundAbilitySystemComponent;

	TMap<FGameplayTag, FDelegateHandle> StateTagEventHandles;
	bool bEnding = false;
};
