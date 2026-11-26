#include "Validation/Project_JCombatWarningAssetValidator.h"
#include "Animation/AnimMontage.h"
#include "Animation/Project_JAnimNotifyState_CombatPresentationCue.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Combat/Project_JAttackDefinition.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Combat/Project_JGroundAttack.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Misc/DataValidation.h"

bool UProject_JCombatWarningAssetValidator::CanValidateAsset_Implementation(const FAssetData&, UObject* Object, FDataValidationContext&) const
{
	if (Object && (Object->IsA<UProject_JCharacterClassDefinition>() || Object->IsA<UProject_JCharacterAdvancementDefinition>()
		|| Object->IsA<UProject_JEquipmentItemDefinition>() || Object->IsA<UProject_JCombatStyleDefinition>())) return true;
	const auto* Sequence = Cast<UAnimSequenceBase>(Object);
	// A saved montage may have lost its last warning. Its attack references still carry the intent.
	if (Sequence && Sequence->IsA<UAnimMontage>() && Sequence->IsAsset()) return true;
	return Sequence && Sequence->Notifies.ContainsByPredicate([](const FAnimNotifyEvent& Event)
	{
		const auto* Cue = Cast<UProject_JAnimNotifyState_CombatPresentationCue>(Event.NotifyStateClass);
		return Cue && Cue->IsGameplayWarning();
	});
}

EDataValidationResult UProject_JCombatWarningAssetValidator::ValidateLoadedAsset_Implementation(const FAssetData&, UObject* Object, FDataValidationContext&)
{
	bool bInvalid = false;
	const auto* Sequence = Cast<UAnimSequenceBase>(Object);
	if (!Sequence)
	{
		const auto CheckEntries = [&](const UObject* Owner, const TArray<FProject_JAbilitySet_GameplayAbility>& Entries)
		{
			for (int32 Index = 0; Index < Entries.Num(); ++Index)
			{
				const auto& Class = Entries[Index].Ability;
				const auto* Ability = Class ? Cast<UProject_JGameplayAbility_GroundAttack>(Class->GetDefaultObject()) : nullptr;
				if (!Ability) continue;
				const FString Failure = Ability->GetOwnerGrantFailure();
				if (!Failure.IsEmpty())
				{
					AssetFails(Object, FText::FromString(FString::Printf(TEXT("%s ability entry %d (%s) - %s"),
						*Owner->GetPathName(), Index, *Class->GetName(), *Failure)));
					bInvalid = true;
				}
			}
		};
		const auto CheckSets = [&](const auto& Sets)
		{
			for (const UProject_JAbilitySet* Set : Sets)
				if (Set) CheckEntries(Set, Set->GrantedAbilityEntries);
		};
		const UProject_JCombatStyleDefinition* Style = Cast<UProject_JCombatStyleDefinition>(Object);
		if (const auto* Job = Cast<UProject_JCharacterClassDefinition>(Object))
		{
			CheckSets(Job->AbilitySets);
			Style = Job->DefaultCombatStyle;
		}
		else if (const auto* Advancement = Cast<UProject_JCharacterAdvancementDefinition>(Object))
		{
			CheckSets(Advancement->AdditionalAbilitySets);
			Style = Advancement->CombatStyleOverride;
		}
		else if (const auto* Item = Cast<UProject_JEquipmentItemDefinition>(Object))
		{
			if (Item->AbilitySet) CheckEntries(Item->AbilitySet, Item->AbilitySet->GrantedAbilityEntries);
			Style = Item->CombatStyleDefinition;
		}
		if (Style)
		{
			CheckEntries(Style, Style->InlineAbilities);
			CheckSets(Style->AbilitySets);
		}
		if (!bInvalid) AssetPasses(Object);
		return bInvalid ? EDataValidationResult::Invalid : EDataValidationResult::Valid;
	}
	for (int32 Index = 0; Index < Sequence->Notifies.Num(); ++Index)
	{
		const auto& Event = Sequence->Notifies[Index];
		const auto* Cue = Cast<UProject_JAnimNotifyState_CombatPresentationCue>(Event.NotifyStateClass);
		if (!Cue || !Cue->IsGameplayWarning()) continue;
		if (const FString Failure = Cue->GetWarningEventFailure(Event); !Failure.IsEmpty())
		{
			AssetFails(Object, FText::Format(NSLOCTEXT("ProjectJCombatWarningValidator", "InvalidNotify", "Gameplay Warning notify {0}: {1}"),
				FText::AsNumber(Index), FText::FromString(Failure)));
			bInvalid = true;
		}
	}
	if (const auto* Montage = Cast<UAnimMontage>(Sequence); Montage && Montage->IsAsset())
	{
		auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		FARFilter Filter;
		Registry.GetReferencers(Montage->GetOutermost()->GetFName(), Filter.PackageNames, UE::AssetRegistry::EDependencyCategory::Package);
		if (!Filter.PackageNames.IsEmpty())
		{
			Filter.ClassPaths.Add(UProject_JAttackDefinition::StaticClass()->GetClassPathName());
			Filter.bRecursiveClasses = true;
			TArray<FAssetData> Definitions;
			Registry.GetAssets(Filter, Definitions);
			for (const FAssetData& Data : Definitions)
			{
				const auto* Attack = Cast<UProject_JAttackDefinition>(Data.GetAsset());
				if (!Attack)
				{
					AssetWarning(Object, FText::FromString(Data.GetObjectPathString() + TEXT(": could not read the referencing attack.")));
					continue;
				}
				if (Attack->Montage != Montage) continue;
				if (const FString Failure = Attack->GetWarningContractFailure(); !Failure.IsEmpty())
				{
					AssetFails(Object, FText::FromString(Attack->GetPathName() + TEXT(": ") + Failure));
					bInvalid = true;
				}
			}
		}
	}
	if (!bInvalid) AssetPasses(Object);
	return bInvalid ? EDataValidationResult::Invalid : EDataValidationResult::Valid;
}
