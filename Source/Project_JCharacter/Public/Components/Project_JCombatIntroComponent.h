#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Project_JCombatIntroComponent.generated.h"

class ACharacter;
class UAnimMontage;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FProject_JCombatIntroEndedSignature, UAnimMontage*, Montage, bool, bInterrupted);

enum class EProject_JCombatTransitionPhase : uint8 { Idle, Drawing, Sheathing };

UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JCombatIntroComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JCombatIntroComponent();
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

	UPROPERTY(BlueprintAssignable, Category = "Combat|Intro")
	FProject_JCombatIntroEndedSignature OnCombatIntroEnded;
	UPROPERTY(BlueprintAssignable, Category = "Combat|Intro")
	FProject_JCombatIntroEndedSignature OnCombatOutroEnded;
	FSimpleMulticastDelegate OnTransitionStateChanged;

	bool PlayIntro(ACharacter& CharacterOwner, UAnimMontage* Montage, float PlayRate);
	void CancelIntro(ACharacter& CharacterOwner, UAnimMontage* FallbackMontage);
	bool PlayOutro(ACharacter& CharacterOwner, UAnimMontage* Montage, float PlayRate);
	void CancelOutro(ACharacter& CharacterOwner);

	UFUNCTION(BlueprintPure, Category = "Combat|Intro")
	bool IsPlayingIntro() const { return Phase == EProject_JCombatTransitionPhase::Drawing; }
	UFUNCTION(BlueprintPure, Category = "Combat|Intro")
	bool IsPlayingOutro() const { return Phase == EProject_JCombatTransitionPhase::Sheathing; }

	UFUNCTION(BlueprintPure, Category = "Combat|Intro")
	bool IsPendingCombatMode() const { return bPendingCombatMode; }

	void ClearPendingCombatMode();

private:
	friend class FProjectJCombatTransitionOwnershipTest;
	bool PlayTransition(ACharacter& CharacterOwner, UAnimMontage* Montage, float PlayRate, EProject_JCombatTransitionPhase NewPhase);
	void CancelTransition(ACharacter& CharacterOwner, EProject_JCombatTransitionPhase ExpectedPhase);
	void HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted, uint64 ExpectedRevision);
	void StopTransitions();
	void PublishState();
	EProject_JCombatTransitionPhase Phase = EProject_JCombatTransitionPhase::Idle;
	uint64 TransitionRevision = 0;
	bool bEndingPlay = false;
	TWeakObjectPtr<ACharacter> MontageOwner;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveTransitionMontage = nullptr;

	// Compatibility view; execution reads Phase only.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Intro", meta = (AllowPrivateAccess = "true"))
	bool bIsPlayingIntro = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Intro", meta = (AllowPrivateAccess = "true"))
	bool bPendingCombatMode = false;
};
