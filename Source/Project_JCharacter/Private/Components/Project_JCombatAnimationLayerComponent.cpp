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

void UProject_JCombatAnimationLayerComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	bEndingPlay = true;
	UnlinkLayer();
	ResetPreload();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
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
	UE_LOG(LogProjectJCombatAnimationLayer, Verbose, TEXT("RefreshLayer Owner=%s State=%d WeaponProfile=%s ExistingLayer=%s ShouldLink=%s"), *GetNameSafe(Player), static_cast<int32>(PresentationState), *GetNameSafe(WeaponProfile), *GetNameSafe(Layer.LinkedClass.Get()), bShouldLinkLayer ? TEXT("true") : TEXT("false"));
	if (!bShouldLinkLayer)
	{
		UnlinkLayer();
		return;
	}

	const FSoftObjectPath RequestedPath = ResolveLayerPath(WeaponProfile);
	if (Layer.PreloadedPath == RequestedPath && Layer.bFailed) { UnlinkLayer(); return; }
	UClass* LoadedLayerClass = Layer.ResolveClass(RequestedPath);

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
		Layer.PreloadedPath = RequestedPath;
		Layer.bFailed = true;
		UE_LOG(LogProjectJCombatAnimationLayer, Error, TEXT("Layer class load failed or is not an AnimInstance. Owner=%s RequestedClass=%s"), *GetNameSafe(Player), *GetNameSafe(LoadedLayerClass));
		UnlinkLayer();
		return;
	}

	if (Layer.IsLinkedTo(Player->GetMesh(), LoadedLayerClass))
	{
		UE_LOG(LogProjectJCombatAnimationLayer, Verbose, TEXT("Layer already linked. Owner=%s Layer=%s"), *GetNameSafe(Player), *GetNameSafe(LoadedLayerClass));
		return;
	}

	if (USkeletalMeshComponent* PlayerMesh = Player->GetMesh(); Layer.Link(PlayerMesh, LoadedLayerClass))
	{
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
	if (Layer.HasPreloadFor(RequestedPath))
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
		if (Layer.PreloadedPath != RequestedPath)
		{
			ResetPreload();
		}
		return;
	}

	if (Layer.PreparePreload(RequestedPath))
	{
		RefreshLayer();
		return;
	}
	Layer.RequestPreload(FStreamableDelegate::CreateUObject(this, &ThisClass::HandleLayerPreloadCompleted, RequestedPath, Layer.Generation));
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
	if (bEndingPlay || !Layer.IsCurrentRequest(RequestedPath, RequestGeneration)) { return; }
	if (!Layer.CompletePreload())
	{
		UE_LOG(LogProjectJCombatAnimationLayer, Warning, TEXT("Layer load failed; default pose retained until path change or explicit retry. Path=%s"), *RequestedPath.ToString());
		return;
	}
	RefreshLayer();
}

void UProject_JCombatAnimationLayerComponent::UnlinkLayer()
{
	Layer.Unlink();
}

void UProject_JCombatAnimationLayerComponent::ResetPreload()
{
	Layer.ResetPreload();
}

void UProject_JCombatAnimationLayerComponent::RetryLayerLoad()
{
	if (bEndingPlay) { return; }
	ResetPreload();
	PreloadLayerForCurrentWeapon();
	RefreshLayer();
}
