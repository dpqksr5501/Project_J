#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/Project_JHandoverManager.h"
#include "Project_JPlayerCharacter.h"
#include "Backend/Project_JHandoverTransport.h"
#include "Tests/Project_JHandoverTestReceiver.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Game/Project_JPlayerState.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Social/Project_JSocialSubsystem.h"

namespace
{
class FImmediateAcceptHandoverTransport final : public IProject_JHandoverTransport
{
public:
	bool bReportDispatchSuccess = true;
	virtual bool Send(
		const FProject_JHandoverTransportRequest& Request,
		FProject_JHandoverTransportCallback Completion) override
	{
		FProject_JHandoverTransportResponse Response;
		Response.TransferId = Request.Envelope.TransferId;
		Response.Attempt = Request.Attempt;
		Response.Outcome = EProject_JHandoverTransportOutcome::Accepted;
		Completion(Response);
		return bReportDispatchSuccess;
	}

	virtual void Cancel(const FGuid& TransferId) override {}
	virtual void Tick(float DeltaSeconds) override {}
};

FProject_JHandoverEnvelope MakeValidEnvelope(const UProject_JHandoverManager& Manager)
{
	FProject_JHandoverEnvelope Envelope;
	Envelope.TransferId = FGuid::NewGuid();
	Envelope.ActorClassPath = TEXT("/Script/Engine.Actor");
	Envelope.Timestamp = FDateTime::UtcNow();
	Envelope.SourceNodeId = TEXT("Source");
	Envelope.TargetNodeId = TEXT("Target");
	Envelope.PayloadChecksum = Manager.CalculatePayloadChecksum(Envelope.Payload);
	return Envelope;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectJHandoverEnvelopeValidationTest,
	"ProjectJ.Architecture.Handover.EnvelopeValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJHandoverEnvelopeValidationTest::RunTest(const FString& Parameters)
{
	const UProject_JHandoverManager* Manager = GetDefault<UProject_JHandoverManager>();

	FProject_JHandoverEnvelope Envelope = MakeValidEnvelope(*Manager);

	TestEqual(
		TEXT("A current empty envelope with a valid checksum is accepted"),
		Manager->ValidateEnvelope(Envelope),
		EProject_JHandoverValidationFailure::None);

	Envelope.Version = 2;
	TestEqual(
		TEXT("Unsupported versions are rejected before deserialization"),
		Manager->ValidateEnvelope(Envelope),
		EProject_JHandoverValidationFailure::UnsupportedVersion);

	Envelope.Version = 1;
	Envelope.Payload.Add(42);
	TestEqual(
		TEXT("Payload corruption is rejected"),
		Manager->ValidateEnvelope(Envelope),
		EProject_JHandoverValidationFailure::ChecksumMismatch);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectJHandoverStateMachineTest,
	"ProjectJ.Architecture.Handover.StateMachine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJHandoverStateMachineTest::RunTest(const FString& Parameters)
{
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	UProject_JHandoverManager* Manager = NewObject<UProject_JHandoverManager>(GameInstance);
	TSharedPtr<FProject_JLoopbackHandoverTransport> Transport =
		MakeShared<FProject_JLoopbackHandoverTransport>();
	Manager->SetTransport(Transport);

	FProject_JHandoverEnvelope InvalidEnvelope = MakeValidEnvelope(*Manager);
	InvalidEnvelope.Payload.Add(42);
	TestFalse(
		TEXT("Invalid envelopes never enter the transport"),
		Manager->StartEnvelopeTransfer(InvalidEnvelope));
	TestEqual(
		TEXT("Invalid envelope failure remains diagnosable"),
		Manager->GetHandoverState(InvalidEnvelope.TransferId),
		EProject_JHandoverState::Failed);
	FProject_JHandoverRecord InvalidRecord;
	TestTrue(
		TEXT("Invalid envelope record remains inspectable"),
		Manager->GetHandoverRecord(InvalidEnvelope.TransferId, InvalidRecord));
	TestEqual(
		TEXT("Invalid envelope records the validation failure"),
		InvalidRecord.FailureReason,
		EProject_JHandoverFailureReason::InvalidEnvelope);

	const FProject_JHandoverEnvelope AcceptedEnvelope = MakeValidEnvelope(*Manager);
	TestTrue(TEXT("Valid envelope starts a transfer"), Manager->StartEnvelopeTransfer(AcceptedEnvelope));
	TestFalse(TEXT("Duplicate transfer id is rejected"), Manager->StartEnvelopeTransfer(AcceptedEnvelope));
	Manager->TickHandover(0.0f);
	TestEqual(
		TEXT("Accepted loopback response completes the transfer"),
		Manager->GetHandoverState(AcceptedEnvelope.TransferId),
		EProject_JHandoverState::Completed);

	Manager->SetTransport(MakeShared<FImmediateAcceptHandoverTransport>());
	const FProject_JHandoverEnvelope ImmediateEnvelope = MakeValidEnvelope(*Manager);
	TestTrue(TEXT("Synchronous transport starts"), Manager->StartEnvelopeTransfer(ImmediateEnvelope));
	TestEqual(
		TEXT("Synchronous acknowledgement is not lost"),
		Manager->GetHandoverState(ImmediateEnvelope.TransferId),
		EProject_JHandoverState::Completed);

	Manager->SetTransport(Transport);
	Transport->SetResponseOutcome(EProject_JHandoverTransportOutcome::Rejected);
	const FProject_JHandoverEnvelope RejectedEnvelope = MakeValidEnvelope(*Manager);
	TestTrue(TEXT("Rejected scenario starts"), Manager->StartEnvelopeTransfer(RejectedEnvelope));
	Manager->TickHandover(0.0f);
	TestEqual(
		TEXT("Destination rejection fails without committing authority"),
		Manager->GetHandoverState(RejectedEnvelope.TransferId),
		EProject_JHandoverState::Failed);

	Transport->SetResponseOutcome(EProject_JHandoverTransportOutcome::Accepted);
	Transport->SetDropResponses(true);
	const FProject_JHandoverEnvelope TimeoutEnvelope = MakeValidEnvelope(*Manager);
	TestTrue(TEXT("Timeout scenario starts"), Manager->StartEnvelopeTransfer(TimeoutEnvelope));
	for (int32 Attempt = 0; Attempt < 3; ++Attempt)
	{
		Manager->TickHandover(6.0f);
		Manager->TickHandover(1.0f);
	}
	TestEqual(
		TEXT("Dropped acknowledgements exhaust retries"),
		Manager->GetHandoverState(TimeoutEnvelope.TransferId),
		EProject_JHandoverState::Failed);

	FProject_JHandoverRecord TimeoutRecord;
	TestTrue(TEXT("Timeout record remains inspectable"), Manager->GetHandoverRecord(TimeoutEnvelope.TransferId, TimeoutRecord));
	TestEqual(TEXT("Initial attempt plus two retries were sent"), TimeoutRecord.AttemptCount, 3);
	TestEqual(
		TEXT("Final timeout reason is retained"),
		TimeoutRecord.FailureReason,
		EProject_JHandoverFailureReason::TimedOut);

	Transport->SetDropResponses(true);
	AActor* SourceActor = NewObject<AActor>(GetTransientPackage());
	const FProject_JHandoverEnvelope FirstActorEnvelope = MakeValidEnvelope(*Manager);
	const FProject_JHandoverEnvelope DuplicateActorEnvelope = MakeValidEnvelope(*Manager);
	TestTrue(
		TEXT("An actor can start one active handover"),
		Manager->StartEnvelopeTransfer(FirstActorEnvelope, SourceActor));
	TestFalse(
		TEXT("The same actor cannot start a second active handover"),
		Manager->StartEnvelopeTransfer(DuplicateActorEnvelope, SourceActor));
	Manager->CancelHandover(SourceActor);
	TestFalse(TEXT("Cancelled actor handover is not active"), Manager->IsHandoverInProgress(SourceActor));
	TestTrue(TEXT("The actor can hand over again while the previous record is retained"),
		Manager->StartEnvelopeTransfer(DuplicateActorEnvelope, SourceActor));
	TestTrue(TEXT("A retained terminal record does not hide the actor's new active transfer"),
		Manager->IsHandoverInProgress(SourceActor));
	Manager->CancelHandover(SourceActor);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectJHandoverTransportReentrancyTest,
	"ProjectJ.Architecture.Handover.TransportReentrancy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJHandoverTransportReentrancyTest::RunTest(const FString& Parameters)
{
	FProject_JLoopbackHandoverTransport Transport;
	Transport.SetResponseDelay(1.0f);
	FProject_JHandoverTransportRequest First, Second, FollowUp;
	First.Envelope.TransferId = FGuid::NewGuid();
	Second.Envelope.TransferId = FGuid::NewGuid();
	FollowUp.Envelope.TransferId = FGuid::NewGuid();
	int32 InitialCompletions = 0;
	int32 FollowUpCompletions = 0;
	auto OnInitialResponse = [&](const FProject_JHandoverTransportResponse& Response)
	{
		++InitialCompletions;
		// Exercise self cancellation and cancellation of another ready request.
		Transport.Cancel(First.Envelope.TransferId);
		Transport.Cancel(Second.Envelope.TransferId);
		Transport.Send(FollowUp, [&](const FProject_JHandoverTransportResponse&)
		{
			++FollowUpCompletions;
		});
	};
	Transport.Send(First, OnInitialResponse);
	Transport.Send(Second, OnInitialResponse);
	Transport.Tick(1.0f);
	TestEqual(TEXT("Cancellation inside completion suppresses the other ready response"), InitialCompletions, 1);
	TestEqual(TEXT("New work is not advanced by the tick that created it"), FollowUpCompletions, 0);
	Transport.Tick(1.0f);
	TestEqual(TEXT("New response survives callback queue mutation and completes once"), FollowUpCompletions, 1);
	Transport.Tick(1.0f);
	TestEqual(TEXT("No response is delivered twice"), FollowUpCompletions, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectJHandoverNotificationReentrancyTest,
	"ProjectJ.Architecture.Handover.NotificationReentrancy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJHandoverNotificationReentrancyTest::RunTest(const FString& Parameters)
{
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	UProject_JHandoverManager* Manager = NewObject<UProject_JHandoverManager>(GameInstance);
	auto Transport = MakeShared<FImmediateAcceptHandoverTransport>();
	Transport->bReportDispatchSuccess = false; // Completion wins over a late dispatch failure.
	Manager->SetTransport(Transport);
	UProject_JHandoverTestReceiver* Receiver = NewObject<UProject_JHandoverTestReceiver>();
	Manager->OnHandoverStateChanged.AddDynamic(Receiver, &UProject_JHandoverTestReceiver::HandleState);

	const auto Expanded = MakeValidEnvelope(*Manager);
	Receiver->OnState = [&](FGuid Id, EProject_JHandoverState State)
	{
		if (Id == Expanded.TransferId && State == EProject_JHandoverState::Sending)
		{
			for (int32 Index = 0; Index < 64; ++Index)
			{
				Manager->StartEnvelopeTransfer(MakeValidEnvelope(*Manager));
			}
		}
	};
	TestTrue(TEXT("A notification can start additional transfers"), Manager->StartEnvelopeTransfer(Expanded));
	TestEqual(TEXT("Map growth during notification preserves the original transfer"),
		Manager->GetHandoverState(Expanded.TransferId), EProject_JHandoverState::Completed);

	const auto Cancelled = MakeValidEnvelope(*Manager);
	Receiver->OnState = [&](FGuid Id, EProject_JHandoverState State)
	{
		if (Id == Cancelled.TransferId && State == EProject_JHandoverState::Ghosting)
		{
			Manager->CancelHandoverByTransferId(Id);
		}
	};
	Manager->StartEnvelopeTransfer(Cancelled);
	TestEqual(TEXT("Cancellation during acknowledgement cannot be overwritten by completion"),
		Manager->GetHandoverState(Cancelled.TransferId), EProject_JHandoverState::Cancelled);

	const auto Shutdown = MakeValidEnvelope(*Manager);
	Receiver->OnState = [&](FGuid Id, EProject_JHandoverState State)
	{
		if (Id == Shutdown.TransferId && State == EProject_JHandoverState::Preparing) { Manager->Deinitialize(); }
	};
	Manager->StartEnvelopeTransfer(Shutdown);
	TestEqual(TEXT("Shutdown inside a notification discards state safely"),
		Manager->GetHandoverState(Shutdown.TransferId), EProject_JHandoverState::None);
	TestFalse(TEXT("Shutdown cannot admit later work"), Manager->StartEnvelopeTransfer(MakeValidEnvelope(*Manager)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectJSocialMembershipTest,
	"ProjectJ.Architecture.Social.Membership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJSocialMembershipTest::RunTest(const FString& Parameters)
{
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	UProject_JSocialSubsystem* SocialSubsystem =
		NewObject<UProject_JSocialSubsystem>(GameInstance);
	AProject_JPlayerState* Leader = NewObject<AProject_JPlayerState>(GetTransientPackage());
	AProject_JPlayerState* Member = NewObject<AProject_JPlayerState>(GetTransientPackage());

	FProject_JAccountId LeaderAccount;
	LeaderAccount.Value = FGuid::NewGuid();
	FProject_JCharacterId LeaderCharacter;
	LeaderCharacter.Value = FGuid::NewGuid();
	Leader->SetIdentity(LeaderAccount, LeaderCharacter);

	FProject_JAccountId MemberAccount;
	MemberAccount.Value = FGuid::NewGuid();
	FProject_JCharacterId MemberCharacter;
	MemberCharacter.Value = FGuid::NewGuid();
	Member->SetIdentity(MemberAccount, MemberCharacter);

	const FProject_JSocialOperationResult Party = SocialSubsystem->CreateParty(Leader);
	TestTrue(TEXT("Leader creates a server-authoritative party"), Party.bSucceeded);
	TestTrue(TEXT("Member joins the existing party"), SocialSubsystem->JoinParty(Member, Party.GroupId).bSucceeded);
	TestEqual(
		TEXT("Leader replicated snapshot receives party id"),
		Leader->GetPartyId_Implementation(),
		Party.GroupId);
	TestEqual(
		TEXT("Member replicated snapshot receives party id"),
		Member->GetPartyId_Implementation(),
		Party.GroupId);
	TestEqual(
		TEXT("Party leader snapshot is visible to the leader"),
		Leader->GetPartyLeaderCharacterId(),
		LeaderCharacter.Value);
	TestEqual(
		TEXT("Party leader snapshot is visible to members"),
		Member->GetPartyLeaderCharacterId(),
		LeaderCharacter.Value);

	TestTrue(TEXT("Leader leaves party"), SocialSubsystem->LeaveParty(Leader).bSucceeded);
	TestEqual(
		TEXT("Remaining member receives transferred leadership"),
		Member->GetPartyLeaderCharacterId(),
		MemberCharacter.Value);
	TestTrue(TEXT("Member leaves party"), SocialSubsystem->LeaveParty(Member).bSucceeded);
	TestTrue(TEXT("Leaving clears replicated party snapshot"), Member->GetPartyId_Implementation().IsNone());
	TestFalse(
		TEXT("Leaving clears the party leader snapshot"),
		Member->GetPartyLeaderCharacterId().IsValid());

	const FProject_JSocialOperationResult Guild = SocialSubsystem->CreateGuild(Leader);
	TestTrue(TEXT("Leader creates a guild"), Guild.bSucceeded);
	TestTrue(TEXT("Trusted restore joins reconnecting member to guild"), SocialSubsystem->RestoreMembership(Member, NAME_None, Guild.GroupId));
	TestEqual(TEXT("Restored guild snapshot is synchronized"), Member->GetGuildId_Implementation(), Guild.GroupId);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectJPlayerCharacterSerializationTest,
	"ProjectJ.Architecture.Handover.PlayerCharacterSerialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJPlayerCharacterSerializationTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("Engine is available for the test world context"), GEngine))
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TestSerializationWorld"));
	if (!World)
	{
		return false;
	}
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	// Actor destruction needs a registered context. Keep it alive through world
	// cleanup, and release both resources on success and every early return.
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		TestNull(TEXT("Serialization fixture releases its world context"), GEngine->GetWorldContextFromWorld(World));
	};

	FString PlayerClassPathString = TEXT("/Game/Character_BPs/BP_Player.BP_Player_C");
	GConfig->GetString(TEXT("ProjectJ.Tests"), TEXT("PlayerCharacterClassPath"), PlayerClassPathString, GEngineIni);

	FSoftClassPath PlayerClassPath(PlayerClassPathString);
	UClass* PlayerClass = PlayerClassPath.TryLoadClass<AProject_JPlayerCharacter>();
	TestNotNull(TEXT("Configured player character class must be loaded"), PlayerClass);
	if (!PlayerClass)
	{
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.bNoFail = true;

	AProject_JPlayerCharacter* SourceCharacter = World->SpawnActor<AProject_JPlayerCharacter>(PlayerClass, SpawnParams);
	TestNotNull(TEXT("Source Character must be spawned"), SourceCharacter);
	if (!SourceCharacter)
	{
		return false;
	}

	const int32 SourceLevel = 42;
	const FVector SourceLocation(1234.5f, 6789.0f, 150.0f);
	const FRotator SourceRotation(0.0f, 90.0f, 0.0f);

	SourceCharacter->SetCharacterLevel(SourceLevel);
	SourceCharacter->SetActorLocationAndRotation(SourceLocation, SourceRotation, false, nullptr, ETeleportType::TeleportPhysics);

	TArray<uint8> Buffer;
	SourceCharacter->SerializeForHandover(Buffer);

	TestTrue(TEXT("Payload size must be less than 1KB"), Buffer.Num() < 1024);
	TestTrue(TEXT("Payload size must be greater than 0"), Buffer.Num() > 0);

	AProject_JPlayerCharacter* DestCharacter = World->SpawnActor<AProject_JPlayerCharacter>(PlayerClass, SpawnParams);
	TestNotNull(TEXT("Destination Character must be spawned"), DestCharacter);
	if (!DestCharacter)
	{
		if (SourceCharacter)
		{
			SourceCharacter->Destroy();
		}
		return false;
	}

	DestCharacter->SetCharacterLevel(1);
	DestCharacter->SetActorLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);

	DestCharacter->DeserializeFromHandover(Buffer);

	const int32 DestLevel = DestCharacter->GetCharacterLevel_Implementation();
	TestEqual(TEXT("Restored level must match source level"), DestLevel, SourceLevel);

	const FVector DestLocation = DestCharacter->GetActorLocation();
	const FRotator DestRotator = DestCharacter->GetActorRotation();

	TestTrue(TEXT("Restored Location matches source location"), DestLocation.Equals(SourceLocation, 1.0f));
	TestTrue(TEXT("Restored Rotation matches source rotation"), DestRotator.Equals(SourceRotation, 1.0f));

	if (SourceCharacter)
	{
		SourceCharacter->Destroy();
	}
	if (DestCharacter)
	{
		DestCharacter->Destroy();
	}
	return true;
}

#endif
