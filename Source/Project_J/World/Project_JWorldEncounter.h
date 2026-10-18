#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "Project_JWorldEncounter.generated.h"
class AProject_JNPCCharacter;
struct FStreamableHandle;

UENUM(BlueprintType)
enum class EProject_JEncounterPhase : uint8 { Dormant, Preparing, Active, Cooldown, Disabled };

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FProject_JEncounterResolved, FName, EncounterId, int32, SpawnRevision);

/** Always-loaded server event owner. Boss gameplay lives on the NPC; no client-driven spawn or automatic rewards. */
UCLASS()
class PROJECT_J_API AProject_JWorldEncounter : public AInfo
{
    GENERATED_BODY()
public:
    AProject_JWorldEncounter();
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter") FName EncounterId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter") TSoftClassPtr<AProject_JNPCCharacter> NPCClass;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter", meta=(ClampMin="1000")) float ActivationRadius = 150000;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter", meta=(ClampMin="1")) float RespawnSeconds = 300;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Encounter") EProject_JEncounterPhase Phase = EProject_JEncounterPhase::Dormant;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Encounter") int32 SpawnRevision = 0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Encounter") double NextSpawnServerTime = 0;
    UPROPERTY(BlueprintAssignable, Category="Encounter") FProject_JEncounterResolved OnResolved;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
private:
    void UpdateEncounter();
    void CompletePreparation();
    UFUNCTION() void OnNPCDestroyed(AActor* Actor);
    void EnterCooldown(bool bDefeated);
    TWeakObjectPtr<AProject_JNPCCharacter> NPC;
    TSharedPtr<FStreamableHandle> LoadHandle;
    FTimerHandle UpdateTimer;
    bool bRegistered = false;
    bool bStopping = false;
};
