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
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	// 1. Record current transform and time
	FProject_JPoseHistoryBuffer NewRecord;
	NewRecord.Timestamp = GetWorld()->GetTimeSeconds();
	NewRecord.Location = Owner->GetActorLocation();
	NewRecord.Rotation = Owner->GetActorQuat();

	PoseHistory.Insert(NewRecord, 0);

	// 2. Cull old records that exceed MaxRecordTime
	while (PoseHistory.Num() > 0 && (PoseHistory[0].Timestamp - PoseHistory.Last().Timestamp) > MaxRecordTime)
	{
		PoseHistory.Pop();
	}
}

bool UProject_JServerSideRewindComponent::ServerVerifyHit(float ClientTimestamp, const FVector& TraceStart, const FVector& TraceEnd)
{
	FProject_JPoseHistoryBuffer Pose1, Pose2;
	float Alpha = 0.0f;

	// In a real implementation, you would:
	// 1. Find the poses at ClientTimestamp
	// 2. Move the Actor's collision shape to the interpolated position
	// 3. Perform the Sweep or LineTrace
	// 4. Move the Actor's collision back to the actual current position
	// 5. Return true if hit, false otherwise.

	return false;
}

bool UProject_JServerSideRewindComponent::GetPosesForTime(float Time, FProject_JPoseHistoryBuffer& OutPose1, FProject_JPoseHistoryBuffer& OutPose2, float& OutAlpha) const
{
	// Implementation for searching the PoseHistory array and calculating Alpha for interpolation
	return false;
}
