#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Project_JCharacterUIBindingComponent.generated.h"

class UProject_JAbilitySystemComponent;
class UProject_JAttributeSet;
class UProject_JCharacterViewModel;
struct FOnAttributeChangeData;
class APawn;
class AController;

/**
 * Owns character UI-facing attribute bindings.
 *
 * Player characters should not need to know how health/mana changes are pushed into MVVM.
 * Keeping this as a component makes owner-only UI state easier to replace for MMORPG HUD,
 * party frames, inspection, and remote target panels.
 */
UCLASS(ClassGroup=(UI), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JCharacterUIBindingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JCharacterUIBindingComponent();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void BeginPlay() override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

	void InitializeFromAttributes(UProject_JAbilitySystemComponent* InAbilitySystemComponent, UProject_JAttributeSet* InAttributeSet, int32 CharacterLevel);

	UFUNCTION(BlueprintPure, Category = "UI")
	UProject_JCharacterViewModel* GetCharacterViewModel();

	/** Remote panels acquire/release independently; local HUD demand is automatic. */
	UFUNCTION(BlueprintCallable, Category = "UI")
	UProject_JCharacterViewModel* AcquireCharacterViewModel(UObject* Consumer);
	UFUNCTION(BlueprintCallable, Category = "UI")
	void ReleaseCharacterViewModel(UObject* Consumer);
	/** Ends the compatibility getter's persistent request without affecting other panels. */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void ReleaseLegacyViewModelRequest();
	void UpdateCharacterLevel(int32 CharacterLevel);
	UProject_JCharacterViewModel* PeekCharacterViewModel() const { return CharacterViewModel; }

private:
	void RefreshPresentationBinding();
	void StopPresentation();
	UFUNCTION()
	void OnControllerChanged(APawn* Pawn, AController* OldController, AController* NewController);
	TWeakObjectPtr<UProject_JAbilitySystemComponent> SourceAbilitySystem;
	TWeakObjectPtr<UProject_JAttributeSet> SourceAttributes;
	TSet<TWeakObjectPtr<UObject>> Consumers;
	int32 SourceLevel = 1;
	bool bLegacyViewModelRequested = false;
	bool bEndingPlay = false;
	void ClearAttributeBindings();
	void OnHealthChanged(const FOnAttributeChangeData& Data);
	void OnMaxHealthChanged(const FOnAttributeChangeData& Data);
	void OnManaChanged(const FOnAttributeChangeData& Data);
	void OnMaxManaChanged(const FOnAttributeChangeData& Data);

	UPROPERTY(Transient, BlueprintReadOnly, Category = "UI", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProject_JCharacterViewModel> CharacterViewModel = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UProject_JAbilitySystemComponent> BoundAbilitySystemComponent = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UProject_JAttributeSet> BoundAttributeSet = nullptr;

	FDelegateHandle HealthChangedHandle;
	FDelegateHandle MaxHealthChangedHandle;
	FDelegateHandle ManaChangedHandle;
	FDelegateHandle MaxManaChangedHandle;
};
