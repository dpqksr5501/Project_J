// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/Project_JCameraComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
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

	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		Pawn->ReceiveControllerChangedDelegate.AddUniqueDynamic(this, &UProject_JCameraComponent::OnControllerChanged);
	}
	RefreshAbilitySystemBinding();
}

void UProject_JCameraComponent::OnControllerChanged(APawn* Pawn, AController* OldController, AController* NewController)
{
	RefreshAbilitySystemBinding();
}

void UProject_JCameraComponent::RefreshAbilitySystemBinding()
{
	check(IsInGameThread());
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const bool bWasLocal = bIsLocallyControlled;
	bIsLocallyControlled = Pawn && Cast<APlayerController>(Pawn->GetController()) && Pawn->IsLocallyControlled();
	SetComponentTickEnabled(bIsLocallyControlled);
	// Preserve authored lag settings across remote -> local possession.
	if (CameraBoom) CameraBoom->SetComponentTickEnabled(bIsLocallyControlled);
	if (FollowCamera) FollowCamera->SetActive(bIsLocallyControlled);

	UAbilitySystemComponent* ASC = bIsLocallyControlled
		? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner()) : nullptr;
	if (BoundAbilitySystemComponent.Get() == ASC && CombatModeTagEventHandle.IsValid() && bWasLocal == bIsLocallyControlled)
	{
		return;
	}
	UnregisterAbilitySystemBinding();
	bIsCombatMode = ASC && ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
	if (!bIsLocallyControlled) return;

	if (CameraBoom)
	{
		CameraBoom->TargetArmLength = bIsCombatMode ? CombatTargetArmLength : NormalTargetArmLength;
		CameraBoom->SocketOffset = bIsCombatMode
			? FVector(CombatSocketOffset.X, bUseRightCombatShoulder ? CombatSocketOffset.Y : -CombatSocketOffset.Y, CombatSocketOffset.Z)
			: NormalSocketOffset;
	}
	if (FollowCamera) FollowCamera->SetFieldOfView(bIsCombatMode ? CombatFieldOfView : NormalFieldOfView);
	if (ASC)
	{
		BoundAbilitySystemComponent = ASC;
		CombatModeTagEventHandle = ASC->RegisterGameplayTagEvent(FProject_JGameplayTags::Get().State_CombatMode, EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &UProject_JCameraComponent::OnCombatStateTagChanged);
	}
}

void UProject_JCameraComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		Pawn->ReceiveControllerChangedDelegate.RemoveDynamic(this, &UProject_JCameraComponent::OnControllerChanged);
	}
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
