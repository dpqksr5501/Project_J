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

void UProject_JMountedAnimationLayerComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	bEndingPlay = true;
	UnlinkLayer();
	ResetPreload();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
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
	if (Layer.PreloadedPath == RequestedPath && Layer.bFailed) { UnlinkLayer(); return; }
	if (!MountedMount || RequestedPath.IsNull())
	{
		UnlinkLayer();
		return;
	}

	UClass* LoadedLayerClass = Layer.ResolveClass(RequestedPath);

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
		Layer.PreloadedPath = RequestedPath;
		Layer.bFailed = true;
		UE_LOG(LogProjectJMountedAnimationLayer, Error,
			TEXT("Rider layer is not an AnimInstance. Owner=%s Mount=%s Layer=%s"),
			*GetNameSafe(Player), *GetNameSafe(MountedMount), *GetNameSafe(LoadedLayerClass));
		UnlinkLayer();
		return;
	}

	if (Layer.IsLinkedTo(Player->GetMesh(), LoadedLayerClass))
	{
		return;
	}

	if (USkeletalMeshComponent* PlayerMesh = Player->GetMesh(); Layer.Link(PlayerMesh, LoadedLayerClass))
	{
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

	if (Layer.HasPreloadFor(RequestedPath))
	{
		return;
	}

	if (Layer.PreparePreload(RequestedPath))
	{
		RefreshLayer();
		return;
	}
	Layer.RequestPreload(FStreamableDelegate::CreateUObject(this, &ThisClass::HandleLayerPreloadCompleted, RequestedPath, Layer.Generation));
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
	if (bEndingPlay || !Layer.IsCurrentRequest(RequestedPath, RequestGeneration)) { return; }
	if (!Layer.CompletePreload())
	{
		UE_LOG(LogProjectJMountedAnimationLayer, Warning, TEXT("Layer load failed; default pose retained until path change or explicit retry. Path=%s"), *RequestedPath.ToString());
		return;
	}
	RefreshLayer();
}

void UProject_JMountedAnimationLayerComponent::UnlinkLayer()
{
	Layer.Unlink();
}

void UProject_JMountedAnimationLayerComponent::ResetPreload()
{
	Layer.ResetPreload();
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
	return Layer.IsLinked();
}
