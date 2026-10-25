#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Project_JPlayerLocomotionLibrary.generated.h"

class UAnimBlueprint;
class UAnimInstance;
class UBlueprint;
class USkeletalMesh;
class USkeletalMeshComponent;
class UChooserTable;
class UIKRetargeter;
class UAnimationAsset;
class USkeleton;
class UAnimSequence;
class UMirrorDataTable;
class UPoseSearchDatabase;
class UBlendSpace;
class UPhysicsAsset;
class UIKRigDefinition;

UCLASS()
class PROJECT_JCHARACTEREDITOR_API UProject_JPlayerLocomotionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
 /** Bake bounded sole skin samples into the explicitly selected footwear/body mesh. Standard geometry/weights stay unchanged. Re-run after mesh/bind changes. Refuses PIE; never saves. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static FString AuthorFootSupport(USkeletalMesh* Mesh,FName LeftFoot,FName RightFoot,float SoleBand=3.5f,float Clearance=.15f,float MaxLift=10.f);
 /** Install optional mesh-driven sole clearance before existing body-contact stages; never adds a common-master writer or a second pose producer. Refuses duplicate/ambiguous input and PIE; never saves. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static bool InstallFootClearance(UAnimBlueprint* Blueprint);
 /** Install current-input wrist goal publication, standard full-body contact IK and final arm refinement in a direct post process. Preserves input feet, blends contact alpha once, refuses retarget graphs/duplicate stages and PIE. Never saves. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static bool InstallDirectBodyContactIK(UAnimBlueprint* Blueprint, UIKRigDefinition* Rig);
 /** Bind existing body RigidBody nodes to the shared cosmetic-quality snapshot. Works for direct post processes and retarget followers; refuses conflicting alpha writers and PIE. Never saves. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static bool BindBodySecondaryMotion(UAnimBlueprint* Blueprint);
 /** Publish four final-pose limb markers before direct-body contact. Preserve authored ik_hand_gun by default; explicitly derive it from hand_r only for a wrist-reference contract. Keeps marker writers out of the common master. Refuses PIE/conflicting writers/retarget graphs; never saves. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static bool InstallBodyPoseMarkers(UAnimBlueprint* Blueprint, bool RebuildWeaponReferenceFromHand=false);
 /** Insert the standard IK Rig solve between a visual retargeter and its hand refinement. Uses existing wrist targets/alphas and pins both feet to the incoming pose. Refuses PIE, duplicate solves and mismatched goals; never saves. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static bool InstallHandContactBodyIK(UAnimBlueprint* Blueprint, UIKRigDefinition* Rig);
 /** Copy the existing named blend/slot contract to a separately validated compatible body. Shared parents must match; refuses conflicting authored profiles and PIE. Never saves or changes the source skeleton. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static bool PrepareCompatibleBody(USkeletalMesh* Source, USkeletalMesh* Target);
 /** Replace a copied visual retarget graph's sole retarget node with the post-process input pose. Keeps hand contact and secondary physics nodes; refuses PIE or ambiguous graphs. Never saves. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static bool UseCompatibleBodyInput(UAnimBlueprint* Blueprint, USkeletalMesh* Target);
 /** Rebase an explicitly supplied physics copy through verified mesh coordinates and bone names. PreserveBoneLocalFrames is for preserved legacy axes reposed with their skinned geometry, and requires an identity mesh basis. Preserves body order, collision pairs, masses, limits and solver settings; preflights every body/constraint. Refuses PIE/unsupported geometry; never saves. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static bool RebaseCompatiblePhysics(UPhysicsAsset* Physics, USkeletalMesh* Previous, USkeletalMesh* Target, const TMap<FName,FName>& BoneMap, FTransform SourceToTarget, bool PreserveBoneLocalFrames=false);
 /** Create standard embedded Chaos clothing for each LOD0 section, with a pinned upper band and a smooth max-distance transition. Refuses PIE/existing clothing; never saves. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static FString CreateSurfaceClothing(USkeletalMesh* Mesh, UPhysicsAsset* Collisions, float PinnedTopDepth=8.f, float TransitionDepth=25.f, float MaxDistance=70.f);
 /** Rebuild embedded cloth caches and section bindings after authoring. Refuses PIE or a non section-per-asset layout; never saves. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static bool RefreshSurfaceClothing(USkeletalMesh* Mesh);
 /** Copy matching per-section cloth configuration objects, preserving shared config identity. Leaves target geometry, masks and collision asset intact; refuses PIE/mismatched classes. Never saves or refreshes. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static bool CopySurfaceClothingConfiguration(USkeletalMesh* Previous, USkeletalMesh* Target);
 /** Paint a smooth 0..1 bending weight only within the lowest HemDepth cm of each embedded cloth section. Preserves fixed vertices and max-distance masks; refreshes bindings but never saves. Refuses PIE/unsupported targets or conflicting artist masks. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static FString PaintSurfaceClothingHemBending(USkeletalMesh* Mesh, float HemDepth=12.f);
 /** Remove only the RigidBody node using the explicitly supplied physics asset, reconnecting its pose input/output. Refuses PIE; never saves. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static bool RemoveSecondaryRigidBody(UAnimBlueprint* Blueprint, UPhysicsAsset* PhysicsAsset);
 /** Limit grounded landing pose searches to the authored impact phase, using Land/BlockTransition timing. Preserves animation data, curves and airborne searches. No save; refuses PIE; INDEX_NONE on incompatible data. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static int32 ConfigureGroundedLandingSearch(UChooserTable* Root);
 /** Restrict standing Sprint stops to the recent Sprint gait before pose matching. Keeps low-speed Walk stops, timing, row order and search data. No save; refuses PIE. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static bool ConfigureStandingStopGaitFilter(UChooserTable* Root);
 /** Add pose-matched backward-to-backward facing turns from the table's authored Reface entries. Preserves low-speed starts and other direction families. No save; returns added rows or INDEX_NONE on an unfamiliar layout. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static int32 ConfigureBackwardFacingTransitions(UChooserTable* Root);
 /** Set an explicit upper stride bound on authored nodes and compile their defaults. Preserves interpolation, FK reach limits, bindings and timing. No save. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static bool ConfigureStrideWarpingMaximum(UAnimBlueprint* Blueprint, float Maximum);
 /** Migrate the reviewed rotation-curve Chooser input; keep every row and curve result. No save. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static bool UseNativeRotationOffsetQuery(UChooserTable* Table);
 /** Use the same normalized timeline for search and playback, rebuilding the serialized marker cache. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static bool ConfigureNormalizedLocomotionSync(UBlendSpace* BlendSpace);
 /** Versioned raw motion/bind-pose fingerprint, independent of asset name, package path and generated GUIDs. Editor-only; never mutates or saves. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static FString GetAnimationMotionFingerprint(UAnimSequence* Sequence);
 /** Authored notify properties/payloads without generated event GUIDs or the owning sequence path. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static FString GetAnimationNotifyFingerprint(UAnimSequence* Sequence);
 /** Read-only comparison of the runtime retarget output, final body pose and a fresh processor using the same source pose. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Diagnostics")
 static FString InspectVisualRetargetPose(UAnimInstance* Instance);
 /** Read final secondary-bone poses and completed cloth particle samples. No CVar, asset or simulation reset. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Diagnostics")
 static FString InspectSecondaryPhysics(USkeletalMeshComponent* Mesh);
 /** Read actual Nanite render-object selection and existing render LODs without changing resources or pose. Intended for before/after profiling; no save. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Diagnostics")
 static FString InspectSkinnedMeshRendering(USkeletalMeshComponent* Mesh);
 /** Read completed cloth simulation topology when active, otherwise LOD bone skinning, in world space. Supports LeaderPose followers; diagnostic only, no simulation/asset mutation. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Diagnostics")
 static FString InspectSkinnedMeshSurface(USkeletalMeshComponent* Mesh, int32 LODIndex = 0);
 /** Read completed sole-plane solve diagnostics for authoring; no asset/pose mutation. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Diagnostics")
 static FString InspectFootClearance(USkeletalMeshComponent* Mesh);
 /** Explicitly retire embedded vertex-cloth data using the editor unbind lifecycle. Keeps skinning, skeleton and physics assets; no save. Refuses PIE or externally owned cloth. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static int32 RetireEmbeddedClothingData(USkeletalMesh* Mesh);
 /** Load explicitly copied upstream animation packages with scoped import remaps. Removes upstream BranchIn registration only; never writes the upstream project or saves packages. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static TArray<UAnimSequence*> PrepareStagedAnimationSources(const TArray<FString>& StagedPackages, const TMap<FString,FString>& PackageRemaps);
 /** Evaluate the selected sequence on the exact source/target meshes used by a retargeter; no live instance mutation. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Diagnostics")
 static FString InspectRetargetAnimation(UIKRetargeter* Retargeter, UAnimSequence* Sequence, USkeletalMesh* Source, USkeletalMesh* Target, double Time);
 /** Align explicit equal-count OneToOne FK chain segments by joint order; source pose only, no save. Returns changed bones or INDEX_NONE on incompatible chains. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static int32 AlignOneToOneRetargetChains(UIKRetargeter* Retargeter, const TArray<FName>& TargetChains);
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static FString ExportNativeLocomotionContract(UBlueprint* Target);
 /** Repair class-owned inline archetype visibility for inheritance. Does not change movement settings or compile/save. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static int32 FinalizePlayerAvatarTemplates(UBlueprint* Target);
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static bool InstallNativeLocomotionPins(UAnimBlueprint* Target);
 /** Inline pure native query adapters, updating authored calls and Chooser bindings together. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static TArray<FString> SimplifyNativeLocomotionQueries(UAnimBlueprint* Target, const TArray<UChooserTable*>& Choosers);
 /** Replace the reviewed unit-scale wrist recomposition with the existing native target. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static bool UseNativeRetargetWristTarget(UAnimBlueprint* Target);
	/** Rebind Chooser output profile names to the copied Player_* profiles; leave independent Manny profiles intact. */
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
	static int32 RemapLocomotionBlendProfileNames(UChooserTable* Chooser, USkeleton* Source, USkeleton* Target);
	/** Refresh bone/curve/notify mirror mappings against the actual target skeleton. */
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
	static bool RefreshLocomotionMirrorTable(UMirrorDataTable* Table, USkeleton* Skeleton);
	/** Remove an explicitly reviewed candidate from one database; retain the animation itself. */
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
	static int32 RemoveLocomotionDatabaseCandidate(UPoseSearchDatabase* Database, UAnimationAsset* Candidate);
	/** Refresh a retargeted sequence in place, preserving its object identity and authored metadata. Never deletes either asset. */
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
	static bool UpdateRetargetedAnimationData(UAnimSequence* Target, UAnimSequence* Converted);
	/** Rebind an explicit owner's serialized animation/data references, including nested Chooser databases. Never consolidates or deletes the source. */
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
	static int32 RemapLocomotionAssets(UObject* Owner, const TArray<UObject*>& Sources, const TArray<UObject*>& Replacements);
	/** Keep authored notify payloads; replace the movement decision graph with one native call. */
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
	static bool InstallNativeTransitionNotify(UBlueprint* Notify);
	/** Prune excluded sample paths from the native production foundation; retain pose/data contracts. */
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
	static TArray<FString> CleanNativeLocomotionBlueprint(UAnimBlueprint* Target);
	/** Remove only disconnected default actor/animation events, without touching construction or pose logic. */
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
	static int32 CleanDisconnectedLocomotionEvents(UBlueprint* Target);
	/** Remap a duplicated foundation's self references and remove proven unused local variables. */
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
	static int32 CleanImportedBlueprintReferences(UBlueprint* Source,UBlueprint* Target);
	/** Rebind the humanoid body graph default to its explicit source-family retargeter. */
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
	static int32 RebindGameplayRetargeter(UAnimBlueprint* Target,UIKRetargeter* Previous,UIKRetargeter* Replacement);
	/** Full-body skills own the final on-foot pose; GASP foot IK must not overwrite their legs. */
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
	static bool RepairFullBodyMontageOrder(UAnimBlueprint* Target);
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
	static bool ConfigureGameplayAvatar(UBlueprint* Avatar, USkeletalMesh* SourceMesh, UAnimBlueprint* Foundation);
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
	static UChooserTable* DuplicateProjectAirChooser(UChooserTable* Template, const FString& DestinationPackage);
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
	static bool InstallProjectAir(UAnimBlueprint* Target, UChooserTable* Chooser);
 UFUNCTION(BlueprintCallable,Category="Project J|Player Locomotion Authoring")
 static bool RestoreFoundationFallOff(UAnimBlueprint* Target);
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static TArray<FString> InspectProjectAirCoverage(UChooserTable* Chooser);
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static TArray<FString> InspectGameplaySockets(USkeletalMesh* PreviousSource, USkeletalMesh* NewSource);
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static bool MigrateGameplaySockets(USkeletalMesh* PreviousSource, USkeletalMesh* NewSource, const TArray<FName>& RequiredSockets);
	/** Bind the existing GASP graph to a compatible Manny source, prepared chooser and matching foot rig. No sequence conversion. */
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
	static bool ConfigureCompatibleFoundation(UAnimBlueprint* Target, USkeletalMesh* SourceMesh, UChooserTable* GroundChooser, UBlueprint* FootRig);
};
