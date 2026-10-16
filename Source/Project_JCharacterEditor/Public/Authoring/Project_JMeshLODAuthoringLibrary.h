#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Project_JMeshLODAuthoringLibrary.generated.h"
class UProject_JMeshLODSettings;
UCLASS()
class PROJECT_JCHARACTEREDITOR_API UProject_JMeshLODAuthoringLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Read-only inspection; bSurfaceReviewed explicitly excludes hair/cards/fur from automatic surface reduction. */
    UFUNCTION(BlueprintCallable,Category="Project J|Authoring|Mesh LOD")
    static FString InspectMeshes(const TArray<UObject*>& Assets,UProject_JMeshLODSettings* Policy=nullptr,bool bSurfaceReviewed=false);
};
