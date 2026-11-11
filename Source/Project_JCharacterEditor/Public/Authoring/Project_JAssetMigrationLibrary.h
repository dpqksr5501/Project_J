#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Project_JAssetMigrationLibrary.generated.h"
class USkeletalMesh;
class USkeletalMeshComponent;
class USkeleton;
class UAnimBlueprint;
class UAnimSequence;
class UBlendSpace;
class UIKRetargeter;
class APlayerController;
class UInputAction;
class UProject_JAttackDefinition;

/** Bounded authoring operations used by reviewed asset migrations. */
UCLASS()
class PROJECT_JCHARACTEREDITOR_API UProject_JAssetMigrationLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Author a new visual follower graph. Optional animation is limited to one target subtree. */
    UFUNCTION(BlueprintCallable, Category="Project J|Authoring|Animation")
    static bool ConfigureRetargetFollower(UAnimBlueprint* Blueprint, USkeletalMesh* Target, UIKRetargeter* Retargeter,
        UAnimSequence* Overlay, FName OverlayRoot);

    /** Layer Winger motion over the body input pose. Optionally use phase-matched run and sprint samples. */
    UFUNCTION(BlueprintCallable, Category="Project J|Authoring|Animation")
    static bool InstallWingerWingMotions(UAnimBlueprint* Blueprint, USkeletalMesh* Target, const TArray<UAnimSequence*>& Motions,
        UBlendSpace* RunBlend = nullptr, UAnimSequence* Sprint = nullptr);

    /** Reskin the far LOD's wing branch to the first authored idle pose and remove its bones from that LOD. Never saves. */
    UFUNCTION(BlueprintCallable, Category="Project J|Asset Migration")
    static FString BakeWingerFarWings(USkeletalMesh* Mesh, UAnimSequence* Idle);

    /** Count bind transforms that differ between the Winger mesh and its separate Skeleton asset. */
    UFUNCTION(BlueprintCallable, Category="Project J|Asset Migration")
    static int32 CountWingerBindDifferences(USkeletalMesh* Mesh);

    /** Copy only the validated local bind transforms into Winger's own Skeleton. Never saves. */
    UFUNCTION(BlueprintCallable, Category="Project J|Asset Migration")
    static bool UpdateWingerSkeletonBind(USkeletalMesh* Mesh);

    /** Align offline UE5 IK markers to their baked FK limbs, preserving the root and weapon tracks. Never saves. */
    UFUNCTION(BlueprintCallable, Category="Project J|Asset Migration")
    static bool RepairOfflineLimbMarkers(UAnimSequence* Sequence, USkeletalMesh* Mesh);

    /** Inject an Enhanced Input action into the local controller during an editor play test. */
    UFUNCTION(BlueprintCallable, Category="Project J|Authoring|Testing")
    static bool InjectPIEInput(APlayerController* Controller, const UInputAction* Action, FVector Value);

    /** Read-only preview mount for a standalone skeletal weapon mesh. */
    UFUNCTION(BlueprintCallable, Category="Project J|Authoring|Combat")
    static bool ResolveSkeletalWeaponGripPreview(USkeletalMeshComponent* Body, USkeletalMesh* Weapon,
        FName GripSocket, FTransform& OutWorldMount);

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

    /** Rebuild the runtime interpolation data after a scripted sample edit. */
    UFUNCTION(BlueprintCallable, Category="Project J|Authoring|Animation")
    static bool RebuildBlendSpaceInterpolation(UBlendSpace* BlendSpace);

    /** Match the default compression sample rate to the authored sequence rate. Refuses conflicting platform overrides and never saves. */
    UFUNCTION(BlueprintCallable, Category="Project J|Authoring|Animation")
    static bool MatchAnimationPlaybackToAuthoredRate(UAnimSequence* Sequence);
};
