#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "GameplayTagContainer.h"
#include "Combat/Project_JCombatPresentationSet.h"
#include "Project_JAnimNotifyState_CombatPresentationCue.generated.h"

/** Authored timing/purpose producer. The character component owns exact cosmetic and warning lifetime. */
UCLASS(meta = (DisplayName = "Project J Combat Presentation Cue"))
class PROJECT_JCHARACTER_API UProject_JAnimNotifyState_CombatPresentationCue : public UAnimNotifyState
{
	GENERATED_BODY()

public:
    /** Native authoring validation reads the one timing producer classification. */
    bool IsGameplayWarning() const { return Purpose == EProject_JCombatCuePurpose::GameplayWarning; }
    FGameplayTag GetCueTag() const { return CueTag; }
	virtual void BeginDestroy() override;
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FGameplayTag CueTag;
    /** Producer-side meaning survives a cold/missing cosmetic profile. No gameplay damage geometry is inferred. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
    EProject_JCombatCuePurpose Purpose = EProject_JCombatCuePurpose::Cosmetic;
	struct FCueLease
	{
		TWeakObjectPtr<class UProject_JCombatPresentationComponent> Presentation;
		uint64 Token = 0;
		// Nonzero claim with Token0 is an acquire in progress, not a stale lease.
		uint64 Claim = 0;
	};
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, TMap<int32, FCueLease>> Leases;
private:
    friend class FProjectJCueLeaseOwnershipTest;
    friend class FProjectJGameplayWarningTest;
    void PruneCollectedMeshes();
    static void EndDetachedLease(const FCueLease& Lease);
    FDelegateHandle PostGCHandle;
    uint64 NextLeaseClaim = 0;
};
