#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Project_JInteractable.h"
#include "Project_JCraftingStation.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UProject_JInteractionTargetComponent;

/** A recipe-bound bench using the normal interaction and profile transaction. */
UCLASS(Blueprintable)
class PROJECT_J_API AProject_JCraftingStation : public AActor, public IProject_JInteractable
{
	GENERATED_BODY()
public:
	AProject_JCraftingStation();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool CanInteract_Implementation(APawn* Interactor) const override;
	virtual void Interact_Implementation(APawn* Interactor) override;
	/** True means admitted, not yet durably completed. Existing bag events publish success. */
	bool TryCraft(APawn* Interactor);

	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category="Crafting")
	FName RecipeId;
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category="Crafting", meta=(ClampMin="1"))
	int32 RecipeVersion = 1;
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category="Crafting", meta=(ClampMin="1", ClampMax="100"))
	int32 Batches = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Crafting")
	TObjectPtr<UBoxComponent> InteractionBounds;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Crafting")
	TObjectPtr<UStaticMeshComponent> BenchVisual;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Crafting")
	TObjectPtr<UProject_JInteractionTargetComponent> InteractionTarget;
private:
	bool IsSpatiallyAvailable(const APawn* Interactor) const;
};
