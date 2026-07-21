// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Project_JCombatCommandSet.h"

#if WITH_EDITOR
#include "Validation/Project_JDataValidation.h"
#endif

namespace
{
bool MatchesOwnerTags(const FGameplayTagContainer& OwnerTags, const FGameplayTagContainer& RequiredTags, const FGameplayTagContainer& BlockedTags)
{
	return OwnerTags.HasAll(RequiredTags) && !OwnerTags.HasAny(BlockedTags);
}

bool DoesCommandMatchHistory(const FProject_JCombatCommandDefinition& Command, const TArray<FProject_JCombatCommandInputEntry>& InputHistory)
{
	const int32 SequenceLength = Command.OrderedInputSequence.Num();
	if (SequenceLength == 0 || InputHistory.Num() < SequenceLength)
	{
		return false;
	}

	const int32 StartIndex = InputHistory.Num() - SequenceLength;
	for (int32 SequenceIndex = 0; SequenceIndex < SequenceLength; ++SequenceIndex)
	{
		if (!InputHistory[StartIndex + SequenceIndex].InputTag.MatchesTagExact(Command.OrderedInputSequence[SequenceIndex]))
		{
			return false;
		}

		if (SequenceIndex > 0)
		{
			const double Interval = InputHistory[StartIndex + SequenceIndex].TimestampSeconds - InputHistory[StartIndex + SequenceIndex - 1].TimestampSeconds;
			if (Interval < 0.0 || Interval > Command.MaxTimeBetweenInputs)
			{
				return false;
			}
		}
	}

	return true;
}
}

const FProject_JCombatCommandDefinition* UProject_JCombatCommandSet::FindBestMatch(
	const TArray<FProject_JCombatCommandInputEntry>& InputHistory,
	const FGameplayTagContainer& OwnerTags) const
{
	const FProject_JCombatCommandDefinition* BestMatch = nullptr;
	for (const FProject_JCombatCommandDefinition& Command : Commands)
	{
		if (!Command.ResultInputTag.IsValid()
			|| Command.OrderedInputSequence.IsEmpty()
			|| !MatchesOwnerTags(OwnerTags, Command.RequiredOwnerTags, Command.BlockedOwnerTags)
			|| !DoesCommandMatchHistory(Command, InputHistory))
		{
			continue;
		}

		if (!BestMatch
			|| Command.GetInputCount() > BestMatch->GetInputCount()
			|| (Command.GetInputCount() == BestMatch->GetInputCount() && Command.Priority > BestMatch->Priority))
		{
			BestMatch = &Command;
		}
	}

	return BestMatch;
}

int32 UProject_JCombatCommandSet::GetMaximumInputCount() const
{
	int32 MaximumInputCount = 0;
	for (const FProject_JCombatCommandDefinition& Command : Commands)
	{
		MaximumInputCount = FMath::Max(MaximumInputCount, Command.GetInputCount());
	}
	return MaximumInputCount;
}

#if WITH_EDITOR
EDataValidationResult UProject_JCombatCommandSet::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	bool bHasError = Result == EDataValidationResult::Invalid;
	TSet<FGameplayTag> SeenCommandTags;

	for (int32 CommandIndex = 0; CommandIndex < Commands.Num(); ++CommandIndex)
	{
		const FProject_JCombatCommandDefinition& Command = Commands[CommandIndex];
		if (!Command.CommandTag.IsValid())
		{
			Project_J::DataValidation::AddError(Context, bHasError, FText::Format(NSLOCTEXT("ProjectJCombatCommandSet", "MissingCommandTag", "Commands[{0}] has no CommandTag."), FText::AsNumber(CommandIndex)));
		}
		else if (SeenCommandTags.Contains(Command.CommandTag))
		{
			Project_J::DataValidation::AddError(Context, bHasError, FText::Format(NSLOCTEXT("ProjectJCombatCommandSet", "DuplicateCommandTag", "CommandTag '{0}' is duplicated."), FText::FromString(Command.CommandTag.ToString())));
		}
		SeenCommandTags.Add(Command.CommandTag);

		if (Command.OrderedInputSequence.IsEmpty())
		{
			Project_J::DataValidation::AddError(Context, bHasError, FText::Format(NSLOCTEXT("ProjectJCombatCommandSet", "EmptySequence", "Command '{0}' has no ordered input sequence."), FText::FromString(Command.CommandTag.ToString())));
		}
		for (const FGameplayTag& InputTag : Command.OrderedInputSequence)
		{
			if (!InputTag.IsValid())
			{
				Project_J::DataValidation::AddError(Context, bHasError, FText::Format(NSLOCTEXT("ProjectJCombatCommandSet", "InvalidSequenceInput", "Command '{0}' contains an invalid input tag."), FText::FromString(Command.CommandTag.ToString())));
			}
		}
		if (!Command.ResultInputTag.IsValid())
		{
			Project_J::DataValidation::AddError(Context, bHasError, FText::Format(NSLOCTEXT("ProjectJCombatCommandSet", "MissingResult", "Command '{0}' has no ResultInputTag."), FText::FromString(Command.CommandTag.ToString())));
		}
		if (Command.MaxTimeBetweenInputs <= 0.0f)
		{
			Project_J::DataValidation::AddError(Context, bHasError, FText::Format(NSLOCTEXT("ProjectJCombatCommandSet", "InvalidInterval", "Command '{0}' has a non-positive MaxTimeBetweenInputs."), FText::FromString(Command.CommandTag.ToString())));
		}
	}

	for (int32 LeftIndex = 0; LeftIndex < Commands.Num(); ++LeftIndex)
	{
		for (int32 RightIndex = LeftIndex + 1; RightIndex < Commands.Num(); ++RightIndex)
		{
			const FProject_JCombatCommandDefinition& Left = Commands[LeftIndex];
			const FProject_JCombatCommandDefinition& Right = Commands[RightIndex];
			if (Left.OrderedInputSequence == Right.OrderedInputSequence && Left.Priority == Right.Priority && Left.ResultInputTag != Right.ResultInputTag)
			{
				Project_J::DataValidation::AddWarning(Context, FText::Format(NSLOCTEXT("ProjectJCombatCommandSet", "AmbiguousCommand", "Commands '{0}' and '{1}' have equal sequence and priority but different results. Assign an explicit priority."), FText::FromString(Left.CommandTag.ToString()), FText::FromString(Right.CommandTag.ToString())));
			}
		}
	}

	return Project_J::DataValidation::MakeResult(Result, bHasError);
}
#endif
