#include "Network/Project_JOwnedPawnScopeComponent.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "Engine/NetConnection.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"
#include "Net/Iris/ReplicationSystem/ReplicationSystemUtil.h"
#include "Net/Iris/ReplicationSystem/EngineReplicationBridge.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ReplicationSystemDelegates.h"
#include "Iris/ReplicationSystem/Filtering/NetObjectFilter.h"
#include "Net/Core/Connection/NetCloseResult.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

namespace
{
UEngineReplicationBridge* LiveScopeBridge(UReplicationSystem* System)
{
	auto* Result=IsValid(System) ? Cast<UEngineReplicationBridge>(System->GetReplicationBridge()) : nullptr;
	return IsValid(Result) && !Result->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ? Result : nullptr;
}

// Exceptional cleanup owns one exact group and weak objects only. It cannot
// retain a dead PC/world; GC also cancels it after the native system shuts down.
struct FOwnedScopeRetirement
{
	TWeakObjectPtr<UReplicationSystem> System;
	TWeakObjectPtr<UEngineReplicationBridge> Bridge;
	UE::Net::FNetObjectGroupHandle Group;
	FDelegateHandle Phase, Cleanup, GC;
	bool Alive() const {return Bridge.IsValid() && LiveScopeBridge(System.Get())==Bridge.Get();}
	void Cancel()
	{
		FWorldDelegates::OnWorldPostActorTick.Remove(Phase);Phase.Reset();
		FWorldDelegates::OnWorldCleanup.Remove(Cleanup);Cleanup.Reset();
		FCoreUObjectDelegates::GetPostGarbageCollect().Remove(GC);GC.Reset();
	}
	void Try(UWorld* World)
	{
		if (!Alive()) {Cancel();return;}
		auto* Current=System.Get();
		if (!Current->IsValidGroup(Group)) {Cancel();return;}
		auto* Driver=Bridge->GetNetDriver();
		if (!IsValid(Driver) || Driver->GetWorld()!=World) return;
		Current->DestroyGroup(Group);
		if (!Current->IsValidGroup(Group)) Cancel();
	}
	static void Adopt(UReplicationSystem* TargetSystem,UEngineReplicationBridge* TargetBridge,UE::Net::FNetObjectGroupHandle OwnedGroup)
	{
		if (!TargetBridge || LiveScopeBridge(TargetSystem)!=TargetBridge || !TargetSystem->IsValidGroup(OwnedGroup)) return;
		auto Ticket=MakeShared<FOwnedScopeRetirement>(); Ticket->System=TargetSystem;Ticket->Bridge=TargetBridge;Ticket->Group=OwnedGroup;
		Ticket->Phase=FWorldDelegates::OnWorldPostActorTick.AddLambda([Ticket](UWorld* World,ELevelTick,float){const auto KeepTicket=Ticket;KeepTicket->Try(World);});
		Ticket->Cleanup=FWorldDelegates::OnWorldCleanup.AddLambda([Ticket](UWorld* World,bool,bool){const auto KeepTicket=Ticket;KeepTicket->Try(World);});
		Ticket->GC=FCoreUObjectDelegates::GetPostGarbageCollect().AddLambda([Ticket](){const auto KeepTicket=Ticket;if (!KeepTicket->Alive() || !KeepTicket->System->IsValidGroup(KeepTicket->Group)) KeepTicket->Cancel();});
	}
};
}

UProject_JOwnedPawnScopeComponent::UProject_JOwnedPawnScopeComponent()
{
	PrimaryComponentTick.bCanEverTick=false;
	bAutoActivate=false;
	SetIsReplicatedByDefault(false);
}

UProject_JOwnedPawnScopeComponent* UProject_JOwnedPawnScopeComponent::Ensure(APlayerController* Controller)
{
	if (!IsValid(Controller) || !Controller->HasAuthority() || Controller->IsActorBeingDestroyed()) return nullptr;
	auto* World=Controller->GetWorld(); auto* Driver=World ? World->GetNetDriver() : nullptr;
	if (!World || World->bIsTearingDown || !Driver || !Driver->GetReplicationSystem()) return nullptr;
	if (Controller->IsLocalController() && !Controller->GetNetConnection()) return nullptr;
	if (auto* Existing=Controller->FindComponentByClass<ThisClass>()) return Existing->bEnding ? nullptr : Existing;
	const TStrongObjectPtr<APlayerController> KeepController(Controller);
	const TStrongObjectPtr<ThisClass> KeepResult(NewObject<ThisClass>(Controller,NAME_None,RF_Transient));
	auto* Result=KeepResult.Get();
	Controller->AddInstanceComponent(Result); Result->RegisterComponent();
	if (!IsValid(Result) || Result->bEnding || !IsValid(Controller) || Controller->IsActorBeingDestroyed()) return nullptr;
	Result->PawnChanged=Controller->GetOnNewPawnNotifier().AddUObject(Result,&ThisClass::OnPawnChanged);
	Result->Schedule();
	return Result;
}

void UProject_JOwnedPawnScopeComponent::Wake(APawn* Pawn, bool bCreate)
{
	if (!IsValid(Pawn) || !Pawn->HasAuthority()) return;
	auto* Controller=Cast<APlayerController>(Pawn->GetController());
	if (!IsValid(Controller)) return;
	auto* Scope=bCreate ? Ensure(Controller) : Controller->FindComponentByClass<ThisClass>();
	if (Scope && !Scope->bEnding) Scope->Schedule();
}

bool UProject_JOwnedPawnScopeComponent::Resolve(FCurrent& Out) const
{
	Out={};
	auto* Controller=Cast<APlayerController>(GetOwner());
	auto* World=IsValid(Controller) ? Controller->GetWorld() : nullptr;
	auto* Driver=World ? World->GetNetDriver() : nullptr;
	if (bEnding || !IsValid(Controller) || !Controller->HasAuthority() || Controller->IsActorBeingDestroyed()
		|| !World || World->bIsTearingDown || !IsValid(Driver)) return false;
	Out.Controller=Controller; Out.System=Driver->GetReplicationSystem();
	Out.Connection=Controller->GetNetConnection(); Out.Pawn=Controller->GetPawn();
	// A retained primary driver may still name its tearing old world. Keep its
	// exact System/group lease; a different live world's driver is not admitted.
	auto* DriverWorld=Driver->GetWorld();
	if (DriverWorld!=World && (!DriverWorld || !DriverWorld->bIsTearingDown)) return false;
	if (!LiveScopeBridge(Out.System) || !Out.System->IsServer() || !IsValid(Out.Connection) || Out.Connection->GetDriver()!=Driver
		|| Out.Connection->PlayerController!=Controller || Out.Connection->GetConnectionState()!=USOCK_Open
		|| !Out.Connection->GetConnectionHandle().IsValid()) return false;
	Out.ConnectionId=Out.Connection->GetConnectionHandle().GetParentConnectionId();
	if (!Out.System->IsValidConnection(Out.ConnectionId) || !IsValid(Out.Pawn) || Out.Pawn->IsActorBeingDestroyed()
		|| !Out.Pawn->HasAuthority() || !Out.Pawn->GetIsReplicated() || Out.Pawn->GetController()!=Controller || Out.Pawn->GetWorld()!=World) return false;
	Out.Bridge=UE::Net::FReplicationSystemUtil::GetActorReplicationBridge(Out.Pawn);
	if (!IsValid(Out.Bridge) || Out.System->GetReplicationBridge()!=Out.Bridge || Out.Bridge->GetNetDriver()!=Driver) return false;
	Out.PawnHandle=Out.Bridge->GetReplicatedRefHandle(Out.Pawn);
	return Out.PawnHandle.IsCompleteHandle() && Out.Bridge->GetReplicatedObject(Out.PawnHandle)==Out.Pawn
		&& Out.System->GetOwningNetConnection(Out.PawnHandle)==Out.ConnectionId;
}

void UProject_JOwnedPawnScopeComponent::WithdrawFor(APawn* Pawn)
{
	auto* Controller=IsValid(Pawn) ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (IsValid(Controller)) if (auto* Scope=Controller->FindComponentByClass<ThisClass>()) Scope->Withdraw(Pawn);
}

void UProject_JOwnedPawnScopeComponent::Schedule(UWorld* TargetWorld)
{
	auto* World=TargetWorld ? TargetWorld : (GetWorld() ? GetWorld() : PhaseWorld.Get());
	if (!World || World->bIsTearingDown || (Phase.IsValid() && PhaseWorld.Get()==World)) return;
	CancelPhase(); PhaseWorld=World;
	// One pending phase callback, not a tick/scan. Pin retirement ownership even
	// if the PC/component EndPlay occurs before this phase; Cleanup closes both.
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	Phase=FWorldDelegates::OnWorldPostActorTick.AddLambda([KeepSelf](UWorld* InWorld,ELevelTick Tick,float Delta){KeepSelf->Flush(InWorld,Tick,Delta);});
	Cleanup=FWorldDelegates::OnWorldCleanup.AddLambda([KeepSelf](UWorld* InWorld,bool Session,bool Resources){KeepSelf->WorldCleanup(InWorld,Session,Resources);});
}

void UProject_JOwnedPawnScopeComponent::CancelPhase()
{
	FWorldDelegates::OnWorldPostActorTick.Remove(Phase); Phase.Reset();
	FWorldDelegates::OnWorldCleanup.Remove(Cleanup); Cleanup.Reset();
}

void UProject_JOwnedPawnScopeComponent::OnPawnChanged(APawn*)
{
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	auto* Controller=Cast<APlayerController>(GetOwner());
	if (bEnding || !IsValid(Controller)) return;
	FinalPawn=Controller->GetPawn(); bLogicalReady=false; ++Revision;
	Schedule();
}

void UProject_JOwnedPawnScopeComponent::ObserveSystem(UReplicationSystem* NewSystem)
{
	if (System.Get()==NewSystem && ObservedBridge.Get()==LiveScopeBridge(NewSystem)) return;
	UnbindSystem(); System=NewSystem;ObservedBridge=LiveScopeBridge(NewSystem);
	if (ObservedBridge.IsValid())
	{
		const TWeakObjectPtr<UReplicationSystem> Source(NewSystem);
		ConnectionAdded=NewSystem->GetDelegates().OnConnectionAdded().AddUObject(this,&ThisClass::OnConnectionChanged,Source,false);
		ConnectionRemoved=NewSystem->GetDelegates().OnConnectionRemoved().AddUObject(this,&ThisClass::OnConnectionChanged,Source,true);
	}
}

void UProject_JOwnedPawnScopeComponent::UnbindSystem()
{
	if (auto* Current=System.Get(); ObservedBridge.IsValid() && LiveScopeBridge(Current)==ObservedBridge.Get())
	{
		Current->GetDelegates().OnConnectionAdded().Remove(ConnectionAdded);
		Current->GetDelegates().OnConnectionRemoved().Remove(ConnectionRemoved);
	}
	ConnectionAdded.Reset(); ConnectionRemoved.Reset();ObservedBridge.Reset();
}

void UProject_JOwnedPawnScopeComponent::OnConnectionChanged(UE::Net::FConnectionHandle Handle,TWeakObjectPtr<UReplicationSystem> Source,bool bRemoved)
{
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	const TStrongObjectPtr<UReplicationSystem> KeepSource(Source.Get());
	if (!KeepSource.IsValid() || !ObservedBridge.IsValid() || LiveScopeBridge(KeepSource.Get())!=ObservedBridge.Get() || Source!=System || bEnding) return;
	auto* Controller=Cast<APlayerController>(GetOwner());
	auto* Current=IsValid(Controller) ? Controller->GetNetConnection() : nullptr;
	if (Handle.GetParentConnectionId()==ConnectionId || (Current && Current->GetConnectionHandle()==Handle))
	{
		if (bRemoved && Handle.GetParentConnectionId()==ConnectionId) bConnectionRetired=true;
		bLogicalReady=false; ++Revision; Schedule();
	}
}

void UProject_JOwnedPawnScopeComponent::Withdraw(APawn* Pawn)
{
	if (MemberPawn.Get()!=Pawn && FinalPawn.Get()!=Pawn && FailurePawn.Get()!=Pawn) return;
	bLogicalReady=false; FinalPawn.Reset(); FailurePawn.Reset(); FailureHandle={}; ++Revision;
	Schedule();
}

bool UProject_JOwnedPawnScopeComponent::RemoveMember()
{
	bLogicalReady=false;
	auto* Current=System.Get(); auto* CurrentBridge=Bridge.Get();
	// Disallow changes this group's mask even after a connection was removed.
	// Never skip this solely because its numeric ID is currently invalid/reused.
	if (CurrentBridge && LiveScopeBridge(Current)==CurrentBridge && Current->IsValidGroup(Group))
	{
		if (ConnectionId) Current->SetGroupFilterStatus(Group,ConnectionId,UE::Net::ENetFilterStatus::Disallow);
		if (CurrentBridge && Current->GetReplicationBridge()==CurrentBridge && IsValid(MemberPawn.Get()) && MemberHandle.IsCompleteHandle()
			&& UE::Net::FNetRefHandle::FullCompare(CurrentBridge->GetReplicatedRefHandle(MemberPawn.Get()),MemberHandle)
			&& CurrentBridge->GetReplicatedObject(MemberHandle)==MemberPawn.Get() && Current->IsInGroup(Group,MemberHandle))
		{
			Current->RemoveFromGroup(Group,MemberHandle);
			if (Current->IsInGroup(Group,MemberHandle)) return false;
		}
	}
	bAllowRequested=false; MemberHandle={}; MemberPawn.Reset(); ++Revision;
	return true;
}

bool UProject_JOwnedPawnScopeComponent::DestroyOwnedGroup()
{
	if (!RemoveMember()) return false;
	if (auto* Current=System.Get(); Bridge.IsValid() && LiveScopeBridge(Current)==Bridge.Get() && Current->IsValidGroup(Group))
	{
		Current->DestroyGroup(Group);
		if (Current->IsValidGroup(Group)) return false; // Keep retirement ownership until a supported phase/cleanup.
	}
	Group={}; Connection.Reset(); ConnectionId=0; Bridge.Reset();
	return true;
}

void UProject_JOwnedPawnScopeComponent::Fail(FName Reason,const FCurrent& Current)
{
	bAdmissionFailed=true; bLogicalReady=false; Failure=Reason;
	FailurePawn=Current.Pawn; FailureHandle=Current.PawnHandle;
	UE_LOG(LogTemp,Error,TEXT("OwnedPawnScope admission failed PC=%s Pawn=%s reason=%s"),*GetNameSafe(Current.Controller),*GetNameSafe(Current.Pawn),*Reason.ToString());
}

void UProject_JOwnedPawnScopeComponent::RejectFailedAdmission(const FCurrent& Current)
{
	if (bRejecting || FinalPawn.Get()!=Current.Pawn || FailurePawn.Get()!=Current.Pawn || !UE::Net::FNetRefHandle::FullCompare(FailureHandle,Current.PawnHandle)) return;
	FCurrent Verified;
	if (!Resolve(Verified) || Verified.Controller!=Current.Controller || Verified.Pawn!=Current.Pawn || Verified.Connection!=Current.Connection
		|| Verified.System!=Current.System || !UE::Net::FNetRefHandle::FullCompare(Verified.PawnHandle,Current.PawnHandle)) return;
	const TStrongObjectPtr<APlayerController> KeepController(Current.Controller);
	const TStrongObjectPtr<APawn> KeepPawn(Current.Pawn);
	const TStrongObjectPtr<UNetConnection> KeepConnection(Current.Connection);
	bRejecting=true; FinalPawn.Reset(); FailurePawn.Reset(); FailureHandle={}; ++Revision;
	const uint64 RejectRevision=Revision;
	auto* Mode=GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr;
	const TStrongObjectPtr<AGameSession> KeepSession(Mode ? Mode->GameSession.Get() : nullptr);
	const FText Reason=NSLOCTEXT("ProjectJNetwork","OwnedPawnScopeCapacity","The server is at capacity. Please try again.");
	const bool Rejected=KeepSession.IsValid() && KeepSession->KickPlayer(KeepController.Get(),Reason);
	UE_LOG(LogTemp,Warning,TEXT("OwnedPawnScope admission result PC=%s Connection=%u GameSessionRejected=%d"),*GetNameSafe(KeepController.Get()),Current.ConnectionId,Rejected);
	FCurrent After;
	const bool SameAfter=!Rejected && Revision==RejectRevision && Resolve(After) && After.Controller==Current.Controller
		&& After.Connection==Current.Connection && After.System==Current.System && After.Pawn==Current.Pawn
		&& UE::Net::FNetRefHandle::FullCompare(After.PawnHandle,Current.PawnHandle);
	if (SameAfter)
	{
		UE_LOG(LogTemp,Error,TEXT("OwnedPawnScope GameSession did not reject admission; closing the exact failing connection"));
		KeepConnection->Close(ENetCloseResult::HostClosedConnection);
	}
	else if (!Rejected && !bEnding) {bRejecting=false;Schedule();}
}

void UProject_JOwnedPawnScopeComponent::Flush(UWorld* World,ELevelTick,float)
{
	if (World!=PhaseWorld.Get()) return;
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	CancelPhase(); // The actual world phase is before NetDriver.TickFlush/filtering.
	if (Group.IsValid() && (!Bridge.IsValid() || LiveScopeBridge(System.Get())!=Bridge.Get()))
	{
		// Shutdown deinitializes native groups before clearing/garbaging Bridge.
		// Never interpret an old token against a new Impl on the same System UObject.
		Group={};MemberHandle={};MemberPawn.Reset();Connection.Reset();ConnectionId=0;
		bLogicalReady=false;bAllowRequested=false;bAdmissionFailed=false;bRejecting=false;Failure=NAME_None;
	}
	if (auto* ResourceBridge=LiveScopeBridge(System.Get()); Bridge.IsValid() && ResourceBridge==Bridge.Get() && Group.IsValid())
	{
		auto* ResourceDriver=ResourceBridge->GetNetDriver();
		auto* ResourceWorld=IsValid(ResourceDriver) ? ResourceDriver->GetWorld() : nullptr;
		if (ResourceWorld && ResourceWorld!=World && !ResourceWorld->bIsTearingDown)
		{
			bLogicalReady=false;
			Schedule(ResourceWorld);
			return;
		}
		// A tearing old world's retained driver has no future actor phase there.
		// Continue in the current owner's approved phase with the same group;
		// Resolve still requires primaryDriver == connectionDriver == bridgeDriver.
	}
	if (bEnding || World->bIsTearingDown)
	{
		if (!DestroyOwnedGroup()) FOwnedScopeRetirement::Adopt(System.Get(),Bridge.Get(),Group);
		UnbindSystem();System.Reset();Group={};PhaseWorld.Reset();return;
	}
	FCurrent Current;
	const bool Ready=Resolve(Current);
	if (bConnectionRetired || ((Group.IsValid() || System.IsValid()) && System.Get()!=Current.System) || (Connection.IsValid() && Current.Connection && Connection.Get()!=Current.Connection))
	{
		if (!DestroyOwnedGroup()) {Schedule();return;}
		UnbindSystem(); System.Reset(); bAdmissionFailed=false; bRejecting=false; Failure=NAME_None; bConnectionRetired=false;
	}
	if (Current.System) ObserveSystem(Current.System);
	if (!Ready) {if (!RemoveMember()) Schedule();return;} // Registration/ownership pending is not a capacity failure.
	if (Current.Controller->GetWorld()!=World) {Schedule(Current.Controller->GetWorld());return;}
	// Late Ensure can miss an earlier notifier. This post-actor phase also
	// establishes the live final reciprocal incarnation, outside possession.
	if (FinalPawn.Get()!=Current.Pawn) FinalPawn=Current.Pawn;
	if (bAdmissionFailed) {FailurePawn=Current.Pawn;FailureHandle=Current.PawnHandle;RejectFailedAdmission(Current);return;}
	if (System.Get()==Current.System && Connection.Get()==Current.Connection && Bridge.Get()==Current.Bridge && ConnectionId==Current.ConnectionId
		&& Current.System->IsValidGroup(Group) && UE::Net::FNetRefHandle::FullCompare(MemberHandle,Current.PawnHandle)
		&& MemberPawn.Get()==Current.Pawn && Current.System->IsInGroup(Group,Current.PawnHandle)) {bLogicalReady=true;return;}
	if (!RemoveMember()) {Schedule();return;}
	Bridge=Current.Bridge; Connection=Current.Connection; ConnectionId=Current.ConnectionId;
	if (!Current.System->IsValidGroup(Group))
	{
		Group=Current.System->CreateGroup(NAME_None);
		if (!Current.System->IsValidGroup(Group)) {Fail(TEXT("GroupCapacity"),Current);RejectFailedAdmission(Current);return;}
		++GroupsCreated;
		if (!Current.System->AddInclusionFilterGroup(Group)) {Fail(TEXT("InclusionRejected"),Current);if (!DestroyOwnedGroup()) Schedule();RejectFailedAdmission(Current);return;}
	}
	Current.System->AddToGroup(Group,Current.PawnHandle);
	if (!Current.System->IsInGroup(Group,Current.PawnHandle)) {Fail(TEXT("MembershipRejected"),Current);if (!DestroyOwnedGroup()) Schedule();RejectFailedAdmission(Current);return;}
	Current.System->SetGroupFilterStatus(Group,Current.ConnectionId,UE::Net::ENetFilterStatus::Allow);
	MemberPawn=Current.Pawn; MemberHandle=Current.PawnHandle; bAllowRequested=true; bLogicalReady=true; ++Revision;
}

void UProject_JOwnedPawnScopeComponent::Close()
{
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	if (bEnding) return;
	bEnding=true; bLogicalReady=false; FinalPawn.Reset(); FailurePawn.Reset(); FailureHandle={}; ++Revision;
	if (auto* Controller=Cast<APlayerController>(GetOwner())) Controller->GetOnNewPawnNotifier().Remove(PawnChanged);
	PawnChanged.Reset(); UnbindSystem();
	auto* World=GetWorld() ? GetWorld() : PhaseWorld.Get();
	auto* ResourceBridge=Bridge.IsValid() && LiveScopeBridge(System.Get())==Bridge.Get() ? Bridge.Get() : nullptr;
	auto* ResourceDriver=ResourceBridge ? ResourceBridge->GetNetDriver() : nullptr;
	auto* ResourceWorld=IsValid(ResourceDriver) ? ResourceDriver->GetWorld() : nullptr;
	if (Group.IsValid() && ResourceWorld && !ResourceWorld->bIsTearingDown) Schedule(ResourceWorld);
	else if (Group.IsValid() && World && !World->bIsTearingDown) Schedule(World);
	else
	{
		// Teardown can retain the driver in a different world. Transfer the exact
		// resource to weak-only retirement rather than equating old-world end with shutdown.
		FOwnedScopeRetirement::Adopt(System.Get(),Bridge.Get(),Group);
		CancelPhase();System.Reset();Group={};PhaseWorld.Reset();
	}
}
void UProject_JOwnedPawnScopeComponent::EndPlay(const EEndPlayReason::Type Reason) {Close();Super::EndPlay(Reason);}
void UProject_JOwnedPawnScopeComponent::OnComponentDestroyed(bool Hierarchy) {Close();Super::OnComponentDestroyed(Hierarchy);}
void UProject_JOwnedPawnScopeComponent::WorldCleanup(UWorld* World,bool,bool)
{
	if (World!=PhaseWorld.Get()) return;
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	const bool RetainedInOtherWorld=!bEnding && GetWorld() && GetWorld()!=World && !GetWorld()->bIsTearingDown;
	bLogicalReady=false; CancelPhase();
	if (RetainedInOtherWorld)
	{
		// Old-world teardown does not imply a retained driver's system deinit.
		// Keep the exact native resource and reuse/retire it in its current world.
		Schedule(GetWorld());return;
	}
	bEnding=true; UnbindSystem();
	if (!DestroyOwnedGroup()) FOwnedScopeRetirement::Adopt(System.Get(),Bridge.Get(),Group);
	System.Reset();Group={};MemberPawn.Reset();FinalPawn.Reset();FailurePawn.Reset();
	PhaseWorld.Reset();
}

FProjectJOwnedPawnScopeSnapshot UProject_JOwnedPawnScopeComponent::Query() const
{
	FProjectJOwnedPawnScopeSnapshot Result;
	Result.GroupToken=Group.GetRawValue(); Result.ConnectionId=ConnectionId; Result.GroupsCreated=GroupsCreated;
	Result.Revision=Revision; Result.PawnHandle=MemberHandle; Result.bAllowRequested=bAllowRequested;
	Result.bMutationPending=Phase.IsValid(); Result.bAdmissionFailed=bAdmissionFailed; Result.Failure=Failure;
	Result.bClosed=bEnding;
	auto* Current=System.Get(); auto* CurrentBridge=Bridge.Get();
	Result.bGroupValid=CurrentBridge && LiveScopeBridge(Current)==CurrentBridge && Current->IsValidGroup(Group);
	Result.bMemberPresent=Result.bGroupValid && CurrentBridge && Current->GetReplicationBridge()==CurrentBridge && IsValid(MemberPawn.Get()) && MemberHandle.IsCompleteHandle()
		&& UE::Net::FNetRefHandle::FullCompare(CurrentBridge->GetReplicatedRefHandle(MemberPawn.Get()),MemberHandle)
		&& CurrentBridge->GetReplicatedObject(MemberHandle)==MemberPawn.Get() && Current->IsInGroup(Group,MemberHandle);
	FCurrent Live;
	Result.bIdentityMatches=Resolve(Live) && Live.System==Current && Live.Bridge==CurrentBridge && Live.Connection==Connection.Get()
		&& Live.ConnectionId==ConnectionId && Live.Pawn==MemberPawn.Get() && UE::Net::FNetRefHandle::FullCompare(Live.PawnHandle,MemberHandle);
	Result.bLogicalReady=bLogicalReady && !bEnding && !bAdmissionFailed && Result.bIdentityMatches && Result.bGroupValid && Result.bMemberPresent;
	return Result;
}
