// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/Project_JCombatAnimationLayerComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/Project_JWeaponAnimProfile.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Mount/Project_JMountComponent.h"
#include "Project_JPlayerCharacter.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJCombatAnimationLayer, Log, All);

UProject_JCombatAnimationLayerComponent::UProject_JCombatAnimationLayerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UProject_JCombatAnimationLayerComponent::BeginPlay()
{
	bEndingPlay = false;
	Super::BeginPlay();
	PreloadLayerForCurrentWeapon();
	RefreshLayer();
}

void UProject_JCombatAnimationLayerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	UnlinkLayer();
	ResetPreload();
	Super::EndPlay(EndPlayReason);
}

void UProject_JCombatAnimationLayerComponent::RefreshLayer()
{
	AProject_JPlayerCharacter* Player = Cast<AProject_JPlayerCharacter>(GetOwner());
	if (bEndingPlay || !Player || Player->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const UProject_JWeaponAnimProfile* WeaponProfile = Player->GetWeaponAnimProfile();
	PresentationState = CalculatePresentationState(*Player);
	const bool bShouldLinkLayer =
		(PresentationState == EProject_JCombatAnimationLayerState::PreparingCombat ||
			PresentationState == EProject_JCombatAnimationLayerState::CombatActive) &&
		WeaponProfile &&
		!WeaponProfile->CombatAnimationLayerClass.IsNull();
	UE_LOG(LogProjectJCombatAnimationLayer, Verbose, TEXT("RefreshLayer Owner=%s State=%d WeaponProfile=%s ExistingLayer=%s ShouldLink=%s"), *GetNameSafe(Player), static_cast<int32>(PresentationState), *GetNameSafe(WeaponProfile), *GetNameSafe(LinkedAnimationLayerClass.Get()), bShouldLinkLayer ? TEXT("true") : TEXT("false"));
	if (!bShouldLinkLayer)
	{
		UnlinkLayer();
		return;
	}

	const FSoftObjectPath RequestedPath = ResolveLayerPath(WeaponProfile);
	if (PreloadedAnimationLayerPath == RequestedPath && bLoadFailed) { UnlinkLayer(); return; }
	UClass* LoadedLayerClass = PreloadedAnimationLayerPath == RequestedPath
		? PreloadedAnimationLayerClass.Get()
		: nullptr;
	if (!LoadedLayerClass)
	{
		LoadedLayerClass = Cast<UClass>(RequestedPath.ResolveObject());
	}

	if (!LoadedLayerClass)
	{
		// Combat input must not synchronously load an AnimBP. The master layer's
		// default implementation remains active until streaming completes.
		UnlinkLayer();
		PreloadLayerForCurrentWeapon();
		return;
	}

	if (!LoadedLayerClass->IsChildOf(UAnimInstance::StaticClass()))
	{
		PreloadedAnimationLayerPath = RequestedPath;
		bLoadFailed = true;
		UE_LOG(LogProjectJCombatAnimationLayer, Error, TEXT("Layer class load failed or is not an AnimInstance. Owner=%s RequestedClass=%s"), *GetNameSafe(Player), *GetNameSafe(LoadedLayerClass));
		UnlinkLayer();
		return;
	}

	USkeletalMeshComponent* CurrentMesh = Player->GetMesh();
	UAnimInstance* CurrentMaster = CurrentMesh ? CurrentMesh->GetAnimInstance() : nullptr;
	if (LinkedAnimationLayerClass == LoadedLayerClass && LinkedMesh == CurrentMesh
		&& LinkedMaster == CurrentMaster && CurrentMaster
		&& CurrentMaster->GetLinkedAnimLayerInstanceByClass(LoadedLayerClass))
	{
		UE_LOG(LogProjectJCombatAnimationLayer, Verbose, TEXT("Layer already linked. Owner=%s Layer=%s"), *GetNameSafe(Player), *GetNameSafe(LoadedLayerClass));
		return;
	}

	UnlinkLayer();
	if (USkeletalMeshComponent* PlayerMesh = Player->GetMesh(); PlayerMesh && PlayerMesh->GetAnimInstance())
	{
		PlayerMesh->LinkAnimClassLayers(LoadedLayerClass);
		LinkedAnimationLayerClass = LoadedLayerClass;
		LinkedMesh = PlayerMesh;
		LinkedMaster = PlayerMesh->GetAnimInstance();
		UE_LOG(LogProjectJCombatAnimationLayer, Log, TEXT("Layer linked. Owner=%s Layer=%s MasterAnimInstance=%s"), *GetNameSafe(Player), *GetNameSafe(LoadedLayerClass), *GetNameSafe(PlayerMesh->GetAnimInstance()));
	}
	else
	{
		UE_LOG(LogProjectJCombatAnimationLayer, Error, TEXT("Layer link failed: player mesh is missing. Owner=%s"), *GetNameSafe(Player));
	}
}

void UProject_JCombatAnimationLayerComponent::PreloadLayerForCurrentWeapon()
{
	AProject_JPlayerCharacter* Player = Cast<AProject_JPlayerCharacter>(GetOwner());
	if (bEndingPlay || !Player || Player->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const UProject_JWeaponAnimProfile* WeaponProfile = Player->GetWeaponAnimProfile();
	if (!WeaponProfile || WeaponProfile->CombatAnimationLayerClass.IsNull())
	{
		ResetPreload();
		return;
	}

	const FSoftObjectPath RequestedPath = ResolveLayerPath(WeaponProfile);
	if (PreloadedAnimationLayerPath == RequestedPath && (PreloadedAnimationLayerClass || LayerPreloadHandle.IsValid() || bLoadFailed))
	{
		return;
	}

	const EProject_JCombatAnimationLayerState DesiredState = CalculatePresentationState(*Player);
	const bool bShouldPreloadNow = Player->IsLocallyControlled() ||
		DesiredState == EProject_JCombatAnimationLayerState::PreparingCombat ||
		DesiredState == EProject_JCombatAnimationLayerState::CombatActive;
	if (!bShouldPreloadNow)
	{
		// In a crowded MMO scene, do not stream every inactive simulated proxy's
		// combat layer merely because its equipped style replicated.
		if (PreloadedAnimationLayerPath != RequestedPath)
		{
			ResetPreload();
		}
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

EProject_JCombatAnimationLayerState UProject_JCombatAnimationLayerComponent::CalculatePresentationState(const AProject_JPlayerCharacter& Player) const
{
	const UProject_JMountComponent* MountComponent = Player.GetMountComponent();
	if (MountComponent && MountComponent->IsMounted())
	{
		return EProject_JCombatAnimationLayerState::SuppressedByMount;
	}

	if (Player.IsCombatIntroPlaying())
	{
		return EProject_JCombatAnimationLayerState::PreparingCombat;
	}

	return Player.IsCombatModeActive()
		? EProject_JCombatAnimationLayerState::CombatActive
		: EProject_JCombatAnimationLayerState::Inactive;
}

FSoftObjectPath UProject_JCombatAnimationLayerComponent::ResolveLayerPath(const UProject_JWeaponAnimProfile* WeaponProfile) const
{
	if (!WeaponProfile || WeaponProfile->CombatAnimationLayerClass.IsNull())
	{
		return FSoftObjectPath();
	}
	return WeaponProfile->CombatAnimationLayerClass.ToSoftObjectPath();
}

void UProject_JCombatAnimationLayerComponent::HandleLayerPreloadCompleted(FSoftObjectPath RequestedPath, uint64 RequestGeneration)
{
	if (bEndingPlay || RequestGeneration != LoadGeneration || PreloadedAnimationLayerPath != RequestedPath) { return; }
	PreloadedAnimationLayerClass = Cast<UClass>(RequestedPath.ResolveObject());
	LayerPreloadHandle.Reset();
	bLoadFailed = !PreloadedAnimationLayerClass || !PreloadedAnimationLayerClass->IsChildOf(UAnimInstance::StaticClass());
	if (bLoadFailed)
	{
		PreloadedAnimationLayerClass = nullptr;
		UE_LOG(LogProjectJCombatAnimationLayer, Warning, TEXT("Layer load failed; default pose retained until path change or explicit retry. Path=%s"), *RequestedPath.ToString());
		return;
	}
	RefreshLayer();
}

void UProject_JCombatAnimationLayerComponent::UnlinkLayer()
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

void UProject_JCombatAnimationLayerComponent::ResetPreload()
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

void UProject_JCombatAnimationLayerComponent::RetryLayerLoad()
{
	if (bEndingPlay) { return; }
	ResetPreload();
	PreloadLayerForCurrentWeapon();
	RefreshLayer();
}
