#include "Animation/Project_JMountAnimInstance.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

void UProject_JMountAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	const ACharacter* Character = Cast<ACharacter>(TryGetPawnOwner());
	const FVector Velocity = Character ? Character->GetVelocity() : FVector::ZeroVector;
	Speed = Velocity.Size2D();
	VerticalSpeed = Velocity.Z;
	bIsFalling = Character && Character->GetCharacterMovement() && Character->GetCharacterMovement()->IsFalling();
}
