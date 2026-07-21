#pragma once

#include "CoreMinimal.h"
#include "Combat/Project_JCombatCommandSet.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Project_JSkillInputExecutionComponent.generated.h"

class AProject_JPlayerCharacter;

/**
 * Executes resolved skill InputTags against GAS.
 *
 * SkillInputRouter owns raw input and chord resolution; this component owns the
 * gameplay side of press/release handling.
 */
UCLASS(ClassGroup=(Input), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JSkillInputExecutionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JSkillInputExecutionComponent();

	void Initialize(AProject_JPlayerCharacter* InPlayerCharacter);

	UFUNCTION(BlueprintCallable, Category = "Combat|Input")
	void HandleInputTagPressed(FGameplayTag InputTag);

	UFUNCTION(BlueprintCallable, Category = "Combat|Input")
	void HandleInputTagReleased(FGameplayTag InputTag);

	/** Sequence-numbered command verification path; GAS owns predicted ability activation. */
	UFUNCTION(Server, Unreliable)
	void ServerSendCombatInputEvent(FGameplayTag InputTag, float ClientTimestamp, int32 InputSequence);

	/** Clears command history after weapon/state changes that invalidate a sequence. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Input")
	void ClearCommandInputHistory();

private:
	FGameplayTag ResolveDispatchInputTag(FGameplayTag RawInputTag, double TimestampSeconds, bool& bOutConsumeRawInput);
	void DispatchInputTag(FGameplayTag InputTag, bool bAllowAbilityActivation = true);
	/**
	 * Combo follow-up inputs must be delivered to the active melee ability as
	 * Gameplay Events only. Re-running generic ability activation here could
	 * activate a second spec sharing the input tag and restart the combo at L1.
	 */
	bool ShouldRouteInputToActiveComboOnly(FGameplayTag InputTag) const;
	double GetSynchronizedInputTimestamp() const;
	void RecordRawInput(FGameplayTag RawInputTag, double TimestampSeconds, const UProject_JCombatCommandSet* CommandSet);
	FGameplayTagContainer GetOwnerGameplayTags() const;

	UPROPERTY(Transient)
	TObjectPtr<AProject_JPlayerCharacter> BoundPlayerCharacter = nullptr;

	TArray<FProject_JCombatCommandInputEntry> CommandInputHistory;
	double LastInputTimestampSeconds = -1.0;
	TWeakObjectPtr<const UProject_JCombatCommandSet> LastCommandSet;

	/** Hard cap protects the server against an unexpectedly large authored command sequence. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Input", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaximumCommandHistoryEntries = 8;
	int32 LocalInputSequence = 0;
	int32 LastServerInputSequence = 0;
	double ServerRateWindowStart = 0.0;
	int32 ServerInputsInRateWindow = 0;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Input|Security", meta = (ClampMin = "1", ClampMax = "120"))
	int32 MaxServerInputsPerSecond = 30;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Input|Security", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float MaxAcceptedInputAge = 0.75f;

};
