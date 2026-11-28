// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LayeredMove.h"
#include "DefaultMovementSet/LayeredMoves/MontageStateProvider.h"
#include "NativeGameplayTags.h"
#include "RootMotionModifier.h"
#include "AnimRootMotionConsumerLayeredMove.generated.h"

#define UE_API MOVER_API

class UAnimMontage;

UE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Mover_AnimRootMotion_Montage_Consumer);

/** Anim Root Motion Consumer Move: handles root motion by consuming extracted root motion
 * from the skeletal mesh component via ConsumeRootMotion().
 *
 * Unlike FLayeredMove_AnimRootMotion which independently samples root motion from the montage
 * animation data, this variant consumes the root motion that the animation system has already
 * computed during its normal tick.
 *
 * Important: When queueing this move, callers should NOT call PushDisableRootMotion() on the
 * montage instance, since this move relies on the AnimInstance continuing to extract root motion
 * normally so there is something to consume.
 *
 * The move will end itself if the montage is no longer playing on the mesh.  It should only be used
 * in cases where animation & movement tick at the same rate.
 */
USTRUCT(BlueprintType)
struct FLayeredMove_AnimRootMotionConsumer : public FLayeredMove_MontageStateProvider
{
	GENERATED_BODY()

	UE_API FLayeredMove_AnimRootMotionConsumer();
	virtual ~FLayeredMove_AnimRootMotionConsumer() = default;

	UPROPERTY(BlueprintReadWrite, Category = Mover)
	FMoverAnimMontageState MontageState;

	// Generate a movement
	UE_API virtual bool GenerateMove(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, const UMoverComponent* MoverComp, UMoverBlackboard* SimBlackboard, FProposedMove& OutProposedMove) override;

	UE_API virtual bool HasGameplayTag(FGameplayTag TagToFind, bool bExactMatch) const override;
	UE_API virtual FLayeredMoveBase* Clone() const override;
	UE_API virtual void NetSerialize(FArchive& Ar) override;
	UE_API virtual UScriptStruct* GetScriptStruct() const override;
	UE_API virtual FString ToSimpleString() const override;
	UE_API virtual void AddReferencedObjects(class FReferenceCollector& Collector) override;

	// FLayeredMove_MontageStateProvider
	UE_API virtual FMoverAnimMontageState GetMontageState() const override;

protected:
	// Cached root motion for resimulation support. These are NOT replicated.
	bool bDidConsumeRootMotionForResim = false;
	FTransform ConsumedLocalRootMotionForResim = FTransform::Identity;
	FMotionWarpingUpdateContext WarpingContextForResim;
};

template<>
struct TStructOpsTypeTraits<FLayeredMove_AnimRootMotionConsumer> : public TStructOpsTypeTraitsBase2<FLayeredMove_AnimRootMotionConsumer>
{
	enum
	{
		WithCopy = true
	};
};

#undef UE_API
