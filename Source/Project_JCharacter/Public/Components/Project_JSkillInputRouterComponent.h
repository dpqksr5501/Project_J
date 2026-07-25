#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif
#include "Project_JSkillInputRouterComponent.generated.h"

class AProject_JPlayerCharacter;
class UInputAction;

UENUM(BlueprintType)
enum class EProject_JSkillInputButton : uint8
{
	LMB,
	RMB
};

USTRUCT(BlueprintType)
struct FProject_JSkillInputChord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	bool bRequiresLMB = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	bool bRequiresRMB = false;

	/** Legacy any-modifier condition. Prefer RequiredModifierTags for new chords. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (DeprecatedProperty, DeprecationMessage = "Use Required Modifier Tags instead."))
	bool bRequiresModifier = false;

	/** Every modifier tag that must be held for this chord. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input|Modifiers")
	FGameplayTagContainer RequiredModifierTags;

	/** Any held modifier tag here prevents this chord from matching. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input|Modifiers")
	FGameplayTagContainer BlockedModifierTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	FGameplayTag InputTag;
};

/** One held modifier key/action represented as a gameplay tag. */
USTRUCT(BlueprintType)
struct FProject_JSkillModifierBinding
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Modifiers")
	TObjectPtr<UInputAction> InputAction = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Modifiers")
	FGameplayTag ModifierTag;
};

/** A direct Enhanced Input action mapped to a gameplay skill input tag. */
USTRUCT(BlueprintType)
struct FProject_JDirectSkillInputBinding
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Skills")
	TObjectPtr<UInputAction> InputAction = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Skills")
	FGameplayTag InputTag;

	/** Higher values win when the same InputAction has multiple modifier variants. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Skills")
	int32 Priority = 0;

	/** Every modifier tag that must be held before this direct skill is dispatched. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Modifiers")
	FGameplayTagContainer RequiredModifierTags;

	/** Any held modifier tag here prevents this direct skill from being dispatched. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Modifiers")
	FGameplayTagContainer BlockedModifierTags;
};

UCLASS(BlueprintType)
class PROJECT_JCHARACTER_API UProject_JSkillInputMappingData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Maximum interval between LMB/RMB presses for a simultaneous two-button chord. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Skills", meta = (ClampMin = "0.0", ClampMax = "0.25", UIMin = "0.0", UIMax = "0.15"))
	float SimultaneousChordGraceSeconds = 0.08f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Skills", meta = (TitleProperty = "InputTag"))
	TArray<FProject_JSkillInputChord> Chords;

	/** Q/R/T and other direct skill actions. Each player job references one mapping asset. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Skills", meta = (TitleProperty = "InputTag"))
	TArray<FProject_JDirectSkillInputBinding> DirectSkillBindings;

	/** Shift/Ctrl/Alt and other modifier actions available to this job's chords. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Modifiers", meta = (TitleProperty = "ModifierTag"))
	TArray<FProject_JSkillModifierBinding> ModifierBindings;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};

/**
 * Resolves raw combat input state into GAS InputTags.
 *
 * The router deliberately stops at input intent. Ability activation, montage policy,
 * damage, and locomotion remain owned by GAS, combat, and animation systems.
 */
UCLASS(ClassGroup=(Input), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JSkillInputRouterComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JSkillInputRouterComponent();

	void Initialize(AProject_JPlayerCharacter* InPlayerCharacter);

	UFUNCTION(BlueprintCallable, Category = "Input|Skills")
	void SetModifierHeld(bool bHeld);

	UFUNCTION(BlueprintCallable, Category = "Input|Modifiers")
	void HandleModifierPressed(FGameplayTag ModifierTag);

	UFUNCTION(BlueprintCallable, Category = "Input|Modifiers")
	void HandleModifierReleased(FGameplayTag ModifierTag);

	bool AreModifierTagsMatched(const FGameplayTagContainer& RequiredModifierTags, const FGameplayTagContainer& BlockedModifierTags) const;

	UFUNCTION(BlueprintCallable, Category = "Input|Skills")
	void HandleButtonPressed(EProject_JSkillInputButton Button);

	UFUNCTION(BlueprintCallable, Category = "Input|Skills")
	void HandleButtonReleased(EProject_JSkillInputButton Button);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input|Skills", meta = (TitleProperty = "InputTag"))
	TArray<FProject_JSkillInputChord> Chords;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input|Skills")
	TObjectPtr<UProject_JSkillInputMappingData> InputMappingData = nullptr;

private:
	const TArray<FProject_JSkillInputChord>& GetEffectiveChords();
	float GetEffectiveSimultaneousChordGraceSeconds() const;
	void BuildDefaultChordsIfNeeded();
	FGameplayTag ResolveInputTagForButton(EProject_JSkillInputButton Button, bool bIncludeCombinedChords = true);
	FGameplayTag ResolveCombinedInputTag();
	bool HasCombinedChordForButton(EProject_JSkillInputButton Button);
	void StartChordGracePeriod(EProject_JSkillInputButton Button);
	void FlushPendingChordButton();
	void DispatchResolvedButtonInput(EProject_JSkillInputButton Button, bool bIncludeCombinedChords);
	void ReleaseActiveChordIfReady();
	bool DoesChordMatchButton(const FProject_JSkillInputChord& Chord, EProject_JSkillInputButton Button) const;
	void ReleaseActiveTagForButton(EProject_JSkillInputButton Button);

	UPROPERTY(Transient)
	TObjectPtr<AProject_JPlayerCharacter> BoundPlayerCharacter = nullptr;

	bool bLMBHeld = false;
	bool bRMBHeld = false;
	bool bModifierHeld = false;
	FGameplayTagContainer ActiveModifierTags;

	FGameplayTag ActiveLMBInputTag;
	FGameplayTag ActiveRMBInputTag;
	FGameplayTag ActiveCombinedInputTag;
	FTimerHandle PendingChordTimerHandle;
	EProject_JSkillInputButton PendingChordButton = EProject_JSkillInputButton::LMB;
	bool bHasPendingChordButton = false;
};
