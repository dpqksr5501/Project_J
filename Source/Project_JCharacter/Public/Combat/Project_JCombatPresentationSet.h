#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Project_JCombatPresentationSet.generated.h"

class UNiagaraSystem;

/** Attachment target for a cosmetic combat cue. Gameplay authority never reads this. */
UENUM(BlueprintType)
enum class EProject_JCombatVFXAttachmentTarget : uint8
{
	Weapon,
	CharacterMesh,
	World
};

/** One authored effect at an animation-facing presentation cue. */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JCombatVFXCueDefinition
{
	GENERATED_BODY()

	/** Semantic timing name, for example PresentationCue.Combat.Trail. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX")
	FGameplayTag CueTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX")
	TObjectPtr<UNiagaraSystem> NiagaraSystem = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX")
	EProject_JCombatVFXAttachmentTarget AttachmentTarget = EProject_JCombatVFXAttachmentTarget::Weapon;

	/** Weapon sockets such as the existing Base/Tip sockets are authored on the weapon mesh. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX")
	FName AttachSocketName = NAME_None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX")
	FTransform RelativeTransform = FTransform::Identity;

	/**
	 * Keeps this spawned Niagara component alive until the matching notify state ends
	 * (or until the attack is cancelled). The Niagara system itself must be authored
	 * to loop; this flag owns component lifetime, not Niagara's internal loop mode.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Lifetime", meta = (DisplayName = "Keep Alive Until Notify End"))
	bool bLooping = false;

	/**
	 * Removes any surviving particles at notify end instead of allowing their Niagara
	 * lifetime/fade to finish. Use for trails that must end exactly with the swing.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX|Lifetime", meta = (EditCondition = "bLooping", DisplayName = "Destroy Immediately On Notify End"))
	bool bDestroyImmediatelyOnStop = false;
};

/** All cosmetic cues for one reusable AttackTag. */
UCLASS(BlueprintType, Const)
class PROJECT_JCHARACTER_API UProject_JAttackPresentationProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VFX")
	TArray<FProject_JCombatVFXCueDefinition> Cues;

	const FProject_JCombatVFXCueDefinition* FindCue(FGameplayTag CueTag) const;
};

/** Maps stable gameplay attack identity to client-only presentation data. */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JCombatAttackPresentationEntry
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	FGameplayTag AttackTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TObjectPtr<UProject_JAttackPresentationProfile> Profile = nullptr;
};

/**
 * Base style, advancement, and weapon-skin sets share this type. Later sets
 * override only the cues they explicitly provide; no attack gameplay data is copied.
 */
UCLASS(BlueprintType, Const)
class PROJECT_JCHARACTER_API UProject_JCombatPresentationSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TArray<FProject_JCombatAttackPresentationEntry> AttackPresentations;

	const UProject_JAttackPresentationProfile* FindProfile(FGameplayTag AttackTag) const;
};
