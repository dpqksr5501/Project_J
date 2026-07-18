#include "Animation/Project_JFlyingMountAnimInstance.h"
#include "Mount/Project_JFlyingMountCharacter.h"
void UProject_JFlyingMountAnimInstance::NativeUpdateAnimation(float DeltaSeconds){ Super::NativeUpdateAnimation(DeltaSeconds); if(const AProject_JFlyingMountCharacter* M=Cast<AProject_JFlyingMountCharacter>(TryGetPawnOwner())){ bIsFlying=M->IsFlyingMount(); bIsGliding=M->IsGliding(); } }
