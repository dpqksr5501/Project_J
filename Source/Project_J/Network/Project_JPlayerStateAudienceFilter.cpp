#include "Network/Project_JPlayerStateAudienceFilter.h"
#include "Game/Project_JPlayerState.h"
#include "Project_JAbilitySystemComponent.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Iris/ReplicationSystem/Filtering/NetObjectFilterDefinitions.h"
#include "Iris/ReplicationSystem/Filtering/ReplicationFilteringConfig.h"
#include "Iris/ReplicationSystem/ObjectReplicationBridgeConfig.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/WorldLocations.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/Iris/ReplicationSystem/EngineReplicationBridge.h"
#include "Net/Iris/ReplicationSystem/ReplicationSystemUtil.h"
#include <cmath>

namespace
{
const FName AudienceName(TEXT("ProjectJPlayerStateAudience"));

bool CurrentSystem(const AProject_JPlayerState* State, UReplicationSystem*& OutSystem, UEngineReplicationBridge*& OutBridge)
{
	if (!IsValid(State) || State->IsActorBeingDestroyed() || !State->HasAuthority()) return false;
	UWorld* World = State->GetWorld();
	if (!World || World->bIsTearingDown || !World->GetNetDriver()) return false;
	OutSystem = World->GetNetDriver()->GetReplicationSystem();
	OutBridge = UE::Net::FReplicationSystemUtil::GetActorReplicationBridge(State);
	return IsValid(OutSystem) && OutSystem->IsServer() && IsValid(OutBridge) &&
		OutSystem->GetReplicationBridge() == OutBridge;
}

bool HasApplicableForcedConfig(const APawn* Pawn)
{
	for (const auto& Config : UObjectReplicationBridgeConfig::GetConfig()->GetFilterConfigs())
	{
		if (!Config.bForceEnableOnAllInstances) continue;
		for (const UClass* Class = Pawn->GetClass(); Class; Class = Class->GetSuperClass())
			if (FName(*Class->GetPathName()) == Config.ClassName) return true;
	}
	return false;
}
}

bool UProject_JPlayerStateAudienceFilter::Enabled()
{
	return FParse::Param(FCommandLine::Get(), TEXT("ProjectJPlayerStateAudience"));
}

UProject_JPlayerStateAudienceFilter* UProject_JPlayerStateAudienceFilter::Find(UWorld* World)
{
	auto* Replication = World ? UE::Net::FReplicationSystemUtil::GetReplicationSystem(World) : nullptr;
	return Replication ? Cast<UProject_JPlayerStateAudienceFilter>(Replication->GetFilter(AudienceName)) : nullptr;
}

UProject_JPlayerStateAudienceFilter* UProject_JPlayerStateAudienceFilter::GetOrCreate(UReplicationSystem* Replication)
{
	if (!Enabled() || !Replication || !Replication->IsServer()) return nullptr;
	if (auto* Existing = Replication->GetFilter(AudienceName)) return Cast<UProject_JPlayerStateAudienceFilter>(Existing);
	FNetObjectFilterDefinition Definition;
	Definition.FilterName = AudienceName;
	Definition.ClassName = FName(*StaticClass()->GetPathName());
	Definition.ConfigClassName = FName(*UNetObjectGridFilterConfig::StaticClass()->GetPathName());
	return Cast<UProject_JPlayerStateAudienceFilter>(Replication->CreateFilter(Definition).Get());
}

bool UProject_JPlayerStateAudienceFilter::TryBind(AProject_JPlayerState* State)
{
	if (!Enabled()) return false;
	UReplicationSystem* Replication = nullptr; UEngineReplicationBridge* Bridge = nullptr;
	if (!CurrentSystem(State, Replication, Bridge)) return false;
	const auto Handle = Bridge->GetReplicatedRefHandle(State);
	if (!Handle.IsCompleteHandle() || Bridge->GetReplicatedObject(Handle) != State) return false;
	auto* Filter = GetOrCreate(Replication);
	if (!Filter) return false;
	const uint32 Index = Filter->GetObjectIndex(Handle);
	if (!Index) return false;
	FEntry& Entry = Filter->Entries.FindOrAdd(Index);
	if (Entry.State.Get() && (!UE::Net::FNetRefHandle::FullCompare(Entry.StateHandle, Handle) || Entry.State.Get() != State))
	{
		// An index must never inherit a previous object's witness or interest history.
		Entry = {};
		Entry.LatchedReasons = UnknownHistory;
	}
	Entry.State = State; Entry.StateHandle = Handle;
	if (Filter->IsObjectFiltered(Index)) return true;
	// Never wrap the serial used by creation witnesses. Exhaustion retains baseline.
	if (Filter->Stats.Assignments == MAX_uint64)
	{
		Filter->Entries.Remove(Index);
		return false;
	}
	if (Replication->SetFilter(Handle, Replication->GetFilterHandle(AudienceName)))
	{
		Filter->Entries.FindChecked(Index).AssignmentSerial = ++Filter->Stats.Assignments;
		return true;
	}
	// SetFilter failure leaves the engine assignment unspecified: explicitly restore baseline.
	Replication->SetFilter(Handle, UE::Net::InvalidNetObjectFilterHandle);
	Filter->Entries.Remove(Index);
	return false;
}

void UProject_JPlayerStateAudienceFilter::ObservePawnPreSpawn(AActor* Actor)
{
	auto* Pawn = Cast<APawn>(Actor);
	auto* Replication = System.Get();
	auto* World = WitnessWorld.Get();
	auto* Bridge = Replication ? Cast<UEngineReplicationBridge>(Replication->GetReplicationBridge()) : nullptr;
	if (!IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || !Pawn->HasAuthority() || !Pawn->GetIsReplicated() ||
		!World || World->bIsTearingDown || Pawn->GetWorld() != World || !Bridge || !World->GetNetDriver() ||
		World->GetNetDriver() != Bridge->GetNetDriver() || World->GetNetDriver()->GetReplicationSystem() != Replication) return;
	// UWorld broadcasts before PostSpawnInitialize: neither ownership nor construction
	// is ready yet. Record only the actual fresh identity; associate on the first pass.
	if (Bridge->GetReplicatedRefHandle(Pawn).IsValid() || Stats.Assignments == MAX_uint64 ||
		uint32(PawnCreations.Num()) >= ObjectCapacity)
	{
		++Stats.RejectedWitnesses;
		return;
	}
	PawnCreations.Add(Pawn, Stats.Assignments);
	++Stats.ObservedPawnCreations;
}

void UProject_JPlayerStateAudienceFilter::OnInit(const FNetObjectFilterInitParams& Params)
{
	Super::OnInit(Params);
	System = Params.ReplicationSystem;
	ObjectCapacity = Params.AbsoluteMaxNetObjectCount;
	RuntimeGridPolicy = Cast<UNetObjectGridFilterConfig>(Params.Config);
	InitialPolicy = ReadPolicy(RuntimeGridPolicy.Get());
	if (auto* Bridge = Cast<UEngineReplicationBridge>(Params.ReplicationSystem->GetReplicationBridge()))
		if (auto* Driver = Bridge->GetNetDriver())
			if (auto* World = Driver->GetWorld(); World && !World->bIsTearingDown)
			{
				WitnessWorld = World;
				PreSpawnHandle = World->AddOnActorPreSpawnInitialization(
					FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::ObservePawnPreSpawn));
			}
}

void UProject_JPlayerStateAudienceFilter::OnDeinit()
{
	const FDelegateHandle RetiringHandle = PreSpawnHandle;
	PreSpawnHandle.Reset();
	if (auto* World = WitnessWorld.Get(); World && RetiringHandle.IsValid())
		World->RemoveOnActorPreSpawnInitialization(RetiringHandle);
	WitnessWorld.Reset();
	Entries.Empty(); PawnCreations.Empty(); OwnerObjects.Empty(); FallbackObjects.Empty();
	RuntimeGridPolicy.Reset();
	System.Reset();
	Super::OnDeinit();
}

bool UProject_JPlayerStateAudienceFilter::BuildObjectInfo(uint32 Index, FNetObjectFilterAddObjectParams&)
{
	// AddObject runs before assignment is published. Unbound entries use finite placeholder bounds.
	Entries.FindOrAdd(Index).View.ObjectIndex = Index;
	return true;
}

void UProject_JPlayerStateAudienceFilter::UpdateObjectInfo(FPerObjectInfo& Info, const FObjectLocationInfo&,
	const UE::Net::FReplicationInstanceProtocol*)
{
	if (const auto* Entry = Entries.Find(Info.ObjectIndex))
	{
		Info.Position = Entry->View.Center;
		Info.SetCullDistance(Entry->View.Radius);
	}
	else
	{
		Info.Position = FVector::ZeroVector;
		Info.SetCullDistance(0);
	}
}

void UProject_JPlayerStateAudienceFilter::OnObjectRemoved(uint32 Index)
{
	Entries.Remove(Index);
	++Stats.RemovedBindings;
}

bool UProject_JPlayerStateAudienceFilter::PolicyMatches(UEngineReplicationBridge* Bridge) const
{
	const auto* ActualGridPolicy = RuntimeGridPolicy.Get();
	if (!Bridge || !System.IsValid() || !ActualGridPolicy ||
		ActualGridPolicy->GetClass() != UNetObjectGridFilterConfig::StaticClass() || !ActualGridPolicy->FilterProfiles.IsEmpty()) return false;
	// Iris allocates a config instance per filter; compare its values, never its address to the CDO.
	if (!(ReadPolicy(ActualGridPolicy) == InitialPolicy) || !(ReadPolicy() == InitialPolicy) || System->GetWorldLocations().GetWorldMinPos() != InitialPolicy.Min ||
		System->GetWorldLocations().GetWorldMaxPos() != InitialPolicy.Max) return false;
	const FName SpatialName = Bridge->GetDefaultSpatialFilterName();
	auto* Spatial = System->GetFilter(SpatialName);
	if (!Spatial || Spatial->GetClass() != UNetObjectGridWorldLocFilter::StaticClass()) return false;
	bool bDefinitionMatches = false;
	uint32 DefinitionsWithName = 0;
	for (const auto& Definition : GetDefault<UNetObjectFilterDefinitions>()->GetFilterDefinitions())
	{
		if (Definition.FilterName != SpatialName) continue;
		++DefinitionsWithName;
		bDefinitionMatches = Definition.ClassName == FName(*UNetObjectGridWorldLocFilter::StaticClass()->GetPathName()) &&
			Definition.ConfigClassName == FName(*UNetObjectGridFilterConfig::StaticClass()->GetPathName());
	}
	const auto* GridPolicy = GetDefault<UNetObjectGridFilterConfig>();
	// Effective per-object profiles are not publicly queryable. Restrict this experiment
	// to the shared defaults instead of assuming an unknown profile has matching history.
	return DefinitionsWithName == 1 && bDefinitionMatches && GridPolicy->bUseExactCullDistance && GridPolicy->FilterProfiles.IsEmpty() &&
		GetDefault<UReplicationFilteringConfig>()->GetHysteresisProfiles().IsEmpty() &&
		FMath::IsFinite(GridPolicy->CellSizeX) && GridPolicy->CellSizeX > 0 &&
		FMath::IsFinite(GridPolicy->CellSizeY) && GridPolicy->CellSizeY > 0;
}

bool UProject_JPlayerStateAudienceFilter::PrivateBootstrap(const APawn* Pawn, UEngineReplicationBridge* Bridge) const
{
	if (!IsValid(Pawn) || Pawn->GetWorld() != Bridge->GetNetDriver()->GetWorld() || !Pawn->bOnlyRelevantToOwner ||
		Pawn->bAlwaysRelevant || Pawn->bNetUseOwnerRelevancy || HasApplicableForcedConfig(Pawn)) return false;
	FActorReplicationParams Params;
	AActor::FFillReplicationParamsContext Context;
	const_cast<APawn*>(Pawn)->FillReplicationParams(Context, Params);
	return Params.FilterType == FActorReplicationParams::ConfigFilter;
}

bool UProject_JPlayerStateAudienceFilter::ReadSource(APawn* Pawn, UEngineReplicationBridge* Bridge,
	UE::Net::FNetRefHandle& OutHandle, uint32& OutIndex, FVector& OutPosition, float& OutRadius) const
{
	if (!IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || !Pawn->HasAuthority() || !Pawn->GetIsReplicated() ||
		UE::Net::FReplicationSystemUtil::GetActorReplicationBridge(Pawn) != Bridge) return false;
	OutHandle = Bridge->GetReplicatedRefHandle(Pawn);
	if (!OutHandle.IsCompleteHandle() || Bridge->GetReplicatedObject(OutHandle) != Pawn) return false;
	OutIndex = GetObjectIndex(OutHandle);
	if (!OutIndex) return false;
	auto* Spatial = System->GetFilter(Bridge->GetDefaultSpatialFilterName());
	const auto Members = Spatial->GetFilteredObjects();
	if (OutIndex >= Members.GetNumBits() || !Members.IsBitSet(OutIndex)) return false;
	const auto Cached = System->GetWorldLocations().GetWorldInfo(OutIndex);
	if (!Cached || !System->GetWorldLocations().IsValidLocation(Cached->WorldLocation)) return false;
	OutPosition = Cached->WorldLocation; OutRadius = Cached->CullDistance;
	return true;
}

bool UProject_JPlayerStateAudienceFilter::BuildConservativeBound(const FVector& PawnPosition, float PawnRadius,
	const FVector& AvatarPosition, float AvatarRadius, float MaxRadius, FVector& OutCenter, float& OutRadius)
{
	OutCenter = FVector::ZeroVector; OutRadius = 0;
	if (PawnPosition.ContainsNaN() || AvatarPosition.ContainsNaN() ||
		!FMath::IsFinite(PawnRadius) || PawnRadius < 0 || !FMath::IsFinite(AvatarRadius) || AvatarRadius < 0 ||
		!FMath::IsFinite(MaxRadius) || MaxRadius <= 0) return false;
	const float PawnSq = PawnRadius * PawnRadius, AvatarSq = AvatarRadius * AvatarRadius;
	if (!FMath::IsFinite(PawnSq) || !FMath::IsFinite(AvatarSq)) return false;
	const double PawnExtent = FMath::Max(double(PawnRadius), std::sqrt(double(PawnSq)));
	const double AvatarExtent = FMath::Max(double(AvatarRadius), std::sqrt(double(AvatarSq)));
	const double Required = FMath::Max(PawnExtent, FVector::Distance(PawnPosition, AvatarPosition) + AvatarExtent);
	if (!FMath::IsFinite(Required) || Required > MaxRadius) return false;
	// Expand even exact casts: double distance/addition and float square both round.
	float Radius = std::nextafter(float(Required), INFINITY);
	while (FMath::IsFinite(Radius) && double(Radius * Radius) < Required * Required)
		Radius = std::nextafter(Radius, INFINITY);
	if (!FMath::IsFinite(Radius) || !FMath::IsFinite(Radius * Radius) || Radius > MaxRadius) return false;
	OutCenter = PawnPosition; OutRadius = Radius;
	return true;
}

void UProject_JPlayerStateAudienceFilter::RefreshEntry(uint32 Index, FEntry& Entry)
{
	Entry.View = {}; Entry.View.ObjectIndex = Index; Entry.View.bAssigned = IsObjectFiltered(Index);
	auto* State = Entry.State.Get();
	UReplicationSystem* Replication = nullptr; UEngineReplicationBridge* Bridge = nullptr;
	if (!CurrentSystem(State, Replication, Bridge) || Replication != System.Get() ||
		!UE::Net::FNetRefHandle::FullCompare(Bridge->GetReplicatedRefHandle(State), Entry.StateHandle) ||
		Bridge->GetReplicatedObject(Entry.StateHandle) != State)
	{
		Entry.LatchedReasons |= StaleLease;
		Entry.View.FallbackReasons = Entry.LatchedReasons;
		return;
	}
	Entry.View.OwnerConnectionId = Replication->GetOwningNetConnection(Entry.StateHandle);
	uint32 Reasons = Entry.LatchedReasons;
	if (!Entry.View.OwnerConnectionId) Reasons |= MissingOwner;
	if (!bCurrentPolicyMatches)
	{
		Entry.LatchedReasons |= UnsupportedPolicy;
		Entry.View.FallbackReasons = Reasons | UnsupportedPolicy;
		return;
	}
	APawn* Pawn = State->GetPawn();
	auto* ASC = State->GetProjectJAbilitySystemComponent();
	APawn* Avatar = ASC && ASC->GetOwnerActor() == State ? Cast<APawn>(ASC->GetAvatarActor()) : nullptr;
	const auto* SpawnFence = PawnCreations.Find(Pawn);
	const bool bFreshLease = SpawnFence && Entry.AssignmentSerial > 0 && Entry.AssignmentSerial <= *SpawnFence;
	const bool bReciprocal = IsValid(Pawn) && Pawn->GetPlayerState() == State;
	const auto PublicRegistered = [&](APawn* Candidate)
	{
		return IsValid(Candidate) && Bridge->GetReplicatedRefHandle(Candidate).IsValid() && !PrivateBootstrap(Candidate, Bridge);
	};
	// A fresh handle first seen during an incomplete association already participated
	// in a real filtering pass. A later actor-info repair cannot retroactively prove freshness.
	if (!Entry.bEstablished && !(bFreshLease && bReciprocal && Avatar == Pawn) &&
		(PublicRegistered(Pawn) || PublicRegistered(Avatar)))
		Entry.LatchedReasons |= UnknownHistory;
	if (!IsValid(Pawn) || !IsValid(Avatar) || !bReciprocal || Pawn->GetWorld() != State->GetWorld() || Avatar->GetWorld() != State->GetWorld())
	{
		if (Entry.bEstablished) Entry.LatchedReasons |= RetiredSource;
		Entry.View.FallbackReasons = Reasons | Entry.LatchedReasons | MissingPawn;
		return;
	}
	// The existing private selection/spectator Pawn has no foreign spatial history.
	if (!Entry.bEstablished && PrivateBootstrap(Pawn, Bridge) && PrivateBootstrap(Avatar, Bridge))
	{
		Entry.View.FallbackReasons = Reasons | Entry.LatchedReasons | MissingPawn;
		return;
	}
	UE::Net::FNetRefHandle PawnHandle, AvatarHandle;
	FVector PawnPosition, AvatarPosition; float PawnRadius = 0, AvatarRadius = 0;
	const bool bComplete = ReadSource(Pawn, Bridge, PawnHandle, Entry.View.PawnIndex, PawnPosition, PawnRadius) &&
		ReadSource(Avatar, Bridge, AvatarHandle, Entry.View.AvatarIndex, AvatarPosition, AvatarRadius);
	if (!Entry.bEstablished)
	{
		const bool bRegistered = Bridge->GetReplicatedRefHandle(Pawn).IsValid() || Bridge->GetReplicatedRefHandle(Avatar).IsValid();
		if (bComplete && bFreshLease && bReciprocal && Avatar == Pawn)
		{
			Entry.bEstablished = true; Entry.Pawn = Pawn; Entry.Avatar = Avatar;
			Entry.PawnHandle = PawnHandle; Entry.AvatarHandle = AvatarHandle;
			PawnCreations.Remove(Pawn); ++Stats.ConsumedPawnCreations;
		}
		else if (bRegistered) Entry.LatchedReasons |= UnknownHistory;
	}
	else if (!bComplete || Entry.Pawn.Get() != Pawn || Entry.Avatar.Get() != Avatar ||
		!UE::Net::FNetRefHandle::FullCompare(Entry.PawnHandle, PawnHandle) ||
		!UE::Net::FNetRefHandle::FullCompare(Entry.AvatarHandle, AvatarHandle)) Entry.LatchedReasons |= RetiredSource;
	Reasons |= Entry.LatchedReasons;
	if (!bComplete || !Entry.bEstablished)
	{
		Entry.View.FallbackReasons = Reasons | MissingPawn;
		return;
	}
	if (!BuildConservativeBound(PawnPosition, PawnRadius, AvatarPosition, AvatarRadius,
		GetDefault<UWorldLocationsConfig>()->MaxNetCullDistance, Entry.View.Center, Entry.View.Radius))
	{
		Entry.LatchedReasons |= InvalidBounds;
		Entry.View.FallbackReasons = Reasons | InvalidBounds;
		return;
	}
	Entry.View.FallbackReasons = Reasons;
	Entry.View.bSpatial = Reasons == 0;
}

void UProject_JPlayerStateAudienceFilter::PreFilter(FNetObjectPreFilteringParams& Params)
{
	// WorldLoc's normal implementation tracks the PS transform. We provide Avatar bounds instead.
	UNetObjectGridFilter::PreFilter(Params);
	++Stats.PreFilterPasses; Stats.SpatialBindings = 0; Stats.FallbackBindings = 0;
	for (auto& Pair : OwnerObjects) Pair.Value.Reset();
	FallbackObjects.Reset();
	bCurrentPolicyMatches = PolicyMatches(System.IsValid() ? Cast<UEngineReplicationBridge>(System->GetReplicationBridge()) : nullptr);
	auto* Bridge = System.IsValid() ? Cast<UEngineReplicationBridge>(System->GetReplicationBridge()) : nullptr;
	for (auto& Pair : Entries)
	{
		const uint32 Index = Pair.Key;
		FEntry& Entry = Pair.Value;
		RefreshEntry(Index, Entry);
		if (Entry.View.OwnerConnectionId) OwnerObjects.FindOrAdd(Entry.View.OwnerConnectionId).Add(Index);
		if (Entry.View.FallbackReasons) { FallbackObjects.Add(Index); ++Stats.FallbackBindings; }
		else ++Stats.SpatialBindings;
		if (auto* Info = GetFilteringInfo(Index))
			UpdateCellInfoForObject(static_cast<const FObjectLocationInfo&>(*Info), nullptr);
	}
	for (auto It = PawnCreations.CreateIterator(); It; ++It)
	{
		auto* Pawn = It.Key().Get();
		if (!IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || !Bridge || Pawn->GetWorld() != WitnessWorld.Get())
		{ It.RemoveCurrent(); continue; }
		if (Bridge->GetReplicatedRefHandle(Pawn).IsValid())
		{
			// Any unconsumed registered source can now have grid history. A later
			// possession, actor-info repair or PS registration cannot reuse it as fresh.
			++Stats.RejectedWitnesses;
			It.RemoveCurrent();
		}
	}
	Stats.PendingWitnesses = PawnCreations.Num();
}

void UProject_JPlayerStateAudienceFilter::RemoveConnection(uint32 ConnectionId)
{
	OwnerObjects.Remove(ConnectionId);
	Super::RemoveConnection(ConnectionId);
}

void UProject_JPlayerStateAudienceFilter::Filter(FNetObjectFilteringParams& Params)
{
	Super::Filter(Params);
	// No dense PS x connection scan. Sparse grid history stays owned by Iris.
	for (uint32 Index : FallbackObjects) Params.OutAllowedObjects.SetBit(Index);
	if (const auto* Owned = OwnerObjects.Find(Params.ConnectionId))
		for (uint32 Index : *Owned) Params.OutAllowedObjects.SetBit(Index);
}

bool UProject_JPlayerStateAudienceFilter::QueryBinding(const AProject_JPlayerState* State, FProjectJPlayerStateAudienceBinding& Out) const
{
	Out = {};
	UReplicationSystem* Replication = nullptr; UEngineReplicationBridge* Bridge = nullptr;
	if (!CurrentSystem(State, Replication, Bridge) || Replication != System.Get()) return false;
	const auto Handle = Bridge->GetReplicatedRefHandle(State);
	if (!Handle.IsCompleteHandle()) return false;
	const uint32 Index = GetObjectIndex(Handle);
	const auto* Entry = Entries.Find(Index);
	if (Entry && Entry->State.Get() == State && UE::Net::FNetRefHandle::FullCompare(Entry->StateHandle, Handle))
	{
		Out = Entry->View; Out.bAssigned = IsObjectFiltered(Index);
		return true;
	}
	return false;
}

UProject_JPlayerStateAudienceFilter::FPolicyState UProject_JPlayerStateAudienceFilter::ReadPolicy(const UNetObjectGridFilterConfig* GridPolicy)
{
	const auto* Grid = GridPolicy ? GridPolicy : GetDefault<UNetObjectGridFilterConfig>();
	const auto* World = GetDefault<UWorldLocationsConfig>();
	const auto* Scope = GetDefault<UReplicationFilteringConfig>();
	FPolicyState Result;
	Result.Min = World->MinPos; Result.Max = World->MaxPos; Result.MaxCull = World->MaxNetCullDistance;
	Result.CellX = Grid->CellSizeX; Result.CellY = Grid->CellSizeY;
	Result.ViewFrames = Grid->ViewPosRelevancyFrameCount; Result.CullFrames = Grid->DefaultFrameCountBeforeCulling;
	Result.bExact = Grid->bUseExactCullDistance; Result.bHysteresis = Scope->IsObjectScopeHysteresisEnabled();
	Result.HysteresisFrames = Scope->GetDefaultHysteresisFrameCount();
	Result.HysteresisStride = Scope->GetHysteresisUpdateConnectionThrottling();
	return Result;
}

bool UProject_JPlayerStateAudienceFilter::FPolicyState::operator==(const FPolicyState& Other) const
{
	return Min == Other.Min && Max == Other.Max && MaxCull == Other.MaxCull && CellX == Other.CellX && CellY == Other.CellY &&
		ViewFrames == Other.ViewFrames && CullFrames == Other.CullFrames && bExact == Other.bExact && bHysteresis == Other.bHysteresis &&
		HysteresisFrames == Other.HysteresisFrames && HysteresisStride == Other.HysteresisStride;
}

FString UProject_JPlayerStateAudienceFilter::DescribeFallback(uint32 Reasons)
{
	FString Result;
	const auto Add = [&](uint32 Bit, const TCHAR* Text) { if (Reasons & Bit) { if (!Result.IsEmpty()) Result += TEXT("|"); Result += Text; } };
	Add(Unbound, TEXT("Unbound")); Add(MissingPawn, TEXT("MissingPawn")); Add(MissingOwner, TEXT("MissingOwner"));
	Add(UnknownHistory, TEXT("UnknownHistory")); Add(RetiredSource, TEXT("RetiredSource"));
	Add(UnsupportedPolicy, TEXT("UnsupportedPolicy")); Add(InvalidBounds, TEXT("InvalidBounds")); Add(StaleLease, TEXT("StaleLease"));
	return Result.IsEmpty() ? TEXT("None") : Result;
}
