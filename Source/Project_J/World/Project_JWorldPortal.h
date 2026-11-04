#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Project_JInteractable.h"
#include "Project_JWorldPortal.generated.h"
class UBoxComponent;
class UProject_JInteractionTargetComponent;

/** Same-world, stationary on-foot travel. Destination is a server-authored place ID. */
UCLASS()
class PROJECT_J_API AProject_JWorldPortal : public AActor, public IProject_JInteractable
{
    GENERATED_BODY()
public:
    AProject_JWorldPortal();
    UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category="Portal") FName DestinationPlaceId;
    virtual bool CanInteract_Implementation(APawn* Interactor) const override;
    virtual void Interact_Implementation(APawn* Interactor) override;
    bool IsSpatiallyAvailable(const APawn* Interactor) const;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> InteractionBounds;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProject_JInteractionTargetComponent> InteractionTarget;
};
