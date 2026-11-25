#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "Project_JWorldEncounter.generated.h"
class AProject_JNPCCharacter;
class UProject_JWorldEventRegistry;
class UProject_JNPCActivationDefinition;
class UProject_JLootTableDefinition;
class UProject_JDungeonDefinition;
class UProject_JCameraComponent;

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
    /** UTC admission window. An already active encounter may finish after it closes. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter|Schedule") bool bUseSchedule = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter|Schedule", meta=(EditCondition="bUseSchedule")) FDateTime StartUtc;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter|Schedule", meta=(EditCondition="bUseSchedule")) FDateTime EndUtc;
    /** Optional one-shot world-space navigation-feet command after spawn. Active/SpawnRevision prove
     * a live spawn and command admission, not arrival. Later navigation cancellation/failure is not defeat. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter|Navigation") bool bUseInitialNavigation = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter|Navigation", meta=(EditCondition="bUseInitialNavigation")) FVector InitialNavigationDestination = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter|Navigation", meta=(EditCondition="bUseInitialNavigation", ClampMin="1", ClampMax="2000")) double InitialNavigationAcceptanceRadius = 25;
    /** Explicit combat capability, prepared with NPCClass in the same bounded async request.
     * Null preserves the existing inert/navigation spawn. Initial navigation and combat are mutually exclusive.
     * Active/SpawnRevision certify spawn/admission; later combat deactivation is not defeat. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter|Combat") TSoftObjectPtr<UProject_JNPCActivationDefinition> CombatDefinition;
    /** Authority-authored faction adapter value; nonnegative when CombatDefinition is selected. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter|Combat", meta=(ClampMin="0")) int32 CombatTeamId = 1;
    /** Optional personal contribution loot table rolled for all eligible participants on defeat (with Mail overflow). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter|Loot") TObjectPtr<UProject_JLootTableDefinition> BossLootTable;
    /** Optional quest objective event credited to all eligible contributors on defeat (falls back to NPC QuestKillEventId / EncounterId). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter|Quest") FName QuestKillEventId = NAME_None;
    /** When true, defeating this boss automatically claims Dungeon Clear rewards & quest credit for active dungeon participants. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter|Dungeon") bool bTriggerDungeonClearOnDefeat = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter|Dungeon", meta=(EditCondition="bTriggerDungeonClearOnDefeat"))
    TObjectPtr<UProject_JDungeonDefinition> DungeonDefinition;
    /** When enabled (or when BossLootTable is set), nearby local players smoothly enter GiantBossZoomOut camera mode while Active. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter|Camera") bool bEnableBossCameraZoomOut = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter|Camera", meta=(ClampMin="500", ClampMax="20000")) float BossCameraRadiusCm = 4500.0f;
    UPROPERTY(ReplicatedUsing=OnRep_Phase, BlueprintReadOnly, Category="Encounter") EProject_JEncounterPhase Phase = EProject_JEncounterPhase::Dormant;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Encounter") int32 SpawnRevision = 0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Encounter") double NextSpawnServerTime = 0;
    UPROPERTY(BlueprintAssignable, Category="Encounter") FProject_JEncounterResolved OnResolved;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    bool IsScheduleOpen(FDateTime NowUtc) const;
#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
private:
    friend class UProject_JWorldEventRegistry;
    bool HasActivationDemand(bool& bKnown) const;
    bool HasValidInitialNavigation() const;
    bool HasValidCombatConfiguration() const;
    void UpdateEncounter();
    void CompletePreparation(uint64 Token);
    UFUNCTION() void OnNPCDestroyed(AActor* Actor);
    UFUNCTION() void OnRep_Phase();
    void UpdateLocalBossCameraState();
    void ClearLocalBossCameraState();
    void EnterCooldown(bool bDefeated);
    TWeakObjectPtr<AProject_JNPCCharacter> NPC;
    TArray<TWeakObjectPtr<UProject_JCameraComponent>, TInlineAllocator<2>> ActiveBossCameras;
    FTimerHandle BossCameraTimerHandle;
    uint64 PreparationToken = 0;
    bool bRegistered = false;
    bool bStopping = false;
};
