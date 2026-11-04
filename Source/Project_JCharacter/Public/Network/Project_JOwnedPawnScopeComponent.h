#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Iris/ReplicationSystem/NetRefHandle.h"
#include "Iris/ReplicationSystem/NetObjectGroupHandle.h"
#include "Net/Core/Connection/ConnectionHandle.h"
#include "Project_JOwnedPawnScopeComponent.generated.h"

class APlayerController;
class APawn;
class UReplicationSystem;
class UEngineReplicationBridge;
class UNetConnection;

/** Read-only identity/membership evidence. No public native allow-bit getter exists. */
struct FProjectJOwnedPawnScopeSnapshot
{
	bool bLogicalReady=false, bIdentityMatches=false, bGroupValid=false, bMemberPresent=false;
	bool bAllowRequested=false, bMutationPending=false, bAdmissionFailed=false, bClosed=false;
	uint32 ConnectionId=0, GroupsCreated=0;
	uint64 GroupToken=0, Revision=0;
	UE::Net::FNetRefHandle PawnHandle;
	FName Failure;
};

/** One nonreplicated, unticked owner per generic PC. One native group per connection lifetime. */
UCLASS(NotBlueprintable)
class PROJECT_JCHARACTER_API UProject_JOwnedPawnScopeComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UProject_JOwnedPawnScopeComponent();
	static UProject_JOwnedPawnScopeComponent* Ensure(APlayerController* Controller);
	static void Wake(APawn* Pawn, bool bCreate=true);
	static void WithdrawFor(APawn* Pawn);
	void Withdraw(APawn* Pawn);
	FProjectJOwnedPawnScopeSnapshot Query() const;

private:
	struct FCurrent
	{
		APlayerController* Controller=nullptr; APawn* Pawn=nullptr; UNetConnection* Connection=nullptr;
		UReplicationSystem* System=nullptr; UEngineReplicationBridge* Bridge=nullptr;
		UE::Net::FNetRefHandle PawnHandle;
		uint32 ConnectionId=0;
	};
	bool Resolve(FCurrent& Out) const;
	void OnPawnChanged(APawn* Ignored);
	void OnConnectionChanged(UE::Net::FConnectionHandle Handle, TWeakObjectPtr<UReplicationSystem> Source, bool bRemoved);
	void Schedule(UWorld* TargetWorld=nullptr);
	void Flush(UWorld* World, ELevelTick Tick, float Delta);
	void WorldCleanup(UWorld* World, bool SessionEnded, bool CleanupResources);
	void CancelPhase();
	void ObserveSystem(UReplicationSystem* NewSystem);
	void UnbindSystem();
	bool RemoveMember();
	bool DestroyOwnedGroup();
	void Fail(FName Reason, const FCurrent& Current);
	void RejectFailedAdmission(const FCurrent& Current);
	void Close();
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void OnComponentDestroyed(bool Hierarchy) override;
	TWeakObjectPtr<UReplicationSystem> System;
	TWeakObjectPtr<UEngineReplicationBridge> Bridge;
	TWeakObjectPtr<UEngineReplicationBridge> ObservedBridge;
	TWeakObjectPtr<UNetConnection> Connection;
	TWeakObjectPtr<UWorld> PhaseWorld;
	TWeakObjectPtr<APawn> MemberPawn, FinalPawn, FailurePawn;
	UE::Net::FNetRefHandle MemberHandle, FailureHandle;
	UE::Net::FNetObjectGroupHandle Group;
	FDelegateHandle PawnChanged, ConnectionAdded, ConnectionRemoved, Phase, Cleanup;
	uint32 ConnectionId=0, GroupsCreated=0;
	uint64 Revision=0;
	FName Failure;
	bool bLogicalReady=false, bAllowRequested=false, bEnding=false, bAdmissionFailed=false, bRejecting=false, bConnectionRetired=false;
};
