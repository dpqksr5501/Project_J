#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Backend/Project_JHttpHandoverTransport.h"
#include "Backend/Project_JOnlineWorldSubsystem.h"
#include "Backend/Project_JServerDrainSubsystem.h"
#include "Backend/Project_JServerMetricsSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "IPAddress.h"
#include "Misc/ScopeExit.h"
#include "Network/Project_JCrowdDensityNetObjectFilter.h"
#include "Optimization/Project_JServerLoadGovernorSubsystem.h"
#include "Security/Project_JServerIntegritySubsystem.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

namespace
{
constexpr auto AAAServerTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

FString FetchLocalHttpResponse(int32 Port, const FString& Path, UProject_JServerMetricsSubsystem* Metrics)
{
	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!Sockets) return FString();
	FSocket* Client = Sockets->CreateSocket(NAME_Stream, TEXT("MetricsTestClient"), false);
	if (!Client) return FString();
	ON_SCOPE_EXIT
	{
		Client->Close();
		Sockets->DestroySocket(Client);
	};

	TSharedRef<FInternetAddr> Addr = Sockets->CreateInternetAddr();
	bool bValid = false;
	Addr->SetIp(TEXT("127.0.0.1"), bValid);
	Addr->SetPort(Port);
	if (!bValid || !Client->Connect(*Addr)) return FString();

	const FString Req = FString::Printf(TEXT("GET %s HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"), *Path);
	const FTCHARToUTF8 ReqUtf8(*Req);
	int32 Sent = 0;
	const auto* Wire = reinterpret_cast<const uint8*>(ReqUtf8.Get());
	const int32 Partial = 7;
	if (!Client->Send(Wire, Partial, Sent) || Sent != Partial) return FString();
	if (Metrics) Metrics->PollMetricsEndpoint();
	if (!Client->Send(Wire + Partial, ReqUtf8.Length() - Partial, Sent) || Sent != ReqUtf8.Length() - Partial) return FString();

	// Pump the server socket on GT so the non-blocking listener accepts and replies immediately.
	if (Metrics)
	{
		Metrics->PollMetricsEndpoint();
	}

	TArray<uint8> Bytes;
	uint8 Chunk[2048];
	const double Deadline = FPlatformTime::Seconds() + 2.0;
	while (FPlatformTime::Seconds() < Deadline)
	{
		if (Metrics) Metrics->PollMetricsEndpoint();
		Client->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(20));
		int32 Read = 0;
		if (Client->Recv(Chunk, sizeof(Chunk), Read) && Read > 0)
		{
			Bytes.Append(Chunk, Read);
		}
		else if (!Bytes.IsEmpty())
		{
			break;
		}
	}
	const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
	return FString(Text.Length(), Text.Get());
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGovernorAndCrowdDensity500Test, "ProjectJ.Server.GovernorAndCrowdDensity500", AAAServerTestFlags)
bool FProjectJGovernorAndCrowdDensity500Test::RunTest(const FString&)
{
	UGameInstance* GI = NewObject<UGameInstance>(GEngine);
	GI->AddToRoot();
	GI->Init();
	ON_SCOPE_EXIT
	{
		GI->Shutdown();
		GI->RemoveFromRoot();
	};

	auto* Gov = GI->GetSubsystem<UProject_JServerLoadGovernorSubsystem>();
	if (!TestNotNull(TEXT("ServerLoadGovernor subsystem exists"), Gov)) return false;
	AddExpectedMessage(TEXT("ServerLoadGovernor escalated"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);

	// 1) Verify single-tick spike does not falsely escalate.
	double T = 100.0;
	for (int32 i = 0; i < 20; ++i)
	{
		Gov->RecordTickSample(8.0, 200, T);
		T += 0.033;
	}
	Gov->RecordTickSample(35.0, 200, T);
	TestEqual(TEXT("Single-tick spike stays Green"), Gov->GetCurrentTier(), EProject_JServerLoadTier::Green);
	TestEqual(TEXT("Over-budget counter incremented"), Gov->GetSnapshot().OverBudgetTicksTotal, static_cast<int64>(1));

	// 2) Sustained 22ms load escalates Green -> Yellow after EscalateHoldSeconds.
	for (int32 i = 0; i < 160; ++i)
	{
		Gov->RecordTickSample(22.0, 200, T);
		T += 0.033;
	}
	TestEqual(TEXT("Sustained 22ms escalates to Yellow"), Gov->GetCurrentTier(), EProject_JServerLoadTier::Yellow);
	TestEqual(TEXT("Yellow scales crowd cap 60 -> 42"), Gov->GetEffectiveCrowdCap(60), 42);
	TestTrue(TEXT("Yellow throttles non-critical cosmetics"), Gov->ShouldThrottleNonCriticalCosmetics());

	// 3) Sustained 29ms load escalates Yellow -> Red.
	for (int32 i = 0; i < 160; ++i)
	{
		Gov->RecordTickSample(29.0, 200, T);
		T += 0.033;
	}
	TestEqual(TEXT("Sustained 29ms escalates to Red"), Gov->GetCurrentTier(), EProject_JServerLoadTier::Red);
	TestEqual(TEXT("Red scales crowd cap 60 -> 27"), Gov->GetEffectiveCrowdCap(60), 27);
	TestEqual(TEXT("Pressure does not raise a deliberately smaller base cap"), Gov->GetEffectiveCrowdCap(4), 4);
	TestEqual(TEXT("Red scales NPC decision cadence 2.5x"), Gov->GetNpcDecisionCadenceScale(), 2.5f);

	// 4) Sustained 10ms load recovers back to Green after RecoverHoldSeconds.
	for (int32 i = 0; i < 400; ++i)
	{
		Gov->RecordTickSample(10.0, 200, T);
		T += 0.05;
	}
	TestEqual(TEXT("Sustained 10ms recovers back to Green"), Gov->GetCurrentTier(), EProject_JServerLoadTier::Green);
	TestEqual(TEXT("Green restores full crowd cap 60"), Gov->GetEffectiveCrowdCap(60), 60);

	// 5) 500-player boss crowd culling stress test with exemptions & hysteresis.
	TArray<FProject_JCrowdDensityCandidate> Crowd;
	Crowd.Reserve(505);
	// Object 1 = Giant Boss at 95m (must never be culled).
	Crowd.Add({1u, FVector(9500.0, 0.0, 0.0), 12000.0 * 12000.0, 4.0f, true});
	// Objects 2..5 = Party members at 150m (beyond normal cull distance, exempt -> never culled).
	for (uint32 Id = 2; Id <= 5; ++Id)
	{
		Crowd.Add({Id, FVector(15000.0, Id * 100.0, 0.0), 12000.0 * 12000.0, 1.0f, true});
	}
	// Objects 100..599 = 500 non-exempt remote players clustered at 5m..100m.
	for (uint32 i = 0; i < 500; ++i)
	{
		const bool bCombatActive = (i % 10 == 0);
		Crowd.Add({100u + i, FVector(500.0 + i * 18.0, 0.0, 0.0), 12000.0 * 12000.0, bCombatActive ? 4.0f : 1.0f, false});
	}

	TSet<uint32> PrevAllowed;
	TSet<uint32> AllowedGreen;
	int32 CulledGreen = 0;
	FProject_JCrowdDensitySelectionCounts GreenCounts;
	FProject_JCrowdDensityBudgetSolver::EvaluateForViewer(
		FVector::ZeroVector, Crowd, PrevAllowed, 60, 1.35f, AllowedGreen, CulledGreen, &GreenCounts);
	TestEqual(TEXT("Five exempt pairs bypass both limits"), GreenCounts.Exempt, 5);
	TestEqual(TEXT("All 440 losses are due to K"), GreenCounts.OverCap, 440);
	TestEqual(TEXT("No non-exempt candidates outside distance"), GreenCounts.OutsideDistance, 0);

	TestTrue(TEXT("Boss always allowed"), AllowedGreen.Contains(1u));
	for (uint32 Id = 2; Id <= 5; ++Id)
	{
		TestTrue(TEXT("Party member always allowed"), AllowedGreen.Contains(Id));
	}
	TestEqual(TEXT("Total allowed = 5 exempt + 60 capped non-exempt"), AllowedGreen.Num(), 65);
	TestEqual(TEXT("Exact 440 remote players culled in Green"), CulledGreen, 440);

	// Red tier tightens cap to 27 while keeping all 5 exempt actors.
	TSet<uint32> AllowedRed;
	int32 CulledRed = 0;
	FProject_JCrowdDensityBudgetSolver::EvaluateForViewer(
		FVector::ZeroVector, Crowd, AllowedGreen, 27, 1.35f, AllowedRed, CulledRed);
	TestEqual(TEXT("Red tier allows 5 exempt + 27 capped non-exempt"), AllowedRed.Num(), 32);
	TestEqual(TEXT("Red tier culls 473 remote players"), CulledRed, 473);
	TestTrue(TEXT("Boss still allowed in Red"), AllowedRed.Contains(1u));

	// Hysteresis stability check: an existing boundary member nudged slightly further retains its slot over a newcomer.
	TArray<FProject_JCrowdDensityCandidate> Boundary;
	Boundary.Add({10u, FVector(1050.0, 0.0, 0.0), 12000.0 * 12000.0, 1.0f, false}); // previously allowed, slightly further
	Boundary.Add({11u, FVector(1000.0, 0.0, 0.0), 12000.0 * 12000.0, 1.0f, false}); // newcomer, slightly closer
	TSet<uint32> BoundaryPrev = {10u};
	TSet<uint32> BoundaryOut;
	int32 BoundaryCulled = 0;
	FProject_JCrowdDensityBudgetSolver::EvaluateForViewer(
		FVector::ZeroVector, Boundary, BoundaryPrev, 1, 1.35f, BoundaryOut, BoundaryCulled);
	TestTrue(TEXT("Hysteresis retention prevents boundary flicker"), BoundaryOut.Contains(10u) && !BoundaryOut.Contains(11u));

	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPrometheusMetricsAndDrainTest, "ProjectJ.Server.PrometheusMetricsAndGracefulDrain", AAAServerTestFlags)
bool FProjectJPrometheusMetricsAndDrainTest::RunTest(const FString&)
{
	UGameInstance* GI = NewObject<UGameInstance>(GEngine);
	GI->AddToRoot();
	GI->Init();
	ON_SCOPE_EXIT
	{
		GI->Shutdown();
		GI->RemoveFromRoot();
	};

	auto* Metrics = GI->GetSubsystem<UProject_JServerMetricsSubsystem>();
	auto* Drain = GI->GetSubsystem<UProject_JServerDrainSubsystem>();
	auto* Gov = GI->GetSubsystem<UProject_JServerLoadGovernorSubsystem>();
	if (!TestNotNull(TEXT("Metrics subsystem"), Metrics)
		|| !TestNotNull(TEXT("Drain subsystem"), Drain)
		|| !TestNotNull(TEXT("Governor subsystem"), Gov))
	{
		return false;
	}

	Gov->RecordTickSample(6.5, 42, FPlatformTime::Seconds());
	Gov->RecordTickSample(6.5, 42, FPlatformTime::Seconds(), 0.034);
	Gov->RecordTickSample(6.5, 42, FPlatformTime::Seconds(), 1.200);
	if (!TestTrue(TEXT("Metrics loopback HTTP listener starts on ephemeral port"), Metrics->StartMetricsEndpoint(0)))
	{
		return false;
	}
	const int32 Port = Metrics->GetBoundPort();
	auto* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	auto Address = Sockets->CreateInternetAddr();
	bool bValidAddress = false;
	Address->SetIp(TEXT("127.0.0.1"), bValidAddress);
	Address->SetPort(Port);
	TArray<FSocket*> QuietClients;
	ON_SCOPE_EXIT
	{
		for (auto* Socket : QuietClients) Sockets->DestroySocket(Socket);
	};
	for (int32 Index = 0; Index < 8 && bValidAddress; ++Index)
	{
		auto* Socket = Sockets->CreateSocket(NAME_Stream, TEXT("MetricsSlowReadTest"), false);
		if (!Socket) break;
		QuietClients.Add(Socket);
		if (!Socket->Connect(*Address)) break;
	}
	TestEqual(TEXT("Eight quiet peers connected on loopback"), QuietClients.Num(), 8);
	const double PollStarted = FPlatformTime::Seconds();
	Metrics->PollMetricsEndpoint();
	TestTrue(TEXT("Quiet peers do not impose eight socket waits on the game thread"), FPlatformTime::Seconds() - PollStarted < 0.12);

	TestTrue(TEXT("Bound ephemeral port > 0"), Port > 0);

	// 1) /healthz returns 200 OK while InService.
	const FString HealthOk = FetchLocalHttpResponse(Port, TEXT("/healthz"), Metrics);
	TestTrue(TEXT("/healthz reports HTTP 200 OK when InService"), HealthOk.Contains(TEXT("200 OK")) && HealthOk.Contains(TEXT("\"status\":\"OK\"")));

	// 2) /metrics returns valid Prometheus 0.0.4 exposition text.
	const FString PromText = FetchLocalHttpResponse(Port, TEXT("/metrics"), Metrics);
	TestTrue(TEXT("/metrics returns HTTP 200 OK"), PromText.Contains(TEXT("200 OK")));
	TestTrue(TEXT("/metrics contains tick gauge"), PromText.Contains(TEXT("projectj_server_tick_ms{quantile=\"0.95\"}")));
	TestTrue(TEXT("Fleet histogram uses a fixed cadence bucket and cumulative counts"),
		PromText.Contains(TEXT("# TYPE projectj_server_tick_interval_seconds histogram\n"))
		&& PromText.Contains(TEXT("projectj_server_tick_interval_seconds_bucket{le=\"0.030000\"} 0\n"))
		&& PromText.Contains(TEXT("projectj_server_tick_interval_seconds_bucket{le=\"0.035000\"} 1\n"))
		&& PromText.Contains(TEXT("projectj_server_tick_interval_seconds_bucket{le=\"+Inf\"} 2\n"))
		&& PromText.Contains(TEXT("projectj_server_tick_interval_seconds_sum 1.234000000\n"))
		&& PromText.Contains(TEXT("projectj_server_tick_interval_seconds_count 2\n")));
	TestTrue(TEXT("/metrics contains CCU=42"), PromText.Contains(TEXT("projectj_server_active_ccu 42")));
	TestTrue(TEXT("/metrics contains outbox dead letters"), PromText.Contains(TEXT("projectj_outbox_dead_letters")));
	TestTrue(TEXT("/metrics exports empty age and capacity-rejection series"),
		PromText.Contains(TEXT("projectj_outbox_oldest_active_age_seconds 0.000\n"))
		&& PromText.Contains(TEXT("projectj_outbox_rejected_capacity_total 0\n")));
	TestTrue(TEXT("/metrics contains integrity counter"), PromText.Contains(TEXT("projectj_integrity_movement_violations_total")));

	// 3) BeginGracefulDrain flips /healthz to 503 and transitions to ReadyToExit when 0 players remain.
	AddExpectedMessage(TEXT("Graceful Server Drain started"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestTrue(TEXT("BeginGracefulDrain starts cleanly"), Drain->BeginGracefulDrain(TEXT("PatchTest"), 0.0f, TEXT("127.0.0.1:7778"), false));
	TestEqual(TEXT("Drain reaches ReadyToExit with 0 players and settled outbox"),
		Drain->GetDrainState(), EProject_JServerDrainState::ReadyToExit);
	TestFalse(TEXT("Duplicate BeginGracefulDrain call is idempotent/ignored"),
		Drain->BeginGracefulDrain(TEXT("SecondCall"), 0.0f));

	const FString HealthDraining = FetchLocalHttpResponse(Port, TEXT("/healthz"), Metrics);
	TestTrue(TEXT("/healthz returns HTTP 503 Service Unavailable while Draining"),
		HealthDraining.Contains(TEXT("503 Service Unavailable")) && HealthDraining.Contains(TEXT("\"draining\":true")));

	Metrics->StopMetricsEndpoint();
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJHttpHandoverHmacAndIntegrityTest, "ProjectJ.Server.HttpHandoverHmacAndIntegrityAudit", AAAServerTestFlags)
bool FProjectJHttpHandoverHmacAndIntegrityTest::RunTest(const FString&)
{
	// 1) RFC 4231 Test Case 2 verification + HTTP Handover HMAC-SHA256 signing & replay/tamper rejection.
	{
		const FTCHARToUTF8 RfcMsg(TEXT("what do ya want for nothing?"));
		TArray<uint8> RfcBytes;
		RfcBytes.Append(reinterpret_cast<const uint8*>(RfcMsg.Get()), RfcMsg.Length());
		TestEqual(TEXT("HMAC-SHA256 matches RFC 4231 Test Case 2 vector"),
			FProject_JHttpHandoverTransport::ComputeHmacSha256Hex(TEXT("Jefe"), RfcBytes),
			FString(TEXT("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843")));
	}
	struct FHmacVector { int32 MessageBytes; int32 KeyBytes; const TCHAR* Digest; };
	const FHmacVector Vectors[] = {
		{0, 4, TEXT("78e5aa9c02020842cef2c6e26ee3d3b49d0d5625b23c2231303f3e00df21a4b5")},
		{55, 4, TEXT("099b57440d8e984208862edf336eb188660044f59a10f62164945b3973227d28")},
		{56, 4, TEXT("e8a37db455da4dd65b1168236031328ab9f39f030eb851b1e6f10945ba73f283")},
		{64, 4, TEXT("7a4f94b1078f586ba13a439bddec0354cc15cc063281e79ac27ff17abc724305")},
		{129, 131, TEXT("3b4d3934cb8136fa309cb64418faafbbcfea1d0190a7cd56acc1e664c1d5b4ce")},
		{1048576, 131, TEXT("0a2785a1b4a074712876a30e568469b75873be0c3939b0630ce370d2e57869d2")},
	};
	for (const auto& Vector : Vectors)
	{
		TArray<uint8> Data;
		Data.Init('a', Vector.MessageBytes);
		TestEqual(TEXT("HMAC block boundaries and long key match independent digest"),
			FProject_JHttpHandoverTransport::ComputeHmacSha256Hex(FString::ChrN(Vector.KeyBytes, 'x'), Data), FString(Vector.Digest));
	}
	const FString Secret = TEXT("project-j-zone-handover-secret-key-2026");
	auto Transport = MakeShared<FProject_JHttpHandoverTransport>(Secret);
	TestTrue(TEXT("Loopback endpoint allowed"), Transport->RegisterNodeEndpoint(TEXT("ZoneB"), TEXT("http://127.0.0.1:18080/handover")));
	TestTrue(TEXT("HTTPS endpoint allowed"), Transport->RegisterNodeEndpoint(TEXT("ZoneC"), TEXT("https://zone-c.internal/handover")));
	TestFalse(TEXT("Plain remote HTTP endpoint rejected"), Transport->RegisterNodeEndpoint(TEXT("Bad"), TEXT("http://example.invalid/handover")));
	TestFalse(TEXT("Empty TLS authority rejected"), Transport->RegisterNodeEndpoint(TEXT("Bad"), TEXT("https://")));
	TestFalse(TEXT("URL user information rejected"), Transport->RegisterNodeEndpoint(TEXT("Bad"), TEXT("https://user@example.invalid/handover")));
	TestFalse(TEXT("Malformed bind port rejected"), Transport->RegisterNodeEndpoint(TEXT("Bad"), TEXT("http://127.0.0.1:65536/handover")));

	FProject_JHandoverEnvelope Env;
	Env.Version = 1;
	Env.TransferId = FGuid::NewGuid();
	Env.ActorClassPath = TEXT("/Script/Project_JCharacter.Project_JSpellbladeCharacter");
	Env.Payload = {0x50, 0x4A, 0x01, 0x02, 0xFF};
	Env.Timestamp = FDateTime(FDateTime::UtcNow().GetTicks() | 1);
	Env.SourceNodeId = TEXT("ZoneA");
	Env.TargetNodeId = TEXT("ZoneB");
	Env.PayloadChecksum = 123456;

	TArray<uint8> JsonBytes;
	TestTrue(TEXT("Envelope serializes to UTF-8 JSON"), FProject_JHttpHandoverTransport::SerializeEnvelopeJson(Env, JsonBytes));

	const int64 NowUnix = FDateTime::UtcNow().ToUnixTimestamp();
	const FString Prefix = FString::Printf(TEXT("%lld."), NowUnix);
	const FTCHARToUTF8 PrefixUtf8(*Prefix);
	TArray<uint8> SignedMsg;
	SignedMsg.Append(reinterpret_cast<const uint8*>(PrefixUtf8.Get()), PrefixUtf8.Length());
	SignedMsg.Append(JsonBytes);
	const FString ValidSig = FProject_JHttpHandoverTransport::ComputeHmacSha256Hex(Secret, SignedMsg);
	TestEqual(TEXT("HMAC-SHA256 hex length == 64"), ValidSig.Len(), 64);

	FProject_JHandoverEnvelope Parsed;
	FString Failure;
	TestTrue(TEXT("Valid signed handover envelope verifies and parses"),
		FProject_JHttpHandoverTransport::VerifyAndParseSignedEnvelope(Secret, ValidSig, NowUnix, JsonBytes, NowUnix + 2, Parsed, Failure));
	TestEqual(TEXT("TransferId round-trips"), Parsed.TransferId, Env.TransferId);
	TestEqual(TEXT("Timestamp lower ticks round-trip without JSON rounding"), Parsed.Timestamp.GetTicks(), Env.Timestamp.GetTicks());
	TestTrue(TEXT("Binary payload round-trips bit-for-bit"), Parsed.Payload == Env.Payload);

	// Tamper 1 byte -> must fail HMAC verification.
	TArray<uint8> Tampered = JsonBytes;
	Tampered[Tampered.Num() - 1] ^= 0x01;
	TestFalse(TEXT("Tampered payload rejected"),
		FProject_JHttpHandoverTransport::VerifyAndParseSignedEnvelope(Secret, ValidSig, NowUnix, Tampered, NowUnix + 2, Parsed, Failure));

	// Stale timestamp (> 30s skew) -> must fail replay window.
	TestFalse(TEXT("Expired timestamp rejected"),
		FProject_JHttpHandoverTransport::VerifyAndParseSignedEnvelope(Secret, ValidSig, NowUnix - 120, JsonBytes, NowUnix, Parsed, Failure));

	// 2) Live Integrity Subsystem escalation, whitelist, audit trail, and logout cleanup.
	UGameInstance* GI = NewObject<UGameInstance>(GEngine);
	GI->AddToRoot();
	GI->Init();
	ON_SCOPE_EXIT
	{
		GI->Shutdown();
		GI->RemoveFromRoot();
	};
	auto* Integrity = GI->GetSubsystem<UProject_JServerIntegritySubsystem>();
	if (!TestNotNull(TEXT("Integrity subsystem"), Integrity)) return false;

	const FGuid CharId = FGuid::NewGuid();
	const double T0 = 500.0;
	Integrity->GrantMovementWhitelist(CharId, 1.0, T0);
	TestEqual(TEXT("Whitelisted dash/teleport is accepted"),
		Integrity->ValidateMovementStep(CharId, FVector::ZeroVector, FVector(3000.0, 0.0, 0.0), 600.0f, 0.25, T0 + 0.5),
		EProject_JIntegrityAction::Accept);

	// After whitelist expires, 5 speedhack steps escalate SnapBack -> FlagAudit -> DisconnectRecommended.
	EProject_JIntegrityAction LastAction = EProject_JIntegrityAction::Accept;
	for (int32 i = 0; i < 5; ++i)
	{
		LastAction = Integrity->ValidateMovementStep(CharId, FVector::ZeroVector, FVector(5000.0, 0.0, 0.0), 600.0f, 0.25, T0 + 2.0 + i * 0.25);
	}
	TestEqual(TEXT("5 violations escalate to DisconnectRecommended"), LastAction, EProject_JIntegrityAction::DisconnectRecommended);
	TestEqual(TEXT("5 audit events recorded"), Integrity->GetRecentAuditEvents().Num(), 5);
	TestEqual(TEXT("MovementViolations counter == 5"), Integrity->GetTelemetrySnapshot().MovementViolations, static_cast<int64>(5));

	Integrity->ClearCharacterState(CharId);
	TestEqual(TEXT("After logout ClearCharacterState, first violation resets to SnapBack"),
		Integrity->ValidateMovementStep(CharId, FVector::ZeroVector, FVector(5000.0, 0.0, 0.0), 600.0f, 0.25, T0 + 5.0),
		EProject_JIntegrityAction::SnapBack);

	return !HasAnyErrors();
}
#endif
