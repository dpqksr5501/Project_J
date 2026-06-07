// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

class AProject_JPlayerCharacter;
class UProject_JCombatAnimProfile;
class UProject_JLocomotionAnimStateComponent;
class UProject_JLocomotionProfile;
class UProject_JMotionMatchingAssetSet;
class UProject_JWeaponAnimProfile;

namespace Project_J::AnimationProfileValidation
{
PROJECT_JCHARACTER_API void ValidatePlayerAnimationConfiguration(
	const AProject_JPlayerCharacter& PlayerCharacter,
	const UProject_JLocomotionProfile* LocomotionProfile,
	const UProject_JMotionMatchingAssetSet* MotionMatchingAssetSet,
	const UProject_JWeaponAnimProfile* WeaponAnimProfile,
	const UProject_JCombatAnimProfile* CombatAnimProfile,
	const UProject_JLocomotionAnimStateComponent* LocomotionAnimStateComponent,
	TArray<FString>& OutWarnings);
}
