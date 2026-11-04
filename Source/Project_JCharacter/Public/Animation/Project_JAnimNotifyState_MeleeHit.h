#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "GameplayTagContainer.h"
#include "Combat/Project_JMeleeTrajectory.h"
#include "Project_JAnimNotifyState_MeleeHit.generated.h"

class USkeletalMeshComponent;
class UProject_JAttackDefinition;

UCLASS(Blueprintable, meta = (DisplayName = "Melee Hit Trace"))
class PROJECT_JCHARACTER_API UProject_JAnimNotifyState_MeleeHit : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UProject_JAnimNotifyState_MeleeHit();
	virtual void BeginDestroy() override;

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	/** Native active-attack pose capture uses the same canonical point formula. */
	FName GetTraceSocketName() const { return SocketName; }
	FVector ResolveTraceLocation(USkeletalMeshComponent* MeshComp, const UProject_JAttackDefinition* AttackDefinition) const;

protected:
	friend class FProjectJCanonicalMeleeTraceTest;
	friend class FProjectJCanonicalBladeTrajectoryTest;
	friend class FProjectJSpatialDiscontinuityTraceTest;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Trace")
	FName SocketName = FName("WeaponSocket_R");

	/**
	 * Deprecated asset field retained for existing montage notifies. Gameplay
	 * hit traces use the Leader SocketName plus the active attack's optional tip
	 * offset. Cosmetic effects can attach to the presentation weapon separately.
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
	struct FTraceState
	{
		FVector PreviousLocation = FVector::ZeroVector;
		TWeakObjectPtr<class UProject_JCombatHitValidationComponent> Validation;
		uint64 WindowToken = 0;
		uint64 SpatialRevision = 0;
		float ConsumedMontageTime = 0;
		bool bTemporalPose = false;
		bool bAuthoredPath = false;
		TWeakObjectPtr<const UProject_JAttackDefinition> AuthoredAttack;
		FProject_JMeleeTrajectory AuthoredPath;
	};
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, TMap<int32, FTraceState>> TraceStates;
	void PruneCollectedMeshes();
	void TickAuthoredPath(USkeletalMeshComponent* Mesh,const FAnimNotifyEventReference& Reference,FTraceState& State,
		class UProject_JCombatHitValidationComponent* Validation,const UProject_JAttackDefinition* Attack);
	FDelegateHandle PostGCHandle;

	FVector ResolveTraceLocation(USkeletalMeshComponent* MeshComp) const;
};
