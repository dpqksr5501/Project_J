#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Project_JFoleyMigrationLibrary.generated.h"

/** Editor-only, explicit package-list migration. Never saves the source GASP project. */
UCLASS()
class PROJECT_JCHARACTEREDITOR_API UProject_JFoleyMigrationLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Returns a JSON audit. Dry run validates every legacy payload before any apply pass. */
	UFUNCTION(BlueprintCallable, Category="Project J|Authoring|Foley")
	static FString MigrateGaspFoley(const TArray<FString>& Packages, const FString& SourceContent, bool bApply, const TArray<FString>& VerifiedNamedEvents);

	/** Recover stripped notify objects by matching source GUID and serialized event metadata.
	 * Reads source animations under a separate mount; never replaces target animation data. */
	UFUNCTION(BlueprintCallable, Category="Project J|Authoring|Foley")
	static FString RepairMissingGaspFoley(const TArray<FString>& Packages, const FString& SourceContent, bool bApply);
};
