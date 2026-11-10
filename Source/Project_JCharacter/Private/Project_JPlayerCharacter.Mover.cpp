#include "Project_JPlayerCharacter.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "AbilitySystemComponent.h"
#include "Combat/Project_JGameplayAbility_Melee.h"
#include "GameFramework/Controller.h"
#include "Movement/Project_JMoverInputs.h"
#include "Movement/Project_JMoverComponent.h"
#include "Movement/Project_JGroundMovement.h"
#include "MoverDataModelTypes.h"

const FProject_JStrafeDirectionPolicy& AProject_JPlayerCharacter::GetStrafeDirectionPolicy() const
{
 const auto* Profile=GetLocomotionProfile();
 return Profile?Profile->StrafeDirectionPolicy:Super::GetStrafeDirectionPolicy();
}

float AProject_JPlayerCharacter::GetMoverMaxSpeed(bool bAcceptedSprint) const
{
 const auto* Profile=GetLocomotionProfile();
 const FProject_JGroundMovementSettings Defaults;
 const float Speed=IsMoveInputIgnored()?0.f:bAcceptedSprint?(Profile?Profile->SprintSpeed:Defaults.SprintSpeed):(Profile?Profile->WalkSpeed:Defaults.RunSpeed);
 if (!FMath::IsFinite(Speed) || Speed <= 0.f) { return 0.f; }
 static const FGameplayTag HasteTag = FGameplayTag::RequestGameplayTag(TEXT("State.Spellblade.Haste"));
 const auto* ASC = GetAbilitySystemComponent();
 return ASC && ASC->HasMatchingGameplayTag(HasteTag) ? Speed * 1.15f : Speed;
}

FProject_JMoverPolicy AProject_JPlayerCharacter::ResolveMoverPolicy(const FProject_JMoverInputs& Requested,const FCharacterDefaultInputs* Input) const
{
 FProject_JMoverPolicy Policy;
 Policy.bRequireDirectionalIntent=true;
 Policy.bStrafe=IsCombatModeActive() && ShouldUseCombatRotationMode();
 const bool bSprintDirectionAllowed=!Policy.bStrafe || (Input &&
  UProject_JGroundMovementLibrary::SupportsStrafeSprint(Input->GetMoveInput_WorldSpace(),FRotator(0,Input->ControlRotation.Yaw,0).Vector()));
 Policy.bSprint=Requested.bSprint && IsSprintLocomotionAllowed() && bSprintDirectionAllowed;
 Policy.bAllowJump=IsJumpLocomotionAllowed();
 Policy.MaxSpeed=Requested.bWalk && !Policy.bSprint?FMath::Min(CastChecked<UProject_JMoverComponent>(GetMover())->GroundMovementSettings.WalkSpeed,GetMoverMaxSpeed(false)):GetMoverMaxSpeed(Policy.bSprint);
 return Policy;
}

void AProject_JPlayerCharacter::ProduceInput_Implementation(int32, FMoverInputCmdContext& InputCmd)
{
	ConsumeMovementInputVector();
	const auto* State = GetLocomotionAnimStateComponent();
	const FVector2D Held = State ? State->GetHeldLocalMoveInput() : FVector2D::ZeroVector;
	const FVector WorldIntent = !Controller || IsMoveInputIgnored() ? FVector::ZeroVector :
		FRotator(0, GetControlRotation().Yaw, 0).RotateVector(FVector(Held.Y, Held.X, 0)).GetSafeNormal();
	const bool bUseStrafe = IsCombatModeActive() && ShouldUseCombatRotationMode();
	const bool bUseSprint = IsSprintLocomotionAllowed() && (!bUseStrafe || UProject_JGroundMovementLibrary::SupportsStrafeSprint(WorldIntent,FRotator(0,GetControlRotation().Yaw,0).Vector()));
	const float Speed = GetMoverMaxSpeed(bUseSprint);
	const auto* ASC = GetAbilitySystemComponent();
	const auto* Melee = ASC ? Cast<UProject_JGameplayAbility_Melee>(ASC->GetAnimatingAbility()) : nullptr;
	const bool bLockFacing = Melee && Melee->LocksFacingDuringSwing();
	PopulateMoverInput(InputCmd, WorldIntent, bUseStrafe, bUseSprint, Speed, IsJumpLocomotionAllowed(), false, Held.Size(), bLockFacing);
}

FVector AProject_JPlayerCharacter::GetGameplayMoveDirection(FVector* OutControlForward) const
{
	if (OutControlForward) { *OutControlForward = FVector::ZeroVector; }
	if (!Controller || IsMoveInputIgnored()) { return FVector::ZeroVector; }
	if (IsLocallyControlled())
	{
		const auto* State = GetLocomotionAnimStateComponent();
		const FVector2D Held = State ? State->GetHeldLocalMoveInput() : FVector2D::ZeroVector;
		const FRotator ControlYaw(0, GetControlRotation().Yaw, 0);
		if (OutControlForward) { *OutControlForward = ControlYaw.Vector(); }
		return ControlYaw.RotateVector(FVector(Held.Y, Held.X, 0)).GetSafeNormal2D();
	}
	const auto* MoveComponent = GetMover();
	const auto* Input = MoveComponent ? MoveComponent->GetLastInputCmd().InputCollection.FindDataByType<FCharacterDefaultInputs>() : nullptr;
	if (Input && OutControlForward) { *OutControlForward = FRotator(0, Input->ControlRotation.Yaw, 0).Vector(); }
	return Input ? Input->GetMoveInput_WorldSpace().GetSafeNormal2D() : FVector::ZeroVector;
}
