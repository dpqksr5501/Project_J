// Copyright Epic Games, Inc. All Rights Reserved.

#include "Project_JPlayerCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Project_JGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "TimerManager.h"
#include "InputCoreTypes.h"

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

AProject_JPlayerCharacter::AProject_JPlayerCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	PrimaryActorTick.bCanEverTick = true; // Tick 활성화

	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, WalkRotationRateYaw, 0.0f);

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	GetCharacterMovement()->JumpZVelocity = 500.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
}


void AProject_JPlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 1. 실시간 Z축 속도 업데이트
	VerticalSpeed = GetVelocity().Z;

	// 2. 실시간 수평 이동 속도 업데이트 (VectorLengthXY)
	FVector Velocity = GetVelocity();
	Velocity.Z = 0; // Z축 제외
	GroundSpeed = Velocity.Size();

	const bool bIsCurrentlyInAir = IsInAirForAnimation();
	bIsPhysicallyInAir = bIsCurrentlyInAir;
	if (!bWasInAir && bIsCurrentlyInAir && !bIsJumping && !bIsLanding && !bSuppressFallOffStart)
	{
		StartFallOffStart();
	}
	else if (bIsCurrentlyInAir)
	{
		bIsInAir = true;
	}
	else if (!bIsJumping && !bLandingRequested && !bIsLanding)
	{
		bIsInAir = false;
		bSuppressFallOffStart = false;
	}

	bWasInAir = bIsCurrentlyInAir;
	bCanEnterLand = bLandingRequested;
	bCanEnterGround = !bIsInAir && !bIsLanding && !bLandingRequested;
	UpdateMovementRequestState(DeltaTime);

	const FVector2D CombatMoveInput = CachedMoveInput.GetClampedToMaxSize(1.0f);
	CombatInputRight = CombatMoveInput.X;
	CombatInputForward = CombatMoveInput.Y;

	if (bUseControllerRotationYaw && bHasMoveInput)
	{
		const float DesiredSpeed = GetCharacterMovement() ? GetCharacterMovement()->MaxWalkSpeed : WalkSpeed;
		CombatRightSpeed = CombatMoveInput.X * DesiredSpeed;
		CombatForwardSpeed = CombatMoveInput.Y * DesiredSpeed;
		MovementDirection = FMath::RadiansToDegrees(FMath::Atan2(CombatMoveInput.X, CombatMoveInput.Y));
	}
	else
	{
		MovementDirection = 0.0f;
		CombatForwardSpeed = 0.0f;
		CombatRightSpeed = 0.0f;
	}
}

void AProject_JPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {

		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &AProject_JPlayerCharacter::DoJumpStart);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &AProject_JPlayerCharacter::DoJumpEnd);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AProject_JPlayerCharacter::Move);
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Completed, this, &AProject_JPlayerCharacter::StopMoveInput);
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Canceled, this, &AProject_JPlayerCharacter::StopMoveInput);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AProject_JPlayerCharacter::Look);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AProject_JPlayerCharacter::Look);
	}
	else
	{
		UE_LOG(LogTemplateCharacter, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}

	// Temporary: Bind Tab key directly to Toggle Combat Mode for testing
	PlayerInputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &AProject_JPlayerCharacter::ToggleCombatMode);
	PlayerInputComponent->BindKey(EKeys::LeftShift, IE_Pressed, this, &AProject_JPlayerCharacter::StartSprint);
	PlayerInputComponent->BindKey(EKeys::LeftShift, IE_Released, this, &AProject_JPlayerCharacter::StopSprint);
	PlayerInputComponent->BindKey(EKeys::RightShift, IE_Pressed, this, &AProject_JPlayerCharacter::StartSprint);
	PlayerInputComponent->BindKey(EKeys::RightShift, IE_Released, this, &AProject_JPlayerCharacter::StopSprint);
}

void AProject_JPlayerCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	// route the input
	DoMove(MovementVector.X, MovementVector.Y);
}

void AProject_JPlayerCharacter::StopMoveInput()
{
	CachedMoveInput = FVector2D::ZeroVector;
}

void AProject_JPlayerCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// route the input
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void AProject_JPlayerCharacter::DoMove(float Right, float Forward)
{
	CachedMoveInput = FVector2D(Right, Forward);

	if (GetController() != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

		// get right vector
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement
		AddMovementInput(ForwardDirection, Forward);
		AddMovementInput(RightDirection, Right);
	}
}

void AProject_JPlayerCharacter::DoLook(float Yaw, float Pitch)
{
	if (GetController() != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AProject_JPlayerCharacter::DoJumpStart()
{
	const bool bHadLandingState = bIsLanding || bLandingRequested || bCanEnterLand;

	// 착지 타이머 초기화 및 상태 해제
	GetWorldTimerManager().ClearTimer(JumpTimerHandle);
	GetWorldTimerManager().ClearTimer(LandingTimerHandle);
	StopFallOffStart();
	bIsLanding = false;
	bLandingRequested = false;
	bCanEnterLand = false;
	bCanEnterGround = false;
	bIsInAir = true;
	bWasInAir = true;
	bSuppressFallOffStart = true;

	// 명시적인 점프 트리거 활성화 및 0.2초 타이머 설정
	bIsJumping = true;
	GetWorldTimerManager().SetTimer(JumpTimerHandle, this, &AProject_JPlayerCharacter::OnJumpTimerFinished, FMath::Max(0.1f, JumpStartMaxDuration), false);

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		if (bHadLandingState)
		{
			ASC->RemoveLooseGameplayTag(FProject_JGameplayTags::Get().State_Movement_Landing);
		}
		if (!ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Movement_InAir))
		{
			ASC->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_Movement_InAir);
		}
	}

	// signal the character to jump
	Jump();
}

void AProject_JPlayerCharacter::DoJumpEnd()
{
	// signal the character to stop jumping
	StopJumping();
}

void AProject_JPlayerCharacter::FinishFallOffStart()
{
	StopFallOffStart();
}

void AProject_JPlayerCharacter::FinishJumpStart()
{
	GetWorldTimerManager().ClearTimer(JumpTimerHandle);
	OnJumpTimerFinished();
}

void AProject_JPlayerCharacter::UpdateMovementRequestState(float DeltaTime)
{
	bPrevHasMoveInput = bHasMoveInput;

	const FVector2D MoveInput = CachedMoveInput.GetClampedToMaxSize(1.0f);
	MoveInputSize = MoveInput.Size();
	bHasMoveInput = MoveInputSize > MoveInputDeadZone;
	MoveInputHeldTime = bHasMoveInput ? MoveInputHeldTime + DeltaTime : 0.0f;
	MoveInputTurnAngle = 0.0f;
	bSharpTurnRequested = false;
	bStartRequested = false;
	bStopRequested = false;
	bUseStartDatabase = false;

	if (bHasMoveInput && bPrevHasMoveInput && PreviousMoveInputForTurn.Size() > MoveInputDeadZone)
	{
		const FVector2D PreviousDirection = PreviousMoveInputForTurn.GetSafeNormal();
		const FVector2D CurrentDirection = MoveInput.GetSafeNormal();
		const float Dot = FMath::Clamp(FVector2D::DotProduct(PreviousDirection, CurrentDirection), -1.0f, 1.0f);
		const float Cross = PreviousDirection.Y * CurrentDirection.X - PreviousDirection.X * CurrentDirection.Y;
		MoveInputTurnAngle = FMath::RadiansToDegrees(FMath::Atan2(Cross, Dot));
	}

	const bool bCanRequestGroundMove = !bIsInAir && !bIsLanding && !bIsJumping && !bIsFallOffStart;
	if (!bCanRequestGroundMove)
	{
		ClearMovementRequests();
		PreviousMoveInputForTurn = bHasMoveInput ? MoveInput : FVector2D::ZeroVector;
		return;
	}

	if (!bPrevHasMoveInput && bHasMoveInput)
	{
		MoveInputHeldTime = 0.0f;
	}

	bSharpTurnRequested =
		bIsSprinting &&
		bHasMoveInput &&
		bPrevHasMoveInput &&
		GroundSpeed >= SharpTurnMinSpeed &&
		FMath::Abs(MoveInputTurnAngle) >= SharpTurnAngleThreshold;

	bStartRequested = !bPrevHasMoveInput && bHasMoveInput;
	bStopRequested = bPrevHasMoveInput && !bHasMoveInput && GroundSpeed > StopIntentSpeedThreshold;
	bUseStartDatabase = bHasMoveInput && MoveInputHeldTime < StartToLoopDelay;

	PreviousMoveInputForTurn = bHasMoveInput ? MoveInput : FVector2D::ZeroVector;
}

void AProject_JPlayerCharacter::ClearMovementRequests()
{
	bStartRequested = false;
	bStopRequested = false;
	bUseStartDatabase = false;
	MoveInputHeldTime = 0.0f;
	bSharpTurnRequested = false;
	MoveInputTurnAngle = 0.0f;
}

void AProject_JPlayerCharacter::ApplyCombatRotationMode(bool bEnableCombatRotation)
{
	bUseControllerRotationYaw = bEnableCombatRotation;

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->bOrientRotationToMovement = !bEnableCombatRotation;
	}
}

void AProject_JPlayerCharacter::UpdateMaxWalkSpeed()
{
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		const bool bCanSprint = bIsSprinting && !bIsAttacking && !bIsDodging && !bIsHitReacting;
		MoveComp->MaxWalkSpeed = bCanSprint ? SprintSpeed : WalkSpeed;
		MoveComp->RotationRate = FRotator(0.0f, bCanSprint ? SprintRotationRateYaw : WalkRotationRateYaw, 0.0f);
	}
}

void AProject_JPlayerCharacter::StartSprint()
{
	bIsSprinting = true;
	UpdateMaxWalkSpeed();
}

void AProject_JPlayerCharacter::StopSprint()
{
	bIsSprinting = false;
	UpdateMaxWalkSpeed();
}

void AProject_JPlayerCharacter::ToggleCombatMode()
{
	if (bIsCombatMode)
	{
		CancelCombatIntroMontage();
		SetCombatMode(false);
		return;
	}

	if (bIsPlayingCombatIntro || bPendingCombatModeFromIntro)
	{
		CancelCombatIntroMontage();
		ApplyCombatRotationMode(false);
		return;
	}

	BeginCombatModeWithIntro();
}

void AProject_JPlayerCharacter::SetCombatMode(bool bInCombatMode)
{
	if (bIsCombatMode == bInCombatMode)
	{
		return;
	}

	bIsCombatMode = bInCombatMode;

	// Update Gameplay Tags
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		if (bIsCombatMode)
		{
			ASC->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
		}
		else
		{
			ASC->RemoveLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
		}
	}

	// Update movement rotation settings for Strafe vs Free movement
	if (bIsCombatMode)
	{
		ApplyCombatRotationMode(true);
	}
	else
	{
		CancelCombatIntroMontage();
		ApplyCombatRotationMode(false);
	}
}

void AProject_JPlayerCharacter::BeginCombatModeWithIntro()
{
	if (bIsCombatMode || bIsPlayingCombatIntro || bPendingCombatModeFromIntro || bIsHitReacting)
	{
		return;
	}

	ApplyCombatRotationMode(true);
	PlayCombatIntroMontage();

	if (!bPendingCombatModeFromIntro)
	{
		SetCombatMode(true);
	}
}

void AProject_JPlayerCharacter::PlayCombatIntroMontage()
{
	if (!CombatIntroMontage || bIsPlayingCombatIntro || bIsHitReacting)
	{
		return;
	}

	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		return;
	}

	const float Duration = PlayAnimMontage(CombatIntroMontage, CombatIntroMontagePlayRate);
	if (Duration <= 0.0f)
	{
		return;
	}

	bIsPlayingCombatIntro = true;
	bPendingCombatModeFromIntro = true;

	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &AProject_JPlayerCharacter::OnCombatIntroMontageEnded);
	AnimInstance->Montage_SetEndDelegate(EndDelegate, CombatIntroMontage);
}

void AProject_JPlayerCharacter::CancelCombatIntroMontage()
{
	if (bIsPlayingCombatIntro)
	{
		StopAnimMontage(CombatIntroMontage);
	}

	bIsPlayingCombatIntro = false;
	bPendingCombatModeFromIntro = false;
}

void AProject_JPlayerCharacter::OnCombatIntroMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage == CombatIntroMontage)
	{
		bIsPlayingCombatIntro = false;

		if (bPendingCombatModeFromIntro)
		{
			bPendingCombatModeFromIntro = false;

			if (!bInterrupted && !bIsHitReacting)
			{
				SetCombatMode(true);
			}
			else if (!bIsCombatMode)
			{
				ApplyCombatRotationMode(false);
			}
		}
	}
}

void AProject_JPlayerCharacter::InterruptCombatIntroForHit()
{
	if (!bInterruptCombatIntroOnHit || !bIsPlayingCombatIntro)
	{
		return;
	}

	StopAnimMontage(CombatIntroMontage);
	bIsPlayingCombatIntro = false;
	bPendingCombatModeFromIntro = false;
	if (!bIsCombatMode)
	{
		ApplyCombatRotationMode(false);
	}
}

void AProject_JPlayerCharacter::Landed(const FHitResult& Hit)
{
	const float ImpactFallSpeed = FMath::Abs(GetVelocity().Z);
	const bool bHadInAirState = bIsInAir || bIsPhysicallyInAir || bIsJumping || bIsFallOffStart;

	Super::Landed(Hit);

	// 천장에 부딪히는 등 예상치 못하게 빨리 착지했을 때를 대비해 점프 트리거 즉시 해제
	GetWorldTimerManager().ClearTimer(JumpTimerHandle);
	StopFallOffStart();
	bIsJumping = false;
	bIsInAir = true;
	bIsPhysicallyInAir = false;
	bWasInAir = false;
	bSuppressFallOffStart = false;

	// 착지 시점의 Z축 하강 속도의 절대값을 저장 (하드 랜딩, 소프트 랜딩 구분을 위해 츄저 테이블로 전달됨)
	LastFallSpeed = ImpactFallSpeed;
	LandStartGroundSpeed = GroundSpeed;
	LandStartFallSpeed = ImpactFallSpeed;
	bLandWasSprinting = bIsSprinting || LandStartGroundSpeed >= RunToSprintSpeedThreshold;
	bLandWasMoving = LandStartGroundSpeed > IdleSpeedThreshold || bHasMoveInput;
	bUseHeavyLand = LandStartFallSpeed >= HeavyLandSpeedThreshold;
	bIsLanding = true;
	bLandingRequested = true;
	bCanEnterLand = true;
	bCanEnterGround = false;

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		if (bHadInAirState)
		{
			ASC->RemoveLooseGameplayTag(FProject_JGameplayTags::Get().State_Movement_InAir);
		}

		if (!ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Movement_Landing))
		{
			ASC->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_Movement_Landing);
		}
	}

	// 0.35초 뒤에 착지 상태 해제
	GetWorldTimerManager().SetTimer(LandingTimerHandle, this, &AProject_JPlayerCharacter::OnLandingTimerFinished, FMath::Max(0.05f, LandingRequestDuration), false);

	// 블루프린트로 이벤트 전달 (필요한 경우 유지)
	if (LastFallSpeed > 300.0f)
	{
		K2_OnRealLanded();
	}
}

void AProject_JPlayerCharacter::OnLandingTimerFinished()
{
	const bool bHadLandingState = bIsLanding || bLandingRequested || bCanEnterLand;

	bIsLanding = false;
	bLandingRequested = false;
	bIsInAir = false;
	bWasInAir = false;
	bSuppressFallOffStart = false;
	bCanEnterLand = false;
	bCanEnterGround = true;

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		if (bHadLandingState)
		{
			ASC->RemoveLooseGameplayTag(FProject_JGameplayTags::Get().State_Movement_Landing);
		}
	}
}

void AProject_JPlayerCharacter::OnJumpTimerFinished()
{
	bIsJumping = false;

	if (!IsInAirForAnimation() && !bIsLanding)
	{
		bIsInAir = false;
		bWasInAir = false;
		bSuppressFallOffStart = false;
	}
}

void AProject_JPlayerCharacter::OnFallOffStartFinished()
{
	StopFallOffStart();
}

bool AProject_JPlayerCharacter::IsInAirForAnimation() const
{
	return GetCharacterMovement()->MovementMode == EMovementMode::MOVE_Falling;
}

void AProject_JPlayerCharacter::StartFallOffStart()
{
	bIsInAir = true;
	bIsFallOffStart = true;
	GetWorldTimerManager().ClearTimer(FallOffStartTimerHandle);
	GetWorldTimerManager().SetTimer(FallOffStartTimerHandle, this, &AProject_JPlayerCharacter::OnFallOffStartFinished, FMath::Max(0.05f, FallOffStartDuration), false);

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		if (!ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Movement_InAir))
		{
			ASC->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_Movement_InAir);
		}
	}
}

void AProject_JPlayerCharacter::StopFallOffStart()
{
	GetWorldTimerManager().ClearTimer(FallOffStartTimerHandle);
	bIsFallOffStart = false;
}
