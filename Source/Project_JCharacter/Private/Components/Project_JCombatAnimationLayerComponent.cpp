// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/Project_JCombatAnimationLayerComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/Project_JWeaponAnimProfile.h"
#include "Components/SkeletalMeshComponent.h"
#include "Mount/Project_JMountComponent.h"
#include "Project_JPlayerCharacter.h"

UProject_JCombatAnimationLayerComponent::UProject_JCombatAnimationLayerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UProject_JCombatAnimationLayerComponent::BeginPlay()
{
	Super::BeginPlay();
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
		return;
	}

	const UProject_JMountComponent* MountComponent = Player->GetMountComponent();
	const UProject_JWeaponAnimProfile* WeaponProfile = Player->GetWeaponAnimProfile();
	const bool bShouldLinkLayer =
		(!MountComponent || !MountComponent->IsMounted()) &&
		Player->IsCombatModeActive() &&
		WeaponProfile &&
		!WeaponProfile->CombatAnimationLayerClass.IsNull();
	if (!bShouldLinkLayer)
	{
		UnlinkLayer();
		return;
	}

	UClass* LoadedLayerClass = WeaponProfile->CombatAnimationLayerClass.LoadSynchronous();
	if (!LoadedLayerClass || !LoadedLayerClass->IsChildOf(UAnimInstance::StaticClass()))
	{
		UnlinkLayer();
		return;
	}

	if (LinkedAnimationLayerClass == LoadedLayerClass)
	{
		return;
	}

	UnlinkLayer();
	if (USkeletalMeshComponent* PlayerMesh = Player->GetMesh())
	{
		PlayerMesh->LinkAnimClassLayers(LoadedLayerClass);
		LinkedAnimationLayerClass = LoadedLayerClass;
	}
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
			PlayerMesh->UnlinkAnimClassLayers(LinkedAnimationLayerClass);
		}
	}

	LinkedAnimationLayerClass = nullptr;
}
