#pragma once

#include "GameplayCueNotify_Static.h"
#include "Project_JGameplayCue_BudgetedImpact.generated.h"

class USoundBase;

/** One cosmetic impact per executed cue, admitted through the local combat audio budget. */
UCLASS(Blueprintable)
class PROJECT_J_API UProject_JGameplayCue_BudgetedImpact : public UGameplayCueNotify_Static
{
	GENERATED_BODY()
public:
	virtual bool OnExecute_Implementation(AActor* Target, const FGameplayCueParameters& Parameters) const override;

	UPROPERTY(EditDefaultsOnly, Category="Audio") TObjectPtr<USoundBase> ImpactSound;
};
