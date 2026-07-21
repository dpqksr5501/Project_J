// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/Project_JCombatAnimationLayerComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/Project_JWeaponAnimProfile.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Mount/Project_JMountComponent.h"
#include "Project_JPlayerCharacter.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJCombatAnimationLayer, Log, All);

UProject_JCombatAnimationLayerComponent::UProject_JCombatAnimationLayerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UProject_JCombatAnimationLayerComponent::BeginPlay()
{
	Super::BeginPlay();
	PreloadLayerForCurrentWeapon();
	RefreshLayer();
}

void UProject_JCombatAnimationLayerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnlinkLayer();
	Super::EndPlay(EndPlayReason);
}

void UProject_JCombatAnimationLayerComponent::RefreshLayer()
{
	AProject_JPlayerCharacter* Player = Cast<AProject_JPlayerCharacter>(GetOwner());
	if (!Player || Player->GetNetMode() == NM_DedicatedServer)
	{
		UE_LOG(LogProjectJCombatAnimationLayer, Warning, TEXT("RefreshLayer skipped. Owner=%s NetMode=%d"), *GetNameSafe(GetOwner()), Player ? static_cast<int32>(Player->GetNetMode()) : INDEX_NONE);
		return;
	}

	const UProject_JWeaponAnimProfile* WeaponProfile = Player->GetWeaponAnimProfile();
	PresentationState = CalculatePresentationState(*Player);
	const UClass* ConfiguredLayerClass = WeaponProfile ? WeaponProfile->CombatAnimationLayerClass.Get() : nullptr;
	const bool bShouldLinkLayer =
		(PresentationState == EProject_JCombatAnimationLayerState::PreparingCombat ||
			PresentationState == EProject_JCombatAnimationLayerState::CombatActive) &&
		WeaponProfile &&
		!WeaponProfile->CombatAnimationLayerClass.IsNull();
	UE_LOG(LogProjectJCombatAnimationLayer, Verbose, TEXT("RefreshLayer Owner=%s State=%d WeaponProfile=%s ConfiguredLayer=%s ExistingLayer=%s ShouldLink=%s"), *GetNameSafe(Player), static_cast<int32>(PresentationState), *GetNameSafe(WeaponProfile), *GetNameSafe(ConfiguredLayerClass), *GetNameSafe(LinkedAnimationLayerClass.Get()), bShouldLinkLayer ? TEXT("true") : TEXT("false"));
	if (!bShouldLinkLayer)
	{
		UnlinkLayer();
		return;
	}

	UClass* LoadedLayerClass = ResolveLayerClass(WeaponProfile);
	if (!LoadedLayerClass || !LoadedLayerClass->IsChildOf(UAnimInstance::StaticClass()))
	{
		UE_LOG(LogProjectJCombatAnimationLayer, Error, TEXT("Layer class load failed or is not an AnimInstance. Owner=%s RequestedClass=%s"), *GetNameSafe(Player), *GetNameSafe(LoadedLayerClass));
		UnlinkLayer();
		return;
	}

	if (LinkedAnimationLayerClass == LoadedLayerClass)
	{
		UE_LOG(LogProjectJCombatAnimationLayer, Verbose, TEXT("Layer already linked. Owner=%s Layer=%s"), *GetNameSafe(Player), *GetNameSafe(LoadedLayerClass));
		return;
	}

	UnlinkLayer();
	if (USkeletalMeshComponent* PlayerMesh = Player->GetMesh())
	{
		PlayerMesh->LinkAnimClassLayers(LoadedLayerClass);
		LinkedAnimationLayerClass = LoadedLayerClass;
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
	if (!Player || Player->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const UProject_JWeaponAnimProfile* WeaponProfile = Player->GetWeaponAnimProfile();
	if (!WeaponProfile || WeaponProfile->CombatAnimationLayerClass.IsNull())
	{
		PreloadedAnimationLayerClass = nullptr;
		PreloadedAnimationLayerPath.Reset();
		LayerPreloadHandle.Reset();
		return;
	}

	const FSoftObjectPath RequestedPath = WeaponProfile->CombatAnimationLayerClass.ToSoftObjectPath();
	if (PreloadedAnimationLayerPath == RequestedPath && (PreloadedAnimationLayerClass || LayerPreloadHandle.IsValid()))
	{
		return;
	}

	PreloadedAnimationLayerPath = RequestedPath;
	PreloadedAnimationLayerClass = nullptr;
	LayerPreloadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		RequestedPath,
		FStreamableDelegate::CreateUObject(this, &ThisClass::HandleLayerPreloadCompleted, RequestedPath));
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

UClass* UProject_JCombatAnimationLayerComponent::ResolveLayerClass(const UProject_JWeaponAnimProfile* WeaponProfile)
{
	if (!WeaponProfile || WeaponProfile->CombatAnimationLayerClass.IsNull())
	{
		PreloadedAnimationLayerClass = nullptr;
		PreloadedAnimationLayerPath.Reset();
		return nullptr;
	}

	const FSoftObjectPath RequestedPath = WeaponProfile->CombatAnimationLayerClass.ToSoftObjectPath();
	if (PreloadedAnimationLayerPath == RequestedPath && PreloadedAnimationLayerClass)
	{
		return PreloadedAnimationLayerClass.Get();
	}

	PreloadedAnimationLayerPath = RequestedPath;
	PreloadedAnimationLayerClass = WeaponProfile->CombatAnimationLayerClass.LoadSynchronous();
	LayerPreloadHandle.Reset();
	return PreloadedAnimationLayerClass.Get();
}

void UProject_JCombatAnimationLayerComponent::HandleLayerPreloadCompleted(FSoftObjectPath RequestedPath)
{
	if (PreloadedAnimationLayerPath != RequestedPath)
	{
		return;
	}

	PreloadedAnimationLayerClass = Cast<UClass>(RequestedPath.ResolveObject());
	LayerPreloadHandle.Reset();
}

void UProject_JCombatAnimationLayerComponent::UnlinkLayer()
{
	if (!LinkedAnimationLayerClass)
	{
		return;
	}

	if (AProject_JPlayerCharacter* Player = Cast<AProject_JPlayerCharacter>(GetOwner()))
	{
		if (USkeletalMeshComponent* PlayerMesh = Player->GetMesh())
		{
			UE_LOG(LogProjectJCombatAnimationLayer, Log, TEXT("Layer unlinked. Owner=%s Layer=%s"), *GetNameSafe(Player), *GetNameSafe(LinkedAnimationLayerClass.Get()));
			PlayerMesh->UnlinkAnimClassLayers(LinkedAnimationLayerClass);
		}
	}

	LinkedAnimationLayerClass = nullptr;
}
