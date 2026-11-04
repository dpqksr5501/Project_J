#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Inventory/Project_JItemInstanceTypes.h"
#include "Project_JCompanionComponent.generated.h"

class APlayerState;
class APawn;
class AProject_JPlayerState;
class AProject_JPlayerCharacter;
class AProject_JNPCCharacter;
class AAIController;
class UAbilitySystemComponent;
class UProject_JNPCActionComponent;
class UProject_JCompanionItemDefinition;
struct FOnAttributeChangeData;

/** Session-owned runtime lease. Existing world preparation and NPC action/path budgets own execution. */
UCLASS(ClassGroup=(Companion), meta=(BlueprintSpawnableComponent))
class PROJECT_J_API UProject_JCompanionComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UProject_JCompanionComponent();
    /** An invalid item ID explicitly dismisses. Remote requests share the PlayerState quota. */
    UFUNCTION(BlueprintCallable, Category="Companion") void RequestCompanion(FGuid ItemInstanceId);
    /** Trusted server entry. True means accepted for preparation or already following, not spawned. */
    bool UseCompanionAuthority(FGuid ItemInstanceId);
    void DismissCompanionAuthority();
    UFUNCTION(BlueprintPure, Category="Companion") AProject_JNPCCharacter* GetActiveCompanion() const;
    bool HasPendingPreparation() const { return PreparationToken != 0; }
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
private:
    UFUNCTION(Server, Reliable) void ServerRequestCompanion(FGuid ItemInstanceId);
    UFUNCTION() void OnPawnSet(APlayerState* Player, APawn* NewPawn, APawn* OldPawn);
    UFUNCTION() void OnLeaseActorEnded(AActor* Actor, EEndPlayReason::Type Reason);
    UFUNCTION() void OnOwnedItemChanged(const FProject_JItemInstanceData& Item);
    UFUNCTION() void OnOwnedItemRemoved(const FProject_JItemInstanceData& Item);
    void OnOwnerHealthChanged(const FOnAttributeChangeData& Data);
    void OnCompanionHealthChanged(const FOnAttributeChangeData& Data);
    bool IsCurrent(uint64 ExpectedGeneration) const;
    void CompletePreparation(uint64 Token, bool bLoadCompleted, uint64 ExpectedGeneration);
    void Stop();
    void RetireLease();
    UPROPERTY(Transient, Replicated) TObjectPtr<AProject_JNPCCharacter> ActiveCompanion;
    UPROPERTY(Transient) TObjectPtr<const UProject_JCompanionItemDefinition> Definition;
    TWeakObjectPtr<AProject_JPlayerState> State;
    TWeakObjectPtr<AProject_JPlayerCharacter> Avatar;
    TWeakObjectPtr<AAIController> CreatedController;
    TWeakObjectPtr<UProject_JNPCActionComponent> Action;
    TWeakObjectPtr<UAbilitySystemComponent> CompanionASC;
    FGuid OwnedItem;
    FDelegateHandle IdentityHandle, OwnerHealthHandle, CompanionHealthHandle;
    uint64 Generation = 0, PreparationToken = 0, ActionToken = 0;
    double NextClientRequest = 0;
    bool bEnding = false, bChanging = false;
};
