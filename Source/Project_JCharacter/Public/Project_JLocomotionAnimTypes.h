// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Project_JLocomotionAnimTypes.generated.h"

UENUM(BlueprintType)
enum class EProject_JGroundMotionMode : uint8
{
	Idle,
	Start,
	Locomotion,
	Stop
};

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

UENUM(BlueprintType)
enum class EProject_JLocomotionPhaseFamily : uint8
{
	Idle,
	Start,
	Cycle,
	Stop,
	Pivot,
	Turn,
	TurnInPlace,
	JumpStart,
	Fall,
	Landing
};

/**
 * Coarse, presentation-only movement direction used by the optional GASP-style
 * State Controller. It is deliberately derived only for Strafe locomotion;
 * Orient-to-Movement does not re-enter a Blend Stack transition because its
 * capsule already turns toward input.
 */
UENUM(BlueprintType)
enum class EProject_JStateControllerStrafeDirection : uint8
{
	Forward,
	Right,
	Backward,
	Left
};

/** GASP Stance equivalent. It is intentionally independent of combat/weapon stance. */
UENUM(BlueprintType)
enum class EProject_JStateControllerStance : uint8
{
	Stand,
	Crouch
};

/**
 * Logical state supplied to the optional GASP-style State Controller.
 * This is presentation state only: it never changes CharacterMovement or
 * the authoritative locomotion phase owned by the locomotion component.
 */
UENUM(BlueprintType)
enum class EProject_JStateControllerPresentationState : uint8
{
	Disabled,
	IdleLoop,
	TransitionToLocomotion,
	LocomotionLoop,
	TransitionToIdle,
	TransitionToInAir,
	InAirLoop
};
