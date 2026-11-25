#include "Network/Project_JCrowdDensityNetObjectFilter.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AI/Project_JThreatComponent.h"
#include "Combat/Project_JMultiPartHitComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Optimization/Project_JServerLoadGovernorSubsystem.h"
#include "Project_JGameplayTags.h"
#include "Project_JNPCCharacter.h"
#include "Project_JSocialStateInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJCrowdFilter, Log, All);

namespace
{
TAutoConsoleVariable<int32> CVarEnableIrisCrowdDensityFilter(
	TEXT("ProjectJ.Net.EnableIrisCrowdDensityFilter"),
	1,
	TEXT("Enables the Iris spatial + dynamic Top-K crowd density filter on characters (1 = enabled, 0 = disabled)."),
	ECVF_Default);

TAutoConsoleVariable<int32> CVarMaxVisibleRemoteActors(
	TEXT("ProjectJ.Net.MaxVisibleRemoteActors"),
	60,
	TEXT("Base per-connection cap on non-exempt remote replicated actors before Governor scaling (0 = uncapped)."),
	ECVF_Default);

const UObject* ResolveSocialObject(const AActor* Actor)
{
	if (!IsValid(Actor)) return nullptr;
	if (Actor->Implements<UProject_JSocialStateInterface>()) return Actor;
	if (const APawn* Pawn = Cast<APawn>(Actor))
	{
		if (const APlayerState* PS = Pawn->GetPlayerState(); PS && PS->Implements<UProject_JSocialStateInterface>())
		{
			return PS;
		}
	}
	if (const AController* PC = Cast<AController>(Actor))
	{
		if (const APlayerState* PS = PC->GetPlayerState<APlayerState>(); PS && PS->Implements<UProject_JSocialStateInterface>())
		{
			return PS;
		}
	}
	return nullptr;
}

bool IsBossEntity(const AActor* Actor)
{
	if (const AProject_JNPCCharacter* NPC = Cast<AProject_JNPCCharacter>(Actor))
	{
		if (NPC->BrainDefinition && NPC->BrainDefinition->bIsGiantBoss) return true;
		if (const auto* MultiPart = NPC->FindComponentByClass<UProject_JMultiPartHitComponent>();
			IsValid(MultiPart) && MultiPart->IsRegistered() && !MultiPart->IsBeingDestroyed())
		{
			return true;
		}
	}
	return false;
}

float ComputeActorCombatWeight(const AActor* Actor)
{
	if (!IsValid(Actor)) return 1.0f;
	const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(const_cast<AActor*>(Actor));
	if (!ASC) return 1.0f;
	const FProject_JGameplayTags& Tags = FProject_JGameplayTags::Get();
	if (ASC->HasMatchingGameplayTag(Tags.State_Dead)) return 0.25f;
	if (ASC->HasMatchingGameplayTag(Tags.State_Attacking)
		|| ASC->HasMatchingGameplayTag(Tags.State_Dodging)
		|| ASC->HasMatchingGameplayTag(Tags.State_HitReacting))
	{
		return 4.0f;
	}
	if (ASC->HasMatchingGameplayTag(Tags.State_CombatMode)) return 2.0f;
	return 1.0f;
}
}

const FName UProject_JCrowdDensityNetObjectFilter::FilterName_CrowdDensity(TEXT("ProjectJCrowdDensity"));

void FProject_JCrowdDensityBudgetSolver::EvaluateForViewer(
	const FVector& ViewerLocation,
	TConstArrayView<FProject_JCrowdDensityCandidate> Candidates,
	const TSet<uint32>& PreviouslyAllowed,
	int32 MaxNonExemptAllowed,
	float HysteresisBonus,
	TSet<uint32>& OutAllowed,
	int32& OutCulledCount)
{
	OutAllowed.Reset();
	OutCulledCount = 0;

	struct FScoredCandidate
	{
		uint32 ObjectId = 0;
		double Score = 0.0;
	};
	TArray<FScoredCandidate, TInlineAllocator<128>> Ranked;
	Ranked.Reserve(FMath::Min(Candidates.Num(), FMath::Max(0, MaxNonExemptAllowed)));
	const auto IsWorse = [](const FScoredCandidate& A, const FScoredCandidate& B)
	{
		return A.Score == B.Score ? A.ObjectId > B.ObjectId : A.Score < B.Score;
	};

	const double SafeBonus = FMath::Max(1.0f, HysteresisBonus);
	for (const FProject_JCrowdDensityCandidate& Cand : Candidates)
	{
		if (Cand.bAlwaysExempt)
		{
			OutAllowed.Add(Cand.ObjectId);
			continue;
		}
		const double DistSq = FVector::DistSquared(ViewerLocation, Cand.Location);
		if (Cand.MaxCullDistanceSq > 0.0 && DistSq > Cand.MaxCullDistanceSq)
		{
			++OutCulledCount;
			continue;
		}
		if (MaxNonExemptAllowed <= 0)
		{
			OutAllowed.Add(Cand.ObjectId);
			continue;
		}
		double Score = static_cast<double>(FMath::Max(0.05f, Cand.CombatWeight)) / (DistSq + 10000.0);
		if (PreviouslyAllowed.Contains(Cand.ObjectId))
		{
			Score *= SafeBonus;
		}
		const FScoredCandidate Candidate{Cand.ObjectId, Score};
		if (Ranked.Num() < MaxNonExemptAllowed)
		{
			Ranked.HeapPush(Candidate, IsWorse);
		}
		else
		{
			++OutCulledCount;
			if (IsWorse(Ranked.HeapTop(), Candidate))
			{
				Ranked.HeapPopDiscard(IsWorse, EAllowShrinking::No);
				Ranked.HeapPush(Candidate, IsWorse);
			}
		}
	}
	for (const FScoredCandidate& Item : Ranked) OutAllowed.Add(Item.ObjectId);
}

bool UProject_JCrowdDensityNetObjectFilter::IsCrowdDensityFilterEnabled()
{
	return CVarEnableIrisCrowdDensityFilter.GetValueOnAnyThread() != 0;
}

int32 UProject_JCrowdDensityNetObjectFilter::GetConfiguredBaseCrowdCap()
{
	return FMath::Max(0, CVarMaxVisibleRemoteActors.GetValueOnAnyThread());
}

FProject_JCrowdDensityStats UProject_JCrowdDensityNetObjectFilter::GetLatestStats(const UWorld* World)
{
	check(IsInGameThread());
	const auto* Driver = World ? World->GetNetDriver() : nullptr;
	auto* System = Driver ? Driver->GetReplicationSystem() : nullptr;
	const auto* Filter = System ? Cast<UProject_JCrowdDensityNetObjectFilter>(System->GetFilter(FilterName_CrowdDensity)) : nullptr;
	return Filter ? Filter->LatestStats : FProject_JCrowdDensityStats{};
}

bool UProject_JCrowdDensityNetObjectFilter::AssignActorToCrowdFilter(AActor* TargetActor)
{
	check(IsInGameThread());
	if (!IsCrowdDensityFilterEnabled() || !IsValid(TargetActor) || TargetActor->IsActorBeingDestroyed()
		|| !TargetActor->HasAuthority() || TargetActor->bAlwaysRelevant || TargetActor->bOnlyRelevantToOwner)
	{
		return false;
	}

	UWorld* World = TargetActor->GetWorld();
	UNetDriver* NetDriver = World && !World->bIsTearingDown ? TargetActor->GetNetDriver() : nullptr;
	UReplicationSystem* RepSys = NetDriver ? NetDriver->GetReplicationSystem() : nullptr;
	if (!IsValid(RepSys) || !RepSys->IsServer())
	{
		return false;
	}
	UObjectReplicationBridge* RepBridge = Cast<UObjectReplicationBridge>(RepSys->GetReplicationBridge());
	if (!IsValid(RepBridge))
	{
		return false;
	}
	const UE::Net::FNetRefHandle Handle = RepBridge->GetReplicatedRefHandle(TargetActor);
	if (!Handle.IsCompleteHandle())
	{
		return false;
	}
	const UE::Net::FNetObjectFilterHandle FilterHandle = RepSys->GetFilterHandle(FilterName_CrowdDensity);
	if (FilterHandle == UE::Net::InvalidNetObjectFilterHandle)
	{
		return false;
	}
	if (!RepSys->SetFilter(Handle, FilterHandle))
	{
		return false;
	}
	if (auto* FilterObj = Cast<UProject_JCrowdDensityNetObjectFilter>(RepSys->GetFilter(FilterName_CrowdDensity)))
	{
		FilterObj->RegisterTrackedActor(Handle, TargetActor);
	}
	return true;
}

void UProject_JCrowdDensityNetObjectFilter::RegisterTrackedActor(UE::Net::FNetRefHandle Handle, AActor* Actor)
{
	if (!IsValid(Actor) || !Handle.IsCompleteHandle()) return;
	const UE::Net::FInternalNetRefIndex ObjectIndex = GetObjectIndex(Handle);
	if (ObjectIndex != UE::Net::InvalidInternalNetRefIndex)
	{
		TrackedActors.Add(ObjectIndex, Actor);
	}
}

void UProject_JCrowdDensityNetObjectFilter::OnInit(const FNetObjectFilterInitParams& Params)
{
	AddFilterTraits(ENetFilterTraits::Spatial);
	ReplicationSystem = Params.ReplicationSystem;
	Bridge = ReplicationSystem ? Cast<UObjectReplicationBridge>(ReplicationSystem->GetReplicationBridge()) : nullptr;
	FilterConfig = Cast<UProject_JCrowdDensityNetObjectFilterConfig>(Params.Config);
	PreviousAllowedByConnection.SetNum(static_cast<int32>(Params.MaxConnectionCount) + 1);
}

void UProject_JCrowdDensityNetObjectFilter::OnDeinit()
{
	TrackedActors.Reset();
	FrameActors.Reset();
	PreviousAllowedByConnection.Reset();
	ScratchCandidates.Reset();
	ScratchAllowed.Reset();
	Bridge = nullptr;
	ReplicationSystem = nullptr;
	FilterConfig = nullptr;
}

void UProject_JCrowdDensityNetObjectFilter::OnMaxInternalNetRefIndexIncreased(UE::Net::FInternalNetRefIndex)
{
}

void UProject_JCrowdDensityNetObjectFilter::AddConnection(uint32 ConnectionId)
{
	const int32 Idx = static_cast<int32>(ConnectionId);
	if (Idx >= PreviousAllowedByConnection.Num())
	{
		PreviousAllowedByConnection.SetNum(Idx + 1);
	}
	PreviousAllowedByConnection[Idx].Reset();
}

void UProject_JCrowdDensityNetObjectFilter::RemoveConnection(uint32 ConnectionId)
{
	const int32 Idx = static_cast<int32>(ConnectionId);
	if (PreviousAllowedByConnection.IsValidIndex(Idx))
	{
		PreviousAllowedByConnection[Idx].Reset();
	}
}

bool UProject_JCrowdDensityNetObjectFilter::AddObject(UE::Net::FInternalNetRefIndex ObjectIndex, FNetObjectFilterAddObjectParams&)
{
	TrackedActors.FindOrAdd(ObjectIndex);
	return true;
}

void UProject_JCrowdDensityNetObjectFilter::RemoveObject(UE::Net::FInternalNetRefIndex ObjectIndex, const FNetObjectFilteringInfo&)
{
	TrackedActors.Remove(ObjectIndex);
	for (auto& PreviouslyAllowed : PreviousAllowedByConnection) PreviouslyAllowed.Remove(ObjectIndex);
}

void UProject_JCrowdDensityNetObjectFilter::PreFilter(FNetObjectPreFilteringParams&)
{
	FrameActors.Reset();
	PassAllowedPairs = 0;
	PassCulledPairs = 0;

	const float DefaultDist = FilterConfig ? FilterConfig->DefaultMaxDistanceCm : 12000.0f;
	const double DefaultDistSq = static_cast<double>(DefaultDist) * static_cast<double>(DefaultDist);
	int32 BaseCap = GetConfiguredBaseCrowdCap();
	if (BaseCap == 60 && FilterConfig && FilterConfig->DefaultMaxVisibleRemoteActors > 0)
	{
		BaseCap = FilterConfig->DefaultMaxVisibleRemoteActors;
	}

	ActiveEffectiveCap = IsCrowdDensityFilterEnabled() ? BaseCap : 0;
	UWorld* SampleWorld = nullptr;

	for (auto It = TrackedActors.CreateIterator(); It; ++It)
	{
		AActor* Actor = It.Value().Get();
		if (!IsValid(Actor) || Actor->IsActorBeingDestroyed())
		{
			continue;
		}
		if (!SampleWorld) SampleWorld = Actor->GetWorld();
		FPreFilteredActor Entry;
		Entry.ObjectIndex = It.Key();
		Entry.Actor = Actor;
		Entry.Location = Actor->GetActorLocation();
		const double ActorCullSq = static_cast<double>(Actor->GetNetCullDistanceSquared());
		Entry.MaxCullDistSq = (ActorCullSq > 0.0) ? FMath::Min(ActorCullSq, DefaultDistSq) : DefaultDistSq;
		Entry.OwnerConnection = Actor->GetNetConnection();
		Entry.bAlwaysRelevant = Actor->bAlwaysRelevant;
		Entry.bOnlyRelevantToOwner = Actor->bOnlyRelevantToOwner;
		Entry.bIsBoss = IsBossEntity(Actor);
		Entry.CombatWeight = ComputeActorCombatWeight(Actor);
		if (const UObject* Social = ResolveSocialObject(Actor))
		{
			Entry.PartyId = IProject_JSocialStateInterface::Execute_GetPartyId(Social);
			Entry.GuildId = IProject_JSocialStateInterface::Execute_GetGuildId(Social);
		}
		FrameActors.Add(MoveTemp(Entry));
	}

	if (SampleWorld)
	{
		if (UGameInstance* GI = SampleWorld->GetGameInstance())
		{
			if (const auto* Gov = GI->GetSubsystem<UProject_JServerLoadGovernorSubsystem>())
			{
				ActiveEffectiveCap = Gov->GetEffectiveCrowdCap(ActiveEffectiveCap);
			}
		}
	}
}

void UProject_JCrowdDensityNetObjectFilter::Filter(FNetObjectFilteringParams& Params)
{
	Params.OutAllowedObjects.ClearAllBits();

	// Resolve viewer connection identity for Owner/Party/Guild exemptions.
	const UNetConnection* Connection = ReplicationSystem
		? Cast<UNetConnection>(ReplicationSystem->GetConnectionUserData(Params.ConnectionId)) : nullptr;
	const APlayerController* ViewerPC = Connection ? Connection->PlayerController.Get() : nullptr;
	const APawn* ViewerPawn = ViewerPC ? ViewerPC->GetPawn() : nullptr;
	FName ViewerPartyId = NAME_None;
	FName ViewerGuildId = NAME_None;
	if (const UObject* ViewerSocial = ResolveSocialObject(ViewerPC ? static_cast<const AActor*>(ViewerPC) : static_cast<const AActor*>(ViewerPawn)))
	{
		ViewerPartyId = IProject_JSocialStateInterface::Execute_GetPartyId(ViewerSocial);
		ViewerGuildId = IProject_JSocialStateInterface::Execute_GetGuildId(ViewerSocial);
	}

	FVector ViewerLocation = FVector::ZeroVector;
	if (!Params.View.Views.IsEmpty())
	{
		ViewerLocation = Params.View.Views[0].Pos;
	}
	else if (IsValid(ViewerPawn))
	{
		ViewerLocation = ViewerPawn->GetActorLocation();
	}

	ScratchCandidates.Reset(FrameActors.Num());
	for (const FPreFilteredActor& Entry : FrameActors)
	{
		const AActor* Actor = Entry.Actor.Get();
		if (!IsValid(Actor)) continue;

		FProject_JCrowdDensityCandidate Cand;
		Cand.ObjectId = Entry.ObjectIndex;
		Cand.Location = Entry.Location;
		Cand.MaxCullDistanceSq = Entry.MaxCullDistSq;
		Cand.CombatWeight = Entry.CombatWeight;

		const bool bOwner = (Actor == ViewerPawn) || (Connection && Entry.OwnerConnection.Get() == Connection);
		if (Entry.bOnlyRelevantToOwner && !bOwner) continue;
		const bool bSameParty = !ViewerPartyId.IsNone() && ViewerPartyId == Entry.PartyId;
		const bool bSameGuild = !ViewerGuildId.IsNone() && ViewerGuildId == Entry.GuildId;
		Cand.bAlwaysExempt = bOwner || Entry.bIsBoss || bSameParty || bSameGuild || Entry.bAlwaysRelevant;
		ScratchCandidates.Add(Cand);
	}

	const int32 ConnIdx = static_cast<int32>(Params.ConnectionId);
	static const TSet<uint32> EmptyPrev;
	const TSet<uint32>& PrevAllowed = PreviousAllowedByConnection.IsValidIndex(ConnIdx) ? PreviousAllowedByConnection[ConnIdx] : EmptyPrev;
	const float Hysteresis = FilterConfig ? FilterConfig->HysteresisRetentionBonus : 1.35f;

	int32 CulledThisConnection = 0;
	FProject_JCrowdDensityBudgetSolver::EvaluateForViewer(
		ViewerLocation, ScratchCandidates, PrevAllowed, ActiveEffectiveCap, Hysteresis, ScratchAllowed, CulledThisConnection);

	for (uint32 AllowedIdx : ScratchAllowed)
	{
		Params.OutAllowedObjects.SetBit(AllowedIdx);
	}
	if (PreviousAllowedByConnection.IsValidIndex(ConnIdx))
	{
		PreviousAllowedByConnection[ConnIdx] = ScratchAllowed;
	}

	PassAllowedPairs += ScratchAllowed.Num();
	PassCulledPairs += CulledThisConnection;
}

void UProject_JCrowdDensityNetObjectFilter::PostFilter(FNetObjectPostFilteringParams&)
{
	LatestStats.TrackedObjects = FrameActors.Num();
	LatestStats.EffectiveCap = ActiveEffectiveCap;
	LatestStats.LastPassAllowedPairs = PassAllowedPairs;
	LatestStats.LastPassCulledPairs = PassCulledPairs;
}
