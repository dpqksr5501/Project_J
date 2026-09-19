#include "Components/Project_JCombatIntroComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"

UProject_JCombatIntroComponent::UProject_JCombatIntroComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UProject_JCombatIntroComponent::PublishState()
{
	bIsPlayingIntro = IsPlayingIntro();
	OnTransitionStateChanged.Broadcast();
}

void UProject_JCombatIntroComponent::ClearPendingCombatMode()
{
	bPendingCombatMode = false;
	PublishState();
}

bool UProject_JCombatIntroComponent::PlayIntro(ACharacter& CharacterOwner, UAnimMontage* Montage, float PlayRate)
{
	return PlayTransition(CharacterOwner, Montage, PlayRate, EProject_JCombatTransitionPhase::Drawing);
}

bool UProject_JCombatIntroComponent::PlayOutro(ACharacter& CharacterOwner, UAnimMontage* Montage, float PlayRate)
{
	return PlayTransition(CharacterOwner, Montage, PlayRate, EProject_JCombatTransitionPhase::Sheathing);
}

bool UProject_JCombatIntroComponent::PlayTransition(ACharacter& CharacterOwner, UAnimMontage* Montage, float PlayRate, EProject_JCombatTransitionPhase NewPhase)
{
	if (bEndingPlay || !Montage || Phase != EProject_JCombatTransitionPhase::Idle || !FMath::IsFinite(PlayRate) || PlayRate <= 0) { return false; }
	auto* Anim = CharacterOwner.GetMesh() ? CharacterOwner.GetMesh()->GetAnimInstance() : nullptr;
	if (!Anim) { return false; }
	const uint64 BeforePlay = TransitionRevision;
	if (CharacterOwner.PlayAnimMontage(Montage, PlayRate) <= 0) { return false; }
	// Montage playback can synchronously invoke callbacks from the previous montage.
	if (bEndingPlay || TransitionRevision != BeforePlay) { return false; }
	Phase = NewPhase;
	bPendingCombatMode = NewPhase == EProject_JCombatTransitionPhase::Drawing;
	ActiveTransitionMontage = Montage;
	MontageOwner = &CharacterOwner;
	const uint64 Revision = ++TransitionRevision;
	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &ThisClass::HandleMontageEnded, Revision);
	Anim->Montage_SetEndDelegate(EndDelegate, Montage);
	PublishState();
	return TransitionRevision == Revision && Phase == NewPhase;
}

void UProject_JCombatIntroComponent::CancelIntro(ACharacter& CharacterOwner, UAnimMontage*)
{
	CancelTransition(CharacterOwner, EProject_JCombatTransitionPhase::Drawing);
}

void UProject_JCombatIntroComponent::CancelOutro(ACharacter& CharacterOwner)
{
	CancelTransition(CharacterOwner, EProject_JCombatTransitionPhase::Sheathing);
}

void UProject_JCombatIntroComponent::CancelTransition(ACharacter& CharacterOwner, EProject_JCombatTransitionPhase ExpectedPhase)
{
	if (Phase != ExpectedPhase || Phase == EProject_JCombatTransitionPhase::Idle) { return; }
	UAnimMontage* Montage = ActiveTransitionMontage;
	const auto PreviousOwner = MontageOwner;
	Phase = EProject_JCombatTransitionPhase::Idle;
	bPendingCombatMode = false;
	ActiveTransitionMontage = nullptr;
	MontageOwner.Reset();
	const uint64 CancelRevision = ++TransitionRevision; // Retire callbacks before stopping playback.
	(PreviousOwner.IsValid() ? PreviousOwner.Get() : &CharacterOwner)->StopAnimMontage(Montage);
	if (TransitionRevision != CancelRevision) { return; }
	PublishState();
	if (bEndingPlay || TransitionRevision != CancelRevision) { return; }
	if (ExpectedPhase == EProject_JCombatTransitionPhase::Drawing) { OnCombatIntroEnded.Broadcast(Montage, true); }
	else { OnCombatOutroEnded.Broadcast(Montage, true); }
}

void UProject_JCombatIntroComponent::HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted, uint64 ExpectedRevision)
{
	if (bEndingPlay || ExpectedRevision != TransitionRevision || Montage != ActiveTransitionMontage) { return; }
	const auto CompletedPhase = Phase;
	Phase = EProject_JCombatTransitionPhase::Idle;
	ActiveTransitionMontage = nullptr;
	MontageOwner.Reset();
	if (bInterrupted) { bPendingCombatMode = false; }
	PublishState();
	if (bEndingPlay || ExpectedRevision != TransitionRevision) { return; }
	if (CompletedPhase == EProject_JCombatTransitionPhase::Drawing) { OnCombatIntroEnded.Broadcast(Montage, bInterrupted); }
	else { OnCombatOutroEnded.Broadcast(Montage, bInterrupted); }
	// An unhandled completion must not leak pending gameplay into a later transition.
	if (ExpectedRevision == TransitionRevision) { ClearPendingCombatMode(); }
}

void UProject_JCombatIntroComponent::StopTransitions()
{
	if (bEndingPlay) { return; }
	bEndingPlay = true;
	if (auto* Character = MontageOwner.Get()) { CancelTransition(*Character, Phase); }
	Phase = EProject_JCombatTransitionPhase::Idle;
	bPendingCombatMode = false;
	ActiveTransitionMontage = nullptr;
	MontageOwner.Reset();
	++TransitionRevision;
	PublishState();
}

void UProject_JCombatIntroComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	StopTransitions();
	Super::EndPlay(Reason);
}

void UProject_JCombatIntroComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	StopTransitions();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}
