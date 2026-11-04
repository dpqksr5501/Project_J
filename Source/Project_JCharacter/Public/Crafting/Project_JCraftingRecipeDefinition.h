#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Crafting/Project_JCraftingRecipeValues.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif
#include "Project_JCraftingRecipeDefinition.generated.h"

class UProject_JItemDefinition;
class UProject_JCharacterDataSubsystem;

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JCraftingRecipeLine
{
	GENERATED_BODY()
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) TSoftObjectPtr<UProject_JItemDefinition> Definition;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="1", ClampMax="1000000")) int32 CountPerBatch = 1;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="1", ClampMax="10000")) int32 ItemLevel = 1;
};

/** Instant or escrow-backed timed crafting. Persistent commands carry stable IDs and a captured
 * rule digest; paths are authoring references, not saved item ownership. */
UCLASS(BlueprintType, Const)
class PROJECT_JCHARACTER_API UProject_JCraftingRecipeDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, AssetRegistrySearchable) FName RecipeId;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="1")) int32 SchemaVersion = 1;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="1", ClampMax="100")) int32 MaxBatches = 1;
	/** Zero retains instant crafting; positive duration is agreed when the job starts. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="86400")) int32 DurationSecondsPerBatch = 0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) TArray<FProject_JCraftingRecipeLine> Inputs;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) TArray<FProject_JCraftingRecipeLine> Outputs;

	/** Loaded canonical lookup only; gameplay capture never starts a synchronous load. */
	bool Capture(const UProject_JCharacterDataSubsystem& Registry, ProjectJ::Crafting::FRecipeValues& Out,
		FString& RulesDigest, FString* Error = nullptr) const;
#if WITH_EDITOR
	EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
private:
	bool CaptureUsing(TFunctionRef<UProject_JItemDefinition*(const TSoftObjectPtr<UProject_JItemDefinition>&)> Resolve,
		ProjectJ::Crafting::FRecipeValues& Out, FString& Digest, FString* Error) const;
};
