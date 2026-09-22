// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/Project_JAnimNotifyState_TwoHandIK.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

void UProject_JAnimNotifyState_TwoHandIK::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	if (AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr)
	{
		if (UProject_JWeaponPresentationComponent* Presentation = Owner->FindComponentByClass<UProject_JWeaponPresentationComponent>())
		{
			Presentation->BeginTwoHandGrip(SecondaryIKAlpha, PrimaryIKAlpha, bOverridePrimaryIK);
		}
	}
}

void UProject_JAnimNotifyState_TwoHandIK::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	if (AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr)
	{
		if (UProject_JWeaponPresentationComponent* Presentation = Owner->FindComponentByClass<UProject_JWeaponPresentationComponent>())
		{
			Presentation->EndTwoHandGrip();
		}
	}
	Super::NotifyEnd(MeshComp, Animation, EventReference);
}
