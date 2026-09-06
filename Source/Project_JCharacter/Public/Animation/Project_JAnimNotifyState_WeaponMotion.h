#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Equipment/Project_JWeaponMotionTypes.h"
#include "Project_JAnimNotifyState_WeaponMotion.generated.h"

/**
 * Plays one unified transform profile during this Montage interval.
 *
 * The state bar and its compact transform keys both live in the Montage. It
 * has no gameplay authority and deliberately sends no RPCs.
 */
UCLASS(meta = (DisplayName = "Weapon Motion"))
class PROJECT_JCHARACTER_API UProject_JAnimNotifyState_WeaponMotion : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

	/** One transform timeline instead of independent Float Curves for every axis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Motion", meta = (TitleProperty = "NormalizedTime"))
	TArray<FProject_JWeaponMotionKey> MotionKeys;

	/** Fades from the normal drawn-socket pose into the authored key timeline. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Motion|Transition", meta = (ClampMin = "0.0", Units = "s"))
	float EntryBlendSeconds = 0.08f;

	/** Fades the authored key timeline back to the normal drawn-socket pose before this state ends. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Motion|Transition", meta = (ClampMin = "0.0", Units = "s"))
	float ExitBlendSeconds = 0.08f;

	/**
	 * The primary grip is the attachment driver (normally hand_r). It must not
	 * IK itself back to a weapon that is attached to the same socket, otherwise
	 * the hand and weapon form a small visual feedback loop.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Motion|Hand IK", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PrimaryGripIKAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Motion|Hand IK", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SecondaryGripIKAlpha = 1.0f;

private:
	struct FRuntimeState
	{
		// Presence marks that this mesh successfully entered independent motion.
		// Timing is read directly from the notify event, never accumulated here.
	};

	/** Notify objects are shared by animation assets, so state must be per preview/runtime mesh. */
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, FRuntimeState> RuntimeStates;
};
