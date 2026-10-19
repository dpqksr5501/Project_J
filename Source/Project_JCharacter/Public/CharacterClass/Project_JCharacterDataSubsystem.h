#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Project_JCharacterDataSubsystem.generated.h"

class UProject_JCharacterClassDefinition;
class UProject_JCharacterAdvancementDefinition;
class AProject_JBaseCharacter;
class UProject_JItemDefinition;

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
	/** Explicit load boundary for saved IDs. Reuses the existing maps and the authored
	 * PrimaryAsset scan; duplicate IDs or invalid graphs leave the maps untouched. */
	bool LoadPrimaryAssetDefinitions();
	UProject_JItemDefinition *GetItemDefinition(FName ItemId, int32 Version) const;

  protected:
	// Array of all available character classes. Configured in editor defaults or dynamically loaded.
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Character|Data")
	TArray<TSoftObjectPtr<UProject_JCharacterClassDefinition>> ClassDefinitions;

	// Array of all available advancements. Configured in editor defaults or dynamically loaded.
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Character|Data")
	TArray<TSoftObjectPtr<UProject_JCharacterAdvancementDefinition>> AdvancementDefinitions;

  private:
	friend class FProjectJProfileRuntimeCoverageTest;
	void PopulateRegistry();

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UProject_JCharacterClassDefinition>> ClassMap;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UProject_JCharacterAdvancementDefinition>> AdvancementMap;
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UProject_JItemDefinition>> ItemMap;
};
