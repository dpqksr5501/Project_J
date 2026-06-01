// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Project_JLocomotionAnimTypes.generated.h"

UENUM(BlueprintType)
enum class EProject_JLocomotionAnimEvent : uint8
{
	GroundStartFinished,
	StopFinished,
	JumpStartFinished,
	FallOffStartFinished,
	LandingFinished,
	HitReactFinished,
	AttackFinished
};

UENUM(BlueprintType)
enum class EProject_JGroundMotionMode : uint8
{
	Idle,
	Start,
	Locomotion,
	Stop
};
