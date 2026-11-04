#include "Components/Project_JCombatHitValidationComponent.h"
#include "Animation/Project_JAnimNotifyState_MeleeHit.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifyQueue.h"
#include "Animation/ActiveMontageInstanceScope.h"
#include "GameFramework/Pawn.h"
#include "Movement/Project_JMovementQueries.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "MoverDataModelTypes.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/Project_JCommandBudgetComponent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Combat/Project_JServerSideRewindComponent.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Combat/Project_JMeleeSweep.h"
#include "GameplayEffect.h"
#include "Engine/World.h"
#include "Engine/NetConnection.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/SkeletalMesh.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "GameFramework/GameStateBase.h"
#include "Project_JPlayerCharacter.h"
#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJCombatHitValidation, Log, All);

UProject_JCombatHitValidationComponent::UProject_JCombatHitValidationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UProject_JCombatHitValidationComponent::BeginAttackNode(const FGameplayTag AttackNodeTag, UProject_JAttackDefinition* AttackDefinition, const int32 PredictionKey)
{
	// Retain only the last key/ordinal through End; repeated visits to the same
	// node or a same-key restart must not accept a previous visit's clock.
	if (PredictionKey != LastNodePredictionKey)
	{
		LastNodePredictionKey = PredictionKey;
		LastNodeVisitOrdinal = 1;
	}
	else if (LastNodeVisitOrdinal < MAX_int32)
	{
		++LastNodeVisitOrdinal;
	}
	// Saturation permanently closes clock admission for this key, never wraps.
	// Authority/legacy work does not consume this ordinal.
	ActiveNodeVisitOrdinal = LastNodeVisitOrdinal == MAX_int32 ? 0 : LastNodeVisitOrdinal;
	bClientNodeClockBound = false;
	BoundClientNodeStartTimestamp = 0.0;
	bNodeClockRetired = ActiveNodeVisitOrdinal == 0;
	EndMeleePoseCapture();
	// Damage context and duplicate suppression belong to this execution only.
	// A repeated node tag/key cannot reuse a previous visit's canonical sweep.
	ResetSweepHistory();
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	ActiveAttackStartTimestamp = GameState ? GameState->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.0f);
	ProtectAttackPose();
	bRequiresWeapon = Cast<AProject_JPlayerCharacter>(GetOwner()) != nullptr;
	AttackEquipment = GetOwner() ? GetOwner()->FindComponentByClass<UProject_JEquipmentRuntimeComponent>() : nullptr;
	AttackWeaponRevision = AttackEquipment.IsValid() ? AttackEquipment->GetWeaponRevision() : 0;
	ActivePredictionKey = PredictionKey;
	ActiveAttackNodeTag = AttackNodeTag;
	ActiveAttackDefinition = AttackDefinition;
	HitWindowTokens.Reset();
	bManualHitWindowOpen = false;
	bHitWindowOpen = false;
	bHasAuthoritativeTrace = false;
	ServerHitActors.Reset();
	BeginMeleePoseCapture();
}

void UProject_JCombatHitValidationComponent::EndAttack()
{
	EndMeleePoseCapture();
	HitWindowTokens.Reset();
	SetHitWindowOpen(false);
	// Closing a live window may append a final state; retire it afterwards.
	ResetSweepHistory();
	AttackEquipment.Reset();
	AttackWeaponRevision = 0;
	ActivePredictionKey = 0;
	ActiveNodeVisitOrdinal = 0;
	bClientNodeClockBound = false;
	BoundClientNodeStartTimestamp = 0.0;
	bNodeClockRetired = false;
	ActiveAttackNodeTag = FGameplayTag();
	ActiveAttackDefinition = nullptr;
	bHasAuthoritativeTrace = false;
	ServerHitActors.Reset();
	RestoreAttackPose();
}

void UProject_JCombatHitValidationComponent::ResetSpatialHistory()
{
	check(IsInGameThread());
	++SpatialRevision;
	// A discontinuity cannot keep the old source/target clock binding. Only a
	// fresh BeginAttackNode opens a new visit; recording new geometry is not enough.
	bClientNodeClockBound = false;
	BoundClientNodeStartTimestamp = 0.0;
	bNodeClockRetired = true;
	MeleeRootAnchor = FMeleeRootAnchor();
	for (auto& Sample : MeleePoseSamples) Sample.bValid = false;
	ResetSweepHistory();
	bHasAuthoritativeTrace = false;
	LastAuthoritativeTraceStart = LastAuthoritativeTraceEnd = FVector::ZeroVector;
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	// In particular, do not let the history lookup's endpoint tolerance admit a
	// timestamp from before this discontinuity once the first new sample arrives.
	ActiveAttackStartTimestamp = GameState ? GameState->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.0f);
}

void UProject_JCombatHitValidationComponent::ProtectAttackPose()
{
	check(IsInGameThread());
	const auto* Character = Cast<APawn>(GetOwner());
	auto* Mesh = Character ? Project_J::Movement::FindPoseSource(Character) : nullptr;
	if (!Mesh) { return; }
	if (ProtectedMesh.Get() != Mesh)
	{
		RestoreAttackPose();
		ProtectedMesh = Mesh;
		SavedVisibility = uint8(Mesh->VisibilityBasedAnimTickOption);
		bSavedURO = Mesh->bEnableUpdateRateOptimizations;
		bSavedSuppressNotifies = Mesh->bSuppressNotifyEventDispatch;
	}
	if (auto* Budgeted = Cast<UProject_JBudgetedSkeletalMeshComponent>(Mesh))
	{
		Budgeted->RequestAnimationUpdate(this, EProject_JAnimationUpdateRequirement::GameplayPose);
		return;
	}
	// A hit window is authoritative gameplay. Hidden meshes/DS must still refresh
	// sockets and dispatch notifies. Do not change root-motion mode or force GT evaluation.
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Mesh->bEnableUpdateRateOptimizations = false;
	Mesh->bSuppressNotifyEventDispatch = false;
}

void UProject_JCombatHitValidationComponent::RestoreAttackPose()
{
	EndMeleePoseCapture();
	if (auto* Mesh = ProtectedMesh.Get())
	{
		if (auto* Budgeted = Cast<UProject_JBudgetedSkeletalMeshComponent>(Mesh))
		{
			Budgeted->ReleaseAnimationUpdate(this);
			ProtectedMesh.Reset();
			return;
		}
		// Preserve a newer explicit policy written by another system.
		if (Mesh->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones)
		{ Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption(SavedVisibility); }
		if (!Mesh->bEnableUpdateRateOptimizations) { Mesh->bEnableUpdateRateOptimizations = bSavedURO; }
		if (!Mesh->bSuppressNotifyEventDispatch) { Mesh->bSuppressNotifyEventDispatch = bSavedSuppressNotifies; }
	}
	ProtectedMesh.Reset();
}

void UProject_JCombatHitValidationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	ResetSweepHistory();
	AuthoritativeSweepHistory.Reset();
	EndAttack();
	Super::EndPlay(Reason);
}

void UProject_JCombatHitValidationComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	ResetSweepHistory();
	AuthoritativeSweepHistory.Reset();
	EndAttack();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

bool UProject_JCombatHitValidationComponent::HasValidAttackWeapon() const
{
	return !bRequiresWeapon || (AttackWeaponRevision != 0 && AttackEquipment.IsValid()
		&& AttackEquipment->GetWeaponRevision() == AttackWeaponRevision);
}

const FProject_JAuthoritativeSweepRecord& UProject_JCombatHitValidationComponent::GetSweepHistoryRecord(int32 LogicalIndex) const
{
	check(SweepHistoryCount > 0);
	check(LogicalIndex >= 0 && LogicalIndex < SweepHistoryCount);
	return AuthoritativeSweepHistory[(SweepHistoryStartIndex + LogicalIndex) % AuthoritativeSweepHistory.Num()];
}

void UProject_JCombatHitValidationComponent::RetireOldestSweepRecord()
{
	if (SweepHistoryCount == 0) return;
	auto& Oldest = AuthoritativeSweepHistory[SweepHistoryStartIndex];
	SweepHistorySegmentCount -= Oldest.bAtomicPath ? Oldest.Segments.Num() : 1;
	// Release owning storage, including capacity; inactive slots must not retain63-piece allocations.
	Oldest = FProject_JAuthoritativeSweepRecord();
	SweepHistoryStartIndex = (SweepHistoryStartIndex + 1) % AuthoritativeSweepHistory.Num();
	--SweepHistoryCount;
}

void UProject_JCombatHitValidationComponent::ResetSweepHistory()
{
	while (SweepHistoryCount > 0) RetireOldestSweepRecord();
	SweepHistoryStartIndex = 0;
	SweepHistorySegmentCount = 0;
}

void UProject_JCombatHitValidationComponent::AppendSweepHistoryRecord(const FProject_JAuthoritativeSweepRecord& Record)
{
	if (NextTracePacketSerial == MAX_uint64 || (Record.bAtomicPath && Record.Segments.Num() > MaxTracePacketSegments)) return;
	FProject_JAuthoritativeSweepRecord Pending = Record; // Own before any whole-slot retirement.
	Pending.PacketSerial = ++NextTracePacketSerial;
	Pending.Segments.Shrink();
	const int32 Cost = Pending.bAtomicPath ? Pending.Segments.Num() : 1;
	if (AuthoritativeSweepHistory.Num() < MaxSweepHistoryCapacity)
	{
		ResetSweepHistory();
		AuthoritativeSweepHistory.SetNum(MaxSweepHistoryCapacity);
	}
	while (SweepHistoryCount > 0 && (SweepHistoryCount >= MaxSweepHistoryCapacity || SweepHistorySegmentCount + Cost > MaxRetainedTraceSegments))
		RetireOldestSweepRecord();
	const int32 Index = (SweepHistoryStartIndex + SweepHistoryCount) % AuthoritativeSweepHistory.Num();
	AuthoritativeSweepHistory[Index] = MoveTemp(Pending);
	++SweepHistoryCount;
	SweepHistorySegmentCount += Cost;
}

void UProject_JCombatHitValidationComponent::DiscardExpiredSweepRecords(double CurrentTimestamp)
{
	const float EffectiveMaxAge = FMath::Max(MaxSweepHistorySeconds, HitValidationPolicy.MaxRequestAge + 0.25f);
	while (SweepHistoryCount > 0 && CurrentTimestamp - GetSweepHistoryRecord(0).ServerTimestamp > EffectiveMaxAge)
		RetireOldestSweepRecord();
}

void UProject_JCombatHitValidationComponent::RecordAuthoritativeTrace(const FVector& TraceStart, const FVector& TraceEnd)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !ActiveAttackNodeTag.IsValid())
	{
		return;
	}

	LastAuthoritativeTraceStart = TraceStart;
	LastAuthoritativeTraceEnd = TraceEnd;
	bHasAuthoritativeTrace = true;

	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	const double CurrentTime = GameState ? GameState->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.0f);

	DiscardExpiredSweepRecords(CurrentTime);

	FProject_JAuthoritativeSweepRecord Record;
	Record.ServerTimestamp = CurrentTime;
	Record.TraceStart = TraceStart;
	Record.TraceEnd = TraceEnd;
	Record.AttackNodeTag = ActiveAttackNodeTag;
	Record.PredictionKey = ActivePredictionKey;
	Record.bHitWindowOpen = bHitWindowOpen;

	AppendSweepHistoryRecord(Record);
}

void UProject_JCombatHitValidationComponent::AppendCurrentAuthoritativeSweepState()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !ActiveAttackNodeTag.IsValid())
	{
		return;
	}

	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	const double CurrentTime = GameState ? GameState->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.0f);

	DiscardExpiredSweepRecords(CurrentTime);

	FProject_JAuthoritativeSweepRecord Record;
	Record.ServerTimestamp = CurrentTime;
	Record.TraceStart = LastAuthoritativeTraceStart;
	Record.TraceEnd = LastAuthoritativeTraceEnd;
	Record.AttackNodeTag = ActiveAttackNodeTag;
	Record.PredictionKey = ActivePredictionKey;
	Record.bHitWindowOpen = bHitWindowOpen;
	Record.bAtomicPath = SweepHistoryCount > 0 && GetSweepHistoryRecord(SweepHistoryCount - 1).bAtomicPath;
	Record.SpatialRevision = SpatialRevision;
	AppendSweepHistoryRecord(Record);
}

uint64 UProject_JCombatHitValidationComponent::RecordAuthoritativeTracePacket(
	TConstArrayView<FProject_JAuthoritativeTraceSegment> Segments, uint64 WindowToken, uint64 ExpectedSpatialRevision)
{
	check(IsInGameThread());
	if (!GetOwner() || !GetOwner()->HasAuthority() || !GetWorld() || GetWorld()->bIsTearingDown ||
		!ActiveAttackNodeTag.IsValid() || !IsHitWindowCurrent(WindowToken) || !bHitWindowOpen || !HasValidAttackWeapon() ||
		ExpectedSpatialRevision != SpatialRevision || Segments.IsEmpty() || Segments.Num() > MaxTracePacketSegments || NextTracePacketSerial == MAX_uint64) return 0;
	for (const auto& Segment : Segments)
		if (Segment.TraceStart.ContainsNaN() || Segment.TraceEnd.ContainsNaN()) return 0;
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	const double Now = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	if (!FMath::IsFinite(Now)) return 0;
	FProject_JAuthoritativeSweepRecord Record;
	Record.ServerTimestamp = Now; Record.AttackNodeTag = ActiveAttackNodeTag; Record.PredictionKey = ActivePredictionKey;
	Record.bHitWindowOpen = true; Record.bAtomicPath = true; Record.WindowToken = WindowToken; Record.SpatialRevision = SpatialRevision;
	Record.Segments.Append(Segments.GetData(), Segments.Num());
	DiscardExpiredSweepRecords(Now);
	AppendSweepHistoryRecord(Record);
	LastAuthoritativeTraceStart = Segments[0].TraceStart; LastAuthoritativeTraceEnd = Segments[0].TraceEnd;
	bHasAuthoritativeTrace = true;
	return GetSweepHistoryRecord(SweepHistoryCount - 1).PacketSerial;
}

bool UProject_JCombatHitValidationComponent::SelectAuthoritativeTraceSegment(uint64 PacketSerial, uint64 WindowToken,
	uint64 ExpectedSpatialRevision, int32 SegmentIndex)
{
	check(IsInGameThread());
	if (!PacketSerial || !GetOwner() || !GetOwner()->HasAuthority() || SweepHistoryCount == 0 ||
		!IsHitWindowCurrent(WindowToken) || !bHitWindowOpen || ExpectedSpatialRevision != SpatialRevision || !HasValidAttackWeapon()) return false;
	const auto& Packet = GetSweepHistoryRecord(SweepHistoryCount - 1);
	if (!Packet.bAtomicPath || Packet.PacketSerial != PacketSerial || Packet.WindowToken != WindowToken ||
		Packet.SpatialRevision != SpatialRevision || Packet.AttackNodeTag != ActiveAttackNodeTag || Packet.PredictionKey != ActivePredictionKey ||
		!Packet.Segments.IsValidIndex(SegmentIndex)) return false;
	LastAuthoritativeTraceStart = Packet.Segments[SegmentIndex].TraceStart;
	LastAuthoritativeTraceEnd = Packet.Segments[SegmentIndex].TraceEnd;
	return true;
}

int32 UProject_JCombatHitValidationComponent::FindExactAtomicPublication(double TargetTimestamp) const
{
	int32 Low = 0, High = SweepHistoryCount - 1, First = INDEX_NONE;
	while (Low <= High)
	{
		const int32 Mid = Low + (High - Low) / 2;
		if (GetSweepHistoryRecord(Mid).ServerTimestamp >= TargetTimestamp) { First = Mid; High = Mid - 1; }
		else Low = Mid + 1;
	}
	if (First == INDEX_NONE || GetSweepHistoryRecord(First).ServerTimestamp != TargetTimestamp) return INDEX_NONE;
	bool Atomic = false; int32 Last = First;
	for (; Last < SweepHistoryCount && GetSweepHistoryRecord(Last).ServerTimestamp == TargetTimestamp; ++Last)
		Atomic |= GetSweepHistoryRecord(Last).bAtomicPath;
	return Atomic ? Last - 1 : INDEX_NONE;
}

bool UProject_JCombatHitValidationComponent::FindAuthoritativeTraceAtTime(
	double TargetTimestamp,
	int32 ExpectedPredictionKey,
	const FGameplayTag& ExpectedAttackNodeTag,
	FVector& OutStart,
	FVector& OutEnd,
	bool& OutHitWindowOpen) const
{
	OutStart = FVector::ZeroVector;
	OutEnd = FVector::ZeroVector;
	OutHitWindowOpen = false;

	if (SweepHistoryCount == 0 || !FMath::IsFinite(TargetTimestamp) || TargetTimestamp < ActiveAttackStartTimestamp)
	{
		return false;
	}

	if (FindExactAtomicPublication(TargetTimestamp) != INDEX_NONE) return false;
	const double OldestTime = GetSweepHistoryRecord(0).ServerTimestamp;
	const double NewestTime = GetSweepHistoryRecord(SweepHistoryCount - 1).ServerTimestamp;
	const float FutureTolerance = FMath::Max(0.05f, HitValidationPolicy.MaxFutureTolerance);
	const float PastTolerance = 0.05f;

	// Reject requests outside recorded history bounds
	if (TargetTimestamp < OldestTime - PastTolerance || TargetTimestamp > NewestTime + FutureTolerance)
	{
		return false;
	}

	// Binary search to find bounding frames in chronologically ordered ring buffer
	int32 Low = 0;
	int32 High = SweepHistoryCount - 1;
	int32 FoundIndex = -1;

	while (Low <= High)
	{
		const int32 Mid = Low + (High - Low) / 2;
		if (GetSweepHistoryRecord(Mid).ServerTimestamp >= TargetTimestamp)
		{
			FoundIndex = Mid;
			High = Mid - 1;
		}
		else
		{
			Low = Mid + 1;
		}
	}

	if (FoundIndex == 0)
	{
		const FProject_JAuthoritativeSweepRecord& SingleRecord = GetSweepHistoryRecord(0);
		if (SingleRecord.bAtomicPath) return false;
		if ((ExpectedPredictionKey == 0 || SingleRecord.PredictionKey == ExpectedPredictionKey) &&
			(!ExpectedAttackNodeTag.IsValid() || SingleRecord.AttackNodeTag.MatchesTagExact(ExpectedAttackNodeTag)))
		{
			OutStart = SingleRecord.TraceStart;
			OutEnd = SingleRecord.TraceEnd;
			OutHitWindowOpen = SingleRecord.bHitWindowOpen;
			return true;
		}
		return false;
	}

	if (FoundIndex == -1)
	{
		const FProject_JAuthoritativeSweepRecord& SingleRecord = GetSweepHistoryRecord(SweepHistoryCount - 1);
		if (SingleRecord.bAtomicPath) return false;
		if ((ExpectedPredictionKey == 0 || SingleRecord.PredictionKey == ExpectedPredictionKey) &&
			(!ExpectedAttackNodeTag.IsValid() || SingleRecord.AttackNodeTag.MatchesTagExact(ExpectedAttackNodeTag)))
		{
			OutStart = SingleRecord.TraceStart;
			OutEnd = SingleRecord.TraceEnd;
			OutHitWindowOpen = SingleRecord.bHitWindowOpen;
			return true;
		}
		return false;
	}

	const FProject_JAuthoritativeSweepRecord& RecordA = GetSweepHistoryRecord(FoundIndex - 1);
	const FProject_JAuthoritativeSweepRecord& RecordB = GetSweepHistoryRecord(FoundIndex);

	const bool bMatchA = (ExpectedPredictionKey == 0 || RecordA.PredictionKey == ExpectedPredictionKey) &&
		(!ExpectedAttackNodeTag.IsValid() || RecordA.AttackNodeTag.MatchesTagExact(ExpectedAttackNodeTag));
	const bool bMatchB = (ExpectedPredictionKey == 0 || RecordB.PredictionKey == ExpectedPredictionKey) &&
		(!ExpectedAttackNodeTag.IsValid() || RecordB.AttackNodeTag.MatchesTagExact(ExpectedAttackNodeTag));

	if (RecordB.ServerTimestamp == TargetTimestamp && bMatchB && !RecordB.bAtomicPath)
	{
		OutStart = RecordB.TraceStart; OutEnd = RecordB.TraceEnd; OutHitWindowOpen = RecordB.bHitWindowOpen;
		return true;
	}
	if (RecordA.bAtomicPath || RecordB.bAtomicPath) return false;
	if (bMatchA && bMatchB)
	{
		const double TimeDelta = RecordB.ServerTimestamp - RecordA.ServerTimestamp;
		const float Alpha = FMath::IsNearlyZero(TimeDelta) ? 0.0f : static_cast<float>(FMath::Clamp((TargetTimestamp - RecordA.ServerTimestamp) / TimeDelta, 0.0, 1.0));
		OutStart = FMath::Lerp(RecordA.TraceStart, RecordB.TraceStart, Alpha);
		OutEnd = FMath::Lerp(RecordA.TraceEnd, RecordB.TraceEnd, Alpha);
		// Precise historical hit window interpolation based on transition boundary timestamps
		if (RecordA.bHitWindowOpen && RecordB.bHitWindowOpen)
		{
			OutHitWindowOpen = true;
		}
		else if (RecordA.bHitWindowOpen && !RecordB.bHitWindowOpen)
		{
			// Window closed at RecordB; valid strictly prior to RecordB timestamp
			OutHitWindowOpen = (TargetTimestamp < RecordB.ServerTimestamp);
		}
		else if (!RecordA.bHitWindowOpen && RecordB.bHitWindowOpen)
		{
			// Window opened at RecordB; valid starting from RecordB timestamp
			OutHitWindowOpen = (TargetTimestamp >= RecordB.ServerTimestamp);
		}
		else
		{
			OutHitWindowOpen = false;
		}
		return true;
	}
	else if (bMatchA && FMath::Abs(TargetTimestamp - RecordA.ServerTimestamp) <= 0.05f)
	{
		OutStart = RecordA.TraceStart;
		OutEnd = RecordA.TraceEnd;
		OutHitWindowOpen = RecordA.bHitWindowOpen;
		return true;
	}
	else if (bMatchB && FMath::Abs(TargetTimestamp - RecordB.ServerTimestamp) <= 0.05f)
	{
		OutStart = RecordB.TraceStart;
		OutEnd = RecordB.TraceEnd;
		OutHitWindowOpen = RecordB.bHitWindowOpen;
		return true;
	}

	return false;
}

bool UProject_JCombatHitValidationComponent::FindAuthoritativeTracePacketAtTime(double TargetTimestamp, int32 ExpectedPredictionKey,
	const FGameplayTag& ExpectedAttackNodeTag, FProject_JAuthoritativeSweepRecord& OutPacket) const
{
	OutPacket = FProject_JAuthoritativeSweepRecord();
	if (SweepHistoryCount > 0 && FMath::IsFinite(TargetTimestamp) && TargetTimestamp >= ActiveAttackStartTimestamp)
	{
		const int32 Exact = FindExactAtomicPublication(TargetTimestamp);
		if (Exact != INDEX_NONE)
		{
			const auto& Final = GetSweepHistoryRecord(Exact);
			if ((ExpectedPredictionKey && Final.PredictionKey != ExpectedPredictionKey) ||
				(ExpectedAttackNodeTag.IsValid() && !Final.AttackNodeTag.MatchesTagExact(ExpectedAttackNodeTag))) return false;
			OutPacket = Final;
			if (!Final.bAtomicPath)
			{
				FProject_JAuthoritativeTraceSegment Piece; Piece.TraceStart = Final.TraceStart; Piece.TraceEnd = Final.TraceEnd;
				OutPacket.Segments.Add(Piece);
			}
			return true;
		}
	}
	FVector Start, End; bool Open = false;
	if (FindAuthoritativeTraceAtTime(TargetTimestamp, ExpectedPredictionKey, ExpectedAttackNodeTag, Start, End, Open))
	{
		OutPacket.ServerTimestamp = TargetTimestamp; OutPacket.TraceStart = Start; OutPacket.TraceEnd = End; OutPacket.bHitWindowOpen = Open;
		OutPacket.AttackNodeTag = ExpectedAttackNodeTag.IsValid() ? ExpectedAttackNodeTag : ActiveAttackNodeTag;
		OutPacket.PredictionKey = ExpectedPredictionKey ? ExpectedPredictionKey : ActivePredictionKey;
		FProject_JAuthoritativeTraceSegment Segment; Segment.TraceStart = Start; Segment.TraceEnd = End; OutPacket.Segments.Add(Segment);
		return true;
	}
	if (SweepHistoryCount == 0 || !FMath::IsFinite(TargetTimestamp) || TargetTimestamp < ActiveAttackStartTimestamp ||
		TargetTimestamp < GetSweepHistoryRecord(0).ServerTimestamp - 0.05 ||
		TargetTimestamp > GetSweepHistoryRecord(SweepHistoryCount - 1).ServerTimestamp + FMath::Max(0.05f, HitValidationPolicy.MaxFutureTolerance)) return false;
	int32 Low = 0, High = SweepHistoryCount - 1, Selected = High;
	while (Low <= High)
	{
		const int32 Mid = Low + (High - Low) / 2;
		if (GetSweepHistoryRecord(Mid).ServerTimestamp >= TargetTimestamp) { Selected = Mid; High = Mid - 1; }
		else Low = Mid + 1;
	}
	// Equal-time window transitions are ordered native publications; the last one owns that instant.
	while (Selected + 1 < SweepHistoryCount && GetSweepHistoryRecord(Selected).ServerTimestamp == TargetTimestamp &&
		GetSweepHistoryRecord(Selected + 1).ServerTimestamp == TargetTimestamp) ++Selected;
	const auto Matches = [&](const FProject_JAuthoritativeSweepRecord& Record)
	{
		return (!ExpectedPredictionKey || Record.PredictionKey == ExpectedPredictionKey) &&
			(!ExpectedAttackNodeTag.IsValid() || Record.AttackNodeTag.MatchesTagExact(ExpectedAttackNodeTag));
	};
	const auto* Packet = &GetSweepHistoryRecord(Selected);
	// A close marker has no copied geometry. Strictly before it, retain the preceding whole dispatch packet.
	if (Packet->bAtomicPath && !Packet->bHitWindowOpen && Packet->Segments.IsEmpty() && TargetTimestamp < Packet->ServerTimestamp && Selected > 0)
	{
		const auto& Previous = GetSweepHistoryRecord(Selected - 1);
		if (Previous.bAtomicPath && Previous.bHitWindowOpen && Matches(*Packet) && Matches(Previous)) Packet = &Previous;
	}
	if (!Packet->bAtomicPath || !Matches(*Packet)) return false;
	OutPacket = *Packet;
	return true;
}

bool UProject_JCombatHitValidationComponent::FindAuthoritativeTraceAtTime(double TargetTimestamp, FVector& OutStart, FVector& OutEnd) const
{
	OutStart = OutEnd = FVector::ZeroVector;
	if (!ActiveAttackNodeTag.IsValid())
	{
		return false;
	}
	bool bIgnoredWindow = false;
	return FindAuthoritativeTraceAtTime(TargetTimestamp, ActivePredictionKey, ActiveAttackNodeTag, OutStart, OutEnd, bIgnoredWindow);
}

void UProject_JCombatHitValidationComponent::SetHitWindowOpen(const bool bOpen)
{
	bManualHitWindowOpen = bOpen && ActiveAttackNodeTag.IsValid();
	ApplyHitWindowState(bManualHitWindowOpen || !HitWindowTokens.IsEmpty());
}

uint64 UProject_JCombatHitValidationComponent::BeginHitWindow()
{
	if (!ActiveAttackNodeTag.IsValid() || HitWindowTokens.Num() >= 64) { return 0; }
	const uint64 Token = ++NextHitWindowToken;
	HitWindowTokens.Add(Token);
	ApplyHitWindowState(true);
	return Token;
}

void UProject_JCombatHitValidationComponent::EndHitWindow(uint64 Token)
{
	if (Token != 0 && HitWindowTokens.Remove(Token))
	{
		ApplyHitWindowState(bManualHitWindowOpen || !HitWindowTokens.IsEmpty());
	}
}

void UProject_JCombatHitValidationComponent::ApplyHitWindowState(const bool bOpen)
{
	const bool bNewState = bOpen && ActiveAttackNodeTag.IsValid();
	if (bHitWindowOpen == bNewState)
	{
		return;
	}

	bHitWindowOpen = bNewState;

	if (GetOwner() && GetOwner()->HasAuthority() && bHasAuthoritativeTrace && ActiveAttackNodeTag.IsValid())
	{
		AppendCurrentAuthoritativeSweepState();
	}
}

void UProject_JCombatHitValidationComponent::SubmitPredictedHit(AActor* HitActor, const double ClientTimestamp, const FVector& TraceStart, const FVector& TraceEnd)
{
	if (!HitActor || !ActiveAttackNodeTag.IsValid() || !bHitWindowOpen || !HasValidAttackWeapon() || ActivePredictionKey == 0)
	{
		return;
	}

	if (LocalRequestSequence == MAX_int32) return; // Do not wrap request ordering.
	if (ActiveAttackDefinition && ActiveAttackDefinition->MeleeTrajectory.bEnabled)
	{
		if (bNodeClockRetired || ActiveNodeVisitOrdinal <= 0) return;
		FProject_JCombatHitRequest Request;
		Request.Target = HitActor;
		Request.ClientTimestamp = ClientTimestamp;
		Request.TraceStart = TraceStart;
		Request.TraceEnd = TraceEnd;
		Request.AttackNodeTag = ActiveAttackNodeTag;
		Request.RequestSequence = ++LocalRequestSequence;
		Request.PredictionKey = ActivePredictionKey;
		Request.NodeClock.bPresent = true;
		Request.NodeClock.ClientNodeStartTimestamp = ActiveAttackStartTimestamp;
		Request.NodeClock.NodeVisitOrdinal = ActiveNodeVisitOrdinal;
		ServerRequestPredictedHit(Request);
		return;
	}
	ServerRequestSSRHit(HitActor, ClientTimestamp, TraceStart, TraceEnd, ActiveAttackNodeTag, ++LocalRequestSequence, ActivePredictionKey);
}

bool UProject_JCombatHitValidationComponent::CanProcessAuthorityHit(AActor* HitActor, int32 TargetLimit) const
{
	check(IsInGameThread());
	return GetOwner() && GetOwner()->HasAuthority() && IsValid(HitActor) && !HitActor->IsActorBeingDestroyed()
		&& ActiveAttackNodeTag.IsValid() && bHitWindowOpen && bHasAuthoritativeTrace && HasValidAttackWeapon()
		&& TargetLimit > 0 && ServerHitActors.Num() < TargetLimit && !ServerHitActors.Contains(HitActor);
}

bool UProject_JCombatHitValidationComponent::ProcessAuthorityHit(AActor* HitActor, int32 TargetLimit)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_Combat_AuthorityHit);
	if (!CanProcessAuthorityHit(HitActor, TargetLimit))
	{
		return false;
	}

	FProject_JCombatHitRequest Request;
	Request.Target = HitActor;
	Request.AttackNodeTag = ActiveAttackNodeTag;
	// Validate the server's recorded weapon sweep, not the request struct's zero
	// default. Otherwise legitimate authority hits depend on distance to world origin.
	Request.TraceStart = LastAuthoritativeTraceStart;
	Request.TraceEnd = LastAuthoritativeTraceEnd;
	if (HitValidationPolicy.ValidateActors(GetOwner(), Request) != EProject_JCombatHitValidationFailure::None)
	{
		return false;
	}
	if (IsTargetObstructed(HitActor))
	{
		return false;
	}

	ServerHitActors.Add(HitActor);
	return ApplyConfirmedHit(HitActor);
}

void UProject_JCombatHitValidationComponent::ServerRequestSSRHit_Implementation(AActor* HitActor, double ClientTimestamp, FVector TraceStart, FVector TraceEnd, FGameplayTag AttackNodeTag, int32 RequestSequence, int32 PredictionKey)
{
	FProject_JCombatHitRequest Request;
	Request.Target = HitActor;
	Request.ClientTimestamp = ClientTimestamp;
	Request.TraceStart = TraceStart;
	Request.TraceEnd = TraceEnd;
	Request.AttackNodeTag = AttackNodeTag;
	Request.RequestSequence = RequestSequence;
	Request.PredictionKey = PredictionKey;
	ProcessServerHitRequest(Request);
}

void UProject_JCombatHitValidationComponent::ServerRequestPredictedHit_Implementation(FProject_JCombatHitRequest Request)
{
	ProcessServerHitRequest(Request);
}

EProject_JCombatHitValidationFailure UProject_JCombatHitValidationComponent::ResolveSourceLookupTimestamp(
	const FProject_JCombatHitRequest& Request, double& OutTimestamp) const
{
	const bool bCanonical = ActiveAttackDefinition && ActiveAttackDefinition->MeleeTrajectory.bEnabled;
	if (!Request.NodeClock.bPresent)
	{
		if (bCanonical) return EProject_JCombatHitValidationFailure::NodeClockRequired;
		OutTimestamp = Request.ClientTimestamp;
		return EProject_JCombatHitValidationFailure::None;
	}
	if (!bCanonical) return EProject_JCombatHitValidationFailure::NodeClockUnexpected;
	if (Request.PredictionKey == 0 || Request.PredictionKey != ActivePredictionKey)
		return EProject_JCombatHitValidationFailure::AttackActivationMismatch;
	if (!Request.AttackNodeTag.MatchesTagExact(ActiveAttackNodeTag))
		return EProject_JCombatHitValidationFailure::AttackNodeMismatch;
	if (bNodeClockRetired) return EProject_JCombatHitValidationFailure::NodeClockRetired;
	if (ActiveNodeVisitOrdinal <= 0 || Request.NodeClock.NodeVisitOrdinal != ActiveNodeVisitOrdinal)
		return EProject_JCombatHitValidationFailure::NodeVisitMismatch;

	const double ClientStart = Request.NodeClock.ClientNodeStartTimestamp;
	const UWorld* ClockWorld = GetWorld();
	const double Now = ClockWorld ? ClockWorld->GetTimeSeconds() : 0.0;
	const double MaxAge = HitValidationPolicy.MaxRequestAge;
	const double MaxFuture = HitValidationPolicy.MaxFutureTolerance;
	const double LeadCap = HitValidationPolicy.MaxCanonicalPredictionLeadSeconds;
	if (!ClockWorld || !FMath::IsFinite(ClientStart) || ClientStart < 0.0 ||
		!FMath::IsFinite(Request.ClientTimestamp) || Request.ClientTimestamp < ClientStart ||
		!FMath::IsFinite(ActiveAttackStartTimestamp) || !FMath::IsFinite(Now) ||
		!FMath::IsFinite(MaxAge) || MaxAge < 0.0 || !FMath::IsFinite(MaxFuture) || MaxFuture < 0.0 ||
		!FMath::IsFinite(LeadCap) || LeadCap < 0.0)
		return EProject_JCombatHitValidationFailure::InvalidNodeClock;
	if (bClientNodeClockBound && ClientStart != BoundClientNodeStartTimestamp)
		return EProject_JCombatHitValidationFailure::NodeClockChanged;

	double AllowedLead = FMath::Min(LeadCap, MaxAge);
	if (ClockWorld->GetNetMode() != NM_Standalone)
	{
		const AActor* ClockOwner = GetOwner();
		const UNetConnection* Connection = ClockOwner ? ClockOwner->GetNetConnection() : nullptr;
		if (!IsValid(Connection) || !FMath::IsFinite(Connection->AvgLag) || Connection->AvgLag < 0.0f)
			return EProject_JCombatHitValidationFailure::InvalidNodeClock;
		// Native AvgLag is server-measured RTT-derived packet lag, not one-way
		// latency or a presentation clock. Zero permits only existing tolerance.
		AllowedLead = FMath::Min(AllowedLead, static_cast<double>(Connection->AvgLag) + MaxFuture);
	}
	// Standalone is an explicit local policy, not a no-connection remote fallback.
	// This is an untrusted immutable claim inside a server-measured envelope;
	// delayed ACKs remain limited by LeadCap, not proved honest by the envelope.
	const double StartSkew = ActiveAttackStartTimestamp - ClientStart;
	const double Elapsed = Request.ClientTimestamp - ClientStart;
	const double Mapped = ActiveAttackStartTimestamp + Elapsed;
	if (!FMath::IsFinite(StartSkew) || !FMath::IsFinite(Elapsed) || !FMath::IsFinite(Mapped) ||
		StartSkew > AllowedLead || StartSkew < -MaxFuture)
		return EProject_JCombatHitValidationFailure::InvalidNodeClock;
	if (Now - Mapped > MaxAge) return EProject_JCombatHitValidationFailure::RequestTooOld;
	if (Mapped - Now > MaxFuture) return EProject_JCombatHitValidationFailure::RequestFromFuture;
	OutTimestamp = Mapped;
	return EProject_JCombatHitValidationFailure::None;
}

void UProject_JCombatHitValidationComponent::ProcessServerHitRequest(const FProject_JCombatHitRequest& Request)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_Combat_SSRRequest);
	TStrongObjectPtr<UProject_JCombatHitValidationComponent> KeepThis(this);
	TStrongObjectPtr<AActor> KeepOwner(GetOwner());
	TStrongObjectPtr<AActor> KeepTarget(Request.Target.Get());
	TStrongObjectPtr<UWorld> KeepWorld(GetWorld());
	TStrongObjectPtr<UProject_JAttackDefinition> KeepDefinition(ActiveAttackDefinition.Get());
	const uint64 ExpectedPoseGeneration = MeleePoseGeneration;
	const uint64 ExpectedSpatialRevision = SpatialRevision;
	const int32 ExpectedPredictionKey = ActivePredictionKey;
	const int32 ExpectedNodeVisit = ActiveNodeVisitOrdinal;
	const FGameplayTag ExpectedNode = ActiveAttackNodeTag;
	const double ExpectedStart = ActiveAttackStartTimestamp;
	const auto IsCurrent = [&]()
	{
		return IsValid(this) && !IsBeingDestroyed() && IsRegistered() && !HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) &&
			IsValid(KeepOwner.Get()) && !KeepOwner->IsActorBeingDestroyed() && KeepOwner->HasAuthority() &&
			IsValid(KeepWorld.Get()) && IsValid(KeepTarget.Get()) && !KeepTarget->IsActorBeingDestroyed() &&
			GetOwner() == KeepOwner.Get() && GetWorld() == KeepWorld.Get() && KeepOwner->GetWorld() == KeepWorld.Get() &&
			(KeepDefinition.Get() == nullptr || IsValid(KeepDefinition.Get())) &&
			ActiveAttackDefinition.Get() == KeepDefinition.Get() && HasValidAttackWeapon() && MeleePoseGeneration == ExpectedPoseGeneration &&
			SpatialRevision == ExpectedSpatialRevision && ActivePredictionKey == ExpectedPredictionKey &&
			ActiveNodeVisitOrdinal == ExpectedNodeVisit && ActiveAttackNodeTag == ExpectedNode && ActiveAttackStartTimestamp == ExpectedStart;
	};
	const int32 RequestSequence = Request.RequestSequence;
	const int32 PredictionKey = Request.PredictionKey;
	if (!UProject_JCommandBudgetComponent::AdmitClientCommand(GetOwner()))
	{
		UE_LOG(LogProjectJCombatHitValidation, Verbose, TEXT("SSR decision=Rejected Reason=CommandBudget Sequence=%d PredictionKey=%d"), RequestSequence, PredictionKey);
		return;
	}
	if (!IsCurrent()) return;
	AActor* HitActor = KeepTarget.Get();
	const double ClientTimestamp = Request.ClientTimestamp;
	const FGameplayTag AttackNodeTag = Request.AttackNodeTag;

	// Reject malformed/basic spatial requests before domain admission. The
	// outer PlayerState command budget still accounts for every client attempt.
	const FProject_JCombatHitValidationResult ValidationResult = ValidateServerHitRequest(Request);
	if (!ValidationResult.bAccepted)
	{
		UE_LOG(LogProjectJCombatHitValidation, Verbose, TEXT("SSR hit request rejected. Requester=%s Target=%s Reason=%s"), *GetNameSafe(GetOwner()), *GetNameSafe(HitActor), LexToString(ValidationResult.Failure));
		return;
	}

	double SourceLookupTimestamp = 0.0;
	if (const EProject_JCombatHitValidationFailure ClockFailure = ResolveSourceLookupTimestamp(Request, SourceLookupTimestamp);
		ClockFailure != EProject_JCombatHitValidationFailure::None)
	{
		UE_LOG(LogProjectJCombatHitValidation, Verbose, TEXT("SSR decision=Rejected Reason=%s Sequence=%d PredictionKey=%d Query=%.17g ActiveStart=%.17g ClientStart=%.17g NodeVisit=%d ExpectedVisit=%d"),
			LexToString(ClockFailure), RequestSequence, PredictionKey, ClientTimestamp, ExpectedStart,
			Request.NodeClock.ClientNodeStartTimestamp, Request.NodeClock.NodeVisitOrdinal, ExpectedNodeVisit);
		return;
	}
	FProject_JAuthoritativeSweepRecord Packet;
	const bool bFoundPacket = FindAuthoritativeTracePacketAtTime(SourceLookupTimestamp, PredictionKey, AttackNodeTag, Packet);
	if (const EProject_JCombatHitValidationFailure ActiveFailure = ValidateActiveAttack(Request, !bFoundPacket); ActiveFailure != EProject_JCombatHitValidationFailure::None)
	{
		UE_LOG(LogProjectJCombatHitValidation, Verbose, TEXT("SSR active attack rejected. Requester=%s Target=%s Node=%s Reason=%s"), *GetNameSafe(GetOwner()), *GetNameSafe(HitActor), *AttackNodeTag.ToString(), LexToString(ActiveFailure));
		return;
	}

	bool bHitConfirmed = false;
	const float TraceRadius = ActiveAttackDefinition ? FMath::Max(0.0f, ActiveAttackDefinition->HitSpec.TraceRadius) : 0.0f;

	// One owning dispatch packet; the client hint never chooses or supplies these pieces.
	if (!bFoundPacket || !Packet.bHitWindowOpen)
	{
		UE_LOG(LogProjectJCombatHitValidation, Verbose, TEXT("SSR decision=Rejected Reason=%s Sequence=%d PredictionKey=%d Query=%.17g ActiveStart=%.17g HistoryCount=%d Oldest=%.17g Newest=%.17g Selected=%.17g SourceQuery=%.17g NodeVisit=%d ClientStart=%.17g"),
			bFoundPacket ? TEXT("PacketClosed") : TEXT("PacketUnavailable"), RequestSequence, PredictionKey,
			ClientTimestamp, ActiveAttackStartTimestamp, SweepHistoryCount,
			SweepHistoryCount ? GetSweepHistoryRecord(0).ServerTimestamp : 0.0,
			SweepHistoryCount ? GetSweepHistoryRecord(SweepHistoryCount - 1).ServerTimestamp : 0.0,
			Packet.ServerTimestamp, SourceLookupTimestamp, Request.NodeClock.NodeVisitOrdinal, Request.NodeClock.ClientNodeStartTimestamp);
		return;
	}
	if (!IsCurrent()) return;
	// Bind after the exact active/open authoritative packet is known, before
	// geometry. A geometry miss cannot be retried with a different source clock.
	if (Request.NodeClock.bPresent && !bClientNodeClockBound)
	{
		BoundClientNodeStartTimestamp = Request.NodeClock.ClientNodeStartTimestamp;
		bClientNodeClockBound = true;
	}
	if (UProject_JServerSideRewindComponent* SSRComp = HitActor->FindComponentByClass<UProject_JServerSideRewindComponent>())
	{
		if (UE_LOG_ACTIVE(LogProjectJCombatHitValidation, Verbose))
		{
			double Oldest = 0.0, Newest = 0.0;
			const bool bHasBounds = SSRComp->TryGetHistoryTimeBounds(Oldest, Newest);
			UE_LOG(LogProjectJCombatHitValidation, Verbose, TEXT("SSR history bounds Requester=%s Target=%s Query=%.17g HasBounds=%d Oldest=%.17g Newest=%.17g Sequence=%d PredictionKey=%d"),
				*GetNameSafe(GetOwner()), *GetNameSafe(HitActor), ClientTimestamp, bHasBounds, Oldest, Newest, RequestSequence, PredictionKey);
		}
		for (const auto& Segment : Packet.Segments)
			if (SSRComp->ServerVerifyHit(ClientTimestamp, Segment.TraceStart, Segment.TraceEnd, TraceRadius) &&
				!IsTargetObstructedFrom(HitActor, Segment.TraceStart)) { bHitConfirmed = true; break; }
	}
	else if (UWorld* World = GetWorld())
	{
		// No history owner: require current native collision, not a coarse swept AABB.
		// This supports every Pawn query primitive the real melee producer can hit.
		TArray<FHitResult> Hits;
		const FCollisionQueryParams Params = Project_J::Combat::MakeMeleeSweepParams(GetOwner());
		for (const auto& Segment : Packet.Segments)
		{
			Project_J::Combat::SweepMeleeSphere(*World, Params, Segment.TraceStart, Segment.TraceEnd, TraceRadius, Hits);
			if (Hits.ContainsByPredicate([HitActor](const FHitResult& Hit) { return Hit.GetActor() == HitActor; }) &&
				!IsTargetObstructedFrom(HitActor, Segment.TraceStart))
			{ bHitConfirmed = true; break; }
		}
	}

	if (!bHitConfirmed)
	{
		UE_LOG(LogProjectJCombatHitValidation, Verbose, TEXT("SSR decision=Rejected Reason=GeometryUnconfirmed Sequence=%d PredictionKey=%d"), RequestSequence, PredictionKey);
		return;
	}

	ServerHitActors.Add(HitActor);
	if (!IsCurrent()) return;
	const bool bSpecSubmitted = ApplyConfirmedHit(HitActor, IsCurrent);
	// Application delegates may retire/collect actors. Log only call-local values.
	UE_LOG(LogProjectJCombatHitValidation, Verbose, TEXT("SSR decision=%s Reason=%s Sequence=%d PredictionKey=%d Query=%.17g SourceQuery=%.17g Selected=%.17g ActiveStart=%.17g ClientStart=%.17g NodeVisit=%d"),
		bSpecSubmitted ? TEXT("Accepted") : TEXT("Rejected"), bSpecSubmitted ? TEXT("SpecSubmitted") : TEXT("SpecUnavailable"), RequestSequence, PredictionKey,
		ClientTimestamp, SourceLookupTimestamp, Packet.ServerTimestamp, ExpectedStart, Request.NodeClock.ClientNodeStartTimestamp, Request.NodeClock.NodeVisitOrdinal);
}

EProject_JCombatHitValidationFailure UProject_JCombatHitValidationComponent::ValidateActiveAttack(const FProject_JCombatHitRequest& Request, bool bCheckLiveTraceLOS)
{
	if (!ActiveAttackNodeTag.IsValid() || !HasValidAttackWeapon())
	{
		return EProject_JCombatHitValidationFailure::NoActiveAttack;
	}
	if (Request.PredictionKey == 0 || Request.PredictionKey != ActivePredictionKey)
	{
		return EProject_JCombatHitValidationFailure::AttackActivationMismatch;
	}
	if (!Request.AttackNodeTag.MatchesTagExact(ActiveAttackNodeTag))
	{
		return EProject_JCombatHitValidationFailure::AttackNodeMismatch;
	}
	if (Request.ClientTimestamp <= 0.0f && !bHitWindowOpen)
	{
		return EProject_JCombatHitValidationFailure::HitWindowClosed;
	}
	if (!bHasAuthoritativeTrace)
	{
		return EProject_JCombatHitValidationFailure::AuthoritativeTraceUnavailable;
	}
	// A rotating blade tip can travel as far as its full diameter while the
	// Leader root is stationary. The submitted segment is still only a hint;
	// SSR intersects the target against the server-recorded canonical sweep.
	const float TipRotationAllowance = ActiveAttackDefinition && ActiveAttackDefinition->HitSpec.bUseCanonicalBladeTipOffset
		? 2.0f * ActiveAttackDefinition->HitSpec.CanonicalBladeTipOffset.Size() : 0.0f;
	if (ActiveAttackDefinition &&
		FVector::DistSquared(Request.TraceStart, Request.TraceEnd) > FMath::Square(FMath::Max(0.0f,
			ActiveAttackDefinition->HitSpec.TraceDistance + TipRotationAllowance + ActiveAttackDefinition->HitSpec.TraceRadius)))
	{
		return EProject_JCombatHitValidationFailure::TraceTooLong;
	}
	if (ServerHitActors.Contains(Request.Target))
	{
		return EProject_JCombatHitValidationFailure::DuplicateTarget;
	}
	if (Request.RequestSequence <= LastServerRequestSequence)
	{
		return EProject_JCombatHitValidationFailure::RequestRateLimited;
	}

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (Now - RateWindowStartSeconds >= 1.0)
	{
		RateWindowStartSeconds = Now;
		RequestsInRateWindow = 0;
	}
	if (++RequestsInRateWindow > MaxRequestsPerSecond)
	{
		return EProject_JCombatHitValidationFailure::RequestRateLimited;
	}
	// Admission precedes the world query: a blocked valid attempt still owns
	// its sequence and rate slot, so retries cannot repeat an unbudgeted LOS.
	LastServerRequestSequence = Request.RequestSequence;
	if (bCheckLiveTraceLOS && IsTargetObstructed(Request.Target))
	{
		return EProject_JCombatHitValidationFailure::WorldObstructed;
	}
	return EProject_JCombatHitValidationFailure::None;
}

bool UProject_JCombatHitValidationComponent::IsTargetObstructed(const AActor* HitActor) const
{
	return IsTargetObstructedFrom(HitActor, LastAuthoritativeTraceStart);
}

bool UProject_JCombatHitValidationComponent::IsTargetObstructedFrom(const AActor* HitActor, const FVector& TraceOrigin) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_Combat_LineOfSight);
	const UWorld* World = GetWorld();
	if (!World || !GetOwner() || !HitActor)
	{
		return true;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ProjectJCombatLineOfSight), false, GetOwner());
	QueryParams.AddIgnoredActor(GetOwner());
	FHitResult BlockingHit;
	if (!World->LineTraceSingleByChannel(BlockingHit, TraceOrigin, HitActor->GetActorLocation(), ECC_Visibility, QueryParams))
	{
		return false;
	}

	return BlockingHit.GetActor() != HitActor;
}

bool UProject_JCombatHitValidationComponent::ApplyConfirmedHit(AActor* HitActor)
{
	return ApplyConfirmedHit(HitActor, [] { return true; });
}

bool UProject_JCombatHitValidationComponent::ApplyConfirmedHit(AActor* HitActor, TFunctionRef<bool()> IsCurrent)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_Combat_ApplyEffect);
	if (!IsCurrent()) return false;
	TStrongObjectPtr<UAbilitySystemComponent> OwnerASC(ResolveOwnerAbilitySystemComponent());
	if (!IsCurrent()) return false;
	const TSubclassOf<UGameplayEffect> DamageEffect = ActiveAttackDefinition ? ActiveAttackDefinition->DamageEffect : nullptr;
	if (!HitActor || !DamageEffect || !IsValid(OwnerASC.Get()))
	{
		return false;
	}

	TStrongObjectPtr<UAbilitySystemComponent> TargetASC(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(HitActor));
	if (!IsCurrent() || !IsValid(TargetASC.Get()))
	{
		return false;
	}

	FGameplayEffectContextHandle EffectContext = OwnerASC->MakeEffectContext();
	if (!IsCurrent()) return false;
	EffectContext.AddInstigator(GetOwner(), GetOwner());
	const FGameplayEffectSpecHandle SpecHandle = OwnerASC->MakeOutgoingSpec(DamageEffect, 1.0f, EffectContext);
	if (SpecHandle.IsValid() && IsCurrent())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_Combat_ApplySpec);
		OwnerASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC.Get());
		return true;
	}
	return false;
}

FProject_JCombatHitValidationResult UProject_JCombatHitValidationComponent::ValidateServerHitRequest(const FProject_JCombatHitRequest& Request) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return FProject_JCombatHitValidationResult::Rejected(EProject_JCombatHitValidationFailure::InvalidRequester);
	}
	if (const EProject_JCombatHitValidationFailure Failure = HitValidationPolicy.ValidateRequestData(Request, World->GetTimeSeconds()); Failure != EProject_JCombatHitValidationFailure::None)
	{
		return FProject_JCombatHitValidationResult::Rejected(Failure);
	}
	if (const EProject_JCombatHitValidationFailure Failure = HitValidationPolicy.ValidateActors(GetOwner(), Request); Failure != EProject_JCombatHitValidationFailure::None)
	{
		return FProject_JCombatHitValidationResult::Rejected(Failure);
	}
	return FProject_JCombatHitValidationResult::Accepted();
}

UAbilitySystemComponent* UProject_JCombatHitValidationComponent::ResolveOwnerAbilitySystemComponent() const
{
	return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
}


void UProject_JCombatHitValidationComponent::EndMeleePoseCapture()
{
	++MeleePoseGeneration;
	if (auto* Mesh = ProtectedMesh.Get()) Mesh->UnregisterOnBoneTransformsFinalizedDelegate(MeleePoseHandle);
	MeleePoseHandle.Reset(); MeleePoseSamples.Reset();
	MeleeRootAnchor = FMeleeRootAnchor();
	MeleePoseAsset.Reset(); MeleePoseAnim.Reset(); MeleePoseAnimClass.Reset(); MeleePoseMontage.Reset(); MeleePoseInstanceID = INDEX_NONE;
}

void UProject_JCombatHitValidationComponent::BeginMeleePoseCapture()
{
	auto* Mesh = ProtectedMesh.Get();
	auto* Montage = ActiveAttackDefinition ? ActiveAttackDefinition->Montage.Get() : nullptr;
	if (!IsValid(Mesh) || !Mesh->IsRegistered() || !Mesh->GetSkeletalMeshAsset() || !Mesh->GetAnimInstance() || !Montage || Montage->Notifies.Num() > 64) return;
	MeleePoseAsset = Mesh->GetSkeletalMeshAsset(); MeleePoseAnim = Mesh->GetAnimInstance(); MeleePoseAnimClass = Mesh->GetAnimInstance()->GetClass(); MeleePoseMontage = Montage;
	for (int32 Index = 0; Index < Montage->Notifies.Num(); ++Index)
	{
		const auto& Event = Montage->Notifies[Index];
		auto* Notify = Cast<UProject_JAnimNotifyState_MeleeHit>(Event.NotifyStateClass.Get());
		if (!Notify || Event.IsBranchingPoint() || Event.GetLinkedMontage() != Montage || !Mesh->DoesSocketExist(Notify->GetTraceSocketName())) continue;
		FMeleePoseSample Sample;
		Sample.Notify = Notify; Sample.NotifyIndex = Index; Sample.Socket = Notify->GetTraceSocketName();
		Sample.TriggerTime = Event.GetTriggerTime(); Sample.EndTriggerTime = Event.GetEndTriggerTime();
		Sample.bUseTipOffset = ActiveAttackDefinition->HitSpec.bUseCanonicalBladeTipOffset;
		Sample.TipOffset = ActiveAttackDefinition->HitSpec.CanonicalBladeTipOffset;
		MeleePoseSamples.Add(MoveTemp(Sample));
	}
	if (!MeleePoseSamples.IsEmpty())
		MeleePoseHandle = Mesh->RegisterOnBoneTransformsFinalizedDelegate(FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(this, &ThisClass::CaptureMeleePose, MeleePoseGeneration));
}

void UProject_JCombatHitValidationComponent::CaptureMeleePose(uint64 ExpectedGeneration)
{
	check(IsInGameThread());
	if (ExpectedGeneration != MeleePoseGeneration || IsBeingDestroyed()) return;
	MeleeRootAnchor.bValid = false;
	auto* Mesh = ProtectedMesh.Get(); auto* Anim = MeleePoseAnim.Get(); auto* Montage = MeleePoseMontage.Get();
	if (!IsValid(Mesh) || !Mesh->IsRegistered() || Mesh->IsBeingDestroyed() || !IsValid(Anim) || !Montage || !ActiveAttackDefinition ||
		ActiveAttackDefinition->Montage != Montage || !GetWorld() || GetWorld()->bIsTearingDown || Mesh->GetWorld() != GetWorld() ||
		Mesh->GetOwner() != GetOwner() || Project_J::Movement::FindPoseSource(GetOwner()) != Mesh || Mesh->GetSkeletalMeshAsset() != MeleePoseAsset.Get() || Mesh->GetAnimInstance() != Anim || Anim->GetClass() != MeleePoseAnimClass.Get())
	{ for (auto& Sample : MeleePoseSamples) Sample.bValid = false; return; }
	// Resolve once initially, then retain the exact ID through legitimate blendout.
	const auto* Instance = MeleePoseInstanceID == INDEX_NONE ? Anim->GetActiveInstanceForMontage(Montage) : Anim->GetMontageInstanceForID(MeleePoseInstanceID);
	if (!Instance || Instance->Montage != Montage || !FMath::IsFinite(Instance->GetPosition())) { for (auto& Sample : MeleePoseSamples) Sample.bValid = false; return; }
	MeleePoseInstanceID = Instance->GetInstanceID();
	MeleeRootAnchor = FMeleeRootAnchor();
	if (ActiveAttackDefinition->MeleeTrajectory.bEnabled) ReadMeleeRootAnchor(Mesh, MeleeRootAnchor);
	for (auto& Sample : MeleePoseSamples)
	{
		auto* Notify = Sample.Notify.Get();
		if (!Notify || !Montage->Notifies.IsValidIndex(Sample.NotifyIndex) || Montage->Notifies[Sample.NotifyIndex].NotifyStateClass.Get() != Notify ||
			Notify->GetTraceSocketName() != Sample.Socket || Sample.bUseTipOffset != ActiveAttackDefinition->HitSpec.bUseCanonicalBladeTipOffset ||
			Sample.TipOffset != ActiveAttackDefinition->HitSpec.CanonicalBladeTipOffset ||
			Montage->Notifies[Sample.NotifyIndex].GetTriggerTime() != Sample.TriggerTime || Montage->Notifies[Sample.NotifyIndex].GetEndTriggerTime() != Sample.EndTriggerTime)
		{ Sample.bValid = false; continue; }
		Sample.WorldPoint = Notify->ResolveTraceLocation(Mesh, ActiveAttackDefinition);
		Sample.MontageTime = Instance->GetPosition(); Sample.Frame = GFrameCounter; Sample.Spatial = SpatialRevision;
		Sample.BoneRevision = Mesh->GetBoneTransformRevisionNumber(); Sample.bValid = !Sample.WorldPoint.ContainsNaN();
	}
}

bool UProject_JCombatHitValidationComponent::TryGetPreviousMeleePose(USkeletalMeshComponent* Mesh, const FAnimNotifyEventReference& EventReference,
	FVector& OutWorldPoint, float& OutMontageTime) const
{
	const FMeleePoseSample* Sample = FindPreviousMeleePoseSample(Mesh, EventReference);
	if (!Sample) return false;
	OutWorldPoint = Sample->WorldPoint; OutMontageTime = Sample->MontageTime;
	return true;
}

const UProject_JCombatHitValidationComponent::FMeleePoseSample* UProject_JCombatHitValidationComponent::FindPreviousMeleePoseSample(
	USkeletalMeshComponent* Mesh, const FAnimNotifyEventReference& EventReference) const
{
	check(IsInGameThread());
	const auto* Context = EventReference.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>();
	const auto* Event = EventReference.GetNotify();
	auto* Anim = MeleePoseAnim.Get(); auto* Montage = MeleePoseMontage.Get();
	if (!Context || !Event || Event->IsBranchingPoint() || !IsValid(Mesh) || !Mesh->IsRegistered() || Mesh->IsBeingDestroyed() || ProtectedMesh.Get() != Mesh || !IsValid(Anim) || !Montage ||
		!ActiveAttackDefinition || ActiveAttackDefinition->Montage != Montage || Mesh->GetAnimInstance() != Anim ||
		!GetWorld() || GetWorld()->bIsTearingDown || Mesh->GetWorld() != GetWorld() || Mesh->GetOwner() != GetOwner() ||
		Project_J::Movement::FindPoseSource(GetOwner()) != Mesh || Anim->GetClass() != MeleePoseAnimClass.Get() || Mesh->GetSkeletalMeshAsset() != MeleePoseAsset.Get() || Event->GetLinkedMontage() != Montage || EventReference.GetSourceObject() != Montage || Context->MontageInstanceID != MeleePoseInstanceID) return nullptr;
	const auto* Instance = Anim->GetMontageInstanceForID(Context->MontageInstanceID);
	const float Current = EventReference.GetCurrentAnimationTime();
	if (!Instance || Instance->Montage != Montage || !FMath::IsFinite(Current) || !FMath::IsFinite(Instance->GetPreviousPosition()) || !FMath::IsFinite(Instance->GetDeltaMoved()) ||
		!FMath::IsNearlyEqual(Current, Instance->GetPosition(), UE_KINDA_SMALL_NUMBER) || Current <= Instance->GetPreviousPosition() ||
		!FMath::IsNearlyEqual(Current - Instance->GetPreviousPosition(), Instance->GetDeltaMoved(), UE_KINDA_SMALL_NUMBER) ||
		Montage->GetSectionIndexFromPosition(Current) != Montage->GetSectionIndexFromPosition(Instance->GetPreviousPosition())) return nullptr;
	for (const auto& Sample : MeleePoseSamples)
	{
		if (!Sample.bValid || (Sample.Frame == MAX_uint64 || Sample.Frame + 1 != GFrameCounter) || Sample.Spatial != SpatialRevision ||
			Sample.BoneRevision != Mesh->GetPreviousBoneTransformRevisionNumber() || !Montage->Notifies.IsValidIndex(Sample.NotifyIndex) ||
			Montage->Notifies[Sample.NotifyIndex].NotifyStateClass.Get() != Sample.Notify.Get() || Event->NotifyStateClass.Get() != Sample.Notify.Get() || !Sample.Notify.IsValid() ||
			Sample.Socket != Sample.Notify->GetTraceSocketName() || Sample.bUseTipOffset != ActiveAttackDefinition->HitSpec.bUseCanonicalBladeTipOffset ||
			Sample.TipOffset != ActiveAttackDefinition->HitSpec.CanonicalBladeTipOffset ||
			Event->GetTriggerTime() != Sample.TriggerTime || Event->GetEndTriggerTime() != Sample.EndTriggerTime ||
			Montage->Notifies[Sample.NotifyIndex].GetTriggerTime() != Sample.TriggerTime || Montage->Notifies[Sample.NotifyIndex].GetEndTriggerTime() != Sample.EndTriggerTime ||
			!FMath::IsNearlyEqual(Sample.MontageTime, Instance->GetPreviousPosition(), UE_KINDA_SMALL_NUMBER)) continue;
		return &Sample;
	}
	return nullptr;
}

namespace
{
bool IsMeleeUniformTransform(const FTransform& Transform)
{
	if (Transform.ContainsNaN() || !Transform.GetRotation().IsNormalized()) return false;
	const FVector Scale = Transform.GetScale3D();
	return Scale.X > 0. && Scale.Y > 0. && Scale.Z > 0. &&
		FMath::IsNearlyEqual(Scale.X, Scale.Y, UE_KINDA_SMALL_NUMBER) &&
		FMath::IsNearlyEqual(Scale.X, Scale.Z, UE_KINDA_SMALL_NUMBER);
}
}

bool UProject_JCombatHitValidationComponent::ReadMeleeRootAnchor(USkeletalMeshComponent* Mesh, FMeleeRootAnchor& OutAnchor) const
{
	check(IsInGameThread());
	auto* Owner = GetOwner();
	auto* Mover = Project_J::Movement::FindMover(Owner);
	if (!IsRegistered() || IsBeingDestroyed() || !IsValid(Owner) || Owner->IsActorBeingDestroyed() || !(Owner->HasAuthority() || (Owner->GetLocalRole()==ROLE_AutonomousProxy && Cast<APawn>(Owner) && CastChecked<APawn>(Owner)->IsLocallyControlled())) || !GetWorld() || GetWorld()->bIsTearingDown ||
		!IsValid(Mesh) || !Mesh->IsRegistered() || Mesh->IsBeingDestroyed() || Mesh->GetOwner() != Owner || Mesh->GetWorld() != GetWorld() ||
		!IsValid(Mover) || !Mover->IsRegistered() || Mover->IsBeingDestroyed() || !Mover->HasBeenInitialized() || !Mover->HasBegunPlay() ||
		Mover->GetOwner() != Owner || Mover->GetWorld() != GetWorld() || Mover->IsBackendAsync() ||
		Mover->ProcessLocalRootMotionDelegate.IsBound() || Mover->ProcessWorldRootMotionDelegate.IsBound()) return false;
	const FMoverTimeStep& LastStep = Mover->GetLastTimeStep();
	if (LastStep.ServerFrame < 0 || !FMath::IsFinite(LastStep.BaseSimTimeMs) || LastStep.BaseSimTimeMs < 0. ||
		!FMath::IsFinite(LastStep.StepMs) || LastStep.StepMs <= 0.f || LastStep.bIsResimulating) return false;
	auto* Root = Mover->GetUpdatedComponent();
	if (!IsValid(Root) || !Root->IsRegistered() || Root->IsBeingDestroyed() || Root->GetOwner() != Owner || Root->GetWorld() != GetWorld() ||
		Root != Owner->GetRootComponent() || !Mesh->IsAttachedTo(Root)) return false;
	const auto* Physical = Mover->GetSyncState().SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
	if (!Physical) return false;
	auto* MovementBase = Physical->GetMovementBase();
	FTransform CapturedBaseFrame = FTransform::Identity;
	FName BaseBone = NAME_None;
	if (MovementBase)
	{
		if (!IsValid(MovementBase) || !MovementBase->IsRegistered() || MovementBase->IsBeingDestroyed() || MovementBase->GetWorld() != GetWorld()) return false;
		const auto* BaseOwner = MovementBase->GetOwner();
		if (BaseOwner && (!IsValid(BaseOwner) || BaseOwner->IsActorBeingDestroyed() || BaseOwner->GetWorld() != GetWorld())) return false;
		// Use the published frame used by GetTransform_WorldSpace, never a new base/socket query.
		const FVector Position = Physical->GetCapturedMovementBasePos();
		const FQuat Rotation = Physical->GetCapturedMovementBaseQuat();
		if (Position.ContainsNaN() || Rotation.ContainsNaN() || !Rotation.IsNormalized()) return false;
		CapturedBaseFrame = FTransform(Rotation, Position);
		BaseBone = Physical->GetMovementBaseBoneName();
	}
	const FTransform RootWorld = Root->GetComponentTransform();
	const FTransform MeshWorld = Mesh->GetComponentTransform();
	const FTransform Committed = Physical->GetTransform_WorldSpace();
	if (!IsMeleeUniformTransform(RootWorld) || !IsMeleeUniformTransform(MeshWorld) || Committed.ContainsNaN() ||
		!Committed.GetRotation().IsNormalized() || !RootWorld.GetLocation().Equals(Committed.GetLocation(), UE_KINDA_SMALL_NUMBER) ||
		!RootWorld.GetRotation().Equals(Committed.GetRotation(), UE_KINDA_SMALL_NUMBER)) return false;
	const FTransform MeshToRoot = MeshWorld.GetRelativeTransform(RootWorld);
	if (!IsMeleeUniformTransform(MeshToRoot)) return false;
	FMeleeRootAnchor Anchor;
	Anchor.Mover = Mover; Anchor.Root = Root; Anchor.RootWorld = RootWorld; Anchor.MeshToRoot = MeshToRoot;
	Anchor.MovementBase = MovementBase; Anchor.bBased = MovementBase != nullptr; Anchor.BaseBone = BaseBone; Anchor.CapturedBaseFrame = CapturedBaseFrame;
	Anchor.Frame = GFrameCounter; Anchor.Spatial = SpatialRevision; Anchor.bValid = true;
	OutAnchor = MoveTemp(Anchor);
	return true;
}

bool UProject_JCombatHitValidationComponent::TryGetMeleeRootInterval(USkeletalMeshComponent* Mesh, const FAnimNotifyEventReference& EventReference,
	FTransform& OutPreviousRoot, FTransform& OutCurrentRoot, FTransform& OutMeshToRoot, float& OutPreviousMontageTime) const
{
	const FMeleePoseSample* Sample = FindPreviousMeleePoseSample(Mesh, EventReference);
	if (!Sample || !MeleeRootAnchor.bValid || !MeleeRootAnchor.Mover.IsValid() || !MeleeRootAnchor.Root.IsValid() ||
		MeleeRootAnchor.Frame != Sample->Frame || MeleeRootAnchor.Spatial != Sample->Spatial) return false;
	FMeleeRootAnchor Current;
	if (!ReadMeleeRootAnchor(Mesh, Current) || Current.Mover != MeleeRootAnchor.Mover || Current.Root != MeleeRootAnchor.Root ||
		Current.Spatial != MeleeRootAnchor.Spatial || Current.Frame != GFrameCounter ||
		Current.bBased != MeleeRootAnchor.bBased || (Current.bBased && (!MeleeRootAnchor.MovementBase.IsValid() ||
			Current.MovementBase != MeleeRootAnchor.MovementBase || Current.BaseBone != MeleeRootAnchor.BaseBone ||
			!Current.CapturedBaseFrame.Equals(MeleeRootAnchor.CapturedBaseFrame, 0.))) ||
		!Current.RootWorld.GetScale3D().Equals(MeleeRootAnchor.RootWorld.GetScale3D(), UE_KINDA_SMALL_NUMBER) ||
		!Current.MeshToRoot.Equals(MeleeRootAnchor.MeshToRoot, UE_KINDA_SMALL_NUMBER)) return false;
	OutPreviousRoot = MeleeRootAnchor.RootWorld;
	OutCurrentRoot = Current.RootWorld;
	OutMeshToRoot = MeleeRootAnchor.MeshToRoot;
	OutPreviousMontageTime = Sample->MontageTime;
	return true;
}
