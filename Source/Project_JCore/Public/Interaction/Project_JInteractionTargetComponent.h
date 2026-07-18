#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Project_JInteractionTargetComponent.generated.h"

/** Data-only, reusable configuration for any actor that implements IProject_JInteractable. */
UCLASS(ClassGroup=(Interaction), meta=(BlueprintSpawnableComponent))
class PROJECT_JCORE_API UProject_JInteractionTargetComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UProject_JInteractionTargetComponent();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Interaction") FText PromptText;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Interaction", meta=(ClampMin="0.0")) float InteractionRange = 250.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Interaction") int32 Priority = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Interaction", meta=(ClampMin="0.0")) float HoldDuration = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction") bool bEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Interaction") bool bRequireLineOfSight = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Interaction") bool bExclusive = false;
	UFUNCTION(BlueprintPure, Category="Interaction") bool IsAvailable() const { return bEnabled; }
};
