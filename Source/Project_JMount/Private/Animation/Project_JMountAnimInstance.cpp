#include "Animation/Project_JMountAnimInstance.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
void UProject_JMountAnimInstance::NativeUpdateAnimation(float DeltaSeconds){ Super::NativeUpdateAnimation(DeltaSeconds); if(const ACharacter* C=Cast<ACharacter>(TryGetPawnOwner())){ const FVector V=C->GetVelocity(); Speed=V.Size2D(); VerticalSpeed=V.Z; bIsFalling=C->GetCharacterMovement()->IsFalling(); } }
