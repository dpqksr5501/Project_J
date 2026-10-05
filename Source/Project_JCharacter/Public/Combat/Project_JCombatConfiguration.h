#pragma once

#include "CoreMinimal.h"
#include "Project_JCombatConfiguration.generated.h"

class UProject_JCombatStyleDefinition;
class UProject_JCharacterAdvancementDefinition;
class UAbilitySystemComponent;

UENUM(BlueprintType)
enum class EProject_JCombatExecutionChangePolicy : uint8
{
	CompleteCurrentExecution,
	CancelOnGameplayStyleChange
};

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
	/** Inspect final live grants across all sources. Conflicts are diagnostics;
	 * mutually exclusive ability activation requirements can intentionally share an input. */
	static void FindInputGrantConflicts(const UAbilitySystemComponent& ASC, TArray<FText>& Conflicts);
	static bool ShouldCancelExecution(EProject_JCombatExecutionChangePolicy Policy,
		const UProject_JCombatStyleDefinition* StartedStyle, const UProject_JCombatStyleDefinition* CurrentStyle)
	{
		return Policy == EProject_JCombatExecutionChangePolicy::CancelOnGameplayStyleChange && StartedStyle != CurrentStyle;
	}
};
