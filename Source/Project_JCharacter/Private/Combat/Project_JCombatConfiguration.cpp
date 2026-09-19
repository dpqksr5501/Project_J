#include "Combat/Project_JCombatConfiguration.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "Combat/Project_JCombatStyleDefinition.h"

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
