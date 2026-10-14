#include "Animation/Project_JAnimationLayerLease.h"

#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"

bool FProject_JAnimationLayerLease::HasPreloadFor(const FSoftObjectPath& Path) const
{
	return PreloadedPath == Path && (PreloadedClass || PreloadHandle.IsValid() || bFailed);
}

UClass* FProject_JAnimationLayerLease::ResolveClass(const FSoftObjectPath& Path) const
{
	UClass* Class = PreloadedPath == Path ? PreloadedClass.Get() : nullptr;
	return Class ? Class : Cast<UClass>(Path.ResolveObject());
}

bool FProject_JAnimationLayerLease::PreparePreload(const FSoftObjectPath& Path)
{
	ResetPreload();
	PreloadedPath = Path;
	PreloadedClass = Cast<UClass>(Path.ResolveObject());
	return PreloadedClass != nullptr;
}

void FProject_JAnimationLayerLease::RequestPreload(const FStreamableDelegate& Completion)
{
	check(IsInGameThread());
	PreloadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(PreloadedPath, Completion);
	if (!PreloadHandle.IsValid()) { bFailed = true; }
}

bool FProject_JAnimationLayerLease::IsCurrentRequest(const FSoftObjectPath& Path, uint64 RequestGeneration) const
{
	return Generation == RequestGeneration && PreloadedPath == Path;
}

bool FProject_JAnimationLayerLease::CompletePreload()
{
	PreloadedClass = Cast<UClass>(PreloadedPath.ResolveObject());
	PreloadHandle.Reset();
	bFailed = !PreloadedClass || !PreloadedClass->IsChildOf(UAnimInstance::StaticClass());
	if (bFailed) { PreloadedClass = nullptr; }
	return !bFailed;
}

void FProject_JAnimationLayerLease::ResetPreload()
{
	++Generation;
	// Publish cancellation before touching the streamable handle: callbacks belong
	// to the old request, even when the next request uses exactly the same path.
	auto PreviousHandle = MoveTemp(PreloadHandle);
	bFailed = false;
	PreloadedClass = nullptr;
	PreloadedPath.Reset();
	if (PreviousHandle.IsValid()) { PreviousHandle->CancelHandle(); }
}

bool FProject_JAnimationLayerLease::IsLinkedTo(USkeletalMeshComponent* Mesh, UClass* Class) const
{
	auto* Master = Mesh ? Mesh->GetAnimInstance() : nullptr;
	return LinkedClass == Class && LinkedMesh == Mesh && LinkedMaster == Master && Master
		&& Master->GetLinkedAnimLayerInstanceByClass(Class);
}

bool FProject_JAnimationLayerLease::IsLinked() const
{
	return LinkedClass && IsLinkedTo(LinkedMesh.Get(), LinkedClass.Get());
}

bool FProject_JAnimationLayerLease::Link(USkeletalMeshComponent* Mesh, UClass* Class)
{
	check(IsInGameThread());
	Unlink();
	if (!Mesh || !Mesh->GetAnimInstance() || !Class || !Class->IsChildOf(UAnimInstance::StaticClass())) { return false; }
	Mesh->LinkAnimClassLayers(Class);
	LinkedClass = Class;
	LinkedMesh = Mesh;
	LinkedMaster = Mesh->GetAnimInstance();
	return true;
}

void FProject_JAnimationLayerLease::Unlink()
{
	// A replaced master can already own another layer of the same class.
	// Detach the stored ownership before invoking engine lifecycle callbacks.
	auto* Mesh = LinkedMesh.Get();
	auto* Master = LinkedMaster.Get();
	const TSubclassOf<UAnimInstance> Class = LinkedClass;
	LinkedClass = nullptr;
	LinkedMesh.Reset();
	LinkedMaster.Reset();
	if (Class && Mesh && Master && Mesh->GetAnimInstance() == Master) { Mesh->UnlinkAnimClassLayers(Class); }
}
