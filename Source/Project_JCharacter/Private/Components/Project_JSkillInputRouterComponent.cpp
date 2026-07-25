#include "Components/Project_JSkillInputRouterComponent.h"

#include "Project_JGameplayTags.h"
#include "Project_JPlayerCharacter.h"
#include "TimerManager.h"

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
		if (!Chord.bRequiresLMB && !Chord.bRequiresRMB)
		{
			Project_J::DataValidation::AddError(Context, bHasError, FText::Format(
				NSLOCTEXT("ProjectJSkillInputMappingData", "ChordNoButton", "Chords[{0}] does not require LMB or RMB."),
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

	TSet<const UInputAction*> SeenDirectActions;
	TMap<const UInputAction*, TSet<int32>> DirectActionPriorities;
	for (int32 BindingIndex = 0; BindingIndex < DirectSkillBindings.Num(); ++BindingIndex)
	{
		const FProject_JDirectSkillInputBinding& Binding = DirectSkillBindings[BindingIndex];
		if (!Binding.InputAction || !Binding.InputTag.IsValid())
		{
			Project_J::DataValidation::AddError(Context, bHasError, FText::Format(
				NSLOCTEXT("ProjectJSkillInputMappingData", "InvalidDirectBinding", "DirectSkillBindings[{0}] requires both an InputAction and an InputTag."),
				FText::AsNumber(BindingIndex)));
			continue;
		}

		TSet<int32>& PrioritiesForAction = DirectActionPriorities.FindOrAdd(Binding.InputAction);
		if (PrioritiesForAction.Contains(Binding.Priority))
		{
			Project_J::DataValidation::AddWarning(Context, FText::Format(
				NSLOCTEXT("ProjectJSkillInputMappingData", "AmbiguousDirectActionPriority", "DirectSkillBindings[{0}] reuses an InputAction with the same priority. If their modifier conditions overlap, the first entry wins."),
				FText::AsNumber(BindingIndex)));
		}
		PrioritiesForAction.Add(Binding.Priority);
		SeenDirectActions.Add(Binding.InputAction);
	}

	TSet<const UInputAction*> SeenModifierActions;
	TSet<FGameplayTag> SeenModifierTags;
	for (int32 BindingIndex = 0; BindingIndex < ModifierBindings.Num(); ++BindingIndex)
	{
		const FProject_JSkillModifierBinding& Binding = ModifierBindings[BindingIndex];
		if (!Binding.InputAction || !Binding.ModifierTag.IsValid())
		{
			Project_J::DataValidation::AddError(Context, bHasError, FText::Format(
				NSLOCTEXT("ProjectJSkillInputMappingData", "InvalidModifierBinding", "ModifierBindings[{0}] requires both an InputAction and a ModifierTag."),
				FText::AsNumber(BindingIndex)));
			continue;
		}

		if (SeenDirectActions.Contains(Binding.InputAction) || SeenModifierActions.Contains(Binding.InputAction) || SeenModifierTags.Contains(Binding.ModifierTag))
		{
			Project_J::DataValidation::AddError(Context, bHasError, FText::Format(
				NSLOCTEXT("ProjectJSkillInputMappingData", "DuplicateModifierBinding", "ModifierBindings[{0}] reuses an InputAction or ModifierTag already bound by this mapping asset."),
				FText::AsNumber(BindingIndex)));
		}
		SeenModifierActions.Add(Binding.InputAction);
		SeenModifierTags.Add(Binding.ModifierTag);
	}

	if (Chords.IsEmpty() && DirectSkillBindings.IsEmpty() && ModifierBindings.IsEmpty())
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
	bLMBHeld = false;
	bRMBHeld = false;
	bHasPendingChordButton = false;
	ActiveLMBInputTag = FGameplayTag();
	ActiveRMBInputTag = FGameplayTag();
	ActiveCombinedInputTag = FGameplayTag();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PendingChordTimerHandle);
	}
	GetEffectiveChords();
}

void UProject_JSkillInputRouterComponent::SetModifierHeld(bool bHeld)
{
	bModifierHeld = bHeld;
}

void UProject_JSkillInputRouterComponent::HandleModifierPressed(const FGameplayTag ModifierTag)
{
	if (ModifierTag.IsValid())
	{
		ActiveModifierTags.AddTag(ModifierTag);
	}
}

void UProject_JSkillInputRouterComponent::HandleModifierReleased(const FGameplayTag ModifierTag)
{
	if (ModifierTag.IsValid())
	{
		ActiveModifierTags.RemoveTag(ModifierTag);
	}
}

bool UProject_JSkillInputRouterComponent::AreModifierTagsMatched(const FGameplayTagContainer& RequiredModifierTags, const FGameplayTagContainer& BlockedModifierTags) const
{
	return ActiveModifierTags.HasAll(RequiredModifierTags) && !ActiveModifierTags.HasAny(BlockedModifierTags);
}

void UProject_JSkillInputRouterComponent::HandleButtonPressed(EProject_JSkillInputButton Button)
{
	if (Button == EProject_JSkillInputButton::LMB)
	{
		bLMBHeld = true;
	}
	else
	{
		bRMBHeld = true;
	}

	if (!BoundPlayerCharacter)
	{
		return;
	}

	if (bHasPendingChordButton)
	{
		if (PendingChordButton != Button)
		{
			const FGameplayTag CombinedInputTag = ResolveCombinedInputTag();
			if (CombinedInputTag.IsValid())
			{
				if (UWorld* World = GetWorld())
				{
					World->GetTimerManager().ClearTimer(PendingChordTimerHandle);
				}
				bHasPendingChordButton = false;
				ActiveCombinedInputTag = CombinedInputTag;
				BoundPlayerCharacter->HandleSkillInputTagPressed(CombinedInputTag);
				return;
			}
		}

		// The two presses did not form a valid chord (for example because a
		// modifier changed). Preserve both normal button presses instead.
		FlushPendingChordButton();
	}

	const bool bOtherButtonHeld = Button == EProject_JSkillInputButton::LMB ? bRMBHeld : bLMBHeld;
	if (!bOtherButtonHeld && HasCombinedChordForButton(Button))
	{
		StartChordGracePeriod(Button);
		return;
	}

	// A button that is held after its partner has already dispatched a normal
	// attack must remain an independent input, not retroactively become a chord.
	DispatchResolvedButtonInput(Button, false);
}

void UProject_JSkillInputRouterComponent::HandleButtonReleased(EProject_JSkillInputButton Button)
{
	if (Button == EProject_JSkillInputButton::LMB)
	{
		bLMBHeld = false;
	}
	else
	{
		bRMBHeld = false;
	}

	if (ActiveCombinedInputTag.IsValid())
	{
		ReleaseActiveChordIfReady();
		return;
	}

	if (bHasPendingChordButton && PendingChordButton == Button)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(PendingChordTimerHandle);
		}
		bHasPendingChordButton = false;
		// A quick tap should not pay the full grace-period delay.
		DispatchResolvedButtonInput(Button, false);
		ReleaseActiveTagForButton(Button);
		return;
	}

	ReleaseActiveTagForButton(Button);
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

float UProject_JSkillInputRouterComponent::GetEffectiveSimultaneousChordGraceSeconds() const
{
	return InputMappingData ? InputMappingData->SimultaneousChordGraceSeconds : 0.08f;
}

void UProject_JSkillInputRouterComponent::BuildDefaultChordsIfNeeded()
{
	if (!Chords.IsEmpty())
	{
		return;
	}

	const FProject_JGameplayTags& GameplayTags = FProject_JGameplayTags::Get();

	FProject_JSkillInputChord LMBChord;
	LMBChord.bRequiresLMB = true;
	LMBChord.bRequiresRMB = false;
	LMBChord.bRequiresModifier = false;
	LMBChord.Priority = 0;
	LMBChord.InputTag = GameplayTags.InputTag_Weapon_LMB;
	Chords.Add(LMBChord);

	FProject_JSkillInputChord RMBChord;
	RMBChord.bRequiresLMB = false;
	RMBChord.bRequiresRMB = true;
	RMBChord.bRequiresModifier = false;
	RMBChord.Priority = 0;
	RMBChord.InputTag = GameplayTags.InputTag_Weapon_RMB;
	Chords.Add(RMBChord);

	FProject_JSkillInputChord ModifiedPrimaryChord;
	ModifiedPrimaryChord.bRequiresLMB = true;
	ModifiedPrimaryChord.bRequiresRMB = false;
	ModifiedPrimaryChord.bRequiresModifier = true;
	ModifiedPrimaryChord.Priority = 10;
	ModifiedPrimaryChord.InputTag = GameplayTags.InputTag_Weapon_RMB;
	Chords.Add(ModifiedPrimaryChord);
}

FGameplayTag UProject_JSkillInputRouterComponent::ResolveInputTagForButton(const EProject_JSkillInputButton Button, const bool bIncludeCombinedChords)
{
	const TArray<FProject_JSkillInputChord>& EffectiveChords = GetEffectiveChords();

	const FProject_JSkillInputChord* BestChord = nullptr;
	for (const FProject_JSkillInputChord& Chord : EffectiveChords)
	{
		const bool bIsCombinedChord = Chord.bRequiresLMB && Chord.bRequiresRMB;
		if (!Chord.InputTag.IsValid() || (!bIncludeCombinedChords && bIsCombinedChord) || !DoesChordMatchButton(Chord, Button))
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

FGameplayTag UProject_JSkillInputRouterComponent::ResolveCombinedInputTag()
{
	const FProject_JSkillInputChord* BestChord = nullptr;
	for (const FProject_JSkillInputChord& Chord : GetEffectiveChords())
	{
		if (!Chord.InputTag.IsValid() || !Chord.bRequiresLMB || !Chord.bRequiresRMB ||
			!bLMBHeld || !bRMBHeld ||
			(Chord.bRequiresModifier && !(bModifierHeld || !ActiveModifierTags.IsEmpty())) ||
			!AreModifierTagsMatched(Chord.RequiredModifierTags, Chord.BlockedModifierTags))
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

bool UProject_JSkillInputRouterComponent::HasCombinedChordForButton(const EProject_JSkillInputButton Button)
{
	for (const FProject_JSkillInputChord& Chord : GetEffectiveChords())
	{
		if (!Chord.InputTag.IsValid() || !Chord.bRequiresLMB || !Chord.bRequiresRMB ||
			(Button == EProject_JSkillInputButton::LMB && !Chord.bRequiresLMB) ||
			(Button == EProject_JSkillInputButton::RMB && !Chord.bRequiresRMB) ||
			(Chord.bRequiresModifier && !(bModifierHeld || !ActiveModifierTags.IsEmpty())) ||
			!AreModifierTagsMatched(Chord.RequiredModifierTags, Chord.BlockedModifierTags))
		{
			continue;
		}

		return GetEffectiveSimultaneousChordGraceSeconds() > 0.0f;
	}

	return false;
}

void UProject_JSkillInputRouterComponent::StartChordGracePeriod(const EProject_JSkillInputButton Button)
{
	bHasPendingChordButton = true;
	PendingChordButton = Button;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(PendingChordTimerHandle, this, &UProject_JSkillInputRouterComponent::FlushPendingChordButton, GetEffectiveSimultaneousChordGraceSeconds(), false);
	}
	else
	{
		FlushPendingChordButton();
	}
}

void UProject_JSkillInputRouterComponent::FlushPendingChordButton()
{
	if (!bHasPendingChordButton)
	{
		return;
	}

	const EProject_JSkillInputButton ButtonToDispatch = PendingChordButton;
	bHasPendingChordButton = false;
	DispatchResolvedButtonInput(ButtonToDispatch, false);
}

void UProject_JSkillInputRouterComponent::DispatchResolvedButtonInput(const EProject_JSkillInputButton Button, const bool bIncludeCombinedChords)
{
	const FGameplayTag InputTag = ResolveInputTagForButton(Button, bIncludeCombinedChords);
	if (!InputTag.IsValid() || !BoundPlayerCharacter)
	{
		return;
	}

	if (Button == EProject_JSkillInputButton::LMB)
	{
		ActiveLMBInputTag = InputTag;
	}
	else
	{
		ActiveRMBInputTag = InputTag;
	}

	BoundPlayerCharacter->HandleSkillInputTagPressed(InputTag);
}

void UProject_JSkillInputRouterComponent::ReleaseActiveChordIfReady()
{
	if (bLMBHeld || bRMBHeld || !BoundPlayerCharacter || !ActiveCombinedInputTag.IsValid())
	{
		return;
	}

	BoundPlayerCharacter->HandleSkillInputTagReleased(ActiveCombinedInputTag);
	ActiveCombinedInputTag = FGameplayTag();
}

bool UProject_JSkillInputRouterComponent::DoesChordMatchButton(const FProject_JSkillInputChord& Chord, EProject_JSkillInputButton Button) const
{
	if (Button == EProject_JSkillInputButton::LMB && !Chord.bRequiresLMB)
	{
		return false;
	}

	if (Button == EProject_JSkillInputButton::RMB && !Chord.bRequiresRMB)
	{
		return false;
	}

	const bool bHasAnyModifier = bModifierHeld || !ActiveModifierTags.IsEmpty();

	return
		(!Chord.bRequiresLMB || bLMBHeld) &&
		(!Chord.bRequiresRMB || bRMBHeld) &&
		(!Chord.bRequiresModifier || bHasAnyModifier) &&
		AreModifierTagsMatched(Chord.RequiredModifierTags, Chord.BlockedModifierTags);
}

void UProject_JSkillInputRouterComponent::ReleaseActiveTagForButton(EProject_JSkillInputButton Button)
{
	if (!BoundPlayerCharacter)
	{
		return;
	}

	FGameplayTag& ActiveInputTag = Button == EProject_JSkillInputButton::LMB ? ActiveLMBInputTag : ActiveRMBInputTag;
	if (ActiveInputTag.IsValid())
	{
		BoundPlayerCharacter->HandleSkillInputTagReleased(ActiveInputTag);
		ActiveInputTag = FGameplayTag();
	}
}
