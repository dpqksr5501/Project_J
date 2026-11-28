// Copyright Epic Games, Inc. All Rights Reserved.

#include "DefaultMovementSet/LayeredMoves/AnimRootMotionConsumerLayeredMove.h"
#include "DefaultMovementSet/LayeredMoves/AnimRootMotionLayeredMove.h"
#include "MoverComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "MoveLibrary/MovementUtils.h"
#include "MoverTypes.h"
#include "MoverLog.h"
#include "MotionWarpingComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimRootMotionConsumerLayeredMove)

UE_DEFINE_GAMEPLAY_TAG_COMMENT(Mover_AnimRootMotion_Montage_Consumer, "Mover.AnimRootMotion.Montage.Consumer", "Signifies an association with root motion that is consumed from the skeletal mesh playing a montage");



FLayeredMove_AnimRootMotionConsumer::FLayeredMove_AnimRootMotionConsumer()
{
	DurationMs = -1.f;	// infinite duration until the associated montage stops playing or this move is canceled
	MixMode = EMoveMixMode::OverrideAll;
}

bool FLayeredMove_AnimRootMotionConsumer::GenerateMove(const FMoverTickStartData& SimState, const FMoverTimeStep& TimeStep, const UMoverComponent* MoverComp, UMoverBlackboard* SimBlackboard, FProposedMove& OutProposedMove)
{
	const float DeltaSeconds = TimeStep.StepMs / 1000.f;

	bool bHasRootMotion = false;
	FTransform LocalRootMotion = FTransform::Identity;
	bool bHasValidWarpingContext = false;
	FMotionWarpingUpdateContext WarpingContext;

	if (!TimeStep.bIsResimulating)
	{
		// Clear resim cache
		bDidConsumeRootMotionForResim = false;
		ConsumedLocalRootMotionForResim = FTransform::Identity;
		WarpingContextForResim = FMotionWarpingUpdateContext();

		USkeletalMeshComponent* MeshComp = MoverComp->GetPrimaryVisualComponent<USkeletalMeshComponent>();
		if (!MeshComp)
		{
			DurationMs = 0.f;
			return false;
		}

		// Check if the montage is still playing
		bool bIsMontageStillPlaying = false;
		const UAnimInstance* MeshAnimInstance = MeshComp->GetAnimInstance();
		const FAnimMontageInstance* ActiveMontageInstance = nullptr;

		if (MeshAnimInstance && MontageState.Montage)
		{
			// Check main anim instance first
			ActiveMontageInstance = MeshAnimInstance->GetActiveInstanceForMontage(MontageState.Montage);
			if (ActiveMontageInstance && ActiveMontageInstance->IsPlaying())
			{
				bIsMontageStillPlaying = true;
			}
		}

		if (!bIsMontageStillPlaying)
		{
			// Montage ended - consume any leftover root motion so it doesn't leak
			MeshComp->ConsumeRootMotion();
			DurationMs = 0.f;
			return false;
		}

		// Consume root motion from the skeletal mesh
		const FRootMotionMovementParams ConsumedRootMotion = MeshComp->ConsumeRootMotion();

		if (ConsumedRootMotion.bHasRootMotion)
		{
			LocalRootMotion = ConsumedRootMotion.GetRootMotionTransform();
			LocalRootMotion.SetScale3D(FVector::OneVector);	// sanitize scaling
			bHasRootMotion = true;
		}

		// Build warping context from the active montage instance
		if (bHasRootMotion && ActiveMontageInstance)
		{
			WarpingContext.Animation = MontageState.Montage;
			WarpingContext.CurrentPosition = ActiveMontageInstance->GetPosition();
			WarpingContext.PreviousPosition = ActiveMontageInstance->GetPreviousPosition();
			WarpingContext.Weight = ActiveMontageInstance->GetWeight();
			WarpingContext.PlayRate = MontageState.Montage->RateScale * ActiveMontageInstance->GetPlayRate();
			WarpingContext.DeltaSeconds = DeltaSeconds;
			bHasValidWarpingContext = true;
		}

		// Update montage position tracking
		if (ActiveMontageInstance)
		{
			MontageState.CurrentPosition = ActiveMontageInstance->GetPosition();
		}

		// Cache for resim
		bDidConsumeRootMotionForResim = bHasRootMotion;
		ConsumedLocalRootMotionForResim = LocalRootMotion;
		if (bHasValidWarpingContext)
		{
			WarpingContextForResim = WarpingContext;
		}
	}
	else // resimulating
	{
		bHasRootMotion = bDidConsumeRootMotionForResim;
		LocalRootMotion = ConsumedLocalRootMotionForResim;
		bHasValidWarpingContext = WarpingContextForResim.Animation != nullptr;
		WarpingContext = WarpingContextForResim;
	}

	if (!bHasRootMotion)
	{
		return false;
	}

	const FMoverDefaultSyncState* SyncState = SimState.SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();

	if (SyncState == nullptr)
	{
		return false;
	}

	// Note that we're forcing the use of the sync state's actor transform data. This is necessary when the movement simulation
	// is running ahead of the actor's visual representation and may be rotated differently, such as in an async physics sim.
	const FTransform SimActorTransform = FTransform(SyncState->GetOrientation_WorldSpace().Quaternion(), SyncState->GetLocation_WorldSpace());
	const FTransform WorldSpaceRootMotion = MoverComp->ConvertLocalRootMotionToWorld(LocalRootMotion, DeltaSeconds, &SimActorTransform, bHasValidWarpingContext ? &WarpingContext : nullptr);

	OutProposedMove = FProposedMove();
	OutProposedMove.MixMode = MixMode;

	// Convert the transform into linear and angular velocities
	if (DeltaSeconds > UE_KINDA_SMALL_NUMBER)
	{
		OutProposedMove.LinearVelocity = WorldSpaceRootMotion.GetTranslation() / DeltaSeconds;
		const FQuat ShortestArcRootRotation = WorldSpaceRootMotion.GetRotation().GetShortestArcWith(FQuat::Identity);
		OutProposedMove.AngularVelocityDegrees = FMath::RadiansToDegrees(ShortestArcRootRotation.ToRotationVector() / DeltaSeconds);
	}

#if !UE_BUILD_SHIPPING
	UE_CLOGF(CVarLogAnimRootMotionSteps->GetBool(), LogMover, Log, "AnimRootMotionConsumer. SimF %i / SimT %.1f (Resim? %i) (dt %.3f) => LocalT: %ls (WST: %ls)  Vel: %.3f  MntPos: %.3f",
	        TimeStep.ServerFrame, TimeStep.BaseSimTimeMs, TimeStep.bIsResimulating, DeltaSeconds,
	        *LocalRootMotion.GetTranslation().ToString(), *WorldSpaceRootMotion.GetTranslation().ToString(), OutProposedMove.LinearVelocity.Length(), MontageState.CurrentPosition);
#endif // !UE_BUILD_SHIPPING

	return true;
}

bool FLayeredMove_AnimRootMotionConsumer::HasGameplayTag(FGameplayTag TagToFind, bool bExactMatch) const
{
	static const FGameplayTag MontageConsumerTag = Mover_AnimRootMotion_Montage_Consumer.GetTag();
	const bool bFoundMatch = bExactMatch ? MontageConsumerTag.MatchesTagExact(TagToFind) : MontageConsumerTag.MatchesTag(TagToFind);

	return bFoundMatch || Super::HasGameplayTag(TagToFind, bExactMatch);
}

FLayeredMoveBase* FLayeredMove_AnimRootMotionConsumer::Clone() const
{
	FLayeredMove_AnimRootMotionConsumer* CopyPtr = new FLayeredMove_AnimRootMotionConsumer(*this);
	return CopyPtr;
}

void FLayeredMove_AnimRootMotionConsumer::NetSerialize(FArchive& Ar)
{
	Super::NetSerialize(Ar);

	MontageState.NetSerialize(Ar);
}

UScriptStruct* FLayeredMove_AnimRootMotionConsumer::GetScriptStruct() const
{
	return FLayeredMove_AnimRootMotionConsumer::StaticStruct();
}

FString FLayeredMove_AnimRootMotionConsumer::ToSimpleString() const
{
	return FString::Printf(TEXT("AnimRootMotionConsumer"));
}

void FLayeredMove_AnimRootMotionConsumer::AddReferencedObjects(class FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
}

FMoverAnimMontageState FLayeredMove_AnimRootMotionConsumer::GetMontageState() const
{
	return MontageState;
}
