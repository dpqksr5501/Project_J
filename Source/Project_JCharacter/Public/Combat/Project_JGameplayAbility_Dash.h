#pragma once

#include "Project_JGameplayAbility.h"
#include "Project_JGameplayAbility_Dash.generated.h"

class UAnimMontage;

/** A grounded movement ability driven by an authored montage. */
UCLASS()
class PROJECT_JCHARACTER_API UProject_JGameplayAbility_Dash : public UProject_JGameplayAbility
{
	GENERATED_BODY()

public:
	UProject_JGameplayAbility_Dash();
	bool PreservesSprintDuringDash() const { return bPreserveSprintDuringDash; }

	virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags,
		FGameplayTagContainer* OptionalRelevantTags) const override;

	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Dash")
	TObjectPtr<UAnimMontage> Montage;

	/** Montage position of the last travel key, before the visual recovery. */
	UPROPERTY(EditDefaultsOnly, Category = "Dash", meta = (ClampMin = "0.01"))
	float TravelEndPosition = 1.f / 3.f;

	UPROPERTY(EditDefaultsOnly, Category = "Dash", meta = (ClampMin = "0"))
	float DurationReduction = .05f;

	/** Half-angle of the accepted input cone. Travel still follows camera forward. */
	UPROPERTY(EditDefaultsOnly, Category = "Dash|Input", meta = (ClampMin = "0", ClampMax = "90", Units = "deg"))
	float ForwardInputHalfAngleDegrees = 22.5f;

	/** Preserve an eligible held sprint through the movement handoff. */
	UPROPERTY(EditDefaultsOnly, Category = "Dash|Recovery")
	bool bPreserveSprintDuringDash = true;

private:
	FVector ResolveForwardDashDirection(const class AProject_JPlayerCharacter& Player) const;
	UFUNCTION() void OnCompleted();
	UFUNCTION() void OnInterrupted();
};
