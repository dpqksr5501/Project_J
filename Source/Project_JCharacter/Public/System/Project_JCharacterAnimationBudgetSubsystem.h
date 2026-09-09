#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Project_JCharacterAnimationBudgetSubsystem.generated.h"

class UProject_JBudgetedSkeletalMeshComponent;

/** One GT policy pass per world. Does not run UObject work on worker threads. */
UCLASS()
class PROJECT_JCHARACTER_API UProject_JCharacterAnimationBudgetSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	void RegisterMesh(UProject_JBudgetedSkeletalMeshComponent* Mesh);
	void UnregisterMesh(UProject_JBudgetedSkeletalMeshComponent* Mesh);
	/** Per-world switch for reproducible tests; unset uses ProjectJ.AnimationBudget.Enabled. */
	void SetEnabledOverride(TOptional<bool> Enabled);
	bool IsEnabledForWorld() const;
	int32 GetManagedCount() const;
	int32 GetTrackedCount() const { return Meshes.Num(); }
	void Refresh();
private:
	void OnPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);
	void RestoreAllocatorIfUnused();
	void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	TArray<TWeakObjectPtr<UProject_JBudgetedSkeletalMeshComponent>> Meshes;
	TOptional<bool> EnabledOverride;
	FDelegateHandle TickHandle;
	FDelegateHandle CleanupHandle;
	bool bOwnAllocatorEnable = false, bEnding = false;
	static constexpr int32 MaxMeshes = 4096;
};
