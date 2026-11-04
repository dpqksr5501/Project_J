#pragma once
#include "CoreMinimal.h"
#include "Game/Project_JPlayerState.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
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
	FProject_JEquipmentOperationResult EquipmentResult;
	int32 Replies = 0, UseReplies = 0, BagReplies = 0, EquipmentReplies = 0;
	UFUNCTION() void Used(FGuid RequestId, EProject_JItemUseResult Result)
	{
		UseResult = Result;
		++UseReplies; ++Replies;
	}
	UFUNCTION() void Bagged(FGuid RequestId, EProject_JBagResult Result)
	{
		BagResult = Result;
		++BagReplies; ++Replies;
	}
	UFUNCTION() void Equipped(FGuid RequestId, const FProject_JEquipmentOperationResult& Result)
	{
		EquipmentResult = Result;
		++EquipmentReplies; ++Replies;
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

/** Observer of the existing encounter resolution signal; owns no rewards or gameplay state. */
UCLASS()
class UProject_JEncounterResolutionReceiver : public UObject
{
    GENERATED_BODY()
public:
    int32 Calls = 0, LastRevision = 0;
    FName LastId;
    TFunction<void()> OnResolution;
    UFUNCTION() void Resolved(FName Id, int32 Revision)
    {
        ++Calls; LastId = Id; LastRevision = Revision;
        auto Callback = MoveTemp(OnResolution);
        if (Callback) Callback();
    }
};

/** Test-only real virtual getter boundary. No production hook or native phase flags are replaced. */
UCLASS(NotBlueprintable, Transient, NotPlaceable)
class AProject_JQuestRewardQueryFixture : public AProject_JPlayerState
{
    GENERATED_BODY()
public:
    mutable TFunction<void()> OnASCQuery;
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override
    {
        auto Callback = MoveTemp(OnASCQuery);
        if (Callback) Callback();
        return Super::GetAbilitySystemComponent();
    }
};
