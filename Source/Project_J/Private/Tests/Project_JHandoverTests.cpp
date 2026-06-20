#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/Project_JHandoverManager.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectJHandoverEnvelopeValidationTest,
	"ProjectJ.Architecture.Handover.EnvelopeValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJHandoverEnvelopeValidationTest::RunTest(const FString& Parameters)
{
	const UProject_JHandoverManager* Manager = GetDefault<UProject_JHandoverManager>();

	FProject_JHandoverEnvelope Envelope;
	Envelope.TransferId = FGuid::NewGuid();
	Envelope.ActorClassPath = TEXT("/Script/Engine.Actor");
	Envelope.Timestamp = FDateTime::UtcNow();
	Envelope.SourceNodeId = TEXT("Source");
	Envelope.TargetNodeId = TEXT("Target");
	Envelope.PayloadChecksum = 0;

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

#endif
