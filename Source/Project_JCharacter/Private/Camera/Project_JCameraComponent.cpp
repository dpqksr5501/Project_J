// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/Project_JCameraComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/SpringArmComponent.h"
#include "Project_JGameplayTags.h"

UProject_JCameraComponent::UProject_JCameraComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UProject_JCameraComponent::Initialize(USpringArmComponent* InCameraBoom, UCameraComponent* InFollowCamera)
{
	CameraBoom = InCameraBoom;
	FollowCamera = InFollowCamera;
}

void UProject_JCameraComponent::BeginPlay()
{
	Super::BeginPlay();

	if (const ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
	{
		bIsLocallyControlled = OwnerChar->IsLocallyControlled();
	}

	// Remote proxies do not need camera interpolation or an active view camera.
	if (!bIsLocallyControlled)
	{
		SetComponentTickEnabled(false);

		if (CameraBoom)
		{
			CameraBoom->bEnableCameraLag = false;
			CameraBoom->bEnableCameraRotationLag = false;
		}
		if (FollowCamera)
		{
			FollowCamera->Deactivate();
		}
		return;
	}

	RefreshAbilitySystemBinding();
}

void UProject_JCameraComponent::RefreshAbilitySystemBinding()
{
	if (!bIsLocallyControlled)
	{
		return;
	}

	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
	if (ASC)
	{
		if (BoundAbilitySystemComponent.Get() == ASC && CombatModeTagEventHandle.IsValid())
		{
			return;
		}

		UnregisterAbilitySystemBinding();

		bIsCombatMode = ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
		if (CameraBoom)
		{
			CameraBoom->TargetArmLength = bIsCombatMode ? CombatTargetArmLength : NormalTargetArmLength;
			const FVector TargetOffset = bIsCombatMode
				? FVector(CombatSocketOffset.X, bUseRightCombatShoulder ? CombatSocketOffset.Y : -CombatSocketOffset.Y, CombatSocketOffset.Z)
				: NormalSocketOffset;
			CameraBoom->SocketOffset = TargetOffset;
		}
		if (FollowCamera)
		{
			FollowCamera->SetFieldOfView(bIsCombatMode ? CombatFieldOfView : NormalFieldOfView);
		}

		BoundAbilitySystemComponent = ASC;
		CombatModeTagEventHandle = ASC->RegisterGameplayTagEvent(FProject_JGameplayTags::Get().State_CombatMode, EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &UProject_JCameraComponent::OnCombatStateTagChanged);
	}
}

void UProject_JCameraComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterAbilitySystemBinding();

	Super::EndPlay(EndPlayReason);
}

void UProject_JCameraComponent::UnregisterAbilitySystemBinding()
{
	if (UAbilitySystemComponent* ASC = BoundAbilitySystemComponent.Get())
	{
		if (CombatModeTagEventHandle.IsValid())
		{
			ASC->RegisterGameplayTagEvent(FProject_JGameplayTags::Get().State_CombatMode, EGameplayTagEventType::NewOrRemoved)
				.Remove(CombatModeTagEventHandle);
		}
	}

	CombatModeTagEventHandle.Reset();
	BoundAbilitySystemComponent.Reset();
}

void UProject_JCameraComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bIsLocallyControlled)
	{
		return;
	}

	if (CameraBoom)
	{
		const float TargetLength = bIsCombatMode ? CombatTargetArmLength : NormalTargetArmLength;
		CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, TargetLength, DeltaTime, ZoomInterpolationSpeed);
		const FVector TargetOffset = bIsCombatMode
			? FVector(CombatSocketOffset.X, bUseRightCombatShoulder ? CombatSocketOffset.Y : -CombatSocketOffset.Y, CombatSocketOffset.Z)
			: NormalSocketOffset;
		CameraBoom->SocketOffset = FMath::VInterpTo(CameraBoom->SocketOffset, TargetOffset, DeltaTime, FramingInterpolationSpeed);
	}
	if (FollowCamera)
	{
		const float TargetFOV = bIsCombatMode ? CombatFieldOfView : NormalFieldOfView;
		FollowCamera->SetFieldOfView(FMath::FInterpTo(FollowCamera->FieldOfView, TargetFOV, DeltaTime, FramingInterpolationSpeed));
	}
}

void UProject_JCameraComponent::OnCombatStateTagChanged(const FGameplayTag CallbackTag, int32 NewCount)
{
	bIsCombatMode = (NewCount > 0);
}

void UProject_JCameraComponent::SetCombatRightShoulder(bool bUseRightShoulder)
{
	if (bIsLocallyControlled)
	{
		bUseRightCombatShoulder = bUseRightShoulder;
	}
}
