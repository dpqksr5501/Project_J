#include "Mount/Project_JMountCharacter.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Mount/Project_JMountComponent.h"
#include "Mount/Project_JMountAttributeSet.h"
#include "Project_JAbilitySystemComponent.h"
#include "Interaction/Project_JInteractionTargetComponent.h"
#include "Net/UnrealNetwork.h"

AProject_JMountCharacter::AProject_JMountCharacter()
{
	bReplicates = true;
	SetReplicateMovement(true);
	GetCharacterMovement()->bOrientRotationToMovement = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
	MountCameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("MountCameraBoom"));
	MountCameraBoom->SetupAttachment(RootComponent);
	MountCameraBoom->TargetArmLength = 800.0f;
	MountCameraBoom->SetRelativeLocation(FVector(0.0f, 0.0f, 280.0f));
	MountCameraBoom->bUsePawnControlRotation = true;
	MountCameraBoom->bEnableCameraLag = true;
	MountCameraBoom->CameraLagSpeed = 10.0f;
	MountCameraBoom->bEnableCameraRotationLag = true;
	MountCameraBoom->CameraRotationLagSpeed = 10.0f;
	MountFollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("MountFollowCamera"));
	MountFollowCamera->SetupAttachment(MountCameraBoom, USpringArmComponent::SocketName);
	MountFollowCamera->bUsePawnControlRotation = false;
	MountAbilitySystemComponent = CreateDefaultSubobject<UProject_JAbilitySystemComponent>(TEXT("MountAbilitySystemComponent"));
	MountAbilitySystemComponent->SetIsReplicated(true);
	MountAbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
	MountAttributeSet = CreateDefaultSubobject<UProject_JMountAttributeSet>(TEXT("MountAttributeSet"));
	InteractionTargetComponent = CreateDefaultSubobject<UProject_JInteractionTargetComponent>(TEXT("InteractionTargetComponent"));
	InteractionTargetComponent->PromptText = FText::FromString(TEXT("탈것 탑승"));
	InteractionTargetComponent->InteractionRange = MountInteractionDistance;
	InteractionTargetComponent->Priority = 70;
	InteractionTargetComponent->bExclusive = true;
}

UAbilitySystemComponent* AProject_JMountCharacter::GetAbilitySystemComponent() const { return MountAbilitySystemComponent; }
bool AProject_JMountCharacter::CanInteract_Implementation(ACharacter* Interactor) const { return CanMountRider(Interactor); }
void AProject_JMountCharacter::Interact_Implementation(ACharacter* Interactor) { TryMountRider(Interactor); }

bool AProject_JMountCharacter::GetRiderHandIKTargetsWorld(FVector& OutLeftTarget, FVector& OutRightTarget) const
{
	OutLeftTarget = FVector::ZeroVector;
	OutRightTarget = FVector::ZeroVector;

	const USkeletalMeshComponent* MountMesh = GetMesh();
	if (!bEnableRiderHandIK || !MountMesh ||
		!MountMesh->DoesSocketExist(RiderLeftHandSocketName) ||
		!MountMesh->DoesSocketExist(RiderRightHandSocketName))
	{
		return false;
	}

	OutLeftTarget = MountMesh->GetSocketLocation(RiderLeftHandSocketName);
	OutRightTarget = MountMesh->GetSocketLocation(RiderRightHandSocketName);
	return true;
}

void AProject_JMountCharacter::BeginPlay()
{
	Super::BeginPlay();
	MountAbilitySystemComponent->InitAbilityActorInfo(this, this);
	if (HasAuthority())
	{
		MountAttributeSet->SetMaxHealth(MaxHealth); MountAttributeSet->SetHealth(Health);
	}
}

void AProject_JMountCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	MountAbilitySystemComponent->InitAbilityActorInfo(this, this);
}

void AProject_JMountCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AProject_JMountCharacter, Rider);
	DOREPLIFETIME(AProject_JMountCharacter, MountState);
	DOREPLIFETIME(AProject_JMountCharacter, MaxHealth);
	DOREPLIFETIME(AProject_JMountCharacter, Health);
}

bool AProject_JMountCharacter::ApplyMountDamage(float Damage)
{
	if (!HasAuthority() || Damage <= 0.0f || Health <= 0.0f)
	{
		return false;
	}

	Health = FMath::Max(0.0f, Health - Damage);
	if (Health <= 0.0f)
	{
		DismountRider(true);
	}
	ForceNetUpdate();
	return true;
}

bool AProject_JMountCharacter::CanMountRider(const ACharacter* NewRider) const
{
	if (!NewRider || Rider || Health <= 0.0f || !NewRider->GetController())
	{
		return false;
	}

	if (const UProject_JMountComponent* MountComponent = NewRider->FindComponentByClass<UProject_JMountComponent>())
	{
		if (MountComponent->IsMounted())
		{
			return false;
		}
	}

	return FVector::DistSquared(NewRider->GetActorLocation(), GetActorLocation()) <= FMath::Square(MountInteractionDistance);
}

bool AProject_JMountCharacter::TryMountRider(ACharacter* NewRider)
{
	if (!HasAuthority() || !CanMountRider(NewRider))
	{
		return false;
	}

	AController* RiderController = NewRider->GetController();
	if (!RiderController)
	{
		return false;
	}

	MountState = EProject_JMountState::Mounting;
	Rider = NewRider;
	AttachRider(NewRider);
	NotifyRiderMountChanged(NewRider, true);
	RiderController->Possess(this);
	MountState = EProject_JMountState::Mounted;
	ForceNetUpdate();
	K2_OnRiderMounted(NewRider);
	return true;
}

void AProject_JMountCharacter::ServerRequestDismount_Implementation()
{
	DismountRider(false);
}

bool AProject_JMountCharacter::DismountRider(bool bForce)
{
	if (!HasAuthority() || !Rider)
	{
		return false;
	}

	if (!bForce && !bAllowAirDismount && GetCharacterMovement()->IsFlying())
	{
		return false;
	}

	ACharacter* PreviousRider = Rider;
	AController* RiderController = GetController();
	FVector DismountLocation;
	if (!FindDismountLocation(DismountLocation))
	{
		return false;
	}

	MountState = EProject_JMountState::Dismounting;
	DetachRider(PreviousRider, DismountLocation);
	Rider = nullptr;
	NotifyRiderMountChanged(PreviousRider, false);
	if (RiderController)
	{
		RiderController->Possess(PreviousRider);
	}
	MountState = EProject_JMountState::Unmounted;
	ForceNetUpdate();
	K2_OnRiderDismounted(PreviousRider);
	return true;
}

void AProject_JMountCharacter::OnRep_Rider(ACharacter* PreviousRider)
{
	if (Rider)
	{
		AttachRider(Rider);
	}
}

void AProject_JMountCharacter::OnRep_MountState(EProject_JMountState PreviousState)
{
}

bool AProject_JMountCharacter::FindDismountLocation(FVector& OutLocation) const
{
	const FVector Right = GetActorRightVector();
	const FVector Candidate = GetActorLocation() + Right * (GetCapsuleComponent()->GetScaledCapsuleRadius() + 120.0f);
	const UCapsuleComponent* RiderCapsule = Rider ? Rider->GetCapsuleComponent() : nullptr;
	if (!RiderCapsule)
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MountDismount), false, this);
	QueryParams.AddIgnoredActor(Rider);
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(RiderCapsule->GetScaledCapsuleRadius(), RiderCapsule->GetScaledCapsuleHalfHeight());
	if (GetWorld()->OverlapBlockingTestByChannel(Candidate, FQuat::Identity, RiderCapsule->GetCollisionObjectType(), Shape, QueryParams))
	{
		return false;
	}

	OutLocation = Candidate;
	return true;
}

void AProject_JMountCharacter::AttachRider(ACharacter* NewRider) const
{
	if (!NewRider)
	{
		return;
	}

	USceneComponent* AttachTarget = GetMesh() ? static_cast<USceneComponent*>(GetMesh()) : GetRootComponent();
	const FName SocketName = GetMesh() && GetMesh()->DoesSocketExist(RiderSocketName) ? RiderSocketName : NAME_None;
	NewRider->AttachToComponent(AttachTarget, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName);
	NewRider->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NewRider->GetCharacterMovement()->DisableMovement();
}

void AProject_JMountCharacter::DetachRider(ACharacter* PreviousRider, const FVector& DismountLocation) const
{
	if (!PreviousRider)
	{
		return;
	}

	PreviousRider->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	PreviousRider->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PreviousRider->TeleportTo(DismountLocation, GetActorRotation(), false, true);
	PreviousRider->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
}

void AProject_JMountCharacter::NotifyRiderMountChanged(ACharacter* ChangedRider, bool bMounted)
{
	if (!ChangedRider)
	{
		return;
	}

	if (UProject_JMountComponent* MountComponent = ChangedRider->FindComponentByClass<UProject_JMountComponent>())
	{
		MountComponent->SetMountedMount(bMounted ? this : nullptr);
	}
}
