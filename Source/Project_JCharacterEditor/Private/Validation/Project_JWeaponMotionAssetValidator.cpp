#include "Validation/Project_JWeaponMotionAssetValidator.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/Project_JAnimNotifyState_WeaponMotion.h"
#include "Misc/DataValidation.h"

bool UProject_JWeaponMotionAssetValidator::CanValidateAsset_Implementation(const FAssetData&, UObject* Object, FDataValidationContext&) const
{
	const auto* Sequence = Cast<UAnimSequenceBase>(Object);
	return Sequence && Sequence->Notifies.ContainsByPredicate([](const FAnimNotifyEvent& Event)
		{ return Event.NotifyStateClass && Event.NotifyStateClass->IsA<UProject_JAnimNotifyState_WeaponMotion>(); });
}

EDataValidationResult UProject_JWeaponMotionAssetValidator::ValidateLoadedAsset_Implementation(const FAssetData&, UObject* Object, FDataValidationContext&)
{
	const auto* Sequence = CastChecked<UAnimSequenceBase>(Object);
	bool bInvalid = false;
	for (int32 Index = 0; Index < Sequence->Notifies.Num(); ++Index)
	{
		const auto& Event = Sequence->Notifies[Index];
		const auto* Motion = Cast<UProject_JAnimNotifyState_WeaponMotion>(Event.NotifyStateClass);
		if (!Motion) continue;
		if (const FName Failure = Project_J::WeaponMotion::GetValidationFailure(Motion->MotionKeys, Motion->PrimaryGripIKAlpha,
			Motion->SecondaryGripIKAlpha, Event.GetDuration(), Motion->EntryBlendSeconds, Motion->ExitBlendSeconds); !Failure.IsNone())
		{
			AssetFails(Object, FText::Format(NSLOCTEXT("ProjectJWeaponMotionValidator", "InvalidNotify", "Weapon Motion notify {0}: invalid {1}."),
				FText::AsNumber(Index), FText::FromName(Failure)));
			bInvalid = true;
		}
	}
	if (!bInvalid) AssetPasses(Object);
	return bInvalid ? EDataValidationResult::Invalid : EDataValidationResult::Valid;
}
