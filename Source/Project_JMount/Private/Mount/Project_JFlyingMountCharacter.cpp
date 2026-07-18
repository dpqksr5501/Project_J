#include "Mount/Project_JFlyingMountCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
AProject_JFlyingMountCharacter::AProject_JFlyingMountCharacter(){ PrimaryActorTick.bCanEverTick=true; GetCharacterMovement()->MaxFlySpeed=FlightSpeed; }
void AProject_JFlyingMountCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const { Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AProject_JFlyingMountCharacter,bIsGliding); }
bool AProject_JFlyingMountCharacter::BeginFlight(){ if(!HasAuthority() || !IsOccupied() || Health <= 0.0f) return false; if(GetCharacterMovement()->IsFlying()) return true; GetCharacterMovement()->SetMovementMode(MOVE_Flying); bIsGliding=false; return true; }
bool AProject_JFlyingMountCharacter::EndFlight(){ if(!HasAuthority() || !GetCharacterMovement()->IsFlying()) return false; bIsGliding=false; GetCharacterMovement()->SetMovementMode(MOVE_Walking); return true; }
void AProject_JFlyingMountCharacter::SetGliding(bool bNewGliding){ if(HasAuthority() && GetCharacterMovement()->IsFlying()){ bIsGliding=bNewGliding; GetCharacterMovement()->MaxFlySpeed=bIsGliding?GlideSpeed:FlightSpeed; } }
bool AProject_JFlyingMountCharacter::IsFlyingMount() const { return GetCharacterMovement()->IsFlying(); }
void AProject_JFlyingMountCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || !IsFlyingMount() || GetVelocity().Z >= -5.0f) return;
	FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(MountLanding), false, this);
	const float TraceDistance = GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 40.0f;
	if (GetWorld()->LineTraceSingleByChannel(Hit, GetActorLocation(), GetActorLocation() - FVector::UpVector * TraceDistance, ECC_Visibility, Params) && Hit.bBlockingHit)
	{
		EndFlight();
	}
}
void AProject_JFlyingMountCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveAction) Input->BindAction(MoveAction,ETriggerEvent::Triggered,this,&AProject_JFlyingMountCharacter::HandleMove);
		if (LookAction) Input->BindAction(LookAction,ETriggerEvent::Triggered,this,&AProject_JFlyingMountCharacter::HandleLook);
		if (AscendAction) { Input->BindAction(AscendAction,ETriggerEvent::Started,this,&AProject_JFlyingMountCharacter::HandleTakeOff); Input->BindAction(AscendAction,ETriggerEvent::Triggered,this,&AProject_JFlyingMountCharacter::HandleAscend); }
		if (DescendAction) Input->BindAction(DescendAction,ETriggerEvent::Triggered,this,&AProject_JFlyingMountCharacter::HandleDescend);
		if (InteractAction) Input->BindAction(InteractAction,ETriggerEvent::Started,this,&AProject_JFlyingMountCharacter::HandleDismount);
	}
}
void AProject_JFlyingMountCharacter::HandleMove(const FInputActionValue& V){ const FVector2D I=V.Get<FVector2D>(); const FRotator R=GetControlRotation(); const FRotator Y(0,R.Yaw,0); AddMovementInput(FRotationMatrix(Y).GetUnitAxis(EAxis::X),I.Y); AddMovementInput(FRotationMatrix(Y).GetUnitAxis(EAxis::Y),I.X); }
void AProject_JFlyingMountCharacter::HandleLook(const FInputActionValue& V){ const FVector2D I=V.Get<FVector2D>(); AddControllerYawInput(I.X); AddControllerPitchInput(I.Y); }
void AProject_JFlyingMountCharacter::HandleAscend(const FInputActionValue& V){ if(IsFlyingMount()) AddMovementInput(FVector::UpVector,V.Get<float>()); }
void AProject_JFlyingMountCharacter::HandleDescend(const FInputActionValue& V){ if(IsFlyingMount()) AddMovementInput(FVector::DownVector,V.Get<float>()); }
void AProject_JFlyingMountCharacter::HandleTakeOff(){ if(!IsFlyingMount()) ServerRequestBeginFlight(); }
void AProject_JFlyingMountCharacter::ServerRequestBeginFlight_Implementation(){ BeginFlight(); }
void AProject_JFlyingMountCharacter::HandleDismount(){ ServerRequestDismount(); }
