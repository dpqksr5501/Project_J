#pragma once
#include "CoreMinimal.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Pawn.h"
#include "Project_JMMOArchitectureReceiver.generated.h"

UCLASS()
class UProject_JMMOArchitectureReceiver : public UObject
{
	GENERATED_BODY()
  public:
	EProject_JItemUseResult UseResult = EProject_JItemUseResult::Unavailable;
	EProject_JBagResult BagResult = EProject_JBagResult::Unavailable;
	int32 Replies = 0;
	UFUNCTION() void Used(FGuid RequestId, EProject_JItemUseResult Result)
	{
		UseResult = Result;
		++Replies;
	}
	UFUNCTION() void Bagged(FGuid RequestId, EProject_JBagResult Result)
	{
		BagResult = Result;
		++Replies;
	}
};

/** Controlled movement fixture; the real quest timer and profile writer are used. */
UCLASS()
class AProject_JProfileTravelTestPawn : public APawn
{
	GENERATED_BODY()
  public:
	AProject_JProfileTravelTestPawn()
	{
		SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	}
	bool Moving = false;
	virtual FVector GetVelocity() const override
	{
		return Moving ? FVector(600, 0, 0) : FVector::ZeroVector;
	}
};
