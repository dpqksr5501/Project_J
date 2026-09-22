#include "Components/Project_JCombatHitValidationComponent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Combat/Project_JServerSideRewindComponent.h"
#include "Combat/Project_JAttackDefinition.h"
#include "GameplayEffect.h"
#include "Engine/World.h"
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
	ProtectAttackPose();
	bRequiresWeapon = Cast<AProject_JPlayerCharacter>(GetOwner()) != nullptr;
	AttackEquipment = GetOwner() ? GetOwner()->FindComponentByClass<UProject_JEquipmentRuntimeComponent>() : nullptr;
	AttackWeaponRevision = AttackEquipment.IsValid() ? AttackEquipment->GetWeaponRevision() : 0;
	ActivePredictionKey = PredictionKey;
	ActiveAttackNodeTag = AttackNodeTag;
	ActiveAttackDefinition = AttackDefinition;
	bHitWindowOpen = false;
	bHasAuthoritativeTrace = false;
	ServerHitActors.Reset();
}

void UProject_JCombatHitValidationComponent::EndAttack()
{
	SetHitWindowOpen(false);
	AttackEquipment.Reset();
	AttackWeaponRevision = 0;
	ActivePredictionKey = 0;
	ActiveAttackNodeTag = FGameplayTag();
	ActiveAttackDefinition = nullptr;
	bHasAuthoritativeTrace = false;
	ServerHitActors.Reset();
	RestoreAttackPose();
}

void UProject_JCombatHitValidationComponent::ProtectAttackPose()
{
	check(IsInGameThread());
	const auto* Character = Cast<ACharacter>(GetOwner());
	auto* Mesh = Character ? Character->GetMesh() : nullptr;
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
	AuthoritativeSweepHistory.Reset();
	SweepHistoryStartIndex = 0;
	SweepHistoryCount = 0;
	EndAttack();
	Super::EndPlay(Reason);
}

void UProject_JCombatHitValidationComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	AuthoritativeSweepHistory.Reset();
	SweepHistoryStartIndex = 0;
	SweepHistoryCount = 0;
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

void UProject_JCombatHitValidationComponent::AppendSweepHistoryRecord(const FProject_JAuthoritativeSweepRecord& Record)
{
	if (AuthoritativeSweepHistory.Num() < MaxSweepHistoryCapacity)
	{
		AuthoritativeSweepHistory.SetNum(MaxSweepHistoryCapacity);
		SweepHistoryStartIndex = 0;
		SweepHistoryCount = 0;
	}

	if (SweepHistoryCount < AuthoritativeSweepHistory.Num())
	{
		const int32 PhysicalIndex = (SweepHistoryStartIndex + SweepHistoryCount) % AuthoritativeSweepHistory.Num();
		AuthoritativeSweepHistory[PhysicalIndex] = Record;
		++SweepHistoryCount;
	}
	else
	{
		AuthoritativeSweepHistory[SweepHistoryStartIndex] = Record;
		SweepHistoryStartIndex = (SweepHistoryStartIndex + 1) % AuthoritativeSweepHistory.Num();
	}
}

void UProject_JCombatHitValidationComponent::DiscardExpiredSweepRecords(float CurrentTimestamp)
{
	const float EffectiveMaxAge = FMath::Max(MaxSweepHistorySeconds, HitValidationPolicy.MaxRequestAge + 0.25f);
	while (SweepHistoryCount > 0)
	{
		const float Age = CurrentTimestamp - GetSweepHistoryRecord(0).ServerTimestamp;
		if (Age > EffectiveMaxAge)
		{
			SweepHistoryStartIndex = (SweepHistoryStartIndex + 1) % AuthoritativeSweepHistory.Num();
			--SweepHistoryCount;
		}
		else
		{
			break;
		}
	}
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
	const float CurrentTime = GameState ? GameState->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.0f);

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
	const float CurrentTime = GameState ? GameState->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.0f);

	DiscardExpiredSweepRecords(CurrentTime);

	FProject_JAuthoritativeSweepRecord Record;
	Record.ServerTimestamp = CurrentTime;
	Record.TraceStart = LastAuthoritativeTraceStart;
	Record.TraceEnd = LastAuthoritativeTraceEnd;
	Record.AttackNodeTag = ActiveAttackNodeTag;
	Record.PredictionKey = ActivePredictionKey;
	Record.bHitWindowOpen = bHitWindowOpen;

	AppendSweepHistoryRecord(Record);
}

bool UProject_JCombatHitValidationComponent::FindAuthoritativeTraceAtTime(
	float TargetTimestamp,
	int32 ExpectedPredictionKey,
	const FGameplayTag& ExpectedAttackNodeTag,
	FVector& OutStart,
	FVector& OutEnd,
	bool& OutHitWindowOpen) const
{
	OutStart = FVector::ZeroVector;
	OutEnd = FVector::ZeroVector;
	OutHitWindowOpen = false;

	if (SweepHistoryCount == 0 || !FMath::IsFinite(TargetTimestamp))
	{
		return false;
	}

	const float OldestTime = GetSweepHistoryRecord(0).ServerTimestamp;
	const float NewestTime = GetSweepHistoryRecord(SweepHistoryCount - 1).ServerTimestamp;
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

	if (bMatchA && bMatchB)
	{
		const float TimeDelta = RecordB.ServerTimestamp - RecordA.ServerTimestamp;
		const float Alpha = FMath::IsNearlyZero(TimeDelta) ? 0.0f : FMath::Clamp((TargetTimestamp - RecordA.ServerTimestamp) / TimeDelta, 0.0f, 1.0f);
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

bool UProject_JCombatHitValidationComponent::FindAuthoritativeTraceAtTime(float TargetTimestamp, FVector& OutStart, FVector& OutEnd) const
{
	bool bIgnoredWindow = false;
	return FindAuthoritativeTraceAtTime(TargetTimestamp, ActivePredictionKey, ActiveAttackNodeTag, OutStart, OutEnd, bIgnoredWindow);
}

void UProject_JCombatHitValidationComponent::SetHitWindowOpen(const bool bOpen)
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

void UProject_JCombatHitValidationComponent::SubmitPredictedHit(AActor* HitActor, const float ClientTimestamp, const FVector& TraceStart, const FVector& TraceEnd)
{
	if (!HitActor || !ActiveAttackNodeTag.IsValid() || !bHitWindowOpen || !HasValidAttackWeapon() || ActivePredictionKey == 0)
	{
		return;
	}

	ServerRequestSSRHit(HitActor, ClientTimestamp, TraceStart, TraceEnd, ActiveAttackNodeTag, ++LocalRequestSequence, ActivePredictionKey);
}

bool UProject_JCombatHitValidationComponent::ProcessAuthorityHit(AActor* HitActor)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !HitActor || !ActiveAttackNodeTag.IsValid() || !bHitWindowOpen || !bHasAuthoritativeTrace || !HasValidAttackWeapon() || ServerHitActors.Contains(HitActor))
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

void UProject_JCombatHitValidationComponent::ServerRequestSSRHit_Implementation(AActor* HitActor, float ClientTimestamp, FVector TraceStart, FVector TraceEnd, FGameplayTag AttackNodeTag, int32 RequestSequence, int32 PredictionKey)
{
	FProject_JCombatHitRequest Request;
	Request.Target = HitActor;
	Request.ClientTimestamp = ClientTimestamp;
	Request.TraceStart = TraceStart;
	Request.TraceEnd = TraceEnd;
	Request.AttackNodeTag = AttackNodeTag;
	Request.RequestSequence = RequestSequence;
	Request.PredictionKey = PredictionKey;

	if (const EProject_JCombatHitValidationFailure ActiveFailure = ValidateActiveAttack(Request); ActiveFailure != EProject_JCombatHitValidationFailure::None)
	{
		UE_LOG(LogProjectJCombatHitValidation, Verbose, TEXT("SSR active attack rejected. Requester=%s Target=%s Node=%s Reason=%s"), *GetNameSafe(GetOwner()), *GetNameSafe(HitActor), *AttackNodeTag.ToString(), LexToString(ActiveFailure));
		return;
	}

	const FProject_JCombatHitValidationResult ValidationResult = ValidateServerHitRequest(Request);
	if (!ValidationResult.bAccepted)
	{
		UE_LOG(LogProjectJCombatHitValidation, Verbose, TEXT("SSR hit request rejected. Requester=%s Target=%s Reason=%s"), *GetNameSafe(GetOwner()), *GetNameSafe(HitActor), LexToString(ValidationResult.Failure));
		return;
	}

	bool bHitConfirmed = false;
	const float TraceRadius = ActiveAttackDefinition ? FMath::Max(0.0f, ActiveAttackDefinition->HitSpec.TraceRadius) : 0.0f;

	// Synchronize attacker weapon sweep to the client's timestamp using the authoritative sweep history
	FVector SynchronizedTraceStart = FVector::ZeroVector;
	FVector SynchronizedTraceEnd = FVector::ZeroVector;
	bool bHistoricalHitWindowOpen = false;

	if (!FindAuthoritativeTraceAtTime(ClientTimestamp, PredictionKey, AttackNodeTag, SynchronizedTraceStart, SynchronizedTraceEnd, bHistoricalHitWindowOpen))
	{
		UE_LOG(LogProjectJCombatHitValidation, Verbose, TEXT("SSR sweep history lookup failed for ClientTimestamp=%.3f Key=%d Node=%s"), ClientTimestamp, PredictionKey, *AttackNodeTag.ToString());
		return;
	}

	if (!bHistoricalHitWindowOpen)
	{
		UE_LOG(LogProjectJCombatHitValidation, Verbose, TEXT("SSR historical hit window was closed at ClientTimestamp=%.3f Key=%d Node=%s"), ClientTimestamp, PredictionKey, *AttackNodeTag.ToString());
		return;
	}

	if (UProject_JServerSideRewindComponent* SSRComp = HitActor->FindComponentByClass<UProject_JServerSideRewindComponent>())
	{
		bHitConfirmed = SSRComp->ServerVerifyHit(ClientTimestamp, SynchronizedTraceStart, SynchronizedTraceEnd, TraceRadius);
	}
	else
	{
		bHitConfirmed = HitActor->GetComponentsBoundingBox().Intersect(FBox(SynchronizedTraceStart.ComponentMin(SynchronizedTraceEnd), SynchronizedTraceStart.ComponentMax(SynchronizedTraceEnd)).ExpandBy(TraceRadius));
	}

	if (!bHitConfirmed)
	{
		return;
	}

	ServerHitActors.Add(HitActor);
	ApplyConfirmedHit(HitActor);
}

EProject_JCombatHitValidationFailure UProject_JCombatHitValidationComponent::ValidateActiveAttack(const FProject_JCombatHitRequest& Request)
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
	if (ActiveAttackDefinition &&
		FVector::DistSquared(Request.TraceStart, Request.TraceEnd) > FMath::Square(FMath::Max(0.0f, ActiveAttackDefinition->HitSpec.TraceDistance + ActiveAttackDefinition->HitSpec.TraceRadius)))
	{
		return EProject_JCombatHitValidationFailure::TraceTooLong;
	}
	if (IsTargetObstructed(Request.Target))
	{
		return EProject_JCombatHitValidationFailure::WorldObstructed;
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
	LastServerRequestSequence = Request.RequestSequence;
	return EProject_JCombatHitValidationFailure::None;
}

bool UProject_JCombatHitValidationComponent::IsTargetObstructed(const AActor* HitActor) const
{
	const UWorld* World = GetWorld();
	if (!World || !GetOwner() || !HitActor)
	{
		return true;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ProjectJCombatLineOfSight), false, GetOwner());
	QueryParams.AddIgnoredActor(GetOwner());
	FHitResult BlockingHit;
	if (!World->LineTraceSingleByChannel(BlockingHit, LastAuthoritativeTraceStart, HitActor->GetActorLocation(), ECC_Visibility, QueryParams))
	{
		return false;
	}

	return BlockingHit.GetActor() != HitActor;
}

bool UProject_JCombatHitValidationComponent::ApplyConfirmedHit(AActor* HitActor)
{
	UAbilitySystemComponent* OwnerASC = ResolveOwnerAbilitySystemComponent();
	const TSubclassOf<UGameplayEffect> DamageEffect = ActiveAttackDefinition ? ActiveAttackDefinition->DamageEffect : nullptr;
	if (!HitActor || !DamageEffect || !OwnerASC)
	{
		return false;
	}

	UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(HitActor);
	if (!TargetASC)
	{
		return false;
	}

	FGameplayEffectContextHandle EffectContext = OwnerASC->MakeEffectContext();
	EffectContext.AddInstigator(GetOwner(), GetOwner());
	const FGameplayEffectSpecHandle SpecHandle = OwnerASC->MakeOutgoingSpec(DamageEffect, 1.0f, EffectContext);
	if (SpecHandle.IsValid())
	{
		OwnerASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);
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
