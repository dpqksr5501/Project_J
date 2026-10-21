// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Project_JLocomotionAnimTypes.generated.h"

UENUM(BlueprintType)
enum class EProject_JLocomotionGaitIntent : uint8
{
	Walk,
	Run,
	Sprint
};

UENUM(BlueprintType)
enum class EProject_JLocomotionRotationMode : uint8
{
	OrientToMovement,
	Strafe
};

/** Project J air clip directions. Hidden legacy ordinals remain serialized compatibility. */
UENUM(BlueprintType)
enum class EProject_JStateControllerStrafeDirection : uint8
{
	Forward,
	Backward,
	LeftLeftFootForward UMETA(Hidden),
	LeftRightFootForward UMETA(Hidden),
	RightLeftFootForward UMETA(Hidden),
	RightRightFootForward UMETA(Hidden),

	ForwardLeft,
	Left,
	BackwardLeft,
	BackwardRight,
	Right,
	ForwardRight
};

UENUM(BlueprintType)
enum class EProject_JStateControllerFoot : uint8
{
	Left,
	Right,
	None
};

UENUM(BlueprintType)
enum class EProject_JStateControllerFootSelectionReason : uint8
{
	MissingContactCurve,
	BothFeetUnplanted,
	ContactsTooSimilar,
	LeftFootLowerContact,
	RightFootLowerContact,
	PhaseHistoryFallback,
	DefaultFootFallback
};

UENUM(BlueprintType)
enum class EProject_JStateControllerPresentationState : uint8
{
	Disabled,
	IdleLoop,
	TransitionToLocomotion,
	LocomotionLoop,
	TransitionToIdle,
	TransitionToInAir,
	InAirLoop,
	TransitionToLand,
	TurnInPlace
};
