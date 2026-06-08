#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Project_JCombatIntroComponent.generated.h"

class ACharacter;
class UAnimMontage;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FProject_JCombatIntroEndedSignature, UAnimMontage*, Montage, bool, bInterrupted);

UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JCombatIntroComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JCombatIntroComponent();

	UPROPERTY(BlueprintAssignable, Category = "Combat|Intro")
	FProject_JCombatIntroEndedSignature OnCombatIntroEnded;

	bool PlayIntro(ACharacter& CharacterOwner, UAnimMontage* Montage, float PlayRate);
	void CancelIntro(ACharacter& CharacterOwner, UAnimMontage* FallbackMontage);

	UFUNCTION(BlueprintPure, Category = "Combat|Intro")
	bool IsPlayingIntro() const { return bIsPlayingIntro; }

	UFUNCTION(BlueprintPure, Category = "Combat|Intro")
	bool IsPendingCombatMode() const { return bPendingCombatMode; }

	void ClearPendingCombatMode() { bPendingCombatMode = false; }

private:
	UFUNCTION()
	void HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveIntroMontage = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Intro", meta = (AllowPrivateAccess = "true"))
	bool bIsPlayingIntro = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Intro", meta = (AllowPrivateAccess = "true"))
	bool bPendingCombatMode = false;
};
