#include "Testing/Project_JNPBlobLifecycleFixture.h"
#include "Testing/Project_JNetworkLoadFixture.h"
#include "Components/SceneComponent.h"
#include "GameFramework/PlayerState.h"
#include "Engine/NetConnection.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "Net/Iris/ReplicationSystem/ReplicationSystemUtil.h"
#include "Net/Iris/ReplicationSystem/EngineReplicationBridge.h"
#include "Net/Core/NetToken/NetTokenExportContext.h"
#include "Iris/Serialization/IrisObjectReferencePackageMap.h"
#include "Iris/ReplicationSystem/NameTokenStore.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/Filtering/NetObjectFilter.h"
#include "Net/Core/Connection/NetEnums.h"
#include "NetworkPredictionModelDef.h"
#include "NetworkPredictionModelDefRegistry.h"
#include "NetworkPredictionDriver.h"
#include "NetworkPredictionProxyInit.h"
#include "NetworkPredictionProxyWrite.h"
#include "Serialization/JsonSerializer.h"
#include "Containers/Ticker.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJNPBlobLifecycle, Log, All);
namespace
{
constexpr int32 LastCase = 12;
bool Enabled()
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return FParse::Param(FCommandLine::Get(), TEXT("ProjectJNetworkFixture")) && FParse::Param(FCommandLine::Get(), TEXT("ProjectJNPBlobLifecycle"));
#endif
}
uint64 EmptyCount()
{
#if !UE_BUILD_SHIPPING
	return FReplicationProxy::GetProcessEmptyDecodeCount(); // Required companion 34A public read-only diagnostic.
#else
	return 0;
#endif
}
FString OutputPath(const TCHAR* File)
{
	FString Directory;
	FParse::Value(FCommandLine::Get(), TEXT("ProjectJDOutput="), Directory);
	if (Directory.IsEmpty()) Directory = FPaths::ProjectSavedDir() / TEXT("NPBlobLifecycle");
	IFileManager::Get().MakeDirectory(*Directory, true);
	return Directory / File;
}
void WriteResult(bool bSuccess, const FString& Reason, const FString& Rows)
{
	auto Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bSuccess); Result->SetStringField(TEXT("reason"), Reason);
	Result->SetStringField(TEXT("cohort"), TEXT("one real NP model; two socket clients; six ref actors; no rendered crowd"));
	FString JSON; auto Writer = TJsonWriterFactory<>::Create(&JSON);
	FJsonSerializer::Serialize(Result, Writer);
	FFileHelper::SaveStringToFile(JSON, *OutputPath(TEXT("np-lifecycle.json")));
	FFileHelper::SaveStringToFile(Rows, *OutputPath(TEXT("np-lifecycle.csv")));
}
void ScheduleFixtureExit(bool bSuccess, double GraceSeconds)
{
	// Reuse the existing fixture lifetime's core ticker. A network-failure travel
	// can destroy a controller/world timer after completion; this owns values only.
	const double ExitAt = FPlatformTime::Seconds() + GraceSeconds;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bSuccess, ExitAt](float)
	{
		if (FPlatformTime::Seconds() < ExitAt) return true;
		FPlatformMisc::RequestExitWithStatus(false, bSuccess ? 0 : 1); return false;
	}), .05f);
}
int32 BitsFor(int32 Case)
{
	// Isolate same-size heap delta coverage from the retained huge-object cases.
	if (Case >= 1 && Case <= 3) return 5001;
	return Case == 0 || Case == 8 ? 31 : Case == 4 || Case == 5 || Case == 11 ? 12001 : 8193;
}
FProjectJNPBlobSync MakeState(int32 Generation, int32 Case, const TArray<TObjectPtr<AProject_JNetworkProbeNPC>>& Refs)
{
	FProjectJNPBlobSync S; S.Generation = Generation; S.Case = Case == 3 ? 2 : Case; S.NumBits = BitsFor(Case);
	S.Bytes.SetNumZeroed((S.NumBits + 7) / 8);
	const int32 Pattern = Case >= 1 && Case <= 3 ? 1 : Case == 5 ? 4 : Case;
	for (int32 I = 0; I < S.Bytes.Num(); ++I) S.Bytes[I] = uint8((I * 73 + Pattern * 29 + Generation * 11) ^ (I >> 3));
	if (Case == 2 || Case == 3) S.Bytes[268] ^= 0x40; // One changed 32-bit word; same length.
	S.Bytes.Last() &= uint8((1u << (S.NumBits & 7)) - 1u); // All authored lengths have a non-byte tail.
	const int32 TokenCase = Case == 3 ? 2 : Case;
	for (int32 I = 0; I < 6; ++I)
	{
		S.Refs[I] = Case == 5 || Refs.Num() != 6 ? nullptr : Refs[(I + (Case >= 3 ? 1 : 0)) % 6].Get();
		S.Names[I] = Case == 5 ? NAME_None : FName(*FString::Printf(TEXT("NPBlob_G%d_C%d_Name%d"), Generation, Case, I));
		S.Tokens[I] = FName(*FString::Printf(TEXT("NPBlob_G%d_C%d_Token%d"), Generation, TokenCase, I));
	}
	return S;
}
bool EqualState(const FProjectJNPBlobSync& A, const FProjectJNPBlobSync& B)
{
	if (A.Generation != B.Generation || A.Case != B.Case || A.NumBits != B.NumBits || A.Bytes != B.Bytes) return false;
	for (int32 I = 0; I < 6; ++I) if (A.Refs[I] != B.Refs[I] || A.Names[I] != B.Names[I] || A.Tokens[I] != B.Tokens[I]) return false;
	return true;
}
uint64 Digest(const FProjectJNPBlobSync& S)
{
	uint64 H = 14695981039346656037ull;
	const auto AddByte = [&H](uint8 B) { H = (H ^ B) * 1099511628211ull; };
	const auto AddInt = [&AddByte](uint32 V) { for (int32 I = 0; I < 4; ++I) AddByte(uint8(V >> (I * 8))); };
	const auto AddName = [&AddByte, &AddInt](FName Name)
	{
		const auto UTF8 = StringCast<UTF8CHAR>(*Name.ToString()); AddInt(UTF8.Length());
		for (int32 I = 0; I < UTF8.Length(); ++I) AddByte(uint8(UTF8.Get()[I]));
	};
	AddInt(S.Generation); AddInt(S.Case); AddInt(S.NumBits);
	for (uint8 B : S.Bytes) AddByte(B);
	for (int32 I = 0; I < 6; ++I) { const auto* Ref = S.Refs[I].Get(); AddInt(Ref ? Ref->ProbeId : 0); AddName(S.Names[I]); AddName(S.Tokens[I]); }
	return H;
}
uint64 LostPackets(const UWorld* World)
{
	uint64 Count = 0;
	if (const auto* Driver = World->GetNetDriver()) for (const UNetConnection* Connection : Driver->ClientConnections)
		// The non-Total counters reset each connection stats period. A difference
		// of those snapshots can miss losses or underflow across the reset.
		if (Connection) Count += uint64(FMath::Max(Connection->OutTotalPacketsLost, 0)) + uint64(FMath::Max(Connection->InTotalPacketsLost, 0));
	return Count;
}
}

struct FProjectJNPBlobModel : FNetworkPredictionModelDef
{
	NP_MODEL_BODY();
	using StateTypes = FProjectJNPBlobStateTypes;
	using Simulation = FProjectJNPBlobSimulation;
	using Driver = UProject_JNPBlobLifecycleComponent;
	static const TCHAR* GetName() { return TEXT("ProjectJNPBlobLifecycle"); }
};
template<> struct FNetworkPredictionDriver<FProjectJNPBlobModel> : FNetworkPredictionDriverBase<FProjectJNPBlobModel>
{
	static constexpr FNetworkPredictionModelDefCapabilities GetCapabilities()
	{
		FNetworkPredictionModelDefCapabilities C;
		C.SupportedTickingPolicies = ENetworkPredictionTickingPolicy::Fixed;
		C.FixedNetworkLODs.AP = ENetworkLOD::ForwardPredict; C.FixedNetworkLODs.SP = ENetworkLOD::Interpolated;
		return C;
	}
};
NP_MODEL_REGISTER(FProjectJNPBlobModel);

void FProjectJNPBlobSync::NetSerialize(const FNetSerializeParams& P)
{
	auto& Ar = P.Ar;
	if (Ar.IsLoading()) { bIrisRead = false; bTokensRead = false; }
	Ar << Generation; Ar << Case; Ar << NumBits;
	if (Ar.IsError() || NumBits < 1 || NumBits > 12001) { Ar.SetError(); return; }
	if (Ar.IsLoading()) Bytes.SetNumZeroed((NumBits + 7) / 8);
	if (Bytes.Num() != (NumBits + 7) / 8) { Ar.SetError(); return; }
	Ar.SerializeBits(Bytes.GetData(), NumBits);
	auto* Map = Cast<UIrisObjectReferencePackageMap>(P.Map);
	if (!Map) { Ar.SetError(); return; } // Reject a helper-only or legacy package-map path.
	for (int32 I = 0; I < 6; ++I)
	{
		UObject* Object = Refs[I].Get();
		Map->SerializeObject(Ar, AProject_JNetworkProbeNPC::StaticClass(), Object, nullptr);
		if (Ar.IsLoading()) Refs[I] = Cast<AProject_JNetworkProbeNPC>(Object);
		Ar << Names[I];
	}
	if (Ar.IsSaving())
	{
		auto* Export = UE::Net::FNetTokenExportContext::GetNetTokenExportContext(Ar);
		auto* Store = Export && Export->GetNetTokenStore() ? Export->GetNetTokenStore()->GetDataStore<UE::Net::FNameTokenStore>() : nullptr;
		if (!Store) { Ar.SetError(); return; }
		for (FName Name : Tokens)
		{
			const auto Token = Store->GetOrCreateToken(Name);
			if (!Token.IsValid()) { Ar.SetError(); return; }
			Store->WriteNetToken(Ar, Token); Export->AddNetTokenPendingExport(Token);
		}
	}
	else
	{
		const auto* Resolve = Map->GetNetTokenResolveContext();
		auto* Store = Resolve && Resolve->NetTokenStore ? Resolve->NetTokenStore->GetDataStore<UE::Net::FNameTokenStore>() : nullptr;
		if (!Store || !Resolve->RemoteNetTokenStoreState) { Ar.SetError(); return; }
		for (FName& Name : Tokens)
		{
			const auto Token = Store->ReadNetToken(Ar); Name = Store->ResolveToken(Token, Resolve->RemoteNetTokenStoreState);
			if (!Token.IsValid() || Name.IsNone()) { Ar.SetError(); return; }
		}
		bIrisRead = !Ar.IsError(); bTokensRead = bIrisRead;
	}
}
bool FProjectJNPBlobSync::ShouldReconcile(const FProjectJNPBlobSync& Authority) const { return !EqualState(*this, Authority); }

UProject_JNPBlobLifecycleComponent::UProject_JNPBlobLifecycleComponent()
{
	bWantsInitializeComponent = true; PrimaryComponentTick.bCanEverTick = false;
}
void UProject_JNPBlobLifecycleComponent::InitializeNetworkPredictionProxy()
{
	if (Enabled()) NetworkPredictionProxy.Init<FProjectJNPBlobModel>(GetWorld(), GetReplicationProxies(), &Simulation, this);
}
void UProject_JNPBlobLifecycleComponent::InitializeSimulationState(FProjectJNPBlobSync* Sync, void*)
{
	// Valid transport before first PostLogin/possession/phase write. Clients do not seed expected authored values.
	*Sync = MakeState(0, 0, {});
}
bool UProject_JNPBlobLifecycleComponent::RestoreSerializer()
{
	if (!bSeamInstalled) return true;
	if (NetworkPredictionProxy.GetID().GetTraceID() != SavedSimID || NetworkPredictionProxy.GetCachedHasNetConnection() != bSavedConnection || !GetOwner()->HasAuthority())
	{ Failure = TEXT("Opaque seam binding changed; refusing stale callback restoration"); return false; }
	const auto Proxies = GetReplicationProxies();
	Proxies.AutonomousProxy->NetSerializeFunc = MoveTemp(SavedAP); Proxies.SimulatedProxy->NetSerializeFunc = MoveTemp(SavedSP);
	bSeamInstalled = false; return true;
}
bool UProject_JNPBlobLifecycleComponent::Publish(int32 Generation, int32 Case, const TArray<TObjectPtr<AProject_JNetworkProbeNPC>>& Refs)
{
	if (!Enabled() || !GetOwner()->HasAuthority()) return false;
	CheckOwnerRoleChange(); // Normal public/protected role lifecycle before the authoritative state write.
	bOpaqueEmpty = Case == 7;
	if (!bOpaqueEmpty && !RestoreSerializer()) return false;
	if (bOpaqueEmpty) return true; // Existing real NP pending frame still advances; only its AP/SP wire body is empty.
	const auto State = MakeState(Generation, Case, Refs);
	return NetworkPredictionProxy.WriteSyncState<FProjectJNPBlobSync>([&State](FProjectJNPBlobSync& Sync) { Sync = State; }) != nullptr;
}
void UProject_JNPBlobLifecycleComponent::PreReplication(IRepChangedPropertyTracker& Tracker)
{
	Super::PreReplication(Tracker); // Real CheckOwnerRoleChange and OnPreReplication set frame/cached frame.
	if (!bOpaqueEmpty || !Failure.IsEmpty()) return;
	const auto Proxies = GetReplicationProxies();
	if (bSeamInstalled)
	{
		if (NetworkPredictionProxy.GetID().GetTraceID() != SavedSimID || NetworkPredictionProxy.GetCachedHasNetConnection() != bSavedConnection)
			Failure = TEXT("NP binding changed during opaque-empty interval");
		return;
	}
	if (!Proxies.AutonomousProxy->NetSerializeFunc || !Proxies.SimulatedProxy->NetSerializeFunc)
	{ Failure = TEXT("Real AP/SP serializers were not bound before empty"); return; }
	SavedSimID = NetworkPredictionProxy.GetID().GetTraceID(); bSavedConnection = NetworkPredictionProxy.GetCachedHasNetConnection();
	SavedAP = MoveTemp(Proxies.AutonomousProxy->NetSerializeFunc); SavedSP = MoveTemp(Proxies.SimulatedProxy->NetSerializeFunc);
	Proxies.AutonomousProxy->NetSerializeFunc = [](const FNetSerializeParams& P) { if (!P.Ar.IsSaving()) P.Ar.SetError(); };
	Proxies.SimulatedProxy->NetSerializeFunc = [](const FNetSerializeParams& P) { if (!P.Ar.IsSaving()) P.Ar.SetError(); };
	bSeamInstalled = true;
}
void UProject_JNPBlobLifecycleComponent::EndPlay(EEndPlayReason::Type Reason)
{
	// No stale restoration after unregister. No component/driver captured by the no-op functions.
	SavedAP = nullptr; SavedSP = nullptr; bSeamInstalled = false; bOpaqueEmpty = false;
	Super::EndPlay(Reason);
}
AProject_JNPBlobLifecyclePawn::AProject_JNPBlobLifecyclePawn()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Probe = CreateDefaultSubobject<UProject_JNPBlobLifecycleComponent>(TEXT("NPBlobProbe"));
	bReplicates = true; bAlwaysRelevant = true; bOnlyRelevantToOwner = false;
	SetReplicateMovement(false); SetNetUpdateFrequency(30); SetMinNetUpdateFrequency(30);
}
void AProject_JNPBlobLifecyclePawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AProject_JNPBlobLifecyclePawn, Generation); DOREPLIFETIME(AProject_JNPBlobLifecyclePawn, CaseMarker);
}

void AProject_JNPBlobLifecycleController::ClientExpect_Implementation(int32 InGeneration, int32 InCase, int32 InSlot)
{
	if (!Enabled() || bTerminal || InSlot < 0 || InSlot > 1 || InCase < 0 || InCase > LastCase || InGeneration < 1 || InGeneration > 2) return;
	ExpectedGeneration = InGeneration; ExpectedCase = InCase; Slot = InSlot; StableSince = 0; ExpectedSince = FPlatformTime::Seconds();
}
void AProject_JNPBlobLifecycleController::PlayerTick(float DeltaSeconds)
{
	Super::PlayerTick(DeltaSeconds);
	if (!Enabled() || GetNetMode() != NM_Client || ExpectedCase < 0 || ReportedCase == ExpectedCase) return;
	const double Now = FPlatformTime::Seconds(); if (Now < NextObserve) return; NextObserve = Now + .05;
	AProject_JNPBlobLifecyclePawn* ObservedPawn = nullptr; int32 Count = 0;
	for (TActorIterator<AProject_JNPBlobLifecyclePawn> It(GetWorld()); It; ++It)
		if (It->Generation == ExpectedGeneration && !It->IsActorBeingDestroyed()) { ObservedPawn = *It; ++Count; }
	if (Now - ExpectedSince > 11)
	{
		const auto* C = ObservedPawn ? ObservedPawn->Probe.Get() : nullptr;
		const auto* S = C ? &C->GetObserved() : nullptr;
		Rows += FString::Printf(TEXT("%d,%d,%u,%d,%llu,%d,0,%d,0\n"), ExpectedGeneration, ExpectedCase, ObservedPawn ? uint8(ObservedPawn->GetLocalRole()) : 0, S ? S->NumBits : 0, S ? Digest(*S) : 0, C ? C->RealFrame() : INDEX_NONE, ObservedPawn == nullptr);
		uint32 RefValid = 0, RefMismatch = 0, NameMismatch = 0, TokenMismatch = 0, TokenNone = 0;
		const auto Expected = MakeState(ExpectedGeneration, ExpectedCase, {});
		if (S) for (int32 I = 0; I < 6; ++I)
		{
			const auto* Ref = S->Refs[I].Get();
			if (Ref) RefValid |= 1U << I;
			const int32 ExpectedId = (I + (ExpectedCase >= 3 ? 1 : 0)) % 6 + 1;
			if (ExpectedCase == 5 ? Ref != nullptr : (!Ref || Ref->ProbeId != ExpectedId)) RefMismatch |= 1U << I;
			if (S->Names[I] != Expected.Names[I]) NameMismatch |= 1U << I;
			if (S->Tokens[I] != Expected.Tokens[I]) TokenMismatch |= 1U << I;
			if (S->Tokens[I].IsNone()) TokenNone |= 1U << I;
		}
		ClientComplete(false, FString::Printf(TEXT("Client deadline expected=%d/%d actual marker=%d model=%d/%d bits=%d frame=%d iris=%d tokens=%d bytes=%d ref_valid=%u ref_mismatch=%u name_mismatch=%u token_mismatch=%u token_none=%u"), ExpectedGeneration, ExpectedCase, ObservedPawn ? ObservedPawn->CaseMarker : INDEX_NONE, S ? S->Generation : 0, S ? S->Case : INDEX_NONE, S ? S->NumBits : 0, C ? C->RealFrame() : INDEX_NONE, S && S->bIrisRead, S && S->bTokensRead, S && S->Bytes == Expected.Bytes, RefValid, RefMismatch, NameMismatch, TokenMismatch, TokenNone)); return;
	}
	if (Count > 1) { ClientComplete(false, TEXT("Duplicate current NP root")); return; }
	const bool bAbsent = ExpectedCase == 10 && Slot == 1;
	if (bAbsent)
	{
		if (ObservedPawn || PreviousRoot.IsValid() || PreviousComponent.IsValid()) { StableSince = 0; return; }
		if (!StableSince) StableSince = Now;
		if (Now - StableSince < .25) return;
		Rows += FString::Printf(TEXT("%d,%d,%u,0,0,-1,0,1,1\n"), ExpectedGeneration, ExpectedCase, uint8(ROLE_SimulatedProxy));
		ServerObserved(ExpectedGeneration, ExpectedCase, ROLE_SimulatedProxy, 0, INDEX_NONE, 0, 0, true); ReportedCase = ExpectedCase; return;
	}
	if (!ObservedPawn || ObservedPawn->CaseMarker != ExpectedCase || !ObservedPawn->Probe) { StableSince = 0; return; }
	const uint8 ObservedRole = ObservedPawn->GetLocalRole(); if (ObservedRole != (Slot == 0 ? ROLE_AutonomousProxy : ROLE_SimulatedProxy)) return;
	if (ExpectedCase == 12 && (PreviousRoot.IsValid() || PreviousComponent.IsValid())) return;
	const int32 Frame = ObservedPawn->Probe->RealFrame();
	if (Frame < 0 || (ExpectedCase != 12 && ExpectedCase != 7 && Frame <= LastAcceptedFrame)) return;
	const auto& Actual = ObservedPawn->Probe->GetObserved();
	uint64 Hash = 0, EmptyDelta = 0; int32 Bits = 0;
	if (ExpectedCase == 7)
	{
		const uint64 Current = EmptyCount();
		if (Current <= EmptyBefore || Actual.Case != 6 || Actual.Generation != ExpectedGeneration) return;
		EmptyDelta = Current - EmptyBefore; // Actual successful opaque decoder event, not a model callback or marker alone.
	}
	else
	{
		TArray<TObjectPtr<AProject_JNetworkProbeNPC>> Refs; Refs.SetNumZeroed(6);
		int32 RefCount = 0;
		for (TActorIterator<AProject_JNetworkProbeNPC> It(GetWorld()); It; ++It)
		{
			if (It->ProbeId < 1 || It->ProbeId > 6 || Refs[It->ProbeId - 1]) return;
			Refs[It->ProbeId - 1] = *It; ++RefCount;
		}
		if (RefCount != 6) return;
		const auto Expected = MakeState(ExpectedGeneration, ExpectedCase, Refs);
		if (!Actual.bIrisRead || !Actual.bTokensRead || !EqualState(Actual, Expected)) { StableSince = 0; return; }
		Hash = Digest(Actual); Bits = Actual.NumBits;
	}
	if (!StableSince) StableSince = Now;
	if (Now - StableSince < .25) return; // More than one real engine/NP frame; no data synthesized on the client.
	Rows += FString::Printf(TEXT("%d,%d,%u,%d,%llu,%d,%llu,0,1\n"), ExpectedGeneration, ExpectedCase, ObservedRole, Bits, Hash, Frame, EmptyDelta);
	if (ExpectedCase == 6) EmptyBefore = EmptyCount();
	PreviousRoot = ObservedPawn; PreviousComponent = ObservedPawn->Probe; LastAcceptedFrame = Frame; ReportedCase = ExpectedCase;
	ServerObserved(ExpectedGeneration, ExpectedCase, ObservedRole, Hash, Frame, Bits, EmptyDelta, false);
}
void AProject_JNPBlobLifecycleController::ServerObserved_Implementation(int32 InGeneration, int32 InCase, uint8 ObservedRole, uint64 Hash, int32 Frame, int32 Bits, uint64 EmptyDelta, bool bAbsent)
{
	if (auto* Mode = GetWorld()->GetAuthGameMode<AProject_JNPBlobLifecycleGameMode>()) Mode->Accept(this, InGeneration, InCase, ObservedRole, Hash, Frame, Bits, EmptyDelta, bAbsent);
}
void AProject_JNPBlobLifecycleController::ClientComplete_Implementation(bool bSuccess, const FString& Reason)
{
	if (!Enabled() || bTerminal || Reason.Len() > 1024) return;
	bTerminal = true; ExpectedCase = INDEX_NONE; WriteResult(bSuccess, Reason, Rows);
	if (GEngine) GEngine->Exec(GetWorld(), TEXT("csvprofile STOP"));
	ScheduleFixtureExit(bSuccess, 1.0);
}

AProject_JNPBlobLifecycleGameMode::AProject_JNPBlobLifecycleGameMode()
{
	PrimaryActorTick.bCanEverTick = true; PlayerControllerClass = AProject_JNPBlobLifecycleController::StaticClass();
	DefaultPawnClass = AProject_JNetworkViewPawn::StaticClass(); PlayerStateClass = APlayerState::StaticClass();
}
void AProject_JNPBlobLifecycleGameMode::BeginPlay()
{
	Super::BeginPlay(); bActive = Enabled(); Started = FPlatformTime::Seconds();
	if (!bActive) { SetActorTickEnabled(false); return; }
	auto* Driver = GetWorld()->GetNetDriver();
	if (!Driver || !Driver->GetReplicationSystem()) { Finish(false, TEXT("Actual Iris net driver missing")); return; }
	ReentryGroup = Driver->GetReplicationSystem()->CreateGroup(TEXT("ProjectJ.NPBlobLifecycle.SPReentry"));
	if (!Driver->GetReplicationSystem()->AddExclusionFilterGroup(ReentryGroup)) { Finish(false, TEXT("Iris exclusion group creation failed")); return; }
	Driver->GetReplicationSystem()->SetGroupFilterStatus(ReentryGroup, UE::Net::ENetFilterStatus::Allow);
	FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 I = 0; I < 6; ++I)
	{
		auto* Ref = GetWorld()->SpawnActor<AProject_JAlwaysRelevantProbeNPC>(AProject_JAlwaysRelevantProbeNPC::StaticClass(), FTransform::Identity, P);
		if (!Ref) { Finish(false, TEXT("Reference spawn failed")); return; } Ref->ProbeId = I + 1; Refs.Add(Ref);
	}
	UE_LOG(LogProjectJNPBlobLifecycle, Display, TEXT("NPBLOB_READY Iris=1 References=6 Models=1 Clients=2 NetMode=%d"), int32(GetNetMode()));
}
void AProject_JNPBlobLifecycleGameMode::PostLogin(APlayerController* Player)
{
	Super::PostLogin(Player); if (!Enabled()) return;
	if (GetNetMode() == NM_ListenServer && Player->IsLocalController() && !Player->GetNetConnection()) return;
	if (Clients.Num() >= 2) { Finish(false, TEXT("Unexpected third socket client")); return; }
	auto* PC = CastChecked<AProject_JNPBlobLifecycleController>(Player); PC->Slot = Clients.Num(); Clients.Add(PC);
}
bool AProject_JNPBlobLifecycleGameMode::CreateProbe()
{
	FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Root = GetWorld()->SpawnActor<AProject_JNPBlobLifecyclePawn>(AProject_JNPBlobLifecyclePawn::StaticClass(), FTransform::Identity, P);
	if (!Root) return false;
	Root->Generation = Generation;
	APawn* Old = Clients[0]->GetPawn(); Clients[0]->Possess(Root); if (Old && Old != Root) Old->Destroy();
	bGroupBound = false; return true;
}
void AProject_JNPBlobLifecycleGameMode::StartCase(int32 Next)
{
	Case = Next; CaseStarted = FPlatformTime::Seconds(); Ack[0] = Ack[1] = false;
	StableCaseStartEngineFrame = GFrameCounter; StableCaseHoldEngineFrames = 0;
	if (Case == 1)
	{
		const IConsoleVariable* Policy = IConsoleManager::Get().FindConsoleVariable(TEXT("net.Iris.MinimumNumberOfFramesBetweenBaselines"));
		if (!Policy || Policy->GetInt() < 0)
		{ Finish(false, TEXT("Baseline refresh unavailable/disabled for bounded word coverage fixture")); return; }
		// Read the existing policy; never reduce it. Retain a stable heap seed for
		// two refresh intervals before the next same-size change. Actual decode
		// counters, not this wait, remain the mandatory coverage proof.
		StableCaseHoldEngineFrames = 2U * (uint64(Policy->GetInt()) + 1U);
		UE_LOG(LogProjectJNPBlobLifecycle, Display, TEXT("NPBLOB_BASELINE_WINDOW policy_frames=%d hold_engine_frames=%llu"), Policy->GetInt(), StableCaseHoldEngineFrames);
	}
	auto* System = GetWorld()->GetNetDriver()->GetReplicationSystem();
	if (Case == 12)
	{
		if (const auto* Bridge = UE::Net::FReplicationSystemUtil::GetActorReplicationBridge(Root))
			System->RemoveFromGroup(ReentryGroup, Bridge->GetReplicatedRefHandle(Root));
		Root->Destroy(); Root = nullptr; Generation = 2;
		if (!CreateProbe()) { Finish(false, TEXT("Root recreation failed")); return; }
	}
	if (Case == 10 || Case == 11)
		System->SetGroupFilterStatus(ReentryGroup, Clients[1]->GetNetConnection()->GetConnectionHandle().GetParentConnectionId(), Case == 10 ? UE::Net::ENetFilterStatus::Disallow : UE::Net::ENetFilterStatus::Allow);
	Root->CaseMarker = Case;
	if (!Root->Probe->Publish(Generation, Case, Refs)) { Finish(false, TEXT("Real NP state write failed")); return; }
	CaseFrame = Root->Probe->RealFrame();
	UE_LOG(LogProjectJNPBlobLifecycle, Display, TEXT("NPBLOB_CASE generation=%d case=%d handle=%llu real_pending_frame=%d opaque_empty=%d"), Generation, Case, LastRootHandle, CaseFrame, Case == 7);
	Root->ForceNetUpdate();
	for (AProject_JNPBlobLifecycleController* PC : Clients) PC->ClientExpect(Generation, Case, PC->Slot);
}
void AProject_JNPBlobLifecycleGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds); if (!bActive || bFinished) return;
	const double Now = FPlatformTime::Seconds();
	if (Now - Started > 150 || (Case >= 0 && Now - CaseStarted > 12)) { Finish(false, FString::Printf(TEXT("Bounded deadline generation=%d case=%d ack=%d/%d"), Generation, Case, Ack[0], Ack[1])); return; }
	if (Clients.Num() != 2) return;
	for (const AProject_JNPBlobLifecycleController* PC : Clients) if (!IsValid(PC) || !PC->GetNetConnection()) { Finish(false, TEXT("Socket connection retired")); return; }
	if (!Root)
	{
		LostAtStart = LostPackets(GetWorld()); if (!CreateProbe()) { Finish(false, TEXT("NP root spawn failed")); return; }
	}
	if (!Root->Probe->GetFailure().IsEmpty()) { Finish(false, Root->Probe->GetFailure()); return; }
	if (!bGroupBound)
	{
		const auto* Bridge = UE::Net::FReplicationSystemUtil::GetActorReplicationBridge(Root); if (!Bridge) return;
		const auto Handle = Bridge->GetReplicatedRefHandle(Root); if (!Handle.IsValid()) return;
		if (Generation == 2 && Handle.GetId() == LastRootHandle) { Finish(false, TEXT("Recreated root reused the retired handle")); return; }
		LastRootHandle = Handle.GetId(); GetWorld()->GetNetDriver()->GetReplicationSystem()->AddToGroup(ReentryGroup, Handle); bGroupBound = true;
	}
	if (Case < 0) { StartCase(0); return; }
	if (!Ack[0] || !Ack[1]) return;
	if (Case == 1)
	{
		const uint64 ElapsedEngineFrames = GFrameCounter - StableCaseStartEngineFrame;
		if (ElapsedEngineFrames < StableCaseHoldEngineFrames) return;
		UE_LOG(LogProjectJNPBlobLifecycle, Display, TEXT("NPBLOB_BASELINE_WINDOW_COMPLETE elapsed_engine_frames=%llu"), ElapsedEngineFrames);
	}
	if (Case == 7 && Root->Probe->RealFrame() <= CaseFrame) return; // Server NP frame must advance through the zero-bit seam.
	if (Case < LastCase) StartCase(Case + 1);
	else
	{
		int32 RequestedLoss = 0; FParse::Value(FCommandLine::Get(), TEXT("PktLoss="), RequestedLoss);
		const uint64 CurrentLost = LostPackets(GetWorld());
		if (CurrentLost < LostAtStart) { Finish(false, TEXT("Connection lifetime loss counters regressed")); return; }
		const uint64 Lost = CurrentLost - LostAtStart;
		Finish(RequestedLoss == 0 || Lost > 0, FString::Printf(TEXT("All 13 cases exact AP/SP; observed connection-lifetime packet loss delta=%llu; specific lost baseline identity uninstrumented"), Lost));
	}
}
void AProject_JNPBlobLifecycleGameMode::Accept(AProject_JNPBlobLifecycleController* PC, int32 InGeneration, int32 InCase, uint8 ObservedRole, uint64 Hash, int32 Frame, int32 Bits, uint64 EmptyDelta, bool bAbsent)
{
	if (!bActive || bFinished || !IsValid(PC) || !Clients.IsValidIndex(PC->Slot) || Clients[PC->Slot] != PC || !PC->GetNetConnection()) return;
	if (InGeneration != Generation || InCase != Case) return; // Late observation from a retired phase.
	const int32 Slot = PC->Slot; if (Ack[Slot]) return;
	const bool WantAbsent = Case == 10 && Slot == 1;
	bool bExact = ObservedRole == (Slot == 0 ? ROLE_AutonomousProxy : ROLE_SimulatedProxy) && bAbsent == WantAbsent;
	if (WantAbsent) bExact &= Hash == 0 && Bits == 0 && Frame == INDEX_NONE;
	else if (Case == 7) bExact &= Hash == 0 && Bits == 0 && EmptyDelta > 0 && Frame >= LastFrames[Slot];
	else
	{
		const auto Expected = MakeState(Generation, Case, Refs);
		bExact &= Hash == Digest(Expected) && Bits == Expected.NumBits && EmptyDelta == 0 && Frame >= 0 && (Case == 12 || Frame > LastFrames[Slot]);
	}
	Rows += FString::Printf(TEXT("%d,%d,%d,%u,%d,%llu,%d,%llu,%d,%d\n"), Generation, Case, Slot, ObservedRole, Bits, Hash, Frame, EmptyDelta, bAbsent, bExact);
	if (!bExact) { Finish(false, TEXT("Actual client NP observation mismatch")); return; }
	Ack[Slot] = true; if (!WantAbsent) LastFrames[Slot] = Frame;
}
void AProject_JNPBlobLifecycleGameMode::Logout(AController* Player)
{
	if (!bFinished && Clients.Contains(Cast<AProject_JNPBlobLifecycleController>(Player))) Finish(false, TEXT("Client disconnected before completion"));
	Super::Logout(Player);
}
void AProject_JNPBlobLifecycleGameMode::ReleaseGroup()
{
	if (auto* Driver = GetWorld() ? GetWorld()->GetNetDriver() : nullptr)
		if (auto* System = Driver->GetReplicationSystem(); System && ReentryGroup.IsValid()) { System->RemoveGroupFilter(ReentryGroup); System->DestroyGroup(ReentryGroup); }
	ReentryGroup = {};
}
void AProject_JNPBlobLifecycleGameMode::Finish(bool bSuccess, const FString& Reason)
{
	if (bFinished) return; bFinished = true;
	WriteResult(bSuccess, Reason, Rows);
	if (GEngine) GEngine->Exec(GetWorld(), TEXT("csvprofile STOP"));
	for (AProject_JNPBlobLifecycleController* PC : Clients) if (IsValid(PC)) PC->ClientComplete(bSuccess, Reason);
	// Let reliable completion observations flush; normal world teardown exercises EndPlay/free.
	ScheduleFixtureExit(bSuccess, 3.0); // Existing small-fixture reliable-RPC grace, extended beyond the client exit.
}
void AProject_JNPBlobLifecycleGameMode::EndPlay(EEndPlayReason::Type Reason)
{
	ReleaseGroup(); if (IsValid(Root)) Root->Destroy();
	for (AProject_JNetworkProbeNPC* Ref : Refs) if (IsValid(Ref)) Ref->Destroy(); Refs.Reset();
	Super::EndPlay(Reason);
}
