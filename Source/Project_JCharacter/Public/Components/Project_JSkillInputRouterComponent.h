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

UENUM(BlueprintType)
enum class EProject_JSkillInputButton : uint8
{
	Primary,
	Secondary
};

USTRUCT(BlueprintType)
struct FProject_JSkillInputChord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	bool bRequiresPrimary = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	bool bRequiresSecondary = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	bool bRequiresModifier = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	FGameplayTag InputTag;
};

UCLASS(BlueprintType)
class PROJECT_JCHARACTER_API UProject_JSkillInputMappingData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Skills", meta = (TitleProperty = "InputTag"))
	TArray<FProject_JSkillInputChord> Chords;

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
	void BuildDefaultChordsIfNeeded();
	FGameplayTag ResolveInputTagForButton(EProject_JSkillInputButton Button);
	bool DoesChordMatchButton(const FProject_JSkillInputChord& Chord, EProject_JSkillInputButton Button) const;
	void ReleaseActiveTagForButton(EProject_JSkillInputButton Button);

	UPROPERTY(Transient)
	TObjectPtr<AProject_JPlayerCharacter> BoundPlayerCharacter = nullptr;

	bool bPrimaryHeld = false;
	bool bSecondaryHeld = false;
	bool bModifierHeld = false;

	FGameplayTag ActivePrimaryInputTag;
	FGameplayTag ActiveSecondaryInputTag;
};
