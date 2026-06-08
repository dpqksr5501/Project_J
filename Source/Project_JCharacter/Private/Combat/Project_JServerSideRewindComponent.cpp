#include "Combat/Project_JServerSideRewindComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"

namespace
{
bool DoesTraceIntersectCapsule(const FVector& TraceStart, const FVector& TraceEnd, const FVector& CapsuleCenter, const FQuat& CapsuleRotation, float CapsuleRadius, float CapsuleHalfHeight)
{
	const float CapsuleSegmentHalfHeight = FMath::Max(0.0f, CapsuleHalfHeight - CapsuleRadius);
	const FVector CapsuleAxis = CapsuleRotation.GetUpVector();
	const FVector CapsuleSegmentStart = CapsuleCenter + CapsuleAxis * CapsuleSegmentHalfHeight;
	const FVector CapsuleSegmentEnd = CapsuleCenter - CapsuleAxis * CapsuleSegmentHalfHeight;

	FVector ClosestTracePoint;
	FVector ClosestCapsulePoint;
	FMath::SegmentDistToSegmentSafe(TraceStart, TraceEnd, CapsuleSegmentStart, CapsuleSegmentEnd, ClosestTracePoint, ClosestCapsulePoint);

	return FVector::DistSquared(ClosestTracePoint, ClosestCapsulePoint) <= FMath::Square(CapsuleRadius);
}
}

UProject_JServerSideRewindComponent::UProject_JServerSideRewindComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// By default, we only want this ticking on the server
	bTickInEditor = false;
}

void UProject_JServerSideRewindComponent::BeginPlay()
{
	Super::BeginPlay();

	const AActor* Owner = GetOwner();
	SetComponentTickEnabled(Owner && Owner->HasAuthority());
	
	const int32 ExpectedRecordCount = FMath::CeilToInt(FMath::Max(1.0f, MaxRecordTime) * FMath::Max(1.0f, RecordRateHz)) + 2;
	PoseHistory.Reserve(ExpectedRecordCount);
}

void UProject_JServerSideRewindComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || !GetWorld())
	{
		return;
	}

	TimeSinceLastRecord += DeltaTime;
	const float RecordInterval = 1.0f / FMath::Max(1.0f, RecordRateHz);
	if (PoseHistory.Num() > 0 && TimeSinceLastRecord < RecordInterval)
	{
		return;
	}
	TimeSinceLastRecord = FMath::Fmod(TimeSinceLastRecord, RecordInterval);

	FProject_JPoseHistoryBuffer NewRecord;
	NewRecord.Timestamp = GetWorld()->GetTimeSeconds();
	NewRecord.Location = Owner->GetActorLocation();
	NewRecord.Rotation = Owner->GetActorQuat();

	PoseHistory.Add(NewRecord);

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

	const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!OwnerCharacter)
	{
		return false;
	}

	const UCapsuleComponent* CapsuleComponent = OwnerCharacter->GetCapsuleComponent();
	if (!CapsuleComponent)
	{
		return false;
	}

	const FVector InterpolatedLocation = FMath::Lerp(Pose1.Location, Pose2.Location, Alpha);
	const FQuat InterpolatedRotation = FQuat::Slerp(Pose1.Rotation, Pose2.Rotation, Alpha);
	const FTransform HistoricalActorTransform(InterpolatedRotation, InterpolatedLocation);
	const FTransform HistoricalCapsuleTransform = CapsuleComponent->GetRelativeTransform() * HistoricalActorTransform;

	return DoesTraceIntersectCapsule(
		TraceStart,
		TraceEnd,
		HistoricalCapsuleTransform.GetLocation(),
		HistoricalCapsuleTransform.GetRotation(),
		CapsuleComponent->GetScaledCapsuleRadius(),
		CapsuleComponent->GetScaledCapsuleHalfHeight());
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
