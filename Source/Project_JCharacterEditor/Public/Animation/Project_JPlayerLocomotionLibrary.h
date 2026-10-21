#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Project_JPlayerLocomotionLibrary.generated.h"

class UAnimBlueprint;
class UAnimInstance;
class UBlueprint;
class USkeletalMesh;
class UChooserTable;
class UIKRetargeter;
class UAnimationAsset;
class USkeleton;
class UAnimSequence;
class UMirrorDataTable;
class UPoseSearchDatabase;

UCLASS()
class PROJECT_JCHARACTEREDITOR_API UProject_JPlayerLocomotionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
 /** Read-only comparison of the runtime retarget output, final body pose and a fresh processor using the same source pose. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Diagnostics")
 static FString InspectVisualRetargetPose(UAnimInstance* Instance);
 /** Evaluate the selected sequence on the exact source/target meshes used by a retargeter; no live instance mutation. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Diagnostics")
 static FString InspectRetargetAnimation(UIKRetargeter* Retargeter, UAnimSequence* Sequence, USkeletalMesh* Source, USkeletalMesh* Target, double Time);
 /** Align explicit equal-count OneToOne FK chain segments by joint order; source pose only, no save. Returns changed bones or INDEX_NONE on incompatible chains. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static int32 AlignOneToOneRetargetChains(UIKRetargeter* Retargeter, const TArray<FName>& TargetChains);
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
 static FString ExportNativeLocomotionContract(UBlueprint* Target);
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
	/** Replace calculation graphs with native adapters without changing pose nodes or Chooser schemas. */
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
	static bool InstallNativeLocomotionLogic(UAnimBlueprint* Target, UBlueprint* GroundMode);
	/** Duplicate the actual 5.8 foundation and replace its gameplay input backend. */
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
	static UAnimBlueprint* PrepareFoundation(UAnimBlueprint* Source, const FString& DestinationPackage);
	/** Remove crouch-only rows and stance-change fallback from the standing-only foundation. */
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
	static int32 ConfigureStandingLocomotion(UChooserTable* Root);
	/** Remap a duplicated foundation's self references and remove proven unused local variables. */
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
	static int32 CleanImportedBlueprintReferences(UBlueprint* Source,UBlueprint* Target);
	/** Rebind the humanoid body graph default to its explicit source-family retargeter. */
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
	static int32 RebindGameplayRetargeter(UAnimBlueprint* Target,UIKRetargeter* Previous,UIKRetargeter* Replacement);
	/** Rebind only the imported namespace's animation-class Chooser parameters. */
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
	static int32 RebindFoundationChoosers(UAnimBlueprint* Source, UAnimBlueprint* Target);
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion Authoring")
	static bool RestoreFoundationTrajectory(UAnimBlueprint* Source, UAnimBlueprint* Target);
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
