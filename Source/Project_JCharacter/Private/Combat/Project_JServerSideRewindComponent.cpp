#include "Combat/Project_JServerSideRewindComponent.h"
#include "GameFramework/Actor.h"

UProject_JServerSideRewindComponent::UProject_JServerSideRewindComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// By default, we only want this ticking on the server
	bTickInEditor = false;
}

void UProject_JServerSideRewindComponent::BeginPlay()
{
	Super::BeginPlay();
	
	// Pre-allocate buffer roughly based on 30Hz target tick rate for server
	PoseHistory.Reserve(30);
}

void UProject_JServerSideRewindComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || !GetWorld())
	{
		return;
	}

	// 1. Record current transform and time
	FProject_JPoseHistoryBuffer NewRecord;
	NewRecord.Timestamp = GetWorld()->GetTimeSeconds();
	NewRecord.Location = Owner->GetActorLocation();
	NewRecord.Rotation = Owner->GetActorQuat();

	PoseHistory.Add(NewRecord);

	// 2. Cull old records that exceed MaxRecordTime (removes oldest from the front)
	const float CurrentTime = NewRecord.Timestamp;
	int32 RemoveCount = 0;
	while (RemoveCount < PoseHistory.Num() && (CurrentTime - PoseHistory[RemoveCount].Timestamp) > MaxRecordTime)
	{
		RemoveCount++;
	}

	if (RemoveCount > 0)
	{
		PoseHistory.RemoveAt(0, RemoveCount, EAllowShrinking::No);
	}
}

bool UProject_JServerSideRewindComponent::ServerVerifyHit(float ClientTimestamp, const FVector& TraceStart, const FVector& TraceEnd)
{
	FProject_JPoseHistoryBuffer Pose1, Pose2;
	float Alpha = 0.0f;

	if (!GetPosesForTime(ClientTimestamp, Pose1, Pose2, Alpha))
	{
		return false;
	}

	AActor* Owner = GetOwner();
	if (!Owner || !GetWorld())
	{
		return false;
	}

	// Interpolate location and rotation
	FVector InterpolatedLocation = FMath::Lerp(Pose1.Location, Pose2.Location, Alpha);
	FQuat InterpolatedRotation = FQuat::Slerp(Pose1.Rotation, Pose2.Rotation, Alpha);

	// Cache current transform
	FVector OriginalLocation = Owner->GetActorLocation();
	FQuat OriginalRotation = Owner->GetActorQuat();

	// Temporarily rollback collision collider of target
	Owner->SetActorLocationAndRotation(InterpolatedLocation, InterpolatedRotation, false, nullptr, ETeleportType::TeleportPhysics);

	// Perform server trace (LineTrace against ECC_Pawn). Do not ignore Owner here:
	// this component is attached to the target being verified.
	FHitResult HitResult;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SSRVerify), true);
	bool bHit = GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_Pawn, QueryParams);

	// Restore original transform immediately
	Owner->SetActorLocationAndRotation(OriginalLocation, OriginalRotation, false, nullptr, ETeleportType::TeleportPhysics);

	// Confirm if we hit this target at that rolled-back position
	return bHit && HitResult.GetActor() == Owner;
}

bool UProject_JServerSideRewindComponent::GetPosesForTime(float Time, FProject_JPoseHistoryBuffer& OutPose1, FProject_JPoseHistoryBuffer& OutPose2, float& OutAlpha) const
{
	if (PoseHistory.Num() < 2)
	{
		return false;
	}

	// Because PoseHistory is sorted oldest to newest:
	// PoseHistory[0] is oldest (smallest timestamp), PoseHistory.Last() is newest (largest timestamp).
	if (Time < PoseHistory[0].Timestamp || Time > PoseHistory.Last().Timestamp)
	{
		return false;
	}

	// Binary Search to find the two bounding frames
	int32 Low = 0;
	int32 High = PoseHistory.Num() - 1;
	int32 FoundIndex = -1;

	while (Low <= High)
	{
		int32 Mid = Low + (High - Low) / 2;
		if (PoseHistory[Mid].Timestamp >= Time)
		{
			FoundIndex = Mid;
			High = Mid - 1;
		}
		else
		{
			Low = Mid + 1;
		}
	}

	if (FoundIndex <= 0)
	{
		return false;
	}

	OutPose2 = PoseHistory[FoundIndex];
	OutPose1 = PoseHistory[FoundIndex - 1];

	const float TimeDiff = OutPose2.Timestamp - OutPose1.Timestamp;
	if (FMath::IsNearlyZero(TimeDiff))
	{
		OutAlpha = 0.0f;
	}
	else
	{
		OutAlpha = (Time - OutPose1.Timestamp) / TimeDiff;
	}

	return true;
}
