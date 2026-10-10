#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Project_JEarlyTransitionMigrationLibrary.generated.h"

/** Six explicit Run clips only. Reads original metadata; never saves source GASP. */
UCLASS()
class PROJECT_JCHARACTEREDITOR_API UProject_JEarlyTransitionMigrationLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="Project J|Authoring")
	static FString RepairSixRunClips(const FString& SourceContent, bool bApply);
};
