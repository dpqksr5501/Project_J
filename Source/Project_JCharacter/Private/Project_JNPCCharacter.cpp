// Fill out your copyright notice in the Description page of Project Settings.

#include "Project_JNPCCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/SkeletalMeshComponent.h"

AProject_JNPCCharacter::AProject_JNPCCharacter()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	// In Stage 5, NPC characters must construct their own local GAS/Equipment components.
	AbilitySystemComponent = CreateDefaultSubobject<UProject_JAbilitySystemComponent>(TEXT("NPCAbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);

	AttributeSet = CreateDefaultSubobject<UProject_JAttributeSet>(TEXT("NPCAttributeSet"));
	EquipmentManager = CreateDefaultSubobject<UProject_JEquipmentManagerComponent>(TEXT("NPCEquipmentManager"));
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
