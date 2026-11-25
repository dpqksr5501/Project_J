#include "Combat/Project_JServerSideRewindComponent.h"
#include "Combat/Project_JCombatSpatialHistorySubsystem.h"
#include "GameFramework/Pawn.h"
#include "Movement/Project_JMovementQueries.h"
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
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.bAllowTickBatching = true;
	// Capture the final world capsule after movement/physics, not an arbitrary
	// pre-movement pose carrying this frame's timestamp.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	// By default, we only want this ticking on the server
	bTickInEditor = false;
}

void UProject_JServerSideRewindComponent::BeginPlay()
{
	Super::BeginPlay();
	StartHistoryCapture();
}

void UProject_JServerSideRewindComponent::OnRegister()
{
	Super::OnRegister();
	if (HasBegunPlay())
	{
		StartHistoryCapture();
	}
}

void UProject_JServerSideRewindComponent::OnUnregister()
{
	StopHistoryCapture();
	Super::OnUnregister();
}

void UProject_JServerSideRewindComponent::SetComponentTickEnabled(bool bEnabled)
{
	if (!bUsingBatchCapture)
	{
		Super::SetComponentTickEnabled(bEnabled);
		return;
	}
	if (bCaptureRequested != bEnabled)
	{
		bCaptureRequested = bEnabled;
		ResetHistory();
	}
}

void UProject_JServerSideRewindComponent::StartHistoryCapture()
{
	APawn* Owner = Cast<APawn>(GetOwner());
	const bool bRecord = Owner && Owner->HasAuthority();
	MaxRecordTime = FMath::IsFinite(MaxRecordTime) ? FMath::Clamp(MaxRecordTime, 0.05f, 10.0f) : 1.0f;
	RecordRateHz = FMath::IsFinite(RecordRateHz) ? FMath::Clamp(RecordRateHz, 1.0f, 120.0f) : 30.0f;
	const float Interval = 1.0f / RecordRateHz;
	SetComponentTickInterval(Interval);
	PoseHistory.SetNum(bRecord ? FMath::CeilToInt(MaxRecordTime * RecordRateHz) + 2 : 0);
	ResetHistory();

	if (bRecord)
	{
		if (UWorld* World = GetWorld())
		{
			if (auto* SpatialHistory = World->GetSubsystem<UProject_JCombatSpatialHistorySubsystem>())
			{
				// Tick aggregation: one centralized TG_PostPhysics pass drives all authority SSR components.
				bUsingBatchCapture = true;
				Super::SetComponentTickEnabled(false);
				SpatialHistory->RegisterSSR(this, Owner, Project_J::Movement::FindCapsule(Owner), Interval);
				return;
			}
		}
	}
	Super::SetComponentTickEnabled(bRecord && bCaptureRequested);
}

void UProject_JServerSideRewindComponent::StopHistoryCapture()
{
	if (bUsingBatchCapture)
	{
		bUsingBatchCapture = false;
		if (UWorld* World = GetWorld())
		{
			if (auto* SpatialHistory = World->GetSubsystem<UProject_JCombatSpatialHistorySubsystem>())
			{
				SpatialHistory->UnregisterSSR(this);
			}
		}
	}
	ResetHistory();
}

void UProject_JServerSideRewindComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopHistoryCapture();
	Super::EndPlay(EndPlayReason);
}

void UProject_JServerSideRewindComponent::ResetHistory()
{
	check(IsInGameThread());
	PoseHistoryStartIndex = PoseHistoryCount = 0;
}

bool UProject_JServerSideRewindComponent::TryGetHistoryTimeBounds(double& OutOldest, double& OutNewest) const
{
	check(IsInGameThread());
	if (PoseHistoryCount <= 0) return false;
	const double Oldest = GetPoseHistoryRecord(0).Timestamp;
	const double Newest = GetPoseHistoryRecord(PoseHistoryCount - 1).Timestamp;
	if (!FMath::IsFinite(Oldest) || !FMath::IsFinite(Newest) || Newest < Oldest) return false;
	OutOldest = Oldest;
	OutNewest = Newest;
	return true;
}

void UProject_JServerSideRewindComponent::RecordBatchedSample(double Timestamp, const AActor* Owner, const UCapsuleComponent* Capsule)
{
	if (!Owner || !Owner->HasAuthority() || PoseHistory.IsEmpty() || !FMath::IsFinite(Timestamp))
	{
		return;
	}
	if (!Capsule)
	{
		const APawn* Character = Cast<APawn>(Owner);
		Capsule = Character ? Project_J::Movement::FindCapsule(Character) : nullptr;
		if (!Capsule) return;
	}

	FProject_JPoseHistoryBuffer NewRecord;
	NewRecord.Timestamp = Timestamp;
	NewRecord.Location = Owner->GetActorLocation();
	NewRecord.Rotation = Owner->GetActorQuat();
	NewRecord.CapsuleLocation = Capsule->GetComponentLocation();
	NewRecord.CapsuleRotation = Capsule->GetComponentQuat();
	NewRecord.CapsuleRadius = Capsule->GetScaledCapsuleRadius();
	NewRecord.CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();

	AppendPoseHistoryRecord(NewRecord);
	DiscardExpiredPoseHistoryRecords(NewRecord.Timestamp);
}

void UProject_JServerSideRewindComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Project_J_ServerSideRewindTick);
	if (bUsingBatchCapture) return;
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || !GetWorld() || PoseHistory.IsEmpty() ||
		!FMath::IsFinite(DeltaTime) || DeltaTime <= 0.0f)
	{
		return;
	}

	// Fallback path when invoked directly outside the centralized spatial history subsystem.
	const APawn* Character = Cast<APawn>(Owner);
	RecordBatchedSample(GetWorld()->GetTimeSeconds(), Owner, Character ? Project_J::Movement::FindCapsule(Character) : nullptr);
}

bool UProject_JServerSideRewindComponent::ServerVerifyHit(double ClientTimestamp, const FVector& TraceStart, const FVector& TraceEnd, float TraceRadius)
{
	FVector UnusedLocation;
	return ServerVerifyHitWithRewoundLocation(ClientTimestamp, TraceStart, TraceEnd, TraceRadius, UnusedLocation);
}

bool UProject_JServerSideRewindComponent::ServerVerifyHitWithRewoundLocation(double ClientTimestamp, const FVector& TraceStart, const FVector& TraceEnd, float TraceRadius, FVector& OutRewoundLocation)
{
	check(IsInGameThread());
	OutRewoundLocation = FVector::ZeroVector;
	if (!GetOwner() || !GetOwner()->HasAuthority() || !GetWorld() ||
		!FMath::IsFinite(ClientTimestamp) || !FMath::IsFinite(TraceRadius) || TraceRadius < 0.0f ||
		TraceStart.ContainsNaN() || TraceEnd.ContainsNaN() ||
		GetWorld()->GetTimeSeconds() - ClientTimestamp > MaxRecordTime) return false;

	FProject_JPoseHistoryBuffer Pose1, Pose2;
	float Alpha = 0.0f;
	if (!GetPosesForTime(ClientTimestamp, Pose1, Pose2, Alpha)) return false;
	const FVector RewoundCapsuleCenter = FMath::Lerp(Pose1.CapsuleLocation, Pose2.CapsuleLocation, Alpha);
	const bool bHit = DoesTraceIntersectCapsule(TraceStart, TraceEnd,
		RewoundCapsuleCenter,
		FQuat::Slerp(Pose1.CapsuleRotation, Pose2.CapsuleRotation, Alpha),
		FMath::Lerp(Pose1.CapsuleRadius, Pose2.CapsuleRadius, Alpha),
		FMath::Lerp(Pose1.CapsuleHalfHeight, Pose2.CapsuleHalfHeight, Alpha), TraceRadius);
	if (bHit)
	{
		OutRewoundLocation = RewoundCapsuleCenter;
	}
	return bHit;
}

bool UProject_JServerSideRewindComponent::GetPosesForTime(double Time, FProject_JPoseHistoryBuffer& OutPose1, FProject_JPoseHistoryBuffer& OutPose2, float& OutAlpha) const
{
	OutPose1 = OutPose2 = FProject_JPoseHistoryBuffer();
	OutAlpha = 0.0f;
	if (PoseHistoryCount == 0 || !FMath::IsFinite(Time))
	{
		return false;
	}

	// Logical indices are sorted oldest to newest even though the physical array wraps.
	const FProject_JPoseHistoryBuffer& OldestRecord = GetPoseHistoryRecord(0);
	const FProject_JPoseHistoryBuffer& NewestRecord = GetPoseHistoryRecord(PoseHistoryCount - 1);
	if (Time < OldestRecord.Timestamp)
	{
		return false;
	}
	if (Time > NewestRecord.Timestamp)
	{
		// Bridge the intra-interval tail gap between the newest 30Hz sample and the current live capsule
		// so low-latency hits (0..33ms) are not falsely rejected between SSR ticks.
		const UWorld* World = GetWorld();
		const AActor* Owner = GetOwner();
		const APawn* Character = Cast<APawn>(Owner);
		const UCapsuleComponent* Capsule = Character ? Project_J::Movement::FindCapsule(Character) : nullptr;
		if (!World || !Capsule)
		{
			return false;
		}
		const double Now = World->GetTimeSeconds();
		const double MaxTailGap = (1.0 / FMath::Max(RecordRateHz, 1.0f)) + 0.02;
		if (!FMath::IsFinite(Now) || Time > Now + 0.01 || (Time - NewestRecord.Timestamp) > MaxTailGap || Now < NewestRecord.Timestamp)
		{
			return false;
		}
		OutPose1 = NewestRecord;
		OutPose2.Timestamp = Now;
		OutPose2.Location = Owner->GetActorLocation();
		OutPose2.Rotation = Owner->GetActorQuat();
		OutPose2.CapsuleLocation = Capsule->GetComponentLocation();
		OutPose2.CapsuleRotation = Capsule->GetComponentQuat();
		OutPose2.CapsuleRadius = Capsule->GetScaledCapsuleRadius();
		OutPose2.CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		const double Span = Now - NewestRecord.Timestamp;
		OutAlpha = Span > UE_SMALL_NUMBER ? static_cast<float>(FMath::Clamp((Time - NewestRecord.Timestamp) / Span, 0.0, 1.0)) : 1.0f;
		return true;
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

	const double TimeDiff = OutPose2.Timestamp - OutPose1.Timestamp;
	if (FMath::IsNearlyZero(TimeDiff))
	{
		OutAlpha = 0.0f;
	}
	else
	{
		OutAlpha = static_cast<float>((Time - OutPose1.Timestamp) / TimeDiff);
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
		const double LastTime = GetPoseHistoryRecord(PoseHistoryCount - 1).Timestamp;
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

void UProject_JServerSideRewindComponent::DiscardExpiredPoseHistoryRecords(double CurrentTime)
{
	while (PoseHistoryCount > 0 && (CurrentTime - GetPoseHistoryRecord(0).Timestamp) > MaxRecordTime)
	{
		PoseHistoryStartIndex = (PoseHistoryStartIndex + 1) % PoseHistory.Num();
		--PoseHistoryCount;
	}
}
