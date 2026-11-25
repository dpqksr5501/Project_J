#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Project_JInteractable.h"
#include "Project_JWorldPortal.generated.h"
class UBoxComponent;
class UProject_JInteractionTargetComponent;
class UProject_JDungeonDefinition;

/** Same-world, stationary on-foot travel. Destination is a server-authored place ID. */
UCLASS()
class PROJECT_J_API AProject_JWorldPortal : public AActor, public IProject_JInteractable
{
    GENERATED_BODY()
public:
    AProject_JWorldPortal();
    UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category="Portal") FName DestinationPlaceId;
    /** Optional dungeon ID: when non-none, interacting allocates an on-demand dungeon instance via BP01 pipeline. */
    UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category="Portal|Dungeon") FName DungeonId;
    /** Optional DungeonDefinition DataAsset: auto-registers map/party-size/reward metadata and fills DungeonId on BeginPlay. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Portal|Dungeon") TObjectPtr<UProject_JDungeonDefinition> DungeonDefinition;
    /** When true, clients receiving a signed join ticket automatically execute ClientTravel (skipped in headless automation tests). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Portal|Dungeon") bool bAutoTravelOnTicket = true;
    virtual void BeginPlay() override;
    virtual bool CanInteract_Implementation(APawn* Interactor) const override;
    virtual void Interact_Implementation(APawn* Interactor) override;
    bool IsSpatiallyAvailable(const APawn* Interactor) const;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> InteractionBounds;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProject_JInteractionTargetComponent> InteractionTarget;
};
