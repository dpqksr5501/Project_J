#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Project_JCharacterDataSubsystem.generated.h"

class UProject_JCharacterClassDefinition;
class UProject_JCharacterAdvancementDefinition;
class AProject_JBaseCharacter;
class UProject_JItemDefinition;
class UProject_JCraftingRecipeDefinition;

struct FProject_JDefinitionRegistryStats
{
	uint64 Refreshes = 0, FailedRefreshes = 0;
	int32 LastResolvedPaths = 0, LastSynchronousLoads = 0;
	double LastRefreshMilliseconds = 0;
};

/**
 * Server-authoritative subsystem to manage and retrieve character classes and advancements.
 * Living as a GameInstanceSubsystem inside the Project_JCharacter module to keep correct dependency direction.
 */
UCLASS(BlueprintType, Config = Game, DefaultConfig)
class PROJECT_JCHARACTER_API UProject_JCharacterDataSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

  public:
	UProject_JCharacterDataSubsystem();

	virtual void Initialize(FSubsystemCollectionBase &Collection) override;
	virtual void Deinitialize() override;
	virtual void PostReloadConfig(FProperty* PropertyThatWasLoaded) override;

	// Retrieves a character class definition by ClassId key.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Character|Data")
	UProject_JCharacterClassDefinition *GetClassDefinition(FName ClassId) const;

	// Retrieves a character advancement definition by AdvancementId key.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Character|Data")
	UProject_JCharacterAdvancementDefinition *GetAdvancementDefinition(FName AdvancementId) const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Character|Data")
	bool InitializeCharacterClassById(AProject_JBaseCharacter *TargetCharacter, FName ClassId) const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Character|Data")
	bool ApplyAdvancementById(AProject_JBaseCharacter *TargetCharacter, FName AdvancementId) const;

	// Validation helper to check registry consistency
	UFUNCTION(BlueprintCallable, Category = "Character|Data")
	bool ValidateDataRegistry(TArray<FText> &OutValidationErrors);
	/** Cheap runtime admission: reuse this GameInstance's successfully published core registry.
	 * Config reload invalidates readiness without loading; other catalog authoring changes
	 * require the explicit refresh below. A failed refresh
	 * closes this admission until a later successful refresh; old getter values remain intact. */
	/** Read-only admission check; unlike Ensure, never refreshes or synchronously loads definitions. */
	bool ArePrimaryAssetDefinitionsReady() const { check(IsInGameThread()); return bPrimaryRegistryReady; }
	bool EnsurePrimaryAssetDefinitions();
	/** Explicit Config/PrimaryAsset refresh. Duplicate IDs or invalid graphs leave published
	 * maps untouched but invalidate readiness for new runtime admission. Optional recipe errors
	 * continue to close crafting only. */
	bool LoadPrimaryAssetDefinitions();
	UProject_JItemDefinition *GetItemDefinition(FName ItemId, int32 Version) const;
	/** Optional domain refresh: failure closes crafting only, never core definitions/profile restore. */
	bool RefreshCraftingRecipes();
	UProject_JCraftingRecipeDefinition* GetCraftingRecipe(FName Id, int32 Version) const;
	const FString& GetCraftingRegistryError() const { return CraftingRegistryError; }
	static constexpr int32 MaxCraftingRecipes = 1024;
	const FProject_JDefinitionRegistryStats& GetRegistryStats() const { return Stats; }
	static constexpr int32 MaxCharacterDefinitions = 1024;
	static constexpr int32 MaxItemDefinitions = 1024;

  protected:
	// Array of all available character classes. Configured in editor defaults or dynamically loaded.
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Character|Data")
	TArray<TSoftObjectPtr<UProject_JCharacterClassDefinition>> ClassDefinitions;

	// Array of all available advancements. Configured in editor defaults or dynamically loaded.
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Character|Data")
	TArray<TSoftObjectPtr<UProject_JCharacterAdvancementDefinition>> AdvancementDefinitions;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Crafting|Data")
	TArray<TSoftObjectPtr<UProject_JCraftingRecipeDefinition>> CraftingRecipeDefinitions;

  private:
	friend class FProjectJProfileRuntimeCoverageTest;
	friend class FProjectJDefinitionRegistryAtomicTest;
	friend class FProjectJCraftingExecutionTest;
	friend class AProject_JCraftingPrototypeFixture; // Exact opt-in owned-map transient recipe only.
	FProject_JDefinitionRegistryStats Stats;
	bool bPrimaryRegistryReady = false;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UProject_JCharacterClassDefinition>> ClassMap;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UProject_JCharacterAdvancementDefinition>> AdvancementMap;
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UProject_JItemDefinition>> ItemMap;
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UProject_JCraftingRecipeDefinition>> CraftingRecipeMap;
	bool bCraftingRegistryReady = false;
	FString CraftingRegistryError;
};
