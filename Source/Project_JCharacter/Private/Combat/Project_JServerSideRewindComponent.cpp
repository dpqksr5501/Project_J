#include "Combat/Project_JServerSideRewindComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
bool DoesTraceIntersectCapsule(const FVector& TraceStart, const FVector& TraceEnd, const FVector& CapsuleCenter, const FQuat& CapsuleRotation, float CapsuleRadius, float CapsuleHalfHeight, float TraceRadius)
{
	const float CapsuleSegmentHalfHeight = FMath::Max(0.0f, CapsuleHalfHeight - CapsuleRadius);
	const FVector CapsuleAxis = CapsuleRotation.GetUpVector();
	const FVector CapsuleSegmentStart = CapsuleCenter + CapsuleAxis * CapsuleSegmentHalfHeight;
	const FVector CapsuleSegmentEnd = CapsuleCenter - CapsuleAxis * CapsuleSegmentHalfHeight;

	FVector ClosestTracePoint;
	FVector ClosestCapsulePoint;
	FMath::SegmentDistToSegmentSafe(TraceStart, TraceEnd, CapsuleSegmentStart, CapsuleSegmentEnd, ClosestTracePoint, ClosestCapsulePoint);

	return FVector::DistSquared(ClosestTracePoint, ClosestCapsulePoint) <= FMath::Square(CapsuleRadius + FMath::Max(0.0f, TraceRadius));
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

	const ACharacter* Owner = Cast<ACharacter>(GetOwner());
	const bool bRecord = Owner && Owner->HasAuthority();
	SetComponentTickEnabled(bRecord);
	MaxRecordTime = FMath::IsFinite(MaxRecordTime) ? FMath::Clamp(MaxRecordTime, 0.05f, 10.0f) : 1.0f;
	RecordRateHz = FMath::IsFinite(RecordRateHz) ? FMath::Clamp(RecordRateHz, 1.0f, 120.0f) : 30.0f;
	PoseHistory.SetNum(bRecord ? FMath::CeilToInt(MaxRecordTime * RecordRateHz) + 2 : 0);
	ResetHistory();
}

void UProject_JServerSideRewindComponent::ResetHistory()
{
	check(IsInGameThread());
	PoseHistoryStartIndex = PoseHistoryCount = 0;
	TimeSinceLastRecord = 0.0f;
}

void UProject_JServerSideRewindComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Project_J_ServerSideRewindTick);
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || !GetWorld() || PoseHistory.IsEmpty() ||
		!FMath::IsFinite(DeltaTime) || DeltaTime <= 0.0f)
	{
		return;
	}

	TimeSinceLastRecord += DeltaTime;
	const float RecordInterval = 1.0f / FMath::Max(1.0f, RecordRateHz);
	if (PoseHistoryCount > 0 && TimeSinceLastRecord < RecordInterval)
	{
		return;
	}
	TimeSinceLastRecord = FMath::Fmod(TimeSinceLastRecord, RecordInterval);

	FProject_JPoseHistoryBuffer NewRecord;
	NewRecord.Timestamp = GetWorld()->GetTimeSeconds();
	NewRecord.Location = Owner->GetActorLocation();
	NewRecord.Rotation = Owner->GetActorQuat();
	const ACharacter* Character = Cast<ACharacter>(Owner);
	const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	if (!Capsule) return;
	NewRecord.CapsuleLocation = Capsule->GetComponentLocation();
	NewRecord.CapsuleRotation = Capsule->GetComponentQuat();
	NewRecord.CapsuleRadius = Capsule->GetScaledCapsuleRadius();
	NewRecord.CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();

	AppendPoseHistoryRecord(NewRecord);
	DiscardExpiredPoseHistoryRecords(NewRecord.Timestamp);
}

bool UProject_JServerSideRewindComponent::ServerVerifyHit(float ClientTimestamp, const FVector& TraceStart, const FVector& TraceEnd, float TraceRadius)
{
	check(IsInGameThread());
	if (!GetOwner() || !GetOwner()->HasAuthority() || !GetWorld() ||
		!FMath::IsFinite(ClientTimestamp) || !FMath::IsFinite(TraceRadius) || TraceRadius < 0.0f ||
		TraceStart.ContainsNaN() || TraceEnd.ContainsNaN() ||
		GetWorld()->GetTimeSeconds() - ClientTimestamp > MaxRecordTime) return false;

	FProject_JPoseHistoryBuffer Pose1, Pose2;
	float Alpha = 0.0f;
	if (!GetPosesForTime(ClientTimestamp, Pose1, Pose2, Alpha)) return false;
	return DoesTraceIntersectCapsule(TraceStart, TraceEnd,
		FMath::Lerp(Pose1.CapsuleLocation, Pose2.CapsuleLocation, Alpha),
		FQuat::Slerp(Pose1.CapsuleRotation, Pose2.CapsuleRotation, Alpha),
		FMath::Lerp(Pose1.CapsuleRadius, Pose2.CapsuleRadius, Alpha),
		FMath::Lerp(Pose1.CapsuleHalfHeight, Pose2.CapsuleHalfHeight, Alpha), TraceRadius);
}

bool UProject_JServerSideRewindComponent::GetPosesForTime(float Time, FProject_JPoseHistoryBuffer& OutPose1, FProject_JPoseHistoryBuffer& OutPose2, float& OutAlpha) const
{
	OutPose1 = OutPose2 = FProject_JPoseHistoryBuffer();
	OutAlpha = 0.0f;
	if (PoseHistoryCount == 0 || !FMath::IsFinite(Time))
	{
		return false;
	}

	// Logical indices are sorted oldest to newest even though the physical array wraps.
	if (Time < GetPoseHistoryRecord(0).Timestamp || Time > GetPoseHistoryRecord(PoseHistoryCount - 1).Timestamp)
	{
		return false;
	}

	// Binary Search to find the two bounding frames
	int32 Low = 0;
	int32 High = PoseHistoryCount - 1;
	int32 FoundIndex = -1;

	while (Low <= High)
	{
		int32 Mid = Low + (High - Low) / 2;
		if (GetPoseHistoryRecord(Mid).Timestamp >= Time)
		{
			FoundIndex = Mid;
			High = Mid - 1;
		}
		else
		{
			Low = Mid + 1;
		}
	}

	if (FoundIndex >= 0 && GetPoseHistoryRecord(FoundIndex).Timestamp == Time)
	{
		OutPose1 = OutPose2 = GetPoseHistoryRecord(FoundIndex);
		return true;
	}
	if (FoundIndex <= 0)
	{
		return false;
	}

	OutPose2 = GetPoseHistoryRecord(FoundIndex);
	OutPose1 = GetPoseHistoryRecord(FoundIndex - 1);

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

const FProject_JPoseHistoryBuffer& UProject_JServerSideRewindComponent::GetPoseHistoryRecord(int32 LogicalIndex) const
{
	check(PoseHistoryCount > 0);
	check(LogicalIndex >= 0 && LogicalIndex < PoseHistoryCount);
	return PoseHistory[(PoseHistoryStartIndex + LogicalIndex) % PoseHistory.Num()];
}

void UProject_JServerSideRewindComponent::AppendPoseHistoryRecord(const FProject_JPoseHistoryBuffer& Record)
{
	check(PoseHistory.Num() > 0);
	if (!FMath::IsFinite(Record.Timestamp) || Record.Location.ContainsNaN() || Record.Rotation.ContainsNaN() ||
		Record.CapsuleLocation.ContainsNaN() || Record.CapsuleRotation.ContainsNaN() ||
		!FMath::IsFinite(Record.CapsuleRadius) || !FMath::IsFinite(Record.CapsuleHalfHeight)) return;
	if (PoseHistoryCount > 0)
	{
		const float LastTime = GetPoseHistoryRecord(PoseHistoryCount - 1).Timestamp;
		if (Record.Timestamp < LastTime) ResetHistory();
		else if (Record.Timestamp == LastTime)
		{
			PoseHistory[(PoseHistoryStartIndex + PoseHistoryCount - 1) % PoseHistory.Num()] = Record;
			return;
		}
	}

	if (PoseHistoryCount == PoseHistory.Num())
	{
		PoseHistory[PoseHistoryStartIndex] = Record;
		PoseHistoryStartIndex = (PoseHistoryStartIndex + 1) % PoseHistory.Num();
		return;
	}

	const int32 WriteIndex = (PoseHistoryStartIndex + PoseHistoryCount) % PoseHistory.Num();
	PoseHistory[WriteIndex] = Record;
	++PoseHistoryCount;
}

void UProject_JServerSideRewindComponent::DiscardExpiredPoseHistoryRecords(float CurrentTime)
{
	while (PoseHistoryCount > 0 && (CurrentTime - GetPoseHistoryRecord(0).Timestamp) > MaxRecordTime)
	{
		PoseHistoryStartIndex = (PoseHistoryStartIndex + 1) % PoseHistory.Num();
		--PoseHistoryCount;
	}
}
