#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Backend/Project_JHandoverManager.h"
#include "Backend/Project_JHandoverTransport.h"
#include "Backend/Project_JBackendConnection.h"
#include "Game/Project_JInputLeaseSubsystem.h"
#include "Game/Project_JPlayerState.h"
#include "Social/Project_JSocialSubsystem.h"
#include "Project_JGreatswordCharacter.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/PlayerController.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/UnrealType.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCheckedHandoverTest, "ProjectJ.Maturity.Handover.ApplyAndAdmission",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCheckedHandoverTest::RunTest(const FString&)
{
	auto* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Player = World->SpawnActor<AProject_JGreatswordCharacter>();
	auto* Manager = NewObject<UProject_JHandoverManager>(NewObject<UGameInstance>(GEngine));
	FProject_JHandoverEnvelope Envelope;
	TestTrue(TEXT("Native player snapshot builds"), Manager->BuildEnvelope(Player, TEXT("LocalNode"), Envelope));
	Envelope.Payload[0] = 2; Envelope.PayloadChecksum = Manager->CalculatePayloadChecksum(Envelope.Payload);
	TestTrue(TEXT("Valid CRC cannot acknowledge an unsupported inner snapshot"), Manager->ApplyEnvelopeChecked(Player, Envelope) == EProject_JHandoverApplyResult::Rejected);
	Envelope.Payload[0] = 1; Envelope.PayloadChecksum = Manager->CalculatePayloadChecksum(Envelope.Payload);
	TestTrue(TEXT("Rejected snapshot did not consume its transfer id"), Manager->ApplyEnvelopeChecked(Player, Envelope) == EProject_JHandoverApplyResult::Applied);
	Player->SetActorLocation(FVector(123, 0, 0));
	TestTrue(TEXT("Lost ACK replay reports already applied"), Manager->ApplyEnvelopeChecked(Player, Envelope) == EProject_JHandoverApplyResult::AlreadyApplied);
	TestEqual(TEXT("Replay does not repeat teleport"), Player->GetActorLocation().X, 123.0);
	auto Changed = Envelope; Changed.Payload.Last() ^= 1; Changed.PayloadChecksum = Manager->CalculatePayloadChecksum(Changed.Payload);
	TestTrue(TEXT("Same id with a different digest is rejected"), Manager->ApplyEnvelopeChecked(Player, Changed) == EProject_JHandoverApplyResult::Rejected);
	auto Truncated = Envelope; Truncated.TransferId = FGuid::NewGuid(); Truncated.Payload.Pop(); Truncated.PayloadChecksum = Manager->CalculatePayloadChecksum(Truncated.Payload);
	TestFalse(TEXT("Truncated reader input fails without mutating state"), Manager->ApplyEnvelope(Player, Truncated));
	FProject_JPlayerHandoverSnapshot Invalid;
	Invalid.Location.X = std::numeric_limits<double>::infinity();
	auto NonFinite = Envelope; NonFinite.TransferId = FGuid::NewGuid(); NonFinite.Payload.Reset();
	FMemoryWriter Writer(NonFinite.Payload, true); Writer << Invalid.Version << Invalid.Level << Invalid.Location << Invalid.Rotation;
	NonFinite.PayloadChecksum = Manager->CalculatePayloadChecksum(NonFinite.Payload);
	TestFalse(TEXT("Nonfinite location is rejected before commit"), Manager->ApplyEnvelope(Player, NonFinite));
	TestEqual(TEXT("Malformed snapshots leave the current location"), Player->GetActorLocation().X, 123.0);
	FindFProperty<FIntProperty>(Manager->GetClass(), TEXT("MaxApplyReceipts"))->SetPropertyValue_InContainer(Manager, 1);
	auto Next = Envelope; Next.TransferId = FGuid::NewGuid();
	TestTrue(TEXT("Unexpired receipts are never evicted to admit a new apply"), Manager->ApplyEnvelopeChecked(Player, Next) == EProject_JHandoverApplyResult::Overloaded);
	TestTrue(TEXT("Replay remains available at capacity"), Manager->ApplyEnvelope(Player, Envelope));
	FindFProperty<FIntProperty>(Manager->GetClass(), TEXT("MaxActiveTransfers"))->SetPropertyValue_InContainer(Manager, 1);
	auto Transport = MakeShared<FProject_JLoopbackHandoverTransport>(); Transport->SetDropResponses(true); Manager->SetTransport(Transport);
	TestTrue(TEXT("First outgoing transfer is admitted"), Manager->StartEnvelopeTransfer(Next));
	Next.TransferId = FGuid::NewGuid();
	TestTrue(TEXT("Active limit returns overload before storing another payload"), Manager->StartEnvelopeTransferChecked(Next) == EProject_JHandoverAdmission::Overloaded);
	Manager->Deinitialize();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMutationOutcomeTest, "ProjectJ.Maturity.Backend.MutationOutcome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMutationOutcomeTest::RunTest(const FString&)
{
	FProject_JBackendResponseEnvelope Response;
	Response.RequestContext.Intent = EProject_JBackendRequestIntent::Mutation;
	TestFalse(TEXT("Mutation cannot dispatch without an idempotency key"), Response.RequestContext.IsValidForDispatch());
	Response.RequestContext.IdempotencyKey = FProject_JIdempotencyKey::NewKey();
	Response.FailureKind = EProject_JBackendFailureKind::ConnectionFailed;
	ApplyProjectJBackendOutcomePolicy(Response, true);
	TestTrue(TEXT("Lost response is an unknown mutation outcome"), Response.FailureKind == EProject_JBackendFailureKind::UnknownOutcome && Response.bRequiresReconciliation && Response.bRetryable);
	const auto Key = Response.RequestContext.IdempotencyKey;
	ApplyProjectJBackendOutcomePolicy(Response, true);
	TestTrue(TEXT("Classification preserves the original replay key"), Key.Value == Response.RequestContext.IdempotencyKey.Value);
	Response = {}; Response.RequestContext.Intent = EProject_JBackendRequestIntent::Mutation; Response.FailureKind = EProject_JBackendFailureKind::DispatchFailed;
	ApplyProjectJBackendOutcomePolicy(Response, false);
	TestFalse(TEXT("An undispatched request does not require reconciliation"), Response.bRequiresReconciliation);
	Response = {}; Response.FailureKind = EProject_JBackendFailureKind::ConnectionFailed;
	ApplyProjectJBackendOutcomePolicy(Response, true);
	TestFalse(TEXT("Query failure retains transport semantics"), Response.bRequiresReconciliation);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGroupSnapshotTest, "ProjectJ.Maturity.Social.GroupSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJGroupSnapshotTest::RunTest(const FString&)
{
	auto* Social = NewObject<UProject_JSocialSubsystem>(NewObject<UGameInstance>(GEngine));
	auto* Member = NewObject<AProject_JPlayerState>();
	Member->SetIdentity(FProject_JAccountId::NewId(), FProject_JCharacterId::NewId());
	FProject_JSocialGroupSnapshot Group; Group.GroupId = TEXT("RestoredGuild"); Group.Revision = 7;
	Group.LeaderCharacterId = FGuid::NewGuid(); Group.Members = {Member->GetCharacterId().Value, Group.LeaderCharacterId};
	TestFalse(TEXT("Membership alone cannot invent an unknown group's leader"), Social->RestoreMembership(Member, NAME_None, Group.GroupId));
	TestTrue(TEXT("Complete group snapshot restores before the leader connects"), Social->RestoreGroupSnapshot(EProject_JSocialGroupKind::Guild, Group));
	Social->BindPlayerState(Member);
	TestEqual(TEXT("First connected member does not become leader"), Member->GetGuildLeaderCharacterId(), Group.LeaderCharacterId);
	TestTrue(TEXT("Identical revision is idempotent"), Social->RestoreGroupSnapshot(EProject_JSocialGroupKind::Guild, Group));
	auto Conflicting = Group; Conflicting.LeaderCharacterId = Member->GetCharacterId().Value;
	TestFalse(TEXT("Same revision cannot change leadership"), Social->RestoreGroupSnapshot(EProject_JSocialGroupKind::Guild, Conflicting));
	TestTrue(TEXT("Local leave advances the projection revision"), Social->LeaveGuild(Member).bSucceeded);
	TestFalse(TEXT("An old backend snapshot cannot undo a leave"), Social->RestoreGroupSnapshot(EProject_JSocialGroupKind::Guild, Group));
	Group.Revision = 9; Group.bDeleted = true; Group.Members.Reset(); Group.LeaderCharacterId.Invalidate();
	TestTrue(TEXT("A versioned deletion clears membership"), Social->RestoreGroupSnapshot(EProject_JSocialGroupKind::Guild, Group));
	Conflicting.Revision = 8;
	TestFalse(TEXT("A deletion tombstone prevents stale resurrection"), Social->RestoreGroupSnapshot(EProject_JSocialGroupKind::Guild, Conflicting));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJInputLeaseTest, "ProjectJ.Maturity.Input.SharedMappingLease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJInputLeaseTest::RunTest(const FString&)
{
	auto* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	auto* Player = NewObject<ULocalPlayer>(GEngine);
	auto* Controller = World->SpawnActor<APlayerController>();
	Controller->PlayerInput = NewObject<UEnhancedPlayerInput>(Controller);
	Controller->Player = Player; Player->PlayerController = Controller;
	Player->PlayerAdded(nullptr, 0);
	ON_SCOPE_EXIT { Player->PlayerRemoved(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Leases = Player->GetSubsystem<UProject_JInputLeaseSubsystem>();
	auto* Input = Player->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!TestNotNull(TEXT("LocalPlayer lease service"), Leases) || !TestNotNull(TEXT("Enhanced Input service"), Input)) { return false; }
	auto* A = NewObject<UInputMappingContext>(); auto* B = NewObject<UInputMappingContext>(); auto* Mapping = NewObject<UInputMappingContext>();
	TestTrue(TEXT("First owner acquires mapping"), Leases->Acquire(A, Mapping));
	Leases->Acquire(A, Mapping); Leases->Acquire(B, Mapping); Leases->Release(A);
	TestTrue(TEXT("Controller A cleanup retains Controller B's mapping"), Input->HasMappingContext(Mapping));
	Leases->Release(B); TestFalse(TEXT("Last owner releases its mapping"), Input->HasMappingContext(Mapping));
	Input->AddMappingContext(Mapping, 4); Leases->Acquire(A, Mapping); Leases->Release(A);
	TestTrue(TEXT("A pre-existing external mapping remains externally owned"), Input->HasMappingContext(Mapping));
	Input->RemoveMappingContext(Mapping);
	return true;
}
#endif
