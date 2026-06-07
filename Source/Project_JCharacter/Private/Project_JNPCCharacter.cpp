// Fill out your copyright notice in the Description page of Project Settings.

#include "Project_JNPCCharacter.h"
#include "Project_JAbilitySystemComponent.h"

#include "Components/SkeletalMeshComponent.h"

AProject_JNPCCharacter::AProject_JNPCCharacter()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
	}
}

void AProject_JNPCCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (bApplyDefaultNPCOptimizationPolicy)
	{
		ApplyDefaultNPCOptimizationPolicy();
	}
}

void AProject_JNPCCharacter::ApplyDefaultNPCOptimizationPolicy()
{
	SetNetCullDistanceSquared(FMath::Square(NPCNetCullDistance));
	SetNetUpdateFrequency(NPCNetUpdateFrequency);
	SetMinNetUpdateFrequency(NPCMinNetUpdateFrequency);

	NearSignificanceTickInterval = NPCNearSignificanceTickInterval;
	MidSignificanceTickInterval = NPCMidSignificanceTickInterval;
	FarSignificanceTickInterval = NPCFarSignificanceTickInterval;
	HiddenSignificanceTickInterval = NPCHiddenSignificanceTickInterval;

	if (USkeletalMeshComponent* MeshComponent = GetMesh())
	{
		MeshComponent->bEnableUpdateRateOptimizations = true;
		MeshComponent->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	}
}
