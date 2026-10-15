#include "Components/Project_JNPCActivationComponent.h"
#include "Components/Project_JNPCActionComponent.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "Project_JNPCCharacter.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

void UProject_JNPCActivationComponent::BeginPlay()
{
	bEnding = false;
	Super::BeginPlay();
}

bool UProject_JNPCActivationComponent::ActivateNPC(const UProject_JNPCActivationDefinition* Definition,
	UProject_JTargetScoringComponent* Scoring, UProject_JNPCActionComponent* Action, int32 TeamId)
{
	check(IsInGameThread());
	auto* NPC = Cast<AProject_JNPCCharacter>(GetOwner());
	if (bChanging || bEnding || IsBeingDestroyed() || ActionToken || !IsRegistered() || !GetWorld() || GetWorld()->bIsTearingDown || !IsValid(Definition) || Definition->AbilityLevel < 1 ||
		!IsValid(NPC) || !NPC->HasAuthority() || NPC->IsActorBeingDestroyed() || !IsValid(Scoring) || !IsValid(Action) ||
		Scoring->GetOwner() != NPC || Action->GetOwner() != NPC || !Scoring->IsRegistered() || !Action->IsRegistered() ||
		Scoring->IsBatchedNPCDecisionRegistered() || Action->GetActionState() != EProjectJNPCActionState::Disabled || Action->HasPendingResume()) { return false; }
	auto* ASC = NPC->GetAbilitySystemComponent();
	if (!IsValid(ASC) || ASC->IsBeingDestroyed() || ASC->GetAvatarActor() != NPC) { return false; }
	const TStrongObjectPtr<ThisClass> PinnedThis(this);
	const TStrongObjectPtr<const UProject_JNPCActivationDefinition> PinnedDefinition(Definition);
	const TStrongObjectPtr<AProject_JNPCCharacter> PinnedNPC(NPC);
	const TStrongObjectPtr<UAbilitySystemComponent> PinnedASC(ASC);
	const TStrongObjectPtr<UProject_JTargetScoringComponent> PinnedScoring(Scoring);
	const TStrongObjectPtr<UProject_JNPCActionComponent> PinnedAction(Action);
	const auto AttackClass = Definition->AttackAbility;
	const int32 AbilityLevel = Definition->AbilityLevel;
	const auto* Ability = Definition->AttackAbility ? Definition->AttackAbility->GetDefaultObject<UGameplayAbility>() : nullptr;
	if (Ability && !UProject_JNPCActionComponent::SupportsAttackAbility(Ability)) { return false; }
	TGuardValue<bool> Guard(bChanging, true);
	if (!Scoring->StartBatchedNPCDecisions(TeamId)) { return false; }
	OwnedScoring = Scoring; ScoringRevision = Scoring->GetBatchRegistrationRevision();
	OwnedASC = ASC; OwnedAction = Action;
	const auto IsCurrent = [&]
	{
		return !bEnding && !IsBeingDestroyed() && IsRegistered() && IsValid(NPC) && !NPC->IsActorBeingDestroyed()
			&& GetOwner() == NPC && NPC->HasAuthority() && GetWorld() && !GetWorld()->bIsTearingDown
			&& IsValid(ASC) && !ASC->IsBeingDestroyed() && NPC->GetAbilitySystemComponent() == ASC && ASC->GetAvatarActor() == NPC
			&& IsValid(Scoring) && !Scoring->IsBeingDestroyed() && Scoring->GetOwner() == NPC && Scoring->IsRegistered()
			&& Scoring->IsBatchedNPCDecisionRegistered() && Scoring->GetBatchRegistrationRevision() == ScoringRevision
			&& IsValid(Action) && !Action->IsBeingDestroyed() && Action->GetOwner() == NPC && Action->IsRegistered();
	};
	if (IsCurrent() && Ability) { OwnedGrant = ASC->GiveAbility(FGameplayAbilitySpec(AttackClass, AbilityLevel, INDEX_NONE, this)); }
	if (!IsCurrent() || (Ability && !OwnedGrant.IsValid()) || !Action->StartActions(Scoring, OwnedGrant))
	{
		ReleaseOwnedActivation(); return false;
	}
	ActionToken = Action->GetOwnershipToken();
	if (!IsCurrent() || !ActionToken) { ReleaseOwnedActivation(); return false; }
	return true;
}

void UProject_JNPCActivationComponent::ReleaseOwnedActivation()
{
	const TStrongObjectPtr<ThisClass> PinnedThis(this);
	// Detach first. Ability/scoring callbacks cannot release a subsequent activation.
	const TStrongObjectPtr<UProject_JNPCActionComponent> Action(OwnedAction.Get());
	const TStrongObjectPtr<UProject_JTargetScoringComponent> Scoring(OwnedScoring.Get());
	const TStrongObjectPtr<UAbilitySystemComponent> ASC(OwnedASC.Get());
	const TStrongObjectPtr<AActor> PinnedOwner(GetOwner());
	const TStrongObjectPtr<AActor> PinnedASCOwner(ASC ? ASC->GetOwnerActor() : nullptr);
	const auto Grant = OwnedGrant; const auto Token = ActionToken; const auto Revision = ScoringRevision;
	OwnedAction.Reset(); OwnedScoring.Reset(); OwnedASC.Reset(); OwnedGrant = {}; ActionToken = ScoringRevision = 0;
	if (IsValid(Action.Get()) && !Action->IsBeingDestroyed()) { Action->StopActionsIfOwned(Token); }
	if (IsValid(ASC.Get()) && !ASC->IsBeingDestroyed() && Grant.IsValid())
	{
		ASC->CancelAbilityHandle(Grant);
		// Unregister/DestroyActiveState already retires a destroyed ASC's grants.
		if (IsValid(ASC.Get()) && !ASC->IsBeingDestroyed()) { ASC->ClearAbility(Grant); }
	}
	if (IsValid(Scoring.Get()) && !Scoring->IsBeingDestroyed() && Scoring->GetBatchRegistrationRevision() == Revision) { Scoring->StopBatchedNPCDecisions(); }
}

void UProject_JNPCActivationComponent::DeactivateNPC()
{
	if (bChanging) { return; }
	const TStrongObjectPtr<ThisClass> PinnedThis(this);
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
