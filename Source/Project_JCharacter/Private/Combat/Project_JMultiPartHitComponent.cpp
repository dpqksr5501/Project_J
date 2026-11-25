#include "Combat/Project_JMultiPartHitComponent.h"
#include "Combat/Project_JCombatSpatialHistorySubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Project_JBaseCharacter.h"

UProject_JMultiPartHitComponent::UProject_JMultiPartHitComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.bAllowTickBatching = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	PrimaryComponentTick.TickInterval = 1.0f / 30.0f;
}

void UProject_JMultiPartHitComponent::ResetHistory()
{
	HistoryStart = 0;
	HistoryCount = 0;
}

void UProject_JMultiPartHitComponent::BeginPlay()
{
	Super::BeginPlay();
	StartHistoryCapture();
}

void UProject_JMultiPartHitComponent::OnRegister()
{
	Super::OnRegister();
	if (auto* Owner = Cast<AProject_JBaseCharacter>(GetOwner())) Owner->SyncIrisCombatPrioritizer();
	if (HasBegunPlay())
	{
		StartHistoryCapture();
	}
}

void UProject_JMultiPartHitComponent::OnUnregister()
{
	StopHistoryCapture();
	Super::OnUnregister();
	if (auto* Owner = Cast<AProject_JBaseCharacter>(GetOwner())) Owner->SyncIrisCombatPrioritizer();
}

void UProject_JMultiPartHitComponent::SetComponentTickEnabled(bool bEnabled)
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

void UProject_JMultiPartHitComponent::StartHistoryCapture()
{
	AActor* Owner = GetOwner();
	const bool bIsAuthority = Owner && Owner->HasAuthority();
	if (bIsAuthority)
	{
		ResolveOwnerMesh();
		if (UWorld* World = GetWorld())
		{
			if (auto* SpatialHistory = World->GetSubsystem<UProject_JCombatSpatialHistorySubsystem>())
			{
				bUsingBatchCapture = true;
				Super::SetComponentTickEnabled(false);
				SpatialHistory->RegisterMultiPart(this, Owner, PrimaryComponentTick.TickInterval);
				return;
			}
		}
	}
	Super::SetComponentTickEnabled(bIsAuthority && bCaptureRequested);
}

void UProject_JMultiPartHitComponent::StopHistoryCapture()
{
	if (bUsingBatchCapture)
	{
		bUsingBatchCapture = false;
		if (UWorld* World = GetWorld())
		{
			if (auto* SpatialHistory = World->GetSubsystem<UProject_JCombatSpatialHistorySubsystem>())
			{
				SpatialHistory->UnregisterMultiPart(this);
			}
		}
	}
	ResetHistory();
}

void UProject_JMultiPartHitComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopHistoryCapture();
	Super::EndPlay(EndPlayReason);
}

USkeletalMeshComponent* UProject_JMultiPartHitComponent::ResolveOwnerMesh() const
{
	if (USkeletalMeshComponent* Mesh = CachedMesh.Get())
	{
		return Mesh;
	}
	if (!bMeshLookupAttempted)
	{
		bMeshLookupAttempted = true;
		if (const AActor* Owner = GetOwner())
		{
			CachedMesh = Owner->FindComponentByClass<USkeletalMeshComponent>();
			return CachedMesh.Get();
		}
	}
	return nullptr;
}

const FProject_JBossPartSample& UProject_JMultiPartHitComponent::GetHistoryAt(int32 ChronologicalIndex) const
{
	return HistoryRing[(HistoryStart + ChronologicalIndex) % MaxHistoryFrames];
}

FVector UProject_JMultiPartHitComponent::ComputeLivePartCenter(const FProject_JBossHitPartSpec& Part, const USkeletalMeshComponent* Mesh) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return FVector::ZeroVector;
	}
	if (!Part.SocketOrBoneName.IsNone() && Mesh && Mesh->DoesSocketExist(Part.SocketOrBoneName))
	{
		return Mesh->GetSocketTransform(Part.SocketOrBoneName).TransformPosition(Part.LocalOffset);
	}
	return Owner->GetActorTransform().TransformPosition(Part.LocalOffset);
}

void UProject_JMultiPartHitComponent::RecordSample(double ServerTimeSeconds)
{
	const int32 PartCount = FMath::Min(HitParts.Num(), MaxParts);
	if (PartCount <= 0 || !FMath::IsFinite(ServerTimeSeconds))
	{
		return;
	}
	const USkeletalMeshComponent* Mesh = ResolveOwnerMesh();
	const int32 WriteIdx = (HistoryCount < MaxHistoryFrames)
		? ((HistoryStart + HistoryCount++) % MaxHistoryFrames)
		: (HistoryStart = (HistoryStart + 1) % MaxHistoryFrames, (HistoryStart + MaxHistoryFrames - 1) % MaxHistoryFrames);

	FProject_JBossPartSample& Sample = HistoryRing[WriteIdx];
	Sample.ServerTime = ServerTimeSeconds;
	Sample.WorldCenters.Reset();
	for (int32 i = 0; i < PartCount; ++i)
	{
		Sample.WorldCenters.Add(ComputeLivePartCenter(HitParts[i], Mesh));
	}
}

void UProject_JMultiPartHitComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if (bUsingBatchCapture) return;
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const AActor* Owner = GetOwner();
	const UWorld* World = GetWorld();
	if (!Owner || !Owner->HasAuthority() || !World || World->bIsTearingDown)
	{
		SetComponentTickEnabled(false);
		return;
	}
	RecordSample(World->GetTimeSeconds());
}

bool UProject_JMultiPartHitComponent::VerifyRewoundSegmentHit(
	double QueryTimeSeconds,
	const FVector& SegmentStart,
	const FVector& SegmentEnd,
	float AttackRadius,
	FName& OutPartName,
	float& OutDamageMultiplier,
	FVector& OutHitCenter) const
{
	const int32 PartCount = FMath::Min(HitParts.Num(), MaxParts);
	if (PartCount <= 0 || !FMath::IsFinite(QueryTimeSeconds) || !FMath::IsFinite(AttackRadius)
		|| SegmentStart.ContainsNaN() || SegmentEnd.ContainsNaN() || AttackRadius < 0.0f)
	{
		return false;
	}

	const USkeletalMeshComponent* Mesh = ResolveOwnerMesh();
	TArray<FVector, TInlineAllocator<MaxParts>> Centers;
	Centers.SetNum(PartCount);

	if (HistoryCount == 0)
	{
		for (int32 i = 0; i < PartCount; ++i)
		{
			Centers[i] = ComputeLivePartCenter(HitParts[i], Mesh);
		}
	}
	else if (HistoryCount == 1 || QueryTimeSeconds <= GetHistoryAt(0).ServerTime)
	{
		const FProject_JBossPartSample& First = GetHistoryAt(0);
		// Reject queries arbitrarily older than retained history rather than silently clamping.
		if (QueryTimeSeconds < First.ServerTime - 0.15)
		{
			return false;
		}
		for (int32 i = 0; i < PartCount; ++i)
		{
			Centers[i] = First.WorldCenters.IsValidIndex(i) ? First.WorldCenters[i] : ComputeLivePartCenter(HitParts[i], Mesh);
		}
	}
	else if (QueryTimeSeconds >= GetHistoryAt(HistoryCount - 1).ServerTime)
	{
		// Low-latency tail interpolation between newest recorded sample and live positions.
		const FProject_JBossPartSample& Newest = GetHistoryAt(HistoryCount - 1);
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : Newest.ServerTime;
		if (QueryTimeSeconds > Now + 0.10)
		{
			return false;
		}
		const double Span = Now - Newest.ServerTime;
		const double Alpha = (Span > UE_KINDA_SMALL_NUMBER) ? FMath::Clamp((QueryTimeSeconds - Newest.ServerTime) / Span, 0.0, 1.0) : 0.0;
		for (int32 i = 0; i < PartCount; ++i)
		{
			const FVector Live = ComputeLivePartCenter(HitParts[i], Mesh);
			const FVector Prev = Newest.WorldCenters.IsValidIndex(i) ? Newest.WorldCenters[i] : Live;
			Centers[i] = FMath::Lerp(Prev, Live, Alpha);
		}
	}
	else
	{
		// Binary search chronological ring buffer for first sample with ServerTime >= QueryTimeSeconds.
		int32 Lo = 1;
		int32 Hi = HistoryCount - 1;
		while (Lo < Hi)
		{
			const int32 Mid = Lo + ((Hi - Lo) >> 1);
			if (GetHistoryAt(Mid).ServerTime < QueryTimeSeconds)
			{
				Lo = Mid + 1;
			}
			else
			{
				Hi = Mid;
			}
		}
		const FProject_JBossPartSample& A = GetHistoryAt(Lo - 1);
		const FProject_JBossPartSample& B = GetHistoryAt(Lo);
		const double Span = FMath::Max(UE_KINDA_SMALL_NUMBER, B.ServerTime - A.ServerTime);
		const double Alpha = FMath::Clamp((QueryTimeSeconds - A.ServerTime) / Span, 0.0, 1.0);
		for (int32 i = 0; i < PartCount; ++i)
		{
			const FVector PA = A.WorldCenters.IsValidIndex(i) ? A.WorldCenters[i] : ComputeLivePartCenter(HitParts[i], Mesh);
			const FVector PB = B.WorldCenters.IsValidIndex(i) ? B.WorldCenters[i] : PA;
			Centers[i] = FMath::Lerp(PA, PB, Alpha);
		}
	}

	bool bHitAny = false;
	float BestMultiplier = -1.0f;
	for (int32 i = 0; i < PartCount; ++i)
	{
		const FVector Closest = FMath::ClosestPointOnSegment(Centers[i], SegmentStart, SegmentEnd);
		const double CombinedR = static_cast<double>(HitParts[i].SphereRadius) + static_cast<double>(AttackRadius);
		if (FVector::DistSquared(Closest, Centers[i]) <= FMath::Square(CombinedR))
		{
			if (!bHitAny || HitParts[i].DamageMultiplier > BestMultiplier)
			{
				bHitAny = true;
				BestMultiplier = HitParts[i].DamageMultiplier;
				OutPartName = HitParts[i].PartName;
				OutDamageMultiplier = HitParts[i].DamageMultiplier;
				OutHitCenter = Centers[i];
			}
		}
	}
	return bHitAny;
}
