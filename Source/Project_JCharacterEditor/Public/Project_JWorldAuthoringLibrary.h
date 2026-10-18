#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Project_JWorldAuthoringLibrary.generated.h"
class ALandscape;
class UMaterialInterface;
class AActor;

/** Offline authoring only. Heightmaps stay ordinary source files; the game loads the saved landscape. */
UCLASS()
class PROJECT_JCHARACTEREDITOR_API UProject_JWorldAuthoringLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Project J|World Authoring")
    static ALandscape* ImportRegionLandscape(const FString& HeightmapFile, int32 Resolution, float XYScale, float ZScale, UMaterialInterface* Material);
    /** Uses the public engine setter, including its editor streaming state update. */
    UFUNCTION(BlueprintCallable, Category="Project J|World Authoring")
    static bool EnableRegionStreaming();
    UFUNCTION(BlueprintPure, Category="Project J|World Authoring")
    static FString GetRegionRuntimeHashName();
    /** Replaces the base edit layer only; preserves landscape identity, paint and streaming proxies. */
    UFUNCTION(BlueprintCallable, Category="Project J|World Authoring")
    static bool ReimportRegionHeightmap(ALandscape* Landscape, const FString& HeightmapFile, int32 Resolution);
    /** Imports a bounded 8-bit visibility patch into the owned exploration edit layer.
     * GridOrigin uses landscape vertex coordinates. Other edit layers/paint are preserved.
     */
    UFUNCTION(BlueprintCallable, Category="Project J|World Authoring")
    static bool ImportRegionVisibilityPatch(ALandscape* Landscape, const FString& MaskFile, FIntPoint GridOrigin, FIntPoint Size);
    /** Creates a saved spline and its ribbon components. Call in bounded spatial chunks for WP. */
    UFUNCTION(BlueprintCallable, Category="Project J|World Authoring")
    static AActor* CreateSplineRibbon(const FString& Label, const TArray<FVector>& Points, UMaterialInterface* Material, float WidthCm);
};
