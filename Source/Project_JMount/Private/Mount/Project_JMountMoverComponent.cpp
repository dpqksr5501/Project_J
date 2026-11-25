#include "Mount/Project_JMountMoverComponent.h"
#include "Animation/Project_JRiderAnimationProfile.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/Project_JCommandBudgetComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "MoveLibrary/MovementUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Interaction/Project_JInteractionTargetComponent.h"
#include "Mount/Project_JMountAttributeSet.h"
#include "Mount/Project_JMountComponent.h"
#include "Movement/Project_JMovementQueries.h"
#include "Net/UnrealNetwork.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAbilitySystemOwnerInterface.h"
#include "Project_JGameplayTags.h"
#include "UObject/StrongObjectPtr.h"
#include "Project_JSocialStateInterface.h"
#include "Security/Project_JServerIntegritySubsystem.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

const FName UProject_JMountMoverComponent::GroundMountModeName(TEXT("GroundMount"));
const FName UProject_JMountMoverComponent::FlyingMountModeName(TEXT("FlyingMount"));

namespace Project_J::Mount
{
void StepFlyingEnergyModel(
	const FProject_JMountMoverInputs& Inputs,
	const FProject_JMountMotionProfile& Profile,
	float DeltaSeconds,
	FProject_JMountMoverSyncState& InOutState)
{
	if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f)
	{
		return;
	}
	const float Dt = FMath::Clamp(DeltaSeconds, 0.0f, 0.1f);
	const float MaxStam = FMath::Max(1.0f, Profile.MaxStamina);

	const float TargetPitch = FMath::Clamp(Inputs.PitchInput, -1.0f, 1.0f) * 55.0f;
	InOutState.PitchDeg = FMath::FInterpTo(InOutState.PitchDeg, TargetPitch, Dt, 4.5f);
	const float TargetBank = FMath::Clamp(Inputs.ThrottleTurn.Y, -1.0f, 1.0f) * 40.0f;
	InOutState.BankDeg = FMath::FInterpTo(InOutState.BankDeg, TargetBank, Dt, 5.0f);

	const bool bDiving = InOutState.PitchDeg < -10.0f;
	const bool bClimbing = InOutState.PitchDeg > 10.0f;

	if (Inputs.bGlideHeld || InOutState.Stamina <= 0.5f)
	{
		InOutState.bIsGliding = true;
	}
	else if (Inputs.ThrottleTurn.X > 0.1f && InOutState.Stamina >= MaxStam * 0.15f)
	{
		InOutState.bIsGliding = false;
	}

	float Speed = FMath::Max(InOutState.AirSpeed, Profile.FlightStallSpeed);
	if (bDiving)
	{
		// Diving converts potential energy into speed; holding boost during a dive still spends stamina.
		const float DiveFactor = FMath::Clamp((-InOutState.PitchDeg - 10.0f) / 45.0f, 0.0f, 1.0f);
		Speed += (1200.0f * DiveFactor) * Dt;
		if (Inputs.bBoostHeld)
		{
			Speed = FMath::FInterpTo(Speed, FMath::Max(Speed, Profile.FlightCruiseSpeed * 1.35f), Dt, 2.2f);
			InOutState.Stamina = FMath::Max(0.0f, InOutState.Stamina - Profile.BoostStaminaCostPerSec * Dt);
			if (InOutState.Stamina <= 0.0f)
			{
				InOutState.bIsGliding = true;
			}
		}
		else
		{
			InOutState.Stamina = FMath::Min(MaxStam, InOutState.Stamina + Profile.GlideStaminaRegenPerSec * 0.5f * Dt);
		}
	}
	else if (InOutState.bIsGliding)
	{
		Speed = FMath::FInterpTo(Speed, Profile.FlightCruiseSpeed * 0.9f, Dt, 0.8f);
		InOutState.Stamina = FMath::Min(MaxStam, InOutState.Stamina + Profile.GlideStaminaRegenPerSec * Dt);
	}
	else
	{
		const float DesiredSpeed = Inputs.bBoostHeld ? (Profile.FlightCruiseSpeed * 1.35f) : Profile.FlightCruiseSpeed;
		Speed = FMath::FInterpTo(Speed, DesiredSpeed, Dt, 2.2f);
		if (bClimbing)
		{
			const float ClimbRatio = FMath::Clamp((InOutState.PitchDeg - 10.0f) / 45.0f, 0.0f, 1.0f);
			Speed -= (420.0f * ClimbRatio) * Dt;
			InOutState.Stamina = FMath::Max(0.0f, InOutState.Stamina - Profile.ClimbStaminaCostPerSec * (0.5f + ClimbRatio) * Dt);
		}
		if (Inputs.bBoostHeld)
		{
			InOutState.Stamina = FMath::Max(0.0f, InOutState.Stamina - Profile.BoostStaminaCostPerSec * Dt);
		}
		if (InOutState.Stamina <= 0.0f)
		{
			InOutState.bIsGliding = true;
		}
	}

	InOutState.AirSpeed = FMath::Clamp(Speed, Profile.FlightStallSpeed, Profile.FlightDiveMaxSpeed);
	if (InOutState.FlightState == EProject_JMountFlightState::Grounded)
	{
		InOutState.FlightState = EProject_JMountFlightState::Flying;
	}
}
} // namespace Project_J::Mount

UProject_JGroundMountMode::UProject_JGroundMountMode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	FloorCheckPolicy = EStaticFloorCheckPolicy::Always;
}

void UProject_JGroundMountMode::GenerateMove_Implementation(
	const FMoverSimContext&,
	const FMoverTickStartData& Start,
	const FMoverTimeStep& Step,
	FProposedMove& Move) const
{
	const auto* Inputs = Start.InputCmd.InputCollection.FindDataByType<FProject_JMountMoverInputs>();
	const auto* DefaultSync = Start.SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
	if (!Inputs || !DefaultSync)
	{
		return;
	}

	const auto* MountMoverOwner = Cast<UProject_JMountMoverComponent>(GetOuter());
	const FProject_JMountMotionProfile& ActiveProfile = MountMoverOwner ? MountMoverOwner->MotionProfile : Profile;

	const float Dt = FMath::Max(0.0f, Step.StepMs * 0.001f);
	const float MaxSpeed = Inputs->bBoostHeld ? ActiveProfile.GroundGallopSpeed : ActiveProfile.GroundWalkSpeed;
	const float SpeedAlpha = FMath::Clamp(
		static_cast<float>(DefaultSync->GetVelocity_WorldSpace().Size2D()) / FMath::Max(1.0f, ActiveProfile.GroundGallopSpeed),
		0.0f,
		1.0f);
	const float TurnRate = FMath::Lerp(ActiveProfile.TurnRateAtRestDegPerSec, ActiveProfile.TurnRateAtGallopDegPerSec, SpeedAlpha);
	const float YawDelta = FMath::Clamp(Inputs->ThrottleTurn.Y, -1.0f, 1.0f) * TurnRate * Dt;

	FRotator Heading = DefaultSync->GetOrientation_WorldSpace();
	Heading.Yaw = FRotator::NormalizeAxis(Heading.Yaw + YawDelta);
	const FVector Forward = Heading.Vector();
	Move.LinearVelocity = Forward * (FMath::Clamp(Inputs->ThrottleTurn.X, -0.4f, 1.0f) * MaxSpeed);
	Move.AngularVelocityDegrees = FVector(0.0, 0.0, TurnRate * FMath::Clamp(Inputs->ThrottleTurn.Y, -1.0f, 1.0f));
	Move.DirectionIntent = Move.LinearVelocity.GetSafeNormal();
	Move.bHasDirIntent = !Move.DirectionIntent.IsNearlyZero();
}

void UProject_JGroundMountMode::SimulationTick_Implementation(const FSimulationTickParams& Params, FMoverTickEndData& Output)
{
	const float Dt = FMath::Max(0.0f, Params.TimeStep.StepMs * 0.001f);
	const auto* StartDefault = Params.StartState.SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
	if (!StartDefault)
	{
		return;
	}
	auto& OutDefault = Output.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>();
	auto& OutMount = Output.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FProject_JMountMoverSyncState>();
	if (const auto* PrevMount = Params.StartState.SyncState.SyncStateCollection.FindDataByType<FProject_JMountMoverSyncState>())
	{
		OutMount = *PrevMount;
	}

	const auto* MountMoverOwner = Cast<UProject_JMountMoverComponent>(GetOuter());
	const FProject_JMountMotionProfile& ActiveProfile = MountMoverOwner ? MountMoverOwner->MotionProfile : Profile;

	const auto* Inputs = Params.StartState.InputCmd.InputCollection.FindDataByType<FProject_JMountMoverInputs>();
	if (Inputs && Inputs->bTakeOffPressed && !Inputs->bLandPressed && MountMoverOwner
		&& MountMoverOwner->HasTakeoffClearance(StartDefault->GetLocation_WorldSpace()))
	{
		OutDefault = *StartDefault;
		Output.MovementEndState.NextModeName = UProject_JMountMoverComponent::FlyingMountModeName;
		Output.MovementEndState.RemainingMs = Params.TimeStep.StepMs;
		return;
	}

	if (!Params.MovingComps.UpdatedComponent.IsValid())
	{
		OutDefault = *StartDefault;
		return;
	}
	Super::SimulationTick_Implementation(Params, Output);
	OutMount.FlightState = EProject_JMountFlightState::Grounded;
	OutMount.bIsGliding = false;
	OutMount.AirSpeed = static_cast<float>(OutDefault.GetVelocity_WorldSpace().Size2D());
	OutMount.PitchDeg = 0.0f;
	OutMount.BankDeg = FMath::Clamp(Params.ProposedMove.AngularVelocityDegrees.Z * 0.2f, -25.0f, 25.0f);
	OutMount.Stamina = FMath::Min(ActiveProfile.MaxStamina, OutMount.Stamina + ActiveProfile.GroundStaminaRegenPerSec * Dt);
}

void UProject_JFlyingMountMode::GenerateMove_Implementation(
	const FMoverSimContext&,
	const FMoverTickStartData& Start,
	const FMoverTimeStep& Step,
	FProposedMove& Move) const
{
	const auto* Inputs = Start.InputCmd.InputCollection.FindDataByType<FProject_JMountMoverInputs>();
	const auto* DefaultSync = Start.SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
	if (!Inputs || !DefaultSync)
	{
		return;
	}

	const auto* MountMoverOwner = Cast<UProject_JMountMoverComponent>(GetOuter());
	const FProject_JMountMotionProfile& ActiveProfile = MountMoverOwner ? MountMoverOwner->MotionProfile : Profile;

	FProject_JMountMoverSyncState SimState;
	if (const auto* PrevMount = Start.SyncState.SyncStateCollection.FindDataByType<FProject_JMountMoverSyncState>())
	{
		SimState = *PrevMount;
	}
	const float Dt = FMath::Max(0.0f, Step.StepMs * 0.001f);
	Project_J::Mount::StepFlyingEnergyModel(*Inputs, ActiveProfile, Dt, SimState);

	const float YawRate = FMath::Clamp(Inputs->ThrottleTurn.Y, -1.0f, 1.0f) * ActiveProfile.TurnRateAtGallopDegPerSec;
	FRotator Heading = DefaultSync->GetOrientation_WorldSpace();
	Heading.Yaw = FRotator::NormalizeAxis(Heading.Yaw + YawRate * Dt);
	Heading.Pitch = SimState.PitchDeg;

	const float TravelSpeed = FMath::Min(SimState.AirSpeed,
		FMath::Max(ActiveProfile.FlightStallSpeed, SimState.AirSpeed * Inputs->GetFlightStreamingSpeedScale()));
	FVector Vel = Heading.Vector() * TravelSpeed;
	if (SimState.bIsGliding && Vel.Z > -ActiveProfile.GlideSinkRateCmPerSec)
	{
		Vel.Z = FMath::Min(Vel.Z, -ActiveProfile.GlideSinkRateCmPerSec);
	}
	Move.LinearVelocity = Vel;
	Move.AngularVelocityDegrees = FVector(0.0, 0.0, YawRate);
}

void UProject_JFlyingMountMode::SimulationTick_Implementation(const FSimulationTickParams& Params, FMoverTickEndData& Output)
{
	const float Dt = FMath::Max(0.0f, Params.TimeStep.StepMs * 0.001f);
	const auto* StartDefault = Params.StartState.SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
	if (!StartDefault)
	{
		return;
	}
	const auto* MountMoverOwner = Cast<UProject_JMountMoverComponent>(GetOuter());
	const FProject_JMountMotionProfile& ActiveProfile = MountMoverOwner ? MountMoverOwner->MotionProfile : Profile;

	auto& OutDefault = Output.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>();
	auto& OutMount = Output.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FProject_JMountMoverSyncState>();
	if (const auto* PrevMount = Params.StartState.SyncState.SyncStateCollection.FindDataByType<FProject_JMountMoverSyncState>())
	{
		OutMount = *PrevMount;
	}
	if (const auto* Inputs = Params.StartState.InputCmd.InputCollection.FindDataByType<FProject_JMountMoverInputs>())
	{
		if (Inputs->bLandPressed)
		{
			OutDefault = *StartDefault;
			OutMount.FlightState = EProject_JMountFlightState::Landing;
			OutMount.bIsGliding = false;
			Output.MovementEndState.NextModeName = DefaultModeNames::Falling;
			Output.MovementEndState.RemainingMs = Params.TimeStep.StepMs;
			return;
		}
		Project_J::Mount::StepFlyingEnergyModel(*Inputs, ActiveProfile, Dt, OutMount);
	}

	const FVector Delta = Params.ProposedMove.LinearVelocity * Dt;
	FRotator NewRot = StartDefault->GetOrientation_WorldSpace();
	NewRot.Yaw = FRotator::NormalizeAxis(NewRot.Yaw + Params.ProposedMove.AngularVelocityDegrees.Z * Dt);
	NewRot.Pitch = OutMount.PitchDeg;
	NewRot.Roll = OutMount.BankDeg;

	FVector NewLoc = StartDefault->GetLocation_WorldSpace() + Delta;
	FVector FinalVel = Params.ProposedMove.LinearVelocity;
	if (Params.MovingComps.UpdatedComponent.IsValid() && !Delta.IsNearlyZero())
	{
		FHitResult MoveHit;
		FMovementRecord MoveRecord;
		MoveRecord.SetDeltaSeconds(Dt);
		UMovementUtils::TrySafeMoveUpdatedComponent(Params.MovingComps, Delta, NewRot.Quaternion(), true, MoveHit, ETeleportType::None, MoveRecord);
		if (MoveHit.IsValidBlockingHit())
		{
			UMovementUtils::TryMoveToSlideAlongSurface(Params.MovingComps, Delta, 1.0f - MoveHit.Time, NewRot.Quaternion(), MoveHit.Normal, MoveHit, true, MoveRecord);
			FinalVel = (Dt > UE_KINDA_SMALL_NUMBER) ? MoveRecord.GetRelevantVelocity() : FVector::ZeroVector;
		}
		NewLoc = Params.MovingComps.UpdatedComponent->GetComponentLocation();
	}
	OutDefault.SetTransforms_WorldSpace(NewLoc, NewRot, FinalVel, Params.ProposedMove.AngularVelocityDegrees);
}

UProject_JMountMoverComponent::UProject_JMountMoverComponent()
{
	UProject_JGroundMountMode* GroundMode = CreateDefaultSubobject<UProject_JGroundMountMode>(TEXT("GroundMountMode"));
	UProject_JFlyingMountMode* FlyingMode = CreateDefaultSubobject<UProject_JFlyingMountMode>(TEXT("FlyingMountMode"));
	MovementModes.Add(GroundMountModeName, GroundMode);
	MovementModes.Add(FlyingMountModeName, FlyingMode);
	StartingMovementMode = GroundMountModeName;
	PersistentSyncStateDataTypes.Add(FMoverDataPersistence{ FProject_JMountMoverSyncState::StaticStruct(), true });
}

void UProject_JMountMoverComponent::OnRegister()
{
	Super::OnRegister();
	if (auto* Settings = FindSharedSettings_Mutable<UCommonLegacyMovementSettings>())
	{
		Settings->GroundMovementModeName = GroundMountModeName;
	}
}

bool UProject_JMountMoverComponent::HasTakeoffClearance(const FVector& Location) const
{
	if (!MotionProfile.bCanFly)
	{
		return false;
	}
	// Verify vertical clearance when running inside a live world with a blocking ceiling above the mount.
	const AActor* Owner = GetOwner();
	const UWorld* World = GetWorld();
	if (Owner && World)
	{
		if (const UCapsuleComponent* Cap = Owner->FindComponentByClass<UCapsuleComponent>())
		{
			FHitResult CeilingHit;
			FCollisionQueryParams Query(SCENE_QUERY_STAT(MountMoverClearance), false, Owner);
			FCollisionResponseParams Resp = FCollisionResponseParams::DefaultResponseParam;
			Resp.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
			const FVector End = Location + FVector(0.0, 0.0, 300.0);
			if (World->SweepSingleByChannel(CeilingHit, Location, End, FQuat::Identity, Cap->GetCollisionObjectType(), Cap->GetCollisionShape(), Query, Resp))
			{
				return false;
			}
		}
	}
	return true;
}

bool UProject_JMountMoverComponent::TryEnterFlightMode()
{
	if (GetMovementModeName() != GroundMountModeName || !GetOwner() || !HasTakeoffClearance(GetOwner()->GetActorLocation()))
	{
		return false;
	}
	QueueNextMode(FlyingMountModeName);
	return true;
}

void UProject_JMountMoverComponent::ForceStopFlightToFalling()
{
	QueueNextMode(DefaultModeNames::Falling);
}

FProject_JMountMoverSyncState UProject_JMountMoverComponent::GetLatestMountSyncState() const
{
	if (const auto* State = GetSyncState().SyncStateCollection.FindDataByType<FProject_JMountMoverSyncState>())
	{
		return *State;
	}
	return FProject_JMountMoverSyncState{};
}

bool UProject_JMountMoverComponent::FindSafeLandingSurface(FHitResult& OutHit, float ProbeDistanceCm) const
{
	const AActor* Owner = GetOwner();
	const UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return false;
	}
	const UCapsuleComponent* Cap = Owner->FindComponentByClass<UCapsuleComponent>();
	const ECollisionChannel Channel = Cap ? Cap->GetCollisionObjectType() : ECC_Pawn;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MountMoverLanding), false, Owner);
	FCollisionResponseParams ResponseParams = FCollisionResponseParams::DefaultResponseParam;
	ResponseParams.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);

	const FVector Start = Owner->GetActorLocation();
	const FVector End = Start - FVector(0.0, 0.0, FMath::Max(50.0f, ProbeDistanceCm));
	return World->LineTraceSingleByChannel(OutHit, Start, End, Channel, QueryParams, ResponseParams);
}

AProject_JMountMoverPawn::AProject_JMountMoverPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;
	SetReplicatingMovement(false); // Mover NetworkPrediction owns replicated transform.

	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(65.0f, 110.0f);
	Capsule->SetCollisionProfileName(TEXT("Pawn"));
	RootComponent = Capsule;

	MountMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("MountMesh"));
	MountMesh->SetupAttachment(Capsule);

	MountMover = CreateDefaultSubobject<UProject_JMountMoverComponent>(TEXT("MountMover"));
	MountAbilitySystemComponent = CreateDefaultSubobject<UProject_JAbilitySystemComponent>(TEXT("MountAbilitySystemComponent"));
	MountAbilitySystemComponent->SetIsReplicated(true);
	MountAttributeSet = CreateDefaultSubobject<UProject_JMountAttributeSet>(TEXT("MountAttributeSet"));
	InteractionTargetComponent = CreateDefaultSubobject<UProject_JInteractionTargetComponent>(TEXT("InteractionTargetComponent"));
	InteractionTargetComponent->InteractionRange = MountInteractionDistance;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(Capsule);
	CameraBoom->TargetArmLength = 520.0f;
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
}

void AProject_JMountMoverPawn::BeginPlay()
{
	Super::BeginPlay();
	if (MountAbilitySystemComponent && MountAttributeSet)
	{
		MountAbilitySystemComponent->InitAbilityActorInfo(this, this);
		if (HasAuthority())
		{
			MountAttributeSet->SetMaxHealth(MaxHealth);
			MountAttributeSet->SetHealth(Health);
		}
		Health = MountAttributeSet->GetHealth();
		MaxHealth = MountAttributeSet->GetMaxHealth();
		HealthChangedHandle = MountAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
			UProject_JMountAttributeSet::GetHealthAttribute()).AddUObject(this, &ThisClass::OnHealthAttributeChanged);
	}
}

void AProject_JMountMoverPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ApplyMountMappingContext(false);
	SetActorTickEnabled(false);
	if (HasAuthority() && Rider && GetWorld() && !GetWorld()->bIsTearingDown)
	{
		DismountRider(true);
	}
	if (MountAbilitySystemComponent && HealthChangedHandle.IsValid())
	{
		MountAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
			UProject_JMountAttributeSet::GetHealthAttribute()).Remove(HealthChangedHandle);
	}
	HealthChangedHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

void AProject_JMountMoverPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AProject_JMountMoverPawn, Rider);
}

UAbilitySystemComponent* AProject_JMountMoverPawn::GetAbilitySystemComponent() const
{
	return MountAbilitySystemComponent;
}

APlayerState* AProject_JMountMoverPawn::ResolveRiderAbilityOwner(const APawn* ExpectedRider) const
{
	if (!IsValid(ExpectedRider) || Rider != ExpectedRider || ExpectedRider->GetController()
		|| IsActorBeingDestroyed() || ExpectedRider->GetAttachParentActor() != this || ExpectedRider->GetWorld() != GetWorld()) return nullptr;
	auto* State = GetPlayerState();
	if (!IsValid(State) || State->IsActorBeingDestroyed() || State->GetPawn() != this || State->GetWorld() != GetWorld()) return nullptr;
	const auto* ActiveController = GetController();
	if (ActiveController)
	{
		if (!IsValid(ActiveController) || ActiveController->GetPawn() != this || State->GetOwner() != ActiveController
			|| (HasAuthority() && ActiveController != SessionController.Get())) return nullptr;
	}
	else if (GetNetMode() != NM_Client || GetLocalRole() != ROLE_SimulatedProxy) return nullptr;
	const auto* OwnerInterface = Cast<IProject_JAbilitySystemOwnerInterface>(State);
	const auto* ASC = OwnerInterface ? OwnerInterface->GetProjectJAbilitySystemComponent() : nullptr;
	return IsValid(ASC) && ASC->IsRegistered() && !ASC->IsBeingDestroyed() && !ASC->IsActorInfoChanging()
		&& ASC->GetOwner() == State && ASC->GetOwnerActor() == State && ASC->GetAvatarActor() == ExpectedRider ? State : nullptr;
}

void AProject_JMountMoverPawn::OnHealthAttributeChanged(const FOnAttributeChangeData& Data)
{
	Health = Data.NewValue;
	if (HasAuthority() && Data.OldValue > 0.0f && Data.NewValue <= 0.0f)
	{
		if (MountMover && (IsFlying() || MountMover->GetNextMovementModeName() == UProject_JMountMoverComponent::FlyingMountModeName))
		{
			MountMover->ForceStopFlightToFalling();
		}
		DismountRider(true);
	}
}

bool AProject_JMountMoverPawn::ApplyMountDamage(float Damage)
{
	if (!HasAuthority() || !MountAbilitySystemComponent || !HealthChangedHandle.IsValid() || !FMath::IsFinite(Damage) || Damage <= 0.0f || Health <= 0.0f)
	{
		return false;
	}
	MountAbilitySystemComponent->ApplyModToAttribute(UProject_JMountAttributeSet::GetHealthAttribute(), EGameplayModOp::Additive, -Damage);
	return true;
}

bool AProject_JMountMoverPawn::CanInteract_Implementation(APawn* Interactor) const
{
	if (!IsValid(Interactor) || Rider != nullptr || Health <= 0.0f)
	{
		return false;
	}
	return FVector::DistSquared(Interactor->GetActorLocation(), GetActorLocation()) <= FMath::Square(MountInteractionDistance);
}

void AProject_JMountMoverPawn::Interact_Implementation(APawn* Interactor)
{
	if (HasAuthority())
	{
		TryMountRider(Interactor);
	}
}

bool AProject_JMountMoverPawn::TryMountRider(APawn* NewRider)
{
	if (!HasAuthority() || !CanInteract_Implementation(NewRider))
	{
		return false;
	}
	AController* RiderController = NewRider->GetController();
	UCapsuleComponent* RiderCapsule = Project_J::Movement::FindCapsule(NewRider);
	if (!RiderController || !RiderCapsule)
	{
		return false;
	}
	TWeakObjectPtr<UProject_JAbilitySystemComponent> RiderASC;
	if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(NewRider))
	{
		if (const UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
		{
			FGameplayTagContainer RiderTags;
			ASC->GetOwnedGameplayTags(RiderTags);
			if (Project_J::Mount::EvaluateRiderGameplayTags(RiderTags) != EProject_JMountEligibilityFailure::None)
			{
				return false;
			}
			RiderASC = Cast<UProject_JAbilitySystemComponent>(ASI->GetAbilitySystemComponent());
		}
	}

	const auto Frame = Project_J::Movement::ReadPhysicalFrame(NewRider);
	const ECollisionEnabled::Type ObsCol = RiderCapsule->GetCollisionEnabled();
	SavedRiderCollision = (ObsCol == ECollisionEnabled::NoCollision) ? ECollisionEnabled::QueryAndPhysics : ObsCol;
	SavedRiderMode = (Frame.Mode == MOVE_None) ? MOVE_Walking : Frame.Mode;
	SavedRiderCustomMode = (Frame.Mode == MOVE_None) ? 0 : Frame.CustomMode;
	bHasSavedRiderState = true;

	Rider = NewRider;
	SessionController = RiderController;

	USceneComponent* AttachTarget = MountMesh ? static_cast<USceneComponent*>(MountMesh) : GetRootComponent();
	const FName Socket = (MountMesh && MountMesh->DoesSocketExist(RiderSocketName)) ? RiderSocketName : NAME_None;
	if (!NewRider->AttachToComponent(AttachTarget, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket))
	{
		Rider = nullptr;
		SessionController.Reset();
		bHasSavedRiderState = false;
		return false;
	}
	RiderCapsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Project_J::Movement::SetMovementMode(NewRider, MOVE_None);

	RiderController->Possess(this);
	if (Rider != NewRider || GetController() != RiderController || RiderController->GetPawn() != this)
	{
		if (Rider == NewRider) DismountRiderInternal(true, false);
		return false;
	}
	if (TStrongObjectPtr<UProject_JAbilitySystemComponent> ASC{RiderASC.Get()}; ASC)
	{
		MountedTagAbilitySystem = ASC.Get();
		ASC->AddProjectJLooseGameplayTag(FProject_JGameplayTags::Get().State_Mounted, true);
		if (Rider != NewRider || GetController() != RiderController) return false;
	}
	GrantRiderIntegrityWhitelist(NewRider);
	ForceNetUpdate();
	return true;
}

bool AProject_JMountMoverPawn::DismountRider(bool bForce)
{
	return DismountRiderInternal(bForce, true);
}

bool AProject_JMountMoverPawn::DismountRiderInternal(bool bForce, bool bRestorePossession)
{
	if (!HasAuthority() || !Rider || bDismountInProgress)
	{
		return false;
	}
	const bool bAirborne = MountMover && MountMover->GetMovementModeName() != UProject_JMountMoverComponent::GroundMountModeName;
	if (!bForce && bAirborne)
	{
		// Normal mid-air dismount disallowed unless forced (fatal damage / unpossess).
		return false;
	}

	const TStrongObjectPtr<APawn> PreviousRider(Rider.Get());
	UCapsuleComponent* RiderCapsule = Project_J::Movement::FindCapsule(PreviousRider.Get());
	TOptional<FVector> DismountLocation;
	if (UWorld* World = GetWorld())
	{
		if (RiderCapsule)
		{
			const FVector Candidates[] = {
				GetActorLocation() + GetActorRightVector() * 140.0,
				GetActorLocation() - GetActorRightVector() * 140.0,
				GetActorLocation() - GetActorForwardVector() * 160.0,
				GetActorLocation() + FVector(0.0, 0.0, 120.0)
			};
			FCollisionQueryParams Params(SCENE_QUERY_STAT(MountMoverDismountExit), false, this);
			Params.AddIgnoredActor(PreviousRider.Get());
			const FCollisionResponseParams Responses(RiderCapsule->GetCollisionResponseToChannels());
			for (const FVector& Cand : Candidates)
			{
				if (!World->OverlapBlockingTestByChannel(Cand, FQuat::Identity, RiderCapsule->GetCollisionObjectType(), RiderCapsule->GetCollisionShape(), Params, Responses))
				{
					DismountLocation = Cand;
					break;
				}
			}
		}
	}
	TGuardValue<bool> DismountGuard(bDismountInProgress, true);
	const bool bMovedToExit = DismountLocation.IsSet() && PreviousRider->TeleportTo(
		DismountLocation.GetValue(), FRotator(0.0, GetActorRotation().Yaw, 0.0), false, true);
	if (!bForce && !bMovedToExit) return false;
	if (bForce && !bMovedToExit)
	{
		// Forced dismount fallback: place rider safely above the mount capsule instead of re-enabling
		// collision while embedded inside the mount.
		const double MountHalfHeight = GetSimpleCollisionHalfHeight();
		const double RiderHalfHeight = IsValid(RiderCapsule) ? RiderCapsule->GetScaledCapsuleHalfHeight() : 88.0;
		const FVector FallbackExit = GetActorLocation() + FVector(0.0, 0.0, MountHalfHeight + RiderHalfHeight + 20.0);
		PreviousRider->TeleportTo(FallbackExit, FRotator(0.0, GetActorRotation().Yaw, 0.0), false, true);
	}

	const TStrongObjectPtr<AController> Ctrl(GetController() ? GetController() : SessionController.Get());
	const TStrongObjectPtr<UProject_JAbilitySystemComponent> TaggedASC(MountedTagAbilitySystem.Get());
	const auto Collision = bHasSavedRiderState ? SavedRiderCollision : ECollisionEnabled::QueryAndPhysics;
	const EMovementMode TargetMode = bAirborne ? MOVE_Falling : (bHasSavedRiderState ? SavedRiderMode : MOVE_Walking);
	const uint8 TargetCustomMode = TargetMode == MOVE_Custom ? SavedRiderCustomMode : 0;
	Rider = nullptr;
	SessionController.Reset();
	MountedTagAbilitySystem.Reset();
	bHasSavedRiderState = false;

	PreviousRider->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	if (IsValid(RiderCapsule)) RiderCapsule->SetCollisionEnabled(Collision);
	if (bRestorePossession && IsValid(Ctrl.Get()) && IsValid(PreviousRider.Get()) && !PreviousRider->IsActorBeingDestroyed())
	{
		Ctrl->Possess(PreviousRider.Get());
	}
	// Restore movement mode AFTER Possess so Restart/Possess cannot overwrite MOVE_Falling.
	if (IsValid(PreviousRider.Get()) && !PreviousRider->IsActorBeingDestroyed())
	{
		Project_J::Movement::SetMovementMode(PreviousRider.Get(), TargetMode, TargetCustomMode);
		GrantRiderIntegrityWhitelist(PreviousRider.Get());
	}
	if (MountMover && IsFlying())
	{
		MountMover->ForceStopFlightToFalling();
	}
	if (IsValid(TaggedASC.Get()))
	{
		TaggedASC->RemoveProjectJLooseGameplayTag(FProject_JGameplayTags::Get().State_Mounted, true);
	}
	ForceNetUpdate();
	return true;
}

bool AProject_JMountMoverPawn::GetRiderHandIKTargets(FVector& OutLeftTarget, FVector& OutRightTarget) const
{
	if (!MountMesh || !MountMesh->DoesSocketExist(RiderLeftHandSocketName) || !MountMesh->DoesSocketExist(RiderRightHandSocketName))
	{
		return false;
	}
	OutLeftTarget = MountMesh->GetSocketLocation(RiderLeftHandSocketName);
	OutRightTarget = MountMesh->GetSocketLocation(RiderRightHandSocketName);
	return true;
}

void AProject_JMountMoverPawn::PawnClientRestart()
{
	Super::PawnClientRestart();
	const bool bLocal = IsLocallyControlled();
	SetActorTickEnabled(bLocal);
	ApplyMountMappingContext(bLocal);
}

void AProject_JMountMoverPawn::OnRep_Controller()
{
	Super::OnRep_Controller();
	const bool bLocal = IsLocallyControlled();
	SetActorTickEnabled(bLocal);
	ApplyMountMappingContext(bLocal);
	if (!bLocal)
	{
		CachedThrottleTurn = FVector2D::ZeroVector;
		CachedPitchInput = 0.0f;
		bCachedBoostHeld = false;
		bCachedGlideHeld = false;
	}
}

void AProject_JMountMoverPawn::UnPossessed()
{
	ApplyMountMappingContext(false);
	SetActorTickEnabled(false);
	CachedThrottleTurn = FVector2D::ZeroVector;
	CachedPitchInput = 0.0f;
	bCachedBoostHeld = false;
	bCachedGlideHeld = false;
	bPendingTakeOff = false;
	bPendingLand = false;
	// A placed pawn can have an AI controller. During possession transfer the
	// engine releases that controller after TryMountRider has staged the rider.
	if (HasAuthority() && Rider && GetController() == SessionController.Get() && GetWorld() && !GetWorld()->bIsTearingDown)
		DismountRiderInternal(true, false);
	Super::UnPossessed();
}

void AProject_JMountMoverPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsLocallyControlled() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f)
	{
		return;
	}
	const bool bFlyingNow = IsFlying();
	const FProject_JMountMoverSyncState Sync = MountMover ? MountMover->GetLatestMountSyncState() : FProject_JMountMoverSyncState{};
	const float DiveFactor = (bFlyingNow && Sync.PitchDeg < -8.0f)
		? FMath::Clamp((-Sync.PitchDeg - 8.0f) / 37.0f, 0.0f, 1.0f)
		: 0.0f;

	if (CameraBoom)
	{
		const float BaseArm = bFlyingNow ? FlyingCameraArmLength : GroundCameraArmLength;
		const float TargetArm = FMath::Clamp(BaseArm + DiveFactor * 120.0f + WheelZoomOffset, 220.0f, 1600.0f);
		CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, TargetArm, DeltaSeconds, 4.5f);
	}
	if (FollowCamera)
	{
		const float Speed2D = static_cast<float>(GetVelocity().Size2D());
		const float SpeedBoost = FMath::GetMappedRangeValueClamped(FVector2f(800.0f, 2400.0f), FVector2f(0.0f, 6.0f), Speed2D);
		const float TargetFOV = FMath::Clamp(BaseCameraFOV + DiveFactor * DiveFOVBoost + SpeedBoost, 60.0f, 125.0f);
		FollowCamera->SetFieldOfView(FMath::FInterpTo(FollowCamera->FieldOfView, TargetFOV, DeltaSeconds, 4.5f));
	}
}

void AProject_JMountMoverPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	if (UEnhancedInputComponent* Enhanced = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveAction)
		{
			Enhanced->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ThisClass::HandleMoveInput);
			Enhanced->BindAction(MoveAction, ETriggerEvent::Completed, this, &ThisClass::HandleMoveCompleted);
		}
		if (LookAction)
		{
			Enhanced->BindAction(LookAction, ETriggerEvent::Triggered, this, &ThisClass::HandleLookInput);
		}
		if (PitchOrAscendAction)
		{
			Enhanced->BindAction(PitchOrAscendAction, ETriggerEvent::Triggered, this, &ThisClass::HandlePitchInput);
			Enhanced->BindAction(PitchOrAscendAction, ETriggerEvent::Completed, this, &ThisClass::HandlePitchCompleted);
		}
		if (BoostAction)
		{
			Enhanced->BindAction(BoostAction, ETriggerEvent::Started, this, &ThisClass::HandleBoostStarted);
			Enhanced->BindAction(BoostAction, ETriggerEvent::Completed, this, &ThisClass::HandleBoostCompleted);
		}
		if (GlideAction)
		{
			Enhanced->BindAction(GlideAction, ETriggerEvent::Started, this, &ThisClass::HandleGlideStarted);
			Enhanced->BindAction(GlideAction, ETriggerEvent::Completed, this, &ThisClass::HandleGlideCompleted);
		}
		if (TakeOffAction)
		{
			Enhanced->BindAction(TakeOffAction, ETriggerEvent::Started, this, &ThisClass::HandleTakeOffTriggered);
		}
		if (LandAction)
		{
			Enhanced->BindAction(LandAction, ETriggerEvent::Started, this, &ThisClass::HandleLandTriggered);
		}
		if (DismountAction)
		{
			Enhanced->BindAction(DismountAction, ETriggerEvent::Started, this, &ThisClass::HandleDismountTriggered);
		}
	}
}

void AProject_JMountMoverPawn::ProduceInput_Implementation(int32 SimTimeMs, FMoverInputCmdContext& InputCmd)
{
	FCharacterDefaultInputs& DefaultInputs = InputCmd.InputCollection.FindOrAddMutableDataByType<FCharacterDefaultInputs>();
	DefaultInputs.ControlRotation = GetControlRotation();
	const FRotator YawRot(0.0, DefaultInputs.ControlRotation.Yaw, 0.0);
	const FVector Forward = FRotationMatrix(YawRot).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(YawRot).GetUnitAxis(EAxis::Y);
	const FVector MoveDir = (Forward * CachedThrottleTurn.X + Right * CachedThrottleTurn.Y).GetClampedToMaxSize(1.0);
	DefaultInputs.SetMoveInput(EMoveInputType::DirectionalIntent, MoveDir);
	DefaultInputs.OrientationIntent = MoveDir.IsNearlyZero() ? GetActorForwardVector() : MoveDir.GetSafeNormal();

	FProject_JMountMoverInputs& MountInputs = InputCmd.InputCollection.FindOrAddMutableDataByType<FProject_JMountMoverInputs>();
	MountInputs.ThrottleTurn = CachedThrottleTurn;
	MountInputs.PitchInput = CachedPitchInput;
	MountInputs.bBoostHeld = bCachedBoostHeld;
	MountInputs.bGlideHeld = bCachedGlideHeld;
	MountInputs.bTakeOffPressed = bPendingTakeOff;
	MountInputs.bLandPressed = bPendingLand;
	MountInputs.FlightActionSeq = CachedFlightActionSeq;
	MountInputs.FlightStreamingSpeedScale = MAX_uint8;
	if (IsFlying() && IsLocallyControlled())
	{
		if (const auto* Source = Cast<IProject_JFlightStreamingInputSource>(GetController()))
		{
			const float Scale = Source->GetLocalFlightStreamingSpeedScale(*this);
			if (FMath::IsFinite(Scale))
				MountInputs.FlightStreamingSpeedScale = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Scale, 0.1f, 1.f) * 255.f));
		}
	}

	bPendingTakeOff = false;
	bPendingLand = false;
}

void AProject_JMountMoverPawn::SetMountControlInput(FVector2D InThrottleTurn, float InPitchInput, bool bInBoost, bool bInGlide)
{
	CachedThrottleTurn = FVector2D(FMath::Clamp(InThrottleTurn.X, -1.0, 1.0), FMath::Clamp(InThrottleTurn.Y, -1.0, 1.0));
	CachedPitchInput = FMath::Clamp(InPitchInput, -1.0f, 1.0f);
	bCachedBoostHeld = bInBoost;
	bCachedGlideHeld = bInGlide;
}

bool AProject_JMountMoverPawn::ApplyWheelZoom(float WheelDelta)
{
	if (!IsLocallyControlled() || !CameraBoom || !FMath::IsFinite(WheelDelta) || WheelDelta == 0.0f)
	{
		return false;
	}
	WheelZoomOffset = FMath::Clamp(WheelZoomOffset - WheelDelta * 35.0f, -250.0f, 650.0f);
	return true;
}

void AProject_JMountMoverPawn::ServerRequestDismount_Implementation()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !Rider || !UProject_JCommandBudgetComponent::AdmitClientCommand(PC))
	{
		return;
	}
	DismountRider(false);
}

void AProject_JMountMoverPawn::ServerRequestToggleFlight_Implementation()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !Rider || Health <= 0.0f || !MountMover || !UProject_JCommandBudgetComponent::AdmitClientCommand(PC))
	{
		return;
	}
	if (IsFlying())
	{
		MountMover->ForceStopFlightToFalling();
	}
	else
	{
		MountMover->TryEnterFlightMode();
	}
}

void AProject_JMountMoverPawn::HandleMoveInput(const FInputActionValue& Value)
{
	const FVector2D Vec = Value.Get<FVector2D>();
	CachedThrottleTurn = FVector2D(FMath::Clamp(Vec.Y, -1.0, 1.0), FMath::Clamp(Vec.X, -1.0, 1.0));
}

void AProject_JMountMoverPawn::HandleMoveCompleted(const FInputActionValue&)
{
	CachedThrottleTurn = FVector2D::ZeroVector;
}

void AProject_JMountMoverPawn::HandleLookInput(const FInputActionValue& Value)
{
	const FVector2D Look = Value.Get<FVector2D>();
	AddControllerYawInput(Look.X);
	AddControllerPitchInput(Look.Y);
}

void AProject_JMountMoverPawn::HandlePitchInput(const FInputActionValue& Value)
{
	CachedPitchInput = FMath::Clamp(Value.Get<float>(), -1.0f, 1.0f);
}

void AProject_JMountMoverPawn::HandlePitchCompleted(const FInputActionValue&)
{
	CachedPitchInput = 0.0f;
}

void AProject_JMountMoverPawn::HandleBoostStarted(const FInputActionValue&) { bCachedBoostHeld = true; }
void AProject_JMountMoverPawn::HandleBoostCompleted(const FInputActionValue&) { bCachedBoostHeld = false; }
void AProject_JMountMoverPawn::HandleGlideStarted(const FInputActionValue&) { bCachedGlideHeld = true; }
void AProject_JMountMoverPawn::HandleGlideCompleted(const FInputActionValue&) { bCachedGlideHeld = false; }

void AProject_JMountMoverPawn::HandleTakeOffTriggered(const FInputActionValue&)
{
	bPendingTakeOff = !IsFlying();
	bPendingLand = !bPendingTakeOff;
	++CachedFlightActionSeq;
}

void AProject_JMountMoverPawn::HandleLandTriggered(const FInputActionValue&)
{
	bPendingLand = true;
	bPendingTakeOff = false;
	++CachedFlightActionSeq;
}

void AProject_JMountMoverPawn::HandleDismountTriggered(const FInputActionValue&)
{
	ServerRequestDismount();
}

void AProject_JMountMoverPawn::ApplyMountMappingContext(bool bAdd)
{
	if (!MountMappingContext)
	{
		return;
	}
	if (!bAdd)
	{
		if (ULocalPlayer* OldLP = BoundInputLocalPlayer.Get())
		{
			if (auto* Sub = OldLP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				Sub->RemoveMappingContext(MountMappingContext);
			}
		}
		BoundInputLocalPlayer.Reset();
		return;
	}
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			if (BoundInputLocalPlayer.IsValid() && BoundInputLocalPlayer.Get() != LP)
			{
				ApplyMountMappingContext(false);
			}
			if (auto* Sub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				Sub->AddMappingContext(MountMappingContext, 2);
				BoundInputLocalPlayer = LP;
			}
		}
	}
}

void AProject_JMountMoverPawn::GrantRiderIntegrityWhitelist(APawn* TargetRider) const
{
	if (!HasAuthority() || !TargetRider || !GetWorld())
	{
		return;
	}
	if (const IProject_JSocialStateInterface* Identity = Cast<IProject_JSocialStateInterface>(TargetRider->GetPlayerState()))
	{
		const FGuid CharId = Identity->GetAuthoritativeCharacterGuid();
		if (CharId.IsValid())
		{
			if (UGameInstance* GI = GetWorld()->GetGameInstance())
			{
				if (auto* Integrity = GI->GetSubsystem<UProject_JServerIntegritySubsystem>())
				{
					Integrity->GrantMovementWhitelist(CharId, 2.5, GetWorld()->GetTimeSeconds());
				}
			}
		}
	}
}

bool AProject_JMountMoverPawn::IsFlying() const
{
	return MountMover && MountMover->GetMovementModeName() == UProject_JMountMoverComponent::FlyingMountModeName;
}

#if WITH_EDITOR
EDataValidationResult AProject_JMountMoverPawn::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult Base = Super::IsDataValid(Context);
	bool bValid = (Base != EDataValidationResult::Invalid);
	if (!FMath::IsFinite(MaxHealth) || MaxHealth <= 0.0f)
	{
		Context.AddError(NSLOCTEXT("ProjectJMount", "InvalidHealth", "MountMoverPawn MaxHealth must be positive."));
		bValid = false;
	}
	if (!FMath::IsFinite(MountInteractionDistance) || MountInteractionDistance < 50.0f)
	{
		Context.AddError(NSLOCTEXT("ProjectJMount", "InvalidInteractDist", "MountMoverPawn MountInteractionDistance must be >= 50cm."));
		bValid = false;
	}
	if (MountMesh && MountMesh->GetSkeletalMeshAsset() != nullptr)
	{
		if (!RiderSocketName.IsNone() && !MountMesh->DoesSocketExist(RiderSocketName))
		{
			Context.AddWarning(FText::Format(
				NSLOCTEXT("ProjectJMount", "MissingRiderSocket", "MountMesh is missing RiderSocketName '{0}'; rider will snap to mesh root."),
				FText::FromName(RiderSocketName)));
		}
	}
	return bValid ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif
