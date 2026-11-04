#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "NetworkPredictionComponent.h"
#include "NetworkPredictionSimulation.h"
#include "NetworkPredictionTickState.h"
#include "Misc/StringBuilder.h"
#include "Iris/ReplicationSystem/NetObjectGroupHandle.h"
#include "Project_JNPBlobLifecycleFixture.generated.h"

class AProject_JNetworkProbeNPC;

// Test-only pure values. UObject ownership stays with the level; NP history stores weak refs.
struct FProjectJNPBlobInput
{
	uint8 Value = 0;
	void NetSerialize(const FNetSerializeParams& P) { P.Ar << Value; }
	void ToString(FAnsiStringBuilderBase& B) const { B.Appendf("input=%u", Value); }
};
struct FProjectJNPBlobSync
{
	int32 Generation = 0, Case = INDEX_NONE, NumBits = 0;
	TArray<uint8> Bytes;
	TWeakObjectPtr<AProject_JNetworkProbeNPC> Refs[6];
	FName Names[6], Tokens[6];
	bool bIrisRead = false, bTokensRead = false; // Local receive witnesses; never serialized.
	void NetSerialize(const FNetSerializeParams& P);
	bool ShouldReconcile(const FProjectJNPBlobSync& Authority) const;
	void Interpolate(const FProjectJNPBlobSync*, const FProjectJNPBlobSync* To, float) { *this = *To; }
	void ToString(FAnsiStringBuilderBase& B) const { B.Appendf("generation=%d case=%d bits=%d", Generation, Case, NumBits); }
};
using FProjectJNPBlobStateTypes = TNetworkPredictionStateTypes<FProjectJNPBlobInput, FProjectJNPBlobSync, void>;
struct FProjectJNPBlobSimulation
{
	void SimulationTick(const FNetSimTimeStep&, const TNetSimInput<FProjectJNPBlobStateTypes>& Input, const TNetSimOutput<FProjectJNPBlobStateTypes>& Output)
	{ *Output.Sync = *Input.Sync; }
};

/** Dormant without both explicit fixture flags. No content, movement, rendering or production policy. */
UCLASS(NotBlueprintable)
class UProject_JNPBlobLifecycleComponent : public UNetworkPredictionComponent
{
	GENERATED_BODY()
public:
	UProject_JNPBlobLifecycleComponent();
	virtual void PreReplication(IRepChangedPropertyTracker& Tracker) override;
	virtual void EndPlay(EEndPlayReason::Type Reason) override;
	void InitializeSimulationState(FProjectJNPBlobSync* Sync, void*);
	void ProduceInput(int32, FProjectJNPBlobInput* Input) { Input->Value = 0; }
	void FinalizeFrame(const FProjectJNPBlobSync* Sync, const void*) { Observed = *Sync; }
	bool Publish(int32 Generation, int32 Case, const TArray<TObjectPtr<AProject_JNetworkProbeNPC>>& Refs);
	const FProjectJNPBlobSync& GetObserved() const { return Observed; }
	int32 RealFrame() const { return NetworkPredictionProxy.GetPendingFrame(); }
	const FString& GetFailure() const { return Failure; }
protected:
	virtual void InitializeNetworkPredictionProxy() override;
private:
	bool RestoreSerializer();
	FProjectJNPBlobSimulation Simulation;
	FProjectJNPBlobSync Observed;
	TFunction<void(const FNetSerializeParams&)> SavedAP, SavedSP;
	int32 SavedSimID = INDEX_NONE;
	bool bOpaqueEmpty = false, bSeamInstalled = false, bSavedConnection = false;
	FString Failure;
};

UCLASS(NotBlueprintable)
class AProject_JNPBlobLifecyclePawn : public APawn
{
	GENERATED_BODY()
public:
	AProject_JNPBlobLifecyclePawn();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProject_JNPBlobLifecycleComponent> Probe;
	// Same replicated root as its NP component. Not an expected-payload transport.
	UPROPERTY(Replicated) int32 Generation = 1;
	UPROPERTY(Replicated) int32 CaseMarker = INDEX_NONE;
};

UCLASS(NotBlueprintable)
class AProject_JNPBlobLifecycleController : public APlayerController
{
	GENERATED_BODY()
public:
	virtual void PlayerTick(float DeltaSeconds) override;
	UFUNCTION(Client, Reliable) void ClientExpect(int32 Generation, int32 Case, int32 InSlot);
	UFUNCTION(Server, Reliable) void ServerObserved(int32 Generation, int32 Case, uint8 ObservedRole, uint64 Digest, int32 Frame, int32 Bits, uint64 EmptyDelta, bool bAbsent);
	UFUNCTION(Client, Reliable) void ClientComplete(bool bSuccess, const FString& Reason);
	int32 Slot = INDEX_NONE;
private:
	int32 ExpectedGeneration = 0, ExpectedCase = INDEX_NONE, ReportedCase = INDEX_NONE, LastAcceptedFrame = INDEX_NONE;
	uint64 EmptyBefore = 0;
	bool bTerminal = false; // First completion owns the receipt and exit status.
	double NextObserve = 0, StableSince = 0, ExpectedSince = 0;
	TWeakObjectPtr<AProject_JNPBlobLifecyclePawn> PreviousRoot;
	TWeakObjectPtr<UProject_JNPBlobLifecycleComponent> PreviousComponent;
	FString Rows = TEXT("generation,case,role,bits,digest,frame,empty_delta,absent,exact\n");
};

/** Separate low-density real-socket cohort, reusing the existing fixture lifetime and ref actors. */
UCLASS(NotBlueprintable)
class AProject_JNPBlobLifecycleGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	AProject_JNPBlobLifecycleGameMode();
	virtual void BeginPlay() override;
	virtual void PostLogin(APlayerController* Player) override;
	virtual void Logout(AController* Player) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(EEndPlayReason::Type Reason) override;
	void Accept(AProject_JNPBlobLifecycleController* PC, int32 Generation, int32 Case, uint8 ObservedRole, uint64 Digest, int32 Frame, int32 Bits, uint64 EmptyDelta, bool bAbsent);
private:
	bool CreateProbe();
	void StartCase(int32 Next);
	void Finish(bool bSuccess, const FString& Reason);
	void ReleaseGroup();
	UPROPERTY(Transient) TArray<TObjectPtr<AProject_JNPBlobLifecycleController>> Clients;
	UPROPERTY(Transient) TArray<TObjectPtr<AProject_JNetworkProbeNPC>> Refs;
	UPROPERTY(Transient) TObjectPtr<AProject_JNPBlobLifecyclePawn> Root;
	UE::Net::FNetObjectGroupHandle ReentryGroup;
	uint64 LastRootHandle = 0, LostAtStart = 0;
	uint64 StableCaseStartEngineFrame = 0, StableCaseHoldEngineFrames = 0;
	int32 Generation = 1, Case = INDEX_NONE;
	int32 LastFrames[2] = {INDEX_NONE, INDEX_NONE};
	bool Ack[2] = {false, false}, bFinished = false, bActive = false, bGroupBound = false;
	int32 CaseFrame = INDEX_NONE;
	double Started = 0, CaseStarted = 0;
	FString Rows = TEXT("generation,case,slot,role,bits,digest,frame,empty_delta,absent,exact\n");
};
