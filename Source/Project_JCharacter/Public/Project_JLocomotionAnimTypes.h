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
