// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Project_JLocomotionAnimTypes.h"
#include "Project_JLocomotionAnimNotify.generated.h"

/**
 * Generic locomotion notify used by ABPs and animation modifiers.
 *
 * C++ owns the durable state and network events. This notify only marks exact
 * animation clip timing such as "jump start finished" or "landing finished".
 */
UCLASS(meta = (DisplayName = "Project J Locomotion Event"))
class PROJECT_JCHARACTER_API UProject_JLocomotionAnimNotify : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion")
	EProject_JLocomotionAnimEvent EventType = EProject_JLocomotionAnimEvent::GroundStartFinished;
};
