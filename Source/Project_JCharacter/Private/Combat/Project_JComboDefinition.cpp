// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Project_JComboDefinition.h"
#include "Combat/Project_JAttackDefinition.h"

#if WITH_EDITOR
#include "Validation/Project_JDataValidation.h"
#endif

namespace
{
bool MatchesComboOwnerTags(const FGameplayTagContainer& OwnerTags, const FGameplayTagContainer& RequiredTags, const FGameplayTagContainer& BlockedTags)
{
	return OwnerTags.HasAll(RequiredTags) && !OwnerTags.HasAny(BlockedTags);
}
}

const FProject_JComboNode* UProject_JComboDefinition::FindNode(const FGameplayTag NodeTag) const
{
	return Nodes.FindByPredicate([NodeTag](const FProject_JComboNode& Node)
	{
		return Node.NodeTag.MatchesTagExact(NodeTag);
	});
}

const FProject_JComboNode* UProject_JComboDefinition::FindStartNode(const FGameplayTag InputTag, const FGameplayTagContainer& OwnerTags) const
{
	return Nodes.FindByPredicate([InputTag, &OwnerTags](const FProject_JComboNode& Node)
	{
		return Node.StartInputTags.HasTagExact(InputTag)
			&& MatchesComboOwnerTags(OwnerTags, Node.RequiredOwnerTags, Node.BlockedOwnerTags);
	});
}

const FProject_JComboTransition* UProject_JComboDefinition::FindTransition(const FProject_JComboNode& FromNode, const FGameplayTag InputTag, const FGameplayTagContainer& OwnerTags) const
{
	return FromNode.Transitions.FindByPredicate([InputTag, &OwnerTags](const FProject_JComboTransition& Transition)
	{
		return Transition.InputTag.MatchesTagExact(InputTag)
			&& MatchesComboOwnerTags(OwnerTags, Transition.RequiredOwnerTags, Transition.BlockedOwnerTags);
	});
}

void UProject_JComboDefinition::GetReferencedInputTags(FGameplayTagContainer& OutInputTags) const
{
	for (const FProject_JComboNode& Node : Nodes)
	{
		OutInputTags.AppendTags(Node.StartInputTags);
		for (const FProject_JComboTransition& Transition : Node.Transitions)
		{
			OutInputTags.AddTag(Transition.InputTag);
		}
	}
}

#if WITH_EDITOR
EDataValidationResult UProject_JComboDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	bool bHasError = Result == EDataValidationResult::Invalid;
	TSet<FGameplayTag> KnownNodeTags;
	bool bHasStartNode = false;

	for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); ++NodeIndex)
	{
		const FProject_JComboNode& Node = Nodes[NodeIndex];
		if (!Node.NodeTag.IsValid())
		{
			Project_J::DataValidation::AddError(Context, bHasError, FText::Format(NSLOCTEXT("ProjectJComboDefinition", "MissingNodeTag", "Nodes[{0}] has no NodeTag."), FText::AsNumber(NodeIndex)));
			continue;
		}

		if (KnownNodeTags.Contains(Node.NodeTag))
		{
			Project_J::DataValidation::AddError(Context, bHasError, FText::Format(NSLOCTEXT("ProjectJComboDefinition", "DuplicateNodeTag", "NodeTag '{0}' is duplicated."), FText::FromString(Node.NodeTag.ToString())));
		}
		KnownNodeTags.Add(Node.NodeTag);
		bHasStartNode |= !Node.StartInputTags.IsEmpty();

		if (!Node.AttackDefinition)
		{
			Project_J::DataValidation::AddError(Context, bHasError, FText::Format(NSLOCTEXT("ProjectJComboDefinition", "MissingAttack", "Node '{0}' requires an AttackDefinition."), FText::FromString(Node.NodeTag.ToString())));
		}

		TSet<FGameplayTag> SeenTransitionInputs;
		for (const FProject_JComboTransition& Transition : Node.Transitions)
		{
			if (!Transition.InputTag.IsValid() || !Transition.TargetNodeTag.IsValid())
			{
				Project_J::DataValidation::AddError(Context, bHasError, FText::Format(NSLOCTEXT("ProjectJComboDefinition", "InvalidTransition", "Node '{0}' has a transition without an InputTag or TargetNodeTag."), FText::FromString(Node.NodeTag.ToString())));
			}
			if (SeenTransitionInputs.Contains(Transition.InputTag))
			{
				Project_J::DataValidation::AddError(Context, bHasError, FText::Format(NSLOCTEXT("ProjectJComboDefinition", "DuplicateTransitionInput", "Node '{0}' has multiple transitions for input '{1}'."), FText::FromString(Node.NodeTag.ToString()), FText::FromString(Transition.InputTag.ToString())));
			}
			SeenTransitionInputs.Add(Transition.InputTag);
		}
	}

	if (!bHasStartNode)
	{
		Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJComboDefinition", "NoStartNode", "The combo graph has no node with StartInputTags."));
	}

	for (const FProject_JComboNode& Node : Nodes)
	{
		for (const FProject_JComboTransition& Transition : Node.Transitions)
		{
			if (Transition.TargetNodeTag.IsValid() && !KnownNodeTags.Contains(Transition.TargetNodeTag))
			{
				Project_J::DataValidation::AddError(Context, bHasError, FText::Format(NSLOCTEXT("ProjectJComboDefinition", "UnknownTransitionTarget", "Node '{0}' transitions to unknown node '{1}'."), FText::FromString(Node.NodeTag.ToString()), FText::FromString(Transition.TargetNodeTag.ToString())));
			}
		}
	}

	return Project_J::DataValidation::MakeResult(Result, bHasError);
}
#endif
