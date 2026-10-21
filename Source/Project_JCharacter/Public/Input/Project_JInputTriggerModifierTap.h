#pragma once

#include "CoreMinimal.h"
#include "InputTriggers.h"
#include "Project_JInputTriggerModifierTap.generated.h"

/** A single modifier released without a new keyboard/mouse chord. Does not consume other actions. */
UCLASS(meta=(DisplayName="Project J Modifier Tap"))
class PROJECT_JCHARACTER_API UProject_JInputTriggerModifierTap : public UInputTrigger
{
	GENERATED_BODY()
public:
	UProject_JInputTriggerModifierTap();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Input")
	FKey ModifierKey = EKeys::LeftControl;

protected:
	virtual ETriggerState UpdateState_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue Value, float DeltaTime) override;

private:
	bool bArmed = false;
	bool bChord = false;
};
