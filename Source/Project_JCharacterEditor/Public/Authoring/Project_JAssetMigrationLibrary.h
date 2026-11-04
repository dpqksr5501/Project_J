#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Project_JAssetMigrationLibrary.generated.h"
class USkeletalMesh;
class USkeleton;
class UAnimBlueprint;
class UProject_JAttackDefinition;

/** Bounded authoring operations used by reviewed asset migrations. */
UCLASS()
class PROJECT_JCHARACTEREDITOR_API UProject_JAssetMigrationLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Bake one forward, non-looping native melee window from RAW sequence poses.
     * Mesh-component gameplay geometry; excludes blended poses, IK and cloth.
     * Dry-run never edits. Execute replaces only the selected attack's path;
     * save writes only its package. Unsupported sources fail without fallback.
     */
    UFUNCTION(BlueprintCallable, Category="Project J|Authoring|Combat")
    static FString BakeCanonicalMeleeTrajectory(UProject_JAttackDefinition* Attack, USkeletalMesh* SourceMesh, bool bExecute=false, bool bSave=false);

    /** Selected non-MM Unarmed template recipe. Dry-run is read-only; execute/save
     * are explicit. Saves only a new target package and never binds an NPC asset.
     * Returns bounded JSON result/threshold/validation evidence, including failures.
     */
    UFUNCTION(BlueprintCallable, Category="Project J|Authoring|Migration")
    static FString CreateNPCMoverUnarmed(UAnimBlueprint* Template, const FString& DestinationPackage, bool bExecute=false, bool bSave=false);
    /** Initialize a persistent skeleton for identical imported reference rigs; refuse unrelated assignments. */
    UFUNCTION(BlueprintCallable, Category="Project J|Authoring|Migration")
    static USkeleton* CreateSharedSkeleton(const TArray<USkeletalMesh*>& Meshes, const FString& PackageName);

    UFUNCTION(BlueprintPure, Category="Project J|Authoring|Migration")
    static FString InspectReferenceSkeleton(USkeletalMesh* Mesh);

    /** Mesh-only socket; shared skeleton users are unaffected. Refuse conflicting sockets. */
    UFUNCTION(BlueprintCallable, Category="Project J|Authoring|Migration")
    static bool AddMeshSocket(USkeletalMesh* Mesh, FName Name, FName Bone, FTransform Relative);
};
