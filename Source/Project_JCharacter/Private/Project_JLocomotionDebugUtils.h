#pragma once

#include "CoreMinimal.h"
#include "Animation/Project_JWeaponAnimProfile.h"
#include "Project_JLocomotionAnimTypes.h"

namespace Project_J::LocomotionDebug
{
inline const TCHAR* ToDebugString(EProject_JWeaponAnimStance WeaponStance)
{
	switch (WeaponStance)
	{
	case EProject_JWeaponAnimStance::None:
		return TEXT("None");
	case EProject_JWeaponAnimStance::OneHanded:
		return TEXT("OneHanded");
	case EProject_JWeaponAnimStance::TwoHanded:
		return TEXT("TwoHanded");
	case EProject_JWeaponAnimStance::DualWield:
		return TEXT("DualWield");
	case EProject_JWeaponAnimStance::Staff:
		return TEXT("Staff");
	case EProject_JWeaponAnimStance::Bow:
		return TEXT("Bow");
	case EProject_JWeaponAnimStance::Unarmed:
		return TEXT("Unarmed");
	default:
		return TEXT("Unknown");
	}
}

inline const TCHAR* ToDebugString(EProject_JGroundMotionMode MotionMode)
{
	switch (MotionMode)
	{
	case EProject_JGroundMotionMode::Idle:
		return TEXT("Idle");
	case EProject_JGroundMotionMode::Start:
		return TEXT("Start");
	case EProject_JGroundMotionMode::Locomotion:
		return TEXT("Locomotion");
	case EProject_JGroundMotionMode::Stop:
		return TEXT("Stop");
	default:
		return TEXT("Unknown");
	}
}

inline const TCHAR* ToDebugString(EProject_JLocomotionGaitIntent GaitIntent)
{
	switch (GaitIntent)
	{
	case EProject_JLocomotionGaitIntent::Walk:
		return TEXT("Walk");
	case EProject_JLocomotionGaitIntent::Run:
		return TEXT("Run");
	case EProject_JLocomotionGaitIntent::Sprint:
		return TEXT("Sprint");
	default:
		return TEXT("Unknown");
	}
}

inline const TCHAR* ToDebugString(EProject_JLocomotionRotationMode RotationMode)
{
	switch (RotationMode)
	{
	case EProject_JLocomotionRotationMode::OrientToMovement:
		return TEXT("Orient");
	case EProject_JLocomotionRotationMode::Strafe:
		return TEXT("Strafe");
	default:
		return TEXT("Unknown");
	}
}

inline const TCHAR* ToDebugString(EProject_JLocomotionPhaseFamily PhaseFamily)
{
	switch (PhaseFamily)
	{
	case EProject_JLocomotionPhaseFamily::Idle:
		return TEXT("Idle");
	case EProject_JLocomotionPhaseFamily::Start:
		return TEXT("Start");
	case EProject_JLocomotionPhaseFamily::Cycle:
		return TEXT("Cycle");
	case EProject_JLocomotionPhaseFamily::Stop:
		return TEXT("Stop");
	case EProject_JLocomotionPhaseFamily::Pivot:
		return TEXT("Pivot");
	case EProject_JLocomotionPhaseFamily::Turn:
		return TEXT("Turn");
	case EProject_JLocomotionPhaseFamily::TurnInPlace:
		return TEXT("TurnInPlace");
	case EProject_JLocomotionPhaseFamily::JumpStart:
		return TEXT("JumpStart");
	case EProject_JLocomotionPhaseFamily::Fall:
		return TEXT("Fall");
	case EProject_JLocomotionPhaseFamily::Landing:
		return TEXT("Landing");
	default:
		return TEXT("Unknown");
	}
}

inline const TCHAR* ToDebugString(ENetMode NetMode)
{
	switch (NetMode)
	{
	case NM_Standalone:
		return TEXT("Standalone");
	case NM_DedicatedServer:
		return TEXT("DedicatedServer");
	case NM_ListenServer:
		return TEXT("ListenServer");
	case NM_Client:
		return TEXT("Client");
	default:
		return TEXT("Unknown");
	}
}

inline const TCHAR* ToDebugString(ENetRole NetRole)
{
	switch (NetRole)
	{
	case ROLE_None:
		return TEXT("None");
	case ROLE_SimulatedProxy:
		return TEXT("SimProxy");
	case ROLE_AutonomousProxy:
		return TEXT("AutoProxy");
	case ROLE_Authority:
		return TEXT("Authority");
	default:
		return TEXT("Unknown");
	}
}
}
