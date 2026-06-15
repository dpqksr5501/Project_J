#include "Components/Project_JSkillInputRouterComponent.h"

#include "Project_JGameplayTags.h"
#include "Project_JPlayerCharacter.h"

#if WITH_EDITOR
#include "Validation/Project_JDataValidation.h"
#endif

#if WITH_EDITOR
EDataValidationResult UProject_JSkillInputMappingData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	bool bHasError = Result == EDataValidationResult::Invalid;

	TSet<FGameplayTag> SeenInputTags;
	for (int32 ChordIndex = 0; ChordIndex < Chords.Num(); ++ChordIndex)
	{
		const FProject_JSkillInputChord& Chord = Chords[ChordIndex];
		if (!Chord.bRequiresPrimary && !Chord.bRequiresSecondary)
		{
			Project_J::DataValidation::AddError(Context, bHasError, FText::Format(
				NSLOCTEXT("ProjectJSkillInputMappingData", "ChordNoButton", "Chords[{0}] does not require a primary or secondary button."),
				FText::AsNumber(ChordIndex)));
		}

		if (!Chord.InputTag.IsValid())
		{
			Project_J::DataValidation::AddError(Context, bHasError, FText::Format(
				NSLOCTEXT("ProjectJSkillInputMappingData", "ChordNoInputTag", "Chords[{0}] has no InputTag."),
				FText::AsNumber(ChordIndex)));
			continue;
		}

		if (SeenInputTags.Contains(Chord.InputTag))
		{
			Project_J::DataValidation::AddWarning(Context, FText::Format(
				NSLOCTEXT("ProjectJSkillInputMappingData", "DuplicateChordInputTag", "InputTag '{0}' appears in more than one chord. This is valid only if priorities and button requirements are intentional."),
				FText::FromString(Chord.InputTag.ToString())));
		}
		SeenInputTags.Add(Chord.InputTag);
	}

	if (Chords.IsEmpty())
	{
		Project_J::DataValidation::AddWarning(Context, NSLOCTEXT("ProjectJSkillInputMappingData", "EmptyMapping", "No skill input chords are configured."));
	}

	return Project_J::DataValidation::MakeResult(Result, bHasError);
}
#endif

UProject_JSkillInputRouterComponent::UProject_JSkillInputRouterComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UProject_JSkillInputRouterComponent::Initialize(AProject_JPlayerCharacter* InPlayerCharacter)
{
	BoundPlayerCharacter = InPlayerCharacter;
	GetEffectiveChords();
}

void UProject_JSkillInputRouterComponent::SetModifierHeld(bool bHeld)
{
	bModifierHeld = bHeld;
}

void UProject_JSkillInputRouterComponent::HandleButtonPressed(EProject_JSkillInputButton Button)
{
	if (Button == EProject_JSkillInputButton::Primary)
	{
		bPrimaryHeld = true;
	}
	else
	{
		bSecondaryHeld = true;
	}

	const FGameplayTag InputTag = ResolveInputTagForButton(Button);
	if (!InputTag.IsValid() || !BoundPlayerCharacter)
	{
		return;
	}

	if (Button == EProject_JSkillInputButton::Primary)
	{
		ActivePrimaryInputTag = InputTag;
	}
	else
	{
		ActiveSecondaryInputTag = InputTag;
	}

	BoundPlayerCharacter->HandleSkillInputTagPressed(InputTag);
}

void UProject_JSkillInputRouterComponent::HandleButtonReleased(EProject_JSkillInputButton Button)
{
	ReleaseActiveTagForButton(Button);

	if (Button == EProject_JSkillInputButton::Primary)
	{
		bPrimaryHeld = false;
	}
	else
	{
		bSecondaryHeld = false;
	}
}

const TArray<FProject_JSkillInputChord>& UProject_JSkillInputRouterComponent::GetEffectiveChords()
{
	if (InputMappingData && !InputMappingData->Chords.IsEmpty())
	{
		return InputMappingData->Chords;
	}

	BuildDefaultChordsIfNeeded();
	return Chords;
}

void UProject_JSkillInputRouterComponent::BuildDefaultChordsIfNeeded()
{
	if (!Chords.IsEmpty())
	{
		return;
	}

	const FProject_JGameplayTags& GameplayTags = FProject_JGameplayTags::Get();

	FProject_JSkillInputChord PrimaryChord;
	PrimaryChord.bRequiresPrimary = true;
	PrimaryChord.bRequiresSecondary = false;
	PrimaryChord.bRequiresModifier = false;
	PrimaryChord.Priority = 0;
	PrimaryChord.InputTag = GameplayTags.InputTag_Weapon_LightAttack;
	Chords.Add(PrimaryChord);

	FProject_JSkillInputChord SecondaryChord;
	SecondaryChord.bRequiresPrimary = false;
	SecondaryChord.bRequiresSecondary = true;
	SecondaryChord.bRequiresModifier = false;
	SecondaryChord.Priority = 0;
	SecondaryChord.InputTag = GameplayTags.InputTag_Weapon_HeavyAttack;
	Chords.Add(SecondaryChord);

	FProject_JSkillInputChord ModifiedPrimaryChord;
	ModifiedPrimaryChord.bRequiresPrimary = true;
	ModifiedPrimaryChord.bRequiresSecondary = false;
	ModifiedPrimaryChord.bRequiresModifier = true;
	ModifiedPrimaryChord.Priority = 10;
	ModifiedPrimaryChord.InputTag = GameplayTags.InputTag_Weapon_HeavyAttack;
	Chords.Add(ModifiedPrimaryChord);
}

FGameplayTag UProject_JSkillInputRouterComponent::ResolveInputTagForButton(EProject_JSkillInputButton Button)
{
	const TArray<FProject_JSkillInputChord>& EffectiveChords = GetEffectiveChords();

	const FProject_JSkillInputChord* BestChord = nullptr;
	for (const FProject_JSkillInputChord& Chord : EffectiveChords)
	{
		if (!Chord.InputTag.IsValid() || !DoesChordMatchButton(Chord, Button))
		{
			continue;
		}

		if (!BestChord || Chord.Priority > BestChord->Priority)
		{
			BestChord = &Chord;
		}
	}

	return BestChord ? BestChord->InputTag : FGameplayTag();
}

bool UProject_JSkillInputRouterComponent::DoesChordMatchButton(const FProject_JSkillInputChord& Chord, EProject_JSkillInputButton Button) const
{
	if (Button == EProject_JSkillInputButton::Primary && !Chord.bRequiresPrimary)
	{
		return false;
	}

	if (Button == EProject_JSkillInputButton::Secondary && !Chord.bRequiresSecondary)
	{
		return false;
	}

	return
		(!Chord.bRequiresPrimary || bPrimaryHeld) &&
		(!Chord.bRequiresSecondary || bSecondaryHeld) &&
		(!Chord.bRequiresModifier || bModifierHeld);
}

void UProject_JSkillInputRouterComponent::ReleaseActiveTagForButton(EProject_JSkillInputButton Button)
{
	if (!BoundPlayerCharacter)
	{
		return;
	}

	FGameplayTag& ActiveInputTag = Button == EProject_JSkillInputButton::Primary ? ActivePrimaryInputTag : ActiveSecondaryInputTag;
	if (ActiveInputTag.IsValid())
	{
		BoundPlayerCharacter->HandleSkillInputTagReleased(ActiveInputTag);
		ActiveInputTag = FGameplayTag();
	}
}
