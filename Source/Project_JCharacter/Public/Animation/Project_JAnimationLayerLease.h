#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "Engine/StreamableManager.h"
#include "Project_JAnimationLayerLease.generated.h"

class UAnimInstance;
class USkeletalMeshComponent;

/**
 * One component's game-thread-only linked-layer resources. Combat/mount selection
 * stays with that component; this value owns only loading and linking lifetime.
 * Embed as a transient, DuplicateTransient UPROPERTY so class pins reach GC and
 * duplicated actors start with no borrowed request or mesh/master ownership.
 */
USTRUCT()
struct PROJECT_JCHARACTER_API FProject_JAnimationLayerLease
{
	GENERATED_BODY()

	UPROPERTY(Transient) TSubclassOf<UAnimInstance> LinkedClass;
	UPROPERTY(Transient) TSubclassOf<UAnimInstance> PreloadedClass;
	FSoftObjectPath PreloadedPath;
	TSharedPtr<FStreamableHandle> PreloadHandle;
	uint64 Generation = 0;
	bool bFailed = false;
	TWeakObjectPtr<USkeletalMeshComponent> LinkedMesh;
	TWeakObjectPtr<UAnimInstance> LinkedMaster;

	bool HasPreloadFor(const FSoftObjectPath& Path) const;
	UClass* ResolveClass(const FSoftObjectPath& Path) const;
	/** Invalidates the previous request; true means the class is already resident. */
	bool PreparePreload(const FSoftObjectPath& Path);
	/** Completion must be weakly bound to the owning component and this generation. */
	void RequestPreload(const FStreamableDelegate& Completion);
	bool IsCurrentRequest(const FSoftObjectPath& Path, uint64 RequestGeneration) const;
	/** Call only for a current request. Failed classes remain cached until reset. */
	bool CompletePreload();
	void ResetPreload();

	bool IsLinkedTo(USkeletalMeshComponent* Mesh, UClass* Class) const;
	bool IsLinked() const;
	bool Link(USkeletalMeshComponent* Mesh, UClass* Class);
	void Unlink();
};
