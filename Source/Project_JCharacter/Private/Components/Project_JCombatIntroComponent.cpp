#include "Components/Project_JCombatIntroComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/Character.h"

UProject_JCombatIntroComponent::UProject_JCombatIntroComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UProject_JCombatIntroComponent::PlayIntro(ACharacter& CharacterOwner, UAnimMontage* Montage, float PlayRate)
{
	if (!Montage || bIsPlayingIntro)
	{
		return false;
	}

	UAnimInstance* AnimInstance = CharacterOwner.GetMesh() ? CharacterOwner.GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		return false;
	}

	const float Duration = CharacterOwner.PlayAnimMontage(Montage, PlayRate);
	if (Duration <= 0.0f)
	{
		return false;
	}

	bIsPlayingIntro = true;
	bPendingCombatMode = true;
	ActiveIntroMontage = Montage;

	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &UProject_JCombatIntroComponent::HandleMontageEnded);
	AnimInstance->Montage_SetEndDelegate(EndDelegate, Montage);
	return true;
}

void UProject_JCombatIntroComponent::CancelIntro(ACharacter& CharacterOwner, UAnimMontage* FallbackMontage)
{
	UAnimMontage* MontageToStop = ActiveIntroMontage ? ActiveIntroMontage.Get() : FallbackMontage;
	if (bIsPlayingIntro)
	{
		CharacterOwner.StopAnimMontage(MontageToStop);
	}

	bIsPlayingIntro = false;
	bPendingCombatMode = false;
	ActiveIntroMontage = nullptr;
}

void UProject_JCombatIntroComponent::HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage != ActiveIntroMontage)
	{
		return;
	}

	bIsPlayingIntro = false;
	ActiveIntroMontage = nullptr;
	OnCombatIntroEnded.Broadcast(Montage, bInterrupted);
}
