// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/Project_JCameraComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Movement/Project_JMoverPawn.h"
#include "Movement/Project_JMontageDashMove.h"
#include "Movement/Project_JMoverLifecycle.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Project_JGameplayTags.h"
#include "Project_JPlayerCharacter.h"

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
		if (const auto* MoverPawn=Cast<AProject_JMoverPawn>(Pawn))
		{
			// Follow the same render-time transform as Mover's visual smoothing.
			// A capsule-attached boom would still step at the simulation frequency.
			// Keep the authored camera anchor and any explicitly authored parent.
			if (CameraBoom && CameraBoom->GetAttachParent()==Pawn->GetRootComponent())
			{
				CameraBoom->AttachToComponent(MoverPawn->GetMesh(),FAttachmentTransformRules::KeepWorldTransform);
				CameraBoom->AddTickPrerequisiteComponent(MoverPawn->GetMover());
			}
		}
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
	DashMotionAlpha = 0.0f;
	SprintMotionAlpha = 0.0f;
	bIsCombatMode = ASC && ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
	if (!bIsLocallyControlled) return;

	if (CameraBoom)
	{
		CameraBoom->TargetArmLength = bIsCombatMode ? CombatTargetArmLength : NormalTargetArmLength;
		CameraBoom->SocketOffset = bIsCombatMode
			? FVector(CombatSocketOffset.X, bUseRightCombatShoulder ? CombatSocketOffset.Y : -CombatSocketOffset.Y, CombatSocketOffset.Z)
			: NormalSocketOffset;
	}
	BaseFieldOfView = bIsCombatMode ? CombatFieldOfView : NormalFieldOfView;
	if (FollowCamera) FollowCamera->SetFieldOfView(BaseFieldOfView);
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
	UpdateMotionFraming(DeltaTime);
	if (FollowCamera)
	{
		const float TargetFOV = bIsCombatMode ? CombatFieldOfView : NormalFieldOfView;
		BaseFieldOfView = FMath::FInterpTo(BaseFieldOfView, TargetFOV, DeltaTime, FramingInterpolationSpeed);
		const float MotionBoost = FMath::Lerp(SprintMotionAlpha * SprintFieldOfViewBoost, DashFieldOfViewBoost, DashMotionAlpha);
		FollowCamera->SetFieldOfView(FMath::Clamp(BaseFieldOfView + MotionBoost, 1.0f, 170.0f));
	}
}

void UProject_JCameraComponent::UpdateMotionFraming(float DeltaTime)
{
	float DashTarget = 0.0f;
	float SprintTarget = 0.0f;
	const auto* Player = Cast<AProject_JPlayerCharacter>(GetOwner());
	const auto* Mover = Player ? Player->GetMover() : nullptr;
	if (Mover && Mover->IsOnGround())
	{
		const auto& State = Mover->GetSyncState();
		const FVector Velocity = Player->GetVelocity();
		if (Player->IsDodging())
		{
			if (const auto* Dash = State.LayeredMoves.FindActiveMove<FProject_JMontageDashMove>())
			{
				// A blocked dash has no forward speed and fades out just like a completed dash.
				const float ForwardSpeed = FVector::DotProduct(Velocity, Dash->TravelDirection);
				DashTarget = FMath::GetMappedRangeValueClamped(
					FVector2f(0.2f * DashFullEffectSpeed, DashFullEffectSpeed), FVector2f(0.0f, 1.0f), ForwardSpeed);
			}
		}
		if (const auto* Gait = State.SyncStateCollection.FindDataByType<FProject_JMoverLifecycle>(); Gait && Gait->bSprint)
		{
			const float SprintSpeed = Player->GetMoverMaxSpeed(true);
			if (SprintSpeed > 0.0f)
			{
				const float SpeedFraction = Velocity.Size2D() / SprintSpeed;
				SprintTarget = FMath::GetMappedRangeValueClamped(FVector2f(0.55f, 1.0f), FVector2f(0.0f, 1.0f), SpeedFraction);
			}
		}
	}

	DashMotionAlpha = FMath::FInterpConstantTo(DashMotionAlpha, DashTarget, DeltaTime,
		DashTarget > DashMotionAlpha ? 20.0f : 6.0f);
	SprintMotionAlpha = FMath::FInterpConstantTo(SprintMotionAlpha, SprintTarget, DeltaTime, 4.0f);
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
