#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "GameplayTagContainer.h"
#include "Project_JAnimNotifyState_MeleeHit.generated.h"

class USkeletalMeshComponent;

UCLASS(Blueprintable, meta = (DisplayName = "Melee Hit Trace"))
class PROJECT_JCHARACTER_API UProject_JAnimNotifyState_MeleeHit : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UProject_JAnimNotifyState_MeleeHit();

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

protected:
	friend class FProjectJCanonicalMeleeTraceTest;
	friend class FProjectJCanonicalMeleeTraceTest;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Trace")
	FName SocketName = FName("WeaponSocket_R");

	/**
	 * Deprecated asset field retained for existing montage notifies. Gameplay
	 * hit traces always use SocketName on the Leader mesh. Cosmetic effects can
	 * still attach to the presentation weapon through WeaponPresentationComponent.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hit Trace|Weapon Presentation", meta = (DeprecatedProperty, DeprecationMessage = "Gameplay hit traces always use the Leader mesh socket"))
	bool bUseWeaponPresentationSocket = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hit Trace|Weapon Presentation", meta = (DeprecatedProperty, DeprecationMessage = "Use cosmetic weapon presentation sockets for effects only"))
	FName WeaponSocketName = TEXT("WeaponHit_Tip");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Trace")
	float TraceRadius = 40.0f;

	/** Event tag to send to the instigator when a hit is registered. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Trace")
	FGameplayTag HitEventTag;

	/** Per-mesh state: notify objects are shared by animation assets, so a single
	 * previous-position field would leak traces between characters. */
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, FVector> PreviousSocketLocations;

	FVector ResolveTraceLocation(USkeletalMeshComponent* MeshComp) const;
};
