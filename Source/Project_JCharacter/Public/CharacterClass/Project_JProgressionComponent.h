#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AbilitySystem/Project_JAbilitySet.h"
#include "Project_JProgressionComponent.generated.h"

class UProject_JCharacterClassDefinition;
class UProject_JCharacterAdvancementDefinition;
class UAbilitySystemComponent;

/** Owned values for repository/handover adapters. Never persist UObject paths or GAS handles. */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JProgressionSnapshot
{
	GENERATED_BODY()
	UPROPERTY() int32 SchemaVersion = 2;
	UPROPERTY() int64 Experience = 0;
	UPROPERTY() FName ClassId;
	UPROPERTY() int32 Level = 1;
	/** Ordered acquisition history; active grants are reconstructed with definition policies. */
	UPROPERTY() TArray<FName> AdvancementHistory;
};

/** Public presentation state. Private acquisition history is replicated separately to the owner. */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JProgressionState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) TObjectPtr<UProject_JCharacterClassDefinition> ClassDefinition = nullptr;
	UPROPERTY(BlueprintReadOnly) TObjectPtr<UProject_JCharacterAdvancementDefinition> Advancement = nullptr;
	UPROPERTY(BlueprintReadOnly) int32 Level = 1;
	UPROPERTY(BlueprintReadOnly) int32 Revision = 0;
};

/** Lives beside the ASC: PlayerState for players, character for NPCs. No tick or worker UObject access. */
UCLASS(ClassGroup=(ProjectJ), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JProgressionComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UProject_JProgressionComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	const FProject_JProgressionState& GetState() const { return State; }
	const TArray<FName>& GetAcquiredAdvancements() const { return AcquiredAdvancements; }
	void InitializeDefaults(UProject_JCharacterClassDefinition* Class, UProject_JCharacterAdvancementDefinition* Advancement, int32 Level);
	bool InitializeClass(UProject_JCharacterClassDefinition* Class);
	bool CanApplyAdvancement(const UProject_JCharacterAdvancementDefinition* Advancement) const;
	bool ApplyAdvancement(UProject_JCharacterAdvancementDefinition* Advancement);
	bool SetLevel(int32 Level);
	/** Trusted reward path; no client RPC accepts an experience amount. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly) bool GrantExperience(int64 Amount);
	UFUNCTION(BlueprintPure) int64 GetExperience() const { return Experience; }
	UFUNCTION(BlueprintPure) int64 GetNextLevelExperience() const;
	UPROPERTY(EditDefaultsOnly, Category="Progression") int32 MaximumLevel = 100;
	/** XP required for level N -> N+1. Missing entries use 100*N. */
	UPROPERTY(EditDefaultsOnly, Category="Progression") TArray<int64> ExperienceRequirements;
	FProject_JProgressionSnapshot CaptureSnapshot() const;
	/** Trusted server adapter only; resolve IDs first, then restore once on an uninitialized owner. */
	bool RestoreSnapshot(const FProject_JProgressionSnapshot& Snapshot, UProject_JCharacterClassDefinition* Class,
		const TArray<UProject_JCharacterAdvancementDefinition*>& OrderedHistory);
	DECLARE_MULTICAST_DELEGATE(FOnChanged);
	FOnChanged OnChanged;
private:
	UPROPERTY(ReplicatedUsing=OnRep_Experience) int64 Experience = 0;
	UFUNCTION() void OnRep_Experience();
	UPROPERTY(ReplicatedUsing=OnRep_State) FProject_JProgressionState State;
	UPROPERTY(Replicated) TArray<FName> AcquiredAdvancements;
	UPROPERTY(Transient) FProject_JAbilitySet_GrantedHandles ClassHandles;
	UPROPERTY(Transient) FProject_JAbilitySet_GrantedHandles AdvancementHandles;
	UPROPERTY(Transient) TArray<TObjectPtr<UProject_JCharacterAdvancementDefinition>> ActiveAdvancements;
	bool bChanging = false;
	UAbilitySystemComponent* GetASC() const;
	bool CanMutate() const;
	void GrantClass();
	void Publish();
	UFUNCTION() void OnRep_State();
};
