#include "Validation/Project_JCombatWarningAssetValidator.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/Project_JAnimNotifyState_CombatPresentationCue.h"
#include "Misc/DataValidation.h"

bool UProject_JCombatWarningAssetValidator::CanValidateAsset_Implementation(const FAssetData&, UObject* Object, FDataValidationContext&) const
{
	const auto* Sequence = Cast<UAnimSequenceBase>(Object);
	return Sequence && Sequence->Notifies.ContainsByPredicate([](const FAnimNotifyEvent& Event)
	{
		const auto* Cue = Cast<UProject_JAnimNotifyState_CombatPresentationCue>(Event.NotifyStateClass);
		return Cue && Cue->IsGameplayWarning();
	});
}

EDataValidationResult UProject_JCombatWarningAssetValidator::ValidateLoadedAsset_Implementation(const FAssetData&, UObject* Object, FDataValidationContext&)
{
	const auto* Sequence = CastChecked<UAnimSequenceBase>(Object);
	bool bInvalid = false;
	for (int32 Index = 0; Index < Sequence->Notifies.Num(); ++Index)
	{
		const auto& Event = Sequence->Notifies[Index];
		const auto* Cue = Cast<UProject_JAnimNotifyState_CombatPresentationCue>(Event.NotifyStateClass);
		if (!Cue || !Cue->IsGameplayWarning()) continue;
		const auto Fail = [&](const FText& Reason)
		{
			AssetFails(Object, FText::Format(NSLOCTEXT("ProjectJCombatWarningValidator", "InvalidNotify", "Gameplay Warning notify {0}: {1}"),
				FText::AsNumber(Index), Reason));
			bInvalid = true;
		};
		if (!Cue->GetCueTag().IsValid())
			Fail(NSLOCTEXT("ProjectJCombatWarningValidator", "Tag", "a valid cue tag is required."));
		if (!FMath::IsFinite(Event.GetDuration()) || Event.GetDuration() <= 0.f)
			Fail(NSLOCTEXT("ProjectJCombatWarningValidator", "Duration", "duration must be finite and positive."));
		if (!Event.bTriggerOnDedicatedServer)
			Fail(NSLOCTEXT("ProjectJCombatWarningValidator", "Server", "enable Trigger On Dedicated Server so authority publishes the warning."));
		if (Event.NotifyTriggerChance != 1.f)
			Fail(NSLOCTEXT("ProjectJCombatWarningValidator", "Chance", "trigger chance must be 1."));
		if (Event.NotifyFilterType != ENotifyFilterType::NoFiltering || Event.bCanBeFilteredViaRequest)
			Fail(NSLOCTEXT("ProjectJCombatWarningValidator", "Filter", "disable LOD and request filtering for the warning producer."));
	}
	if (!bInvalid) AssetPasses(Object);
	return bInvalid ? EDataValidationResult::Invalid : EDataValidationResult::Valid;
}
