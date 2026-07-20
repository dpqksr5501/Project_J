#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Project_JSkillInputExecutionComponent.generated.h"

class AProject_JPlayerCharacter;
class UProject_JCombatStateComponent;

/**
 * Executes resolved skill InputTags against GAS.
 *
 * SkillInputRouter owns raw input and chord resolution; this component owns the
 * gameplay side of press/release handling and the temporary legacy fallback path.
 */
UCLASS(ClassGroup=(Input), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JSkillInputExecutionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JSkillInputExecutionComponent();

	void Initialize(AProject_JPlayerCharacter* InPlayerCharacter, UProject_JCombatStateComponent* InCombatStateComponent);

	UFUNCTION(BlueprintCallable, Category = "Combat|Input")
	void HandleInputTagPressed(FGameplayTag InputTag);

	UFUNCTION(BlueprintCallable, Category = "Combat|Input")
	void HandleInputTagReleased(FGameplayTag InputTag);

	/** Server receives the same combo input event as the predicting client. Reliable is appropriate for discrete combat input, not movement axes. */
	UFUNCTION(Server, Reliable)
	void ServerSendCombatInputEvent(FGameplayTag InputTag);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities|Input")
	bool bAllowLegacySkillInputFallback = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities|Input", meta = (EditCondition = "bAllowLegacySkillInputFallback"))
	bool bWarnOnLegacySkillInputFallback = true;

private:
	bool TryLegacyFallback(FGameplayTag InputTag);

	UPROPERTY(Transient)
	TObjectPtr<AProject_JPlayerCharacter> BoundPlayerCharacter = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UProject_JCombatStateComponent> BoundCombatStateComponent = nullptr;

	TSet<FGameplayTag> WarnedLegacySkillInputFallbackTags;
};
