#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Animation/Project_JCombatAnimProfile.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"

namespace
{
TAutoConsoleVariable<int32> CVarStrafeUpperBodyFacing(TEXT("p.ProjectJ.StrafeUpperBodyFacing"), 1,
	TEXT("Profile-controlled forward Strafe torso yaw presentation. 0=exact node bypass, 1=enabled. No movement or search changes."));
TAutoConsoleVariable<int32> CVarStrafeLowerBodyFacing(TEXT("p.ProjectJ.StrafeLowerBodyFacing"), 1,
	TEXT("Profile-controlled forward Strafe Run pelvis facing. Preserves evaluated feet; 0=exact bypass. Turns/actions/OTM are excluded."));
}

FProject_JStrafeUpperBodyFacingInput UProject_JCharacterAnimInstance::GetThreadSafeStrafeUpperBodyFacing() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().StrafeUpperBodyFacing;
}

FProject_JStrafeLowerBodyFacingInput UProject_JCharacterAnimInstance::GetThreadSafeStrafeLowerBodyFacing() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().StrafeLowerBodyFacing;
}

void UProject_JCharacterAnimInstance::UpdateStrafeFacingData(FProject_JAnimThreadSafeData& Data) const
{
	const auto* Profile = OwningPlayerCharacter ? OwningPlayerCharacter->GetCombatAnimProfile() : nullptr;
	const auto* Movement = OwningCharacter ? OwningCharacter->GetCharacterMovement() : nullptr;
	Data.StrafeUpperBodyFacing = {};
	Data.StrafeLowerBodyFacing = {};
	if (!Profile || !Movement) return;
	const bool bOwner = IsPrimaryMeshAnimInstance() && OwningPlayerCharacter->IsPlayerControlled() && Data.bIsLocallyControlled;
	const bool bActionOwner = !Movement->IsMovingOnGround() || Movement->HasAnimRootMotion() ||
		Movement->CurrentRootMotion.HasActiveRootMotionSources() || IsAnyMontagePlaying();
	Data.StrafeUpperBodyFacing = Project_J::Animation::BuildStrafeUpperBodyFacingInput(Data,
		Profile->StrafeUpperBodyFacing, bOwner, bActionOwner, CVarStrafeUpperBodyFacing.GetValueOnGameThread() != 0,
		OwningCharacter->GetActorRotation().Yaw);
	Data.StrafeLowerBodyFacing = Project_J::Animation::BuildStrafeLowerBodyFacingInput(Data,
		Profile->StrafeLowerBodyFacing, bOwner, bActionOwner, CVarStrafeLowerBodyFacing.GetValueOnGameThread() != 0,
		OwningCharacter->GetActorRotation().Yaw,
		OwningPlayerCharacter->GetLocomotionAnimStateComponent() ?
			OwningPlayerCharacter->GetLocomotionAnimStateComponent()->GetHeldLocalMoveInput() : FVector2D::ZeroVector);
}
