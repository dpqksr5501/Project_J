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
 * GASP-style presentation movement direction used by the optional State
 * Controller. It is deliberately derived only for Strafe locomotion.
 *
 * F and B need one directional asset each. L/R additionally encode the foot
 * that should be forward, matching GASP's LL/LR/RL/RR convention. Orient to
 * Movement never uses these side sectors because its capsule turns toward
 * input before the locomotion animation is selected.
 */
UENUM(BlueprintType)
enum class EProject_JStateControllerStrafeDirection : uint8
{
	/**
	 * Legacy GASP-compatible values. Retained for serialized data compatibility,
	 * but hidden from new Project_J Chooser rows: one-shot foot is selected by the
	 * separate StateControllerOneShotFootForChooser column.
	 */
	Forward,
	Backward,
	LeftLeftFootForward UMETA(Hidden),
	LeftRightFootForward UMETA(Hidden),
	RightLeftFootForward UMETA(Hidden),
	RightRightFootForward UMETA(Hidden),

	/**
	 * Project_J combat assets are authored in eight movement sectors. These are
	 * deliberately independent from EProject_JStateControllerFoot: direction
	 * chooses the clip family, while Foot chooses a one-shot contact variant.
	 */
	ForwardLeft,
	Left,
	BackwardLeft,
	BackwardRight,
	Right,
	ForwardRight
};

/**
 * Static authored preference equivalent to GASP's Movement Direction Bias.
 * It only affects Strafe side sectors; it is neither a physical foot-contact
 * query nor an Orient-to-Movement selector.
 */
UENUM(BlueprintType)
enum class EProject_JStateControllerMovementDirectionBias : uint8
{
	LeftFootForward,
	RightFootForward
};

/**
 * Presentation-only authored-foot preference for State Controller one-shots.
 * It is deliberately not a physical contact/plant state; projects that later
 * expose gait-phase or foot-plant data may replace its selection policy.
 */
UENUM(BlueprintType)
enum class EProject_JStateControllerFoot : uint8
{
	Left,
	Right,
	None
};

/**
 * Why a direct one-shot foot was latched. This is diagnostic-only: Chooser
 * rows should continue to use EProject_JStateControllerFoot.
 */
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
	InAirLoop,
	TransitionToLand
};
