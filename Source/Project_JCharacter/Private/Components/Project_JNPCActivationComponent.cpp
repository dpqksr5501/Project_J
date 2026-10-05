#include "Components/Project_JNPCActivationComponent.h"
#include "Components/Project_JNPCActionComponent.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "Project_JNPCCharacter.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"

bool UProject_JNPCActivationComponent::ActivateNPC(const UProject_JNPCActivationDefinition* Definition,
	UProject_JTargetScoringComponent* Scoring, UProject_JNPCActionComponent* Action, int32 TeamId)
{
	check(IsInGameThread());
	auto* NPC = Cast<AProject_JNPCCharacter>(GetOwner());
	if (bChanging || bEnding || ActionToken || !IsRegistered() || !IsValid(Definition) || Definition->AbilityLevel < 1 ||
		!IsValid(NPC) || !NPC->HasAuthority() || NPC->IsActorBeingDestroyed() || !IsValid(Scoring) || !IsValid(Action) ||
		Scoring->GetOwner() != NPC || Action->GetOwner() != NPC || !Scoring->IsRegistered() || !Action->IsRegistered() ||
		Scoring->IsBatchedNPCDecisionRegistered() || Action->GetActionState() != EProjectJNPCActionState::Disabled || Action->HasPendingResume()) { return false; }
	auto* ASC = NPC->GetAbilitySystemComponent();
	if (!ASC || ASC->GetAvatarActor() != NPC) { return false; }
	const auto* Ability = Definition->AttackAbility ? Definition->AttackAbility->GetDefaultObject<UGameplayAbility>() : nullptr;
	if (Ability && !UProject_JNPCActionComponent::SupportsAttackAbility(Ability)) { return false; }
	TGuardValue<bool> Guard(bChanging, true);
	if (!Scoring->StartBatchedNPCDecisions(TeamId)) { return false; }
	OwnedScoring = Scoring; ScoringRevision = Scoring->GetBatchRegistrationRevision();
	OwnedASC = ASC; OwnedAction = Action;
	if (Ability) { OwnedGrant = ASC->GiveAbility(FGameplayAbilitySpec(Definition->AttackAbility, Definition->AbilityLevel, INDEX_NONE, this)); }
	if (bEnding || (Ability && !OwnedGrant.IsValid()) || !Action->StartActions(Scoring, OwnedGrant))
	{
		ReleaseOwnedActivation(); return false;
	}
	ActionToken = Action->GetOwnershipToken();
	return true;
}

void UProject_JNPCActivationComponent::ReleaseOwnedActivation()
{
	// Detach first. Ability/scoring callbacks cannot release a subsequent activation.
	const auto Action = OwnedAction; const auto Scoring = OwnedScoring; const auto ASC = OwnedASC;
	const auto Grant = OwnedGrant; const auto Token = ActionToken; const auto Revision = ScoringRevision;
	OwnedAction.Reset(); OwnedScoring.Reset(); OwnedASC.Reset(); OwnedGrant = {}; ActionToken = ScoringRevision = 0;
	if (Action.IsValid()) { Action->StopActionsIfOwned(Token); }
	if (ASC.IsValid() && Grant.IsValid()) { ASC->CancelAbilityHandle(Grant); ASC->ClearAbility(Grant); }
	if (Scoring.IsValid() && Scoring->GetBatchRegistrationRevision() == Revision) { Scoring->StopBatchedNPCDecisions(); }
}

void UProject_JNPCActivationComponent::DeactivateNPC()
{
	if (bChanging) { return; }
	TGuardValue<bool> Guard(bChanging, true); ReleaseOwnedActivation();
}
void UProject_JNPCActivationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	bEnding = true; DeactivateNPC(); Super::EndPlay(Reason);
}
void UProject_JNPCActivationComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	bEnding = true; DeactivateNPC(); Super::OnComponentDestroyed(bDestroyingHierarchy);
}
