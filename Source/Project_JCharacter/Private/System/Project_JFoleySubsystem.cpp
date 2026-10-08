#include "System/Project_JFoleySubsystem.h"

#include "Audio/Project_JFoleyAudioProfile.h"
#include "AudioDevice.h"
#include "Components/Project_JFoleyComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundConcurrency.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJFoley, Log, All);

namespace
{
TAutoConsoleVariable<int32> Enabled(TEXT("ProjectJ.Foley.Enabled"), 1, TEXT("Client Foley admission; 0 drops all events."));
TAutoConsoleVariable<int32> OtherCharacters(TEXT("ProjectJ.Foley.OtherCharacters"), 1, TEXT("Include nearby remote players and NPCs."));
TAutoConsoleVariable<int32> RemotePerFrame(TEXT("ProjectJ.Foley.RemotePerFrame"), 6, TEXT("Remote contact/replay operations per post-actor pass."));
TAutoConsoleVariable<float> RemotePerSecond(TEXT("ProjectJ.Foley.RemotePerSecond"), 60.f, TEXT("Remote admission token rate, independent of rendering FPS."));
TAutoConsoleVariable<float> MaxAge(TEXT("ProjectJ.Foley.MaxEventAge"), 0.15f, TEXT("Discard aged semantic events and queued contacts (seconds)."));
TAutoConsoleVariable<float> WorkMilliseconds(TEXT("ProjectJ.Foley.WorkMilliseconds"), 0.75f, TEXT("Soft GT budget; checked between accepted contacts. Does not preempt a trace."));
FAutoConsoleCommandWithWorld Status(TEXT("ProjectJ.Foley.Status"), TEXT("Report listener-side Foley admission, drops and traces."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (const auto* Service = World ? World->GetSubsystem<UProject_JFoleySubsystem>() : nullptr)
		{
			const auto& S = Service->GetStats();
			UE_LOG(LogProjectJFoley, Display,
				TEXT("World=%s Pending=%d Submitted=%llu Played=%llu Distance=%llu NotReady=%llu Duplicate=%llu Capacity=%llu Expired=%llu Budget=%llu Traces=%llu Unmapped=%llu Fallbacks=%llu Loops=%llu LastMs=%.3f"),
				*World->GetName(), S.Pending, S.Submitted, S.Played, S.DistanceRejected, S.NotReady,
				S.DuplicateRejected, S.CapacityRejected, S.Expired, S.BudgetDropped, S.SurfaceTraces,
				S.Unmapped, S.EventFallbacks, S.LoopRejected, S.LastMilliseconds);
		}
	}));
}

bool UProject_JFoleySubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
	return Type == EWorldType::Game || Type == EWorldType::PIE;
}

bool UProject_JFoleySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->GetNetMode() != NM_DedicatedServer;
}

void UProject_JFoleySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PostActorTickHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(this, &ThisClass::OnPostActorTick);
	// Two shared groups protect the owner's contacts from crowd voice stealing.
	// PlaySoundAtLocation uses active sounds directly: no project-owned AudioComponent pool.
	LocalConcurrency = NewObject<USoundConcurrency>(this);
	LocalConcurrency->Concurrency.MaxCount = 4;
	LocalConcurrency->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::StopOldest;
	LocalConcurrency->Concurrency.VoiceStealReleaseTime = 0.03f;
	RemoteConcurrency = NewObject<USoundConcurrency>(this);
	RemoteConcurrency->Concurrency.MaxCount = 16;
	RemoteConcurrency->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::StopQuietest;
	RemoteConcurrency->Concurrency.VoiceStealReleaseTime = 0.05f;
}

bool UProject_JFoleySubsystem::ValidateEvent(const FProject_JFoleyEvent& Event, float AgeLimit)
{
	return Event.Event.IsValid() && uint8(Event.Side) <= uint8(EProject_JFoleySide::Right) &&
		FMath::IsFinite(Event.VolumeMultiplier) && Event.VolumeMultiplier > 0 &&
		FMath::IsFinite(Event.PitchMultiplier) && Event.PitchMultiplier > 0 &&
		FMath::IsFinite(Event.AgeSeconds) && Event.AgeSeconds >= 0 && Event.AgeSeconds <= AgeLimit;
}

float UProject_JFoleySubsystem::ScoreEvent(float Importance, float DistanceSquared, float Radius)
{
	const float SafeImportance = FMath::IsFinite(Importance) ? FMath::Clamp(Importance, 0.1f, 4.0f) : 1.0f;
	return SafeImportance / (1.0f + 4.0f * DistanceSquared / FMath::Square(Radius));
}

bool UProject_JFoleySubsystem::HigherPriority(const FRequest& A, const FRequest& B)
{
	if (A.bLocal != B.bLocal) { return A.bLocal; }
	if (A.Score != B.Score) { return A.Score > B.Score; }
	return A.Order < B.Order;
}

TArray<UProject_JFoleySubsystem::FRequest> UProject_JFoleySubsystem::SelectBatch(TArray<FRequest>& Requests,
	double Now, float AgeLimit, int32 LocalLimit, int32 RemoteLimit, uint64& OutExpired, uint64& OutDropped)
{
	OutExpired += Requests.RemoveAll([=](const FRequest& R)
	{
		return Now < R.SubmittedAt || Now - R.SubmittedAt + R.Event.AgeSeconds > AgeLimit;
	});
	Requests.Sort(&HigherPriority);
	TArray<FRequest> Selected;
	TSet<TWeakObjectPtr<UProject_JFoleyComponent>> RemoteEmitters;
	int32 LocalCount = 0, RemoteCount = 0;
	for (const FRequest& Request : Requests)
	{
		// A single noisy emitter cannot consume every crowd slot in this frame.
		if (!Request.bLocal && RemoteEmitters.Contains(Request.Component)) { ++OutDropped; continue; }
		int32& Count = Request.bLocal ? LocalCount : RemoteCount;
		if (Count < (Request.bLocal ? LocalLimit : RemoteLimit))
		{
			Selected.Add(Request); ++Count;
			if (!Request.bLocal) { RemoteEmitters.Add(Request.Component); }
		}
		else { ++OutDropped; }
	}
	Requests.Reset(); // Never drain a backlog over subsequent frames.
	return Selected;
}

void UProject_JFoleySubsystem::RefreshListeners()
{
	if (ListenerFrame == GFrameCounter) { return; }
	ListenerFrame = GFrameCounter;
	ListenerPositions.Reset();
	// World-local PCs avoid accidentally hearing another PIE world's AudioDevice listener.
	for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalController()) { continue; }
		FVector Position, Front, Right;
		if (!PC->GetAudioListenerAttenuationOverridePosition(Position))
		{
			PC->GetAudioListenerPosition(Position, Front, Right);
		}
		if (!Position.ContainsNaN()) { ListenerPositions.Add(Position); }
	}
}

bool UProject_JFoleySubsystem::GetListenerDistanceSquared(const FVector& Location, float& OutDistanceSquared)
{
	if (Location.ContainsNaN()) { return false; }
	RefreshListeners();
	OutDistanceSquared = MAX_flt;
	for (const FVector& Position : ListenerPositions)
	{
		OutDistanceSquared = FMath::Min(OutDistanceSquared, float(FVector::DistSquared(Location, Position)));
	}
	return !ListenerPositions.IsEmpty();
}

UProject_JFoleyAudioProfile* UProject_JFoleySubsystem::ResolveReadyProfile(const FSoftObjectPath& Path)
{
	if (!Path.IsValid() || bStopped) { return nullptr; }
	FResources* Entry = Resources.Find(Path);
	if (!Entry)
	{
		if (Resources.Num() >= MaxProfiles)
		{
			FSoftObjectPath Oldest;
			double OldestTime = GetWorld()->GetTimeSeconds() - 2.0;
			for (const auto& Pair : Resources)
			{
				if (Pair.Value.LastUsed < OldestTime) { Oldest = Pair.Key; OldestTime = Pair.Value.LastUsed; }
			}
			if (!Oldest.IsValid()) { return nullptr; }
			FResources Evicted;
			Resources.RemoveAndCopyValue(Oldest, Evicted);
			if (Evicted.ProfileLoad) { Evicted.ProfileLoad->CancelHandle(); }
			if (Evicted.SoundsLoad) { Evicted.SoundsLoad->CancelHandle(); }
			LoadedProfiles.Remove(Oldest); FallbackAttenuation.Remove(Oldest);
		}
		Entry = &Resources.Add(Path);
		Entry->LastUsed = GetWorld()->GetTimeSeconds();
		if (Path.ResolveObject()) { PrepareProfile(Path); }
		else
		{
			Entry->ProfileLoad = UAssetManager::GetStreamableManager().RequestAsyncLoad(Path,
				FStreamableDelegate::CreateWeakLambda(this, [this, Path] { PrepareProfile(Path); }));
		}
	}
	Entry = Resources.Find(Path);
	Entry->LastUsed = GetWorld()->GetTimeSeconds();
	return LoadedProfiles.FindRef(Path);
}

void UProject_JFoleySubsystem::PrepareProfile(const FSoftObjectPath& Path)
{
	FResources* Entry = Resources.Find(Path);
	if (bStopped || !Entry || Entry->bPrepared || !GetWorld() || GetWorld()->bIsTearingDown ||
		GetWorld()->GetNetMode() == NM_DedicatedServer) { return; }
	Entry->bPrepared = true;
	auto* Profile = Cast<UProject_JFoleyAudioProfile>(Path.ResolveObject());
	if (!Profile) { return; }
	LoadedProfiles.Add(Path, Profile);
	TArray<FSoftObjectPath> SoundPaths;
	Profile->GatherSoundPaths(SoundPaths);
	if (!SoundPaths.IsEmpty())
	{
		Entry->SoundsLoad = UAssetManager::GetStreamableManager().RequestAsyncLoad(SoundPaths);
	}
	USoundAttenuation* Attenuation = NewObject<USoundAttenuation>(this);
	Attenuation->Attenuation.bAttenuate = true;
	Attenuation->Attenuation.bSpatialize = true;
	Attenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	Attenuation->Attenuation.AttenuationShapeExtents = FVector(100.f, 0.f, 0.f);
	const float Radius = FMath::IsFinite(Profile->AudibleDistance) ? FMath::Clamp(Profile->AudibleDistance, 100.f, 5000.f) : 1800.f;
	Attenuation->Attenuation.FalloffDistance = FMath::Max(1.f, Radius - 100.f);
	FallbackAttenuation.Add(Path, Attenuation);
}

bool UProject_JFoleySubsystem::PrepareLocalAudio(UProject_JFoleyComponent* Component)
{
	check(IsInGameThread());
	UWorld* World = GetWorld();
	if (bStopped || !World || World->GetNetMode() == NM_DedicatedServer || !Enabled.GetValueOnGameThread() ||
		!IsValid(Component) || Component->GetWorld() != World || !Component->CanPresent(EProject_JFoleyGroup::Other)) { return false; }
	const auto* Character = Cast<ACharacter>(Component->GetOwner());
	if (!Character || !Character->IsPlayerControlled() || !Character->IsLocallyControlled()) { return false; }
	const FSoftObjectPath Path = Component->GetEffectiveProfile().ToSoftObjectPath();
	if (!Path.IsValid()) { return false; }
	ResolveReadyProfile(Path); // Shared async handles and the normal bounded cache; no listener query or event replay.
	return Resources.Contains(Path);
}

bool UProject_JFoleySubsystem::Submit(UProject_JFoleyComponent* Component, const FProject_JFoleyEvent& Event)
{
	check(IsInGameThread());
	UWorld* World = GetWorld();
	if (bStopped || !World || World->bIsTearingDown || World->GetNetMode() == NM_DedicatedServer ||
		!Enabled.GetValueOnGameThread() || !IsValid(Component) || Component->GetWorld() != World ||
		!ValidateEvent(Event, FMath::Clamp(MaxAge.GetValueOnGameThread(), 0.f, 0.5f))) { return false; }
	auto* Character = Cast<ACharacter>(Component->GetOwner());
	if (!Character) { return false; }
	const bool bLocal = Character->IsPlayerControlled() && Character->IsLocallyControlled();
	if (!bLocal && !OtherCharacters.GetValueOnGameThread()) { return false; }
	float DistanceSquared;
	if (!GetListenerDistanceSquared(Character->GetActorLocation(), DistanceSquared) || DistanceSquared > FMath::Square(5000.f))
	{
		++Stats.DistanceRejected; return false;
	}
	const FSoftObjectPath ProfilePath = Component->GetEffectiveProfile().ToSoftObjectPath();
	auto* Profile = ResolveReadyProfile(ProfilePath);
	if (!Profile) { ++Stats.NotReady; return false; }
	FGameplayTag ResolvedEvent;
	const auto* Definition = Profile->ResolveEvent(Event.Event, &ResolvedEvent);
	if (!Definition) { ++Stats.Unmapped; return false; }
	if (!Component->CanPresent(Definition->Group)) { return false; }
	if (!bLocal && Profile->OtherCharacterVolume <= 0.f) { return false; }
	const float Radius = FMath::IsFinite(Profile->AudibleDistance) ? FMath::Clamp(Profile->AudibleDistance, 100.f, 5000.f) : 1800.f;
	if (DistanceSquared > FMath::Square(Radius)) { ++Stats.DistanceRejected; return false; }
	// Skip all surface work until at least one of the event's sounds has loaded.
	bool bSoundReady = Definition->DefaultSound.IsValid();
	for (const auto& Pair : Definition->SurfaceSounds) { bSoundReady |= Pair.Value.IsValid(); }
	if (!bSoundReady) { ++Stats.NotReady; return false; }
	const int32 CooldownIndex = int32(Definition->Group) * 3 + int32(Event.Side);
	if (CooldownIndex < 0 || CooldownIndex >= UE_ARRAY_COUNT(Component->LastAdmission)) { return false; }
	const double Now = World->GetTimeSeconds();
	const double Cooldown = (Definition->Group == EProject_JFoleyGroup::Jump || Definition->Group == EProject_JFoleyGroup::Land) ? 0.2 : 0.075;
	if (Now >= Component->LastAdmission[CooldownIndex] && Now - Component->LastAdmission[CooldownIndex] < Cooldown)
	{
		++Stats.DuplicateRejected; return false;
	}
	FRequest Request{Component, Event, ProfilePath, Now, ScoreEvent(Definition->Importance, DistanceSquared, Radius), bLocal, ++NextOrder};
	if (Pending.Num() >= MaxPending)
	{
		int32 Worst = 0;
		for (int32 I = 1; I < Pending.Num(); ++I) { if (HigherPriority(Pending[Worst], Pending[I])) { Worst = I; } }
		++Stats.CapacityRejected;
		if (!HigherPriority(Request, Pending[Worst])) { return false; }
		Pending[Worst] = MoveTemp(Request);
	}
	else { Pending.Add(MoveTemp(Request)); }
	Component->LastAdmission[CooldownIndex] = Now;
	if (ResolvedEvent != Event.Event) { ++Stats.EventFallbacks; }
	++Stats.Submitted; Stats.Pending = Pending.Num();
	return true;
}

void UProject_JFoleySubsystem::OnPostActorTick(UWorld* World, ELevelTick TickType, float)
{
	if (World != GetWorld() || TickType == LEVELTICK_ViewportsOnly || bStopped || World->bIsTearingDown || Pending.IsEmpty()) { return; }
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_Foley_Admission);
	const double Start = FPlatformTime::Seconds();
	if (World->GetNetMode() == NM_DedicatedServer || !Enabled.GetValueOnGameThread() || !World->GetAudioDevice().IsValid())
	{
		Pending.Reset(); Stats.Pending = 0; return;
	}
	const double Now = World->GetTimeSeconds();
	const float Rate = FMath::Clamp(RemotePerSecond.GetValueOnGameThread(), 0.f, 240.f);
	if (LastTokenTime >= 0) { RemoteTokens += FMath::Max(0.0, Now - LastTokenTime) * Rate; }
	LastTokenTime = Now;
	RemoteTokens = FMath::Clamp(RemoteTokens, 0.0, 12.0);
	const int32 RemoteLimit = Rate > 0 ? FMath::Min(FMath::Clamp(RemotePerFrame.GetValueOnGameThread(), 0, 32), FMath::FloorToInt(RemoteTokens)) : 0;
	auto Selected = SelectBatch(Pending, Now, FMath::Clamp(MaxAge.GetValueOnGameThread(), 0.f, 0.5f),
		4, RemoteLimit, Stats.Expired, Stats.BudgetDropped);
	Stats.Pending = 0;
	const double Milliseconds = FMath::Clamp(WorkMilliseconds.GetValueOnGameThread(), 0.0f, 5.0f);
	for (const FRequest& Request : Selected)
	{
		if ((FPlatformTime::Seconds() - Start) * 1000.0 >= Milliseconds) { ++Stats.BudgetDropped; continue; }
		if (!Request.bLocal) { RemoteTokens = FMath::Max(0.0, RemoteTokens - 1.0); }
		Execute(Request);
	}
	Stats.LastMilliseconds = (FPlatformTime::Seconds() - Start) * 1000.0;
}

void UProject_JFoleySubsystem::Execute(const FRequest& Request)
{
	auto* Component = Request.Component.Get();
	auto* Profile = LoadedProfiles.FindRef(Request.Profile).Get();
	if (!IsValid(Component) || !Profile || Component->GetEffectiveProfile().ToSoftObjectPath() != Request.Profile) { return; }
	const auto* Definition = Profile->ResolveEvent(Request.Event.Event);
	if (!Definition || !Component->CanPresent(Definition->Group)) { return; }
	FVector Location = Component->ResolveContactLocation(*Profile, Request.Event.Side, Definition);
	float DistanceSquared;
	const float Radius = FMath::IsFinite(Profile->AudibleDistance) ? FMath::Clamp(Profile->AudibleDistance, 100.f, 5000.f) : 1800.f;
	if (!GetListenerDistanceSquared(Location, DistanceSquared) || DistanceSquared > FMath::Square(Radius)) { ++Stats.DistanceRejected; return; }
	EPhysicalSurface Surface = SurfaceType_Default;
	if (Definition->bTraceSurface)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_Foley_SurfaceTrace);
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ProjectJFoley), false, Component->GetOwner());
		Params.bReturnPhysicalMaterial = true;
		const FVector Up = Component->GetOwner()->GetActorUpVector();
		++Stats.SurfaceTraces;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Location + Up * FMath::Clamp(Profile->TraceAboveContact, 0.f, 100.f),
			Location - Up * FMath::Clamp(Profile->TraceBelowContact, 1.f, 200.f), Profile->SurfaceTraceChannel, Params))
		{
			Location = Hit.ImpactPoint;
			Surface = UPhysicalMaterial::DetermineSurfaceType(Hit.PhysMaterial.Get());
		}
	}
	USoundBase* Sound = Definition->ResolveSound(Surface).Get();
	if (!Sound) { Sound = Definition->DefaultSound.Get(); }
	if (!Sound) { return; }
	if (Sound->IsLooping()) { ++Stats.LoopRejected; return; } // Looping slide belongs to a separate start/stop lifetime.
	const float Volume = FMath::Clamp(Request.Event.VolumeMultiplier, 0.f, 4.f) *
		(Request.bLocal ? 1.f : FMath::Clamp(Profile->OtherCharacterVolume, 0.f, 1.f));
	UGameplayStatics::PlaySoundAtLocation(GetWorld(), Sound, Location, FRotator::ZeroRotator, Volume,
		FMath::Clamp(Request.Event.PitchMultiplier, 0.1f, 4.f), 0.f,
		Profile->Attenuation ? Profile->Attenuation.Get() : FallbackAttenuation.FindRef(Request.Profile).Get(),
		Request.bLocal ? LocalConcurrency.Get() : RemoteConcurrency.Get(), Component->GetOwner());
	++Stats.Played;
}

void UProject_JFoleySubsystem::Cancel(UProject_JFoleyComponent* Component)
{
	Pending.RemoveAll([Component](const FRequest& Request) { return Request.Component.Get() == Component; });
	Stats.Pending = Pending.Num();
}

void UProject_JFoleySubsystem::Stop()
{
	bStopped = true;
	Pending.Reset(); ListenerPositions.Reset(); Stats.Pending = 0;
	for (auto& Pair : Resources)
	{
		if (Pair.Value.ProfileLoad) { Pair.Value.ProfileLoad->CancelHandle(); }
		if (Pair.Value.SoundsLoad) { Pair.Value.SoundsLoad->CancelHandle(); }
	}
	Resources.Reset(); LoadedProfiles.Reset(); FallbackAttenuation.Reset();
}

void UProject_JFoleySubsystem::OnWorldEndPlay(UWorld& World)
{
	Stop(); Super::OnWorldEndPlay(World);
}

void UProject_JFoleySubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldPostActorTick.Remove(PostActorTickHandle);
	Stop(); Super::Deinitialize();
}
