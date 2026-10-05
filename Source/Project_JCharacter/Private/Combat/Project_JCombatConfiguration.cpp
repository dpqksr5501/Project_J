#include "Combat/Project_JCombatConfiguration.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"

FProject_JCombatConfiguration FProject_JCombatConfiguration::Resolve(const UProject_JCombatStyleDefinition* ClassStyle,
	const UProject_JCharacterAdvancementDefinition* Advancement, const UProject_JCombatStyleDefinition* EquipmentStyle)
{
	check(IsInGameThread());
	FProject_JCombatConfiguration Result;
	Result.GameplayStyle = ClassStyle;
	Result.GameplaySource = ClassStyle ? EProject_JCombatConfigurationSource::Class : EProject_JCombatConfigurationSource::None;
	if (Advancement && Advancement->CombatStyleOverride)
	{
		Result.GameplayStyle = Advancement->CombatStyleOverride;
		Result.GameplaySource = EProject_JCombatConfigurationSource::Advancement;
	}
	Result.AnimationStyle = Result.GameplayStyle;
	Result.AnimationSource = Result.GameplaySource;
	if (EquipmentStyle)
	{
		Result.AnimationStyle = EquipmentStyle;
		Result.AnimationSource = EProject_JCombatConfigurationSource::Equipment;
		if (!Advancement || !Advancement->bOverrideEquippedGameplay || !Advancement->CombatStyleOverride)
		{
			Result.GameplayStyle = EquipmentStyle;
			Result.GameplaySource = EProject_JCombatConfigurationSource::Equipment;
		}
	}
	return Result;
}

void FProject_JCombatConfiguration::FindInputGrantConflicts(const UAbilitySystemComponent& ASC, TArray<FText>& Conflicts)
{
	check(IsInGameThread());
	Conflicts.Reset();
	TMap<FGameplayTag, const FGameplayAbilitySpec*> FirstByInput;
	for (const auto& Spec : ASC.GetActivatableAbilities())
	{
		if (!Spec.Ability || Spec.PendingRemove) { continue; }
		for (const FGameplayTag Tag : Spec.GetDynamicSpecSourceTags())
		{
			if (!Tag.ToString().StartsWith(TEXT("InputTag.")) && !Tag.ToString().StartsWith(TEXT("Command."))) { continue; }
			if (const auto* Prior = FirstByInput.Find(Tag))
			{
				Conflicts.Add(FText::FromString(FString::Printf(TEXT("Input %s is granted to %s (%s) and %s (%s). Verify activation exclusivity across grant sources."),
					*Tag.ToString(), *GetNameSafe((*Prior)->Ability), *GetNameSafe((*Prior)->SourceObject.Get()),
					*GetNameSafe(Spec.Ability), *GetNameSafe(Spec.SourceObject.Get()))));
			}
			else { FirstByInput.Add(Tag, &Spec); }
		}
	}
}
