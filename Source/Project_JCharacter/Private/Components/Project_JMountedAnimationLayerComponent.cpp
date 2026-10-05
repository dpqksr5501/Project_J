// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/Project_JMountedAnimationLayerComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/Project_JRiderAnimationProfile.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Mount/Project_JMountCharacter.h"
#include "Mount/Project_JMountComponent.h"
#include "Project_JPlayerCharacter.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJMountedAnimationLayer, Log, All);

UProject_JMountedAnimationLayerComponent::UProject_JMountedAnimationLayerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UProject_JMountedAnimationLayerComponent::BeginPlay()
{
	bEndingPlay = false;
	Super::BeginPlay();

	if (AProject_JPlayerCharacter* Player = Cast<AProject_JPlayerCharacter>(GetOwner()))
	{
		if (UProject_JMountComponent* MountComponent = Player->GetMountComponent())
		{
			MountComponent->OnMountChanged.AddUniqueDynamic(this, &ThisClass::HandleMountChanged);
			PreloadLayerForMount(MountComponent->GetMountedMount());
		}
	}

	RefreshLayer();
}

void UProject_JMountedAnimationLayerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	if (AProject_JPlayerCharacter* Player = Cast<AProject_JPlayerCharacter>(GetOwner()))
	{
		if (UProject_JMountComponent* MountComponent = Player->GetMountComponent())
		{
			MountComponent->OnMountChanged.RemoveDynamic(this, &ThisClass::HandleMountChanged);
		}
	}

	UnlinkLayer();
	ResetPreload();
	Super::EndPlay(EndPlayReason);
}

void UProject_JMountedAnimationLayerComponent::RefreshLayer()
{
	AProject_JPlayerCharacter* Player = Cast<AProject_JPlayerCharacter>(GetOwner());
	if (bEndingPlay || !Player || Player->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const UProject_JMountComponent* MountComponent = Player->GetMountComponent();
	const AProject_JMountCharacter* MountedMount = MountComponent ? MountComponent->GetMountedMount() : nullptr;
	const FSoftObjectPath RequestedPath = ResolveLayerPath(MountedMount);
	if (PreloadedAnimationLayerPath == RequestedPath && bLoadFailed) { UnlinkLayer(); return; }
	if (!MountedMount || RequestedPath.IsNull())
	{
		UnlinkLayer();
		return;
	}

	UClass* LoadedLayerClass = nullptr;
	if (PreloadedAnimationLayerPath == RequestedPath)
	{
		LoadedLayerClass = PreloadedAnimationLayerClass.Get();
	}
	if (!LoadedLayerClass)
	{
		LoadedLayerClass = Cast<UClass>(RequestedPath.ResolveObject());
	}

	if (!LoadedLayerClass)
	{
		// Never block the game thread on a first-time mount. The master's default
		// MountedLocomotion pose remains active until the async request completes.
		UnlinkLayer();
		PreloadLayerForMount(MountedMount);
		return;
	}

	if (!LoadedLayerClass->IsChildOf(UAnimInstance::StaticClass()))
	{
		PreloadedAnimationLayerPath = RequestedPath;
		bLoadFailed = true;
		UE_LOG(LogProjectJMountedAnimationLayer, Error,
			TEXT("Rider layer is not an AnimInstance. Owner=%s Mount=%s Layer=%s"),
			*GetNameSafe(Player), *GetNameSafe(MountedMount), *GetNameSafe(LoadedLayerClass));
		UnlinkLayer();
		return;
	}

	USkeletalMeshComponent* CurrentMesh = Player->GetMesh();
	UAnimInstance* CurrentMaster = CurrentMesh ? CurrentMesh->GetAnimInstance() : nullptr;
	if (LinkedAnimationLayerClass == LoadedLayerClass && LinkedMesh == CurrentMesh
		&& LinkedMaster == CurrentMaster && CurrentMaster
		&& CurrentMaster->GetLinkedAnimLayerInstanceByClass(LoadedLayerClass))
	{
		return;
	}

	UnlinkLayer();
	if (USkeletalMeshComponent* PlayerMesh = Player->GetMesh(); PlayerMesh && PlayerMesh->GetAnimInstance())
	{
		PlayerMesh->LinkAnimClassLayers(LoadedLayerClass);
		LinkedAnimationLayerClass = LoadedLayerClass;
		LinkedMesh = PlayerMesh;
		LinkedMaster = PlayerMesh->GetAnimInstance();
		UE_LOG(LogProjectJMountedAnimationLayer, Verbose,
			TEXT("Rider layer linked. Owner=%s Mount=%s Layer=%s"),
			*GetNameSafe(Player), *GetNameSafe(MountedMount), *GetNameSafe(LoadedLayerClass));
	}
}

void UProject_JMountedAnimationLayerComponent::PreloadLayerForMount(const AProject_JMountCharacter* Mount)
{
	const AProject_JPlayerCharacter* Player = Cast<AProject_JPlayerCharacter>(GetOwner());
	if (bEndingPlay || !Player || Player->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// A dismount keeps the summoned mount warm for a likely remount. Replication
	// clearing both active and summoned mounts releases the one-entry cache.
	const AProject_JMountCharacter* CandidateMount = Mount;
	if (!CandidateMount)
	{
		const UProject_JMountComponent* MountComponent = Player->GetMountComponent();
		CandidateMount = MountComponent ? MountComponent->GetMountedMount() : nullptr;
	}
	if (!CandidateMount)
	{
		CandidateMount = Player->GetSummonedMount();
	}
	if (!CandidateMount)
	{
		ResetPreload();
		return;
	}

	const FSoftObjectPath RequestedPath = ResolveLayerPath(CandidateMount);
	if (RequestedPath.IsNull())
	{
		return;
	}

	if (PreloadedAnimationLayerPath == RequestedPath &&
		(PreloadedAnimationLayerClass || LayerPreloadHandle.IsValid() || bLoadFailed))
	{
		return;
	}

	ResetPreload();
	PreloadedAnimationLayerPath = RequestedPath;
	if (UClass* AlreadyLoadedClass = Cast<UClass>(RequestedPath.ResolveObject()))
	{
		PreloadedAnimationLayerClass = AlreadyLoadedClass;
		RefreshLayer();
		return;
	}

	LayerPreloadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		RequestedPath,
		FStreamableDelegate::CreateUObject(this, &ThisClass::HandleLayerPreloadCompleted, RequestedPath, LoadGeneration));
	if (!LayerPreloadHandle.IsValid()) { bLoadFailed = true; }
}

void UProject_JMountedAnimationLayerComponent::HandleMountChanged(
	AProject_JMountCharacter* PreviousMount,
	AProject_JMountCharacter* NewMount)
{
	PreloadLayerForMount(NewMount);
	RefreshLayer();
}

FSoftObjectPath UProject_JMountedAnimationLayerComponent::ResolveLayerPath(const AProject_JMountCharacter* Mount) const
{
	if (!Mount)
	{
		return FSoftObjectPath();
	}

	if (const UProject_JRiderAnimationProfile* Profile = Mount->GetRiderAnimationProfile())
	{
		if (!Profile->AnimationLayerClass.IsNull())
		{
			return Profile->AnimationLayerClass.ToSoftObjectPath();
		}
	}

	const AProject_JPlayerCharacter* Player = Cast<AProject_JPlayerCharacter>(GetOwner());
	return Player ? Player->GetFallbackMountedAnimationLayerClass().ToSoftObjectPath() : FSoftObjectPath();
}

void UProject_JMountedAnimationLayerComponent::HandleLayerPreloadCompleted(FSoftObjectPath RequestedPath, uint64 RequestGeneration)
{
	if (bEndingPlay || RequestGeneration != LoadGeneration || PreloadedAnimationLayerPath != RequestedPath) { return; }
	PreloadedAnimationLayerClass = Cast<UClass>(RequestedPath.ResolveObject());
	LayerPreloadHandle.Reset();
	bLoadFailed = !PreloadedAnimationLayerClass || !PreloadedAnimationLayerClass->IsChildOf(UAnimInstance::StaticClass());
	if (bLoadFailed)
	{
		PreloadedAnimationLayerClass = nullptr;
		UE_LOG(LogProjectJMountedAnimationLayer, Warning, TEXT("Layer load failed; default pose retained until path change or explicit retry. Path=%s"), *RequestedPath.ToString());
		return;
	}
	RefreshLayer();
}

void UProject_JMountedAnimationLayerComponent::UnlinkLayer()
{
	// Unlink from the original mesh/master only. A replacement master may
	// already have another owner's layer of the same class.
	USkeletalMeshComponent* Mesh = LinkedMesh.Get();
	if (LinkedAnimationLayerClass && Mesh && LinkedMaster.IsValid() && Mesh->GetAnimInstance() == LinkedMaster.Get())
	{
		Mesh->UnlinkAnimClassLayers(LinkedAnimationLayerClass);
	}
	LinkedAnimationLayerClass = nullptr;
	LinkedMesh.Reset();
	LinkedMaster.Reset();
}

void UProject_JMountedAnimationLayerComponent::ResetPreload()
{
	++LoadGeneration;
	bLoadFailed = false;
	if (LayerPreloadHandle.IsValid())
	{
		LayerPreloadHandle->CancelHandle();
	}
	LayerPreloadHandle.Reset();
	PreloadedAnimationLayerClass = nullptr;
	PreloadedAnimationLayerPath.Reset();
}

void UProject_JMountedAnimationLayerComponent::RetryLayerLoad()
{
	if (bEndingPlay) { return; }
	ResetPreload();
	PreloadLayerForMount(nullptr);
	RefreshLayer();
}

bool UProject_JMountedAnimationLayerComponent::IsMountedLayerLinked() const
{
	const auto* Mesh = LinkedMesh.Get();
	const auto* Master = LinkedMaster.Get();
	return LinkedAnimationLayerClass && Mesh && Master && Mesh->GetAnimInstance() == Master
		&& Master->GetLinkedAnimLayerInstanceByClass(LinkedAnimationLayerClass);
}
