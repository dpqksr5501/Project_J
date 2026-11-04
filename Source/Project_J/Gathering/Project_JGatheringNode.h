#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Project_JInteractable.h"
#include "Gathering/Project_JGatheringSubsystem.h"
#include "Project_JGatheringNode.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UProject_JInteractionTargetComponent;

/** Instant, one-yield gathering using the current standalone profile provider.
 * Each loaded instance must have a stable authored NodeId; it is not a client request parameter. */
UCLASS(Blueprintable)
class PROJECT_J_API AProject_JGatheringNode : public AActor, public IProject_JInteractable
{
    GENERATED_BODY()
public:
    AProject_JGatheringNode();
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Gathering") FName NodeId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gathering") FName YieldItemId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gathering", meta=(ClampMin="1")) int32 YieldItemVersion = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gathering", meta=(ClampMin="1")) int32 YieldCount = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gathering", meta=(ClampMin="0.1")) double CooldownSeconds = 30;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Gathering") TObjectPtr<UBoxComponent> InteractionBounds;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Gathering") TObjectPtr<UStaticMeshComponent> NodeVisual;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Gathering") TObjectPtr<UProject_JInteractionTargetComponent> InteractionTarget;
    UFUNCTION(BlueprintPure, Category="Gathering") EProject_JGatheringPhase GetPhase() const;
    UFUNCTION(BlueprintPure, Category="Gathering") double GetNextAvailableServerTime() const;
    virtual bool CanInteract_Implementation(APawn* Interactor) const override;
    virtual void Interact_Implementation(APawn* Interactor) override;
    bool TryGather(APawn* Interactor);
    bool HasValidDefinition() const;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
private:
    friend class UProject_JGatheringSubsystem;
    bool IsSpatiallyAvailable(const APawn* Interactor) const;
    bool EnsureRegistration();
    void ApplyLedgerState(const FProject_JGatheringView& View);
    FName RegisteredId;
    uint64 Registration = 0;
    UPROPERTY(Replicated) EProject_JGatheringPhase Phase = EProject_JGatheringPhase::Disabled;
    UPROPERTY(Replicated) double NextAvailableServerTime = 0;
};
