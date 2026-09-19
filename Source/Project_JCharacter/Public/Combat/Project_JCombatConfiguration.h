#pragma once

#include "CoreMinimal.h"
#include "Project_JCombatConfiguration.generated.h"

class UProject_JCombatStyleDefinition;
class UProject_JCharacterAdvancementDefinition;

UENUM(BlueprintType)
enum class EProject_JCombatConfigurationSource : uint8 { None, Class, Advancement, Equipment };

/** Resolved domains retain their source assets. No copied DA, per-frame allocation, or UObject worker access. */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JCombatConfiguration
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) TObjectPtr<const UProject_JCombatStyleDefinition> GameplayStyle = nullptr;
	UPROPERTY(BlueprintReadOnly) TObjectPtr<const UProject_JCombatStyleDefinition> AnimationStyle = nullptr;
	UPROPERTY(BlueprintReadOnly) EProject_JCombatConfigurationSource GameplaySource = EProject_JCombatConfigurationSource::None;
	UPROPERTY(BlueprintReadOnly) EProject_JCombatConfigurationSource AnimationSource = EProject_JCombatConfigurationSource::None;
	UPROPERTY(BlueprintReadOnly) int32 Revision = 0;

	static FProject_JCombatConfiguration Resolve(const UProject_JCombatStyleDefinition* ClassStyle,
		const UProject_JCharacterAdvancementDefinition* Advancement, const UProject_JCombatStyleDefinition* EquipmentStyle);
};
