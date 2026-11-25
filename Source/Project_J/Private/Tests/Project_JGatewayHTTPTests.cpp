#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Backend/Project_JGatewaySubsystem.h"
#include "Backend/Project_JNakamaServicePort.h"
#include "Backend/Project_JOnlineWorldSubsystem.h"
#include "Dom/JsonObject.h"
#include "MMO/Coordination.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/Base64.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Async/Async.h"
#include <atomic>

namespace
{
struct FGatewayHTTPState
{
	TStrongObjectPtr<UGameInstance> GameInstance;
	TStrongObjectPtr<UProject_JGatewaySubsystem> Gateway;
	std::atomic<bool> Stop {false}, ServerDone {false};
	int32 Completed = 0;
	double Started = FPlatformTime::Seconds();
};

class FWaitForGatewayHTTP final : public IAutomationLatentCommand
{
public:
	FWaitForGatewayHTTP(FAutomationTestBase* InTest, TSharedRef<FGatewayHTTPState, ESPMode::ThreadSafe> InState) : Test(InTest), State(InState) {}
	bool Update() override
	{
		if (FPlatformTime::Seconds() - State->Started > 15)
		{
			State->Stop = true;
			State->Gateway->Deinitialize();
			if (!State->ServerDone) return false; // Native listener finishes its bounded wait normally.
			Test->AddError(TEXT("Local HTTP integration did not complete four real requests"));
			return true;
		}
		if (State->Completed != 4 || !State->ServerDone) return false;
		State->Gateway->Deinitialize();
		return true;
	}
private:
	FAutomationTestBase* Test;
	TSharedRef<FGatewayHTTPState, ESPMode::ThreadSafe> State;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGatewayHTTPTest, "ProjectJ.MMO.Architecture.GatewayRealHTTP",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJGatewayHTTPTest::RunTest(const FString&)
{
	auto* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	FSocket* Listener = Sockets->CreateSocket(NAME_Stream, TEXT("ProjectJLocalHTTPTest"), false);
	if (!Listener) { AddError(TEXT("Cannot create local integration listener")); return false; }
	auto Address = Sockets->CreateInternetAddr();
	bool Valid = false; Address->SetIp(TEXT("127.0.0.1"), Valid); Address->SetPort(0);
	if (!Valid || !Listener->Bind(*Address) || !Listener->Listen(4))
	{
		Listener->Close(); Sockets->DestroySocket(Listener);
		AddError(TEXT("Cannot bind local integration listener")); return false;
	}
	Listener->GetAddress(*Address);
	Listener->SetNonBlocking(true);
	auto State = MakeShared<FGatewayHTTPState, ESPMode::ThreadSafe>();
	State->GameInstance.Reset(NewObject<UGameInstance>());
	State->Gateway.Reset(NewObject<UProject_JGatewaySubsystem>(State->GameInstance.Get()));
	auto* Gateway = State->Gateway.Get();
	Gateway->GatewayUrl = FString::Printf(TEXT("http://127.0.0.1:%d/"), Address->GetPort());
	Gateway->MaxResponseBytes = 16;
	Gateway->RequestTimeoutSeconds = 5;
	Gateway->RequestTracker = MakeShared<ProjectJ::MMO::FRequestTracker, ESPMode::ThreadSafe>(4);
	const auto Tracker = Gateway->RequestTracker;
	// Real TCP/HTTP transport. Worker handles native socket bytes only; all UObject completion stays on GT.
	Async(EAsyncExecution::Thread, [State, Listener, Sockets]
	{
		int32 Connections = 0;
		const double Deadline = FPlatformTime::Seconds() + 12;
		while (!State->Stop && Connections < 4 && FPlatformTime::Seconds() < Deadline)
		{
			bool Pending = false;
			Listener->WaitForPendingConnection(Pending, FTimespan::FromMilliseconds(50));
			if (!Pending) continue;
			FSocket* Client = Listener->Accept(TEXT("ProjectJLocalHTTPClient"));
			if (!Client) continue;
			++Connections;
			Client->SetNonBlocking(true);
			TArray<uint8> Request;
			FString RequestText;
			const double ClientDeadline = FPlatformTime::Seconds() + 2;
			while (Request.Num() < 8192 && FPlatformTime::Seconds() < ClientDeadline)
			{
				Client->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(50));
				uint8 Chunk[1024]; int32 Read = 0;
				if (Client->Recv(Chunk, sizeof(Chunk), Read) && Read > 0) Request.Append(Chunk, Read);
				if (!Request.IsEmpty())
				{
					FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Request.GetData()), Request.Num());
					RequestText = FString(Text.Length(), Text.Get());
					if (RequestText.Contains(TEXT("\r\n\r\n"))) break;
				}
			}
			const bool Large = RequestText.Contains(TEXT(" /large "));
			const bool Error = RequestText.Contains(TEXT(" /error "));
			const FString Body = Large ? FString::ChrN(2048, 'x') : TEXT("{\"ok\":true}");
			const FString Response = FString::Printf(TEXT("HTTP/1.1 %s\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s"),
				Error ? TEXT("503 Unavailable") : TEXT("200 OK"), Body.Len(), *Body);
			FTCHARToUTF8 Wire(*Response); int32 Sent = 0;
			while (Sent < Wire.Length() && FPlatformTime::Seconds() < ClientDeadline)
			{
				int32 Count = 0;
				if (!Client->Send(reinterpret_cast<const uint8*>(Wire.Get()) + Sent, Wire.Length() - Sent, Count)) break;
				Sent += Count;
			}
			Client->Close(); Sockets->DestroySocket(Client);
		}
		Listener->Close(); Sockets->DestroySocket(Listener);
		State->ServerDone = true;
	});
	FProject_JBackendRequestContext Mutation;
	Mutation.Intent = EProject_JBackendRequestIntent::Mutation;
	Mutation.IdempotencyKey = FProject_JIdempotencyKey::NewKey();
	for (int32 Case = 0; Case < 4; ++Case)
	{
		Gateway->DispatchTrackedRequest(Case == 0 ? TEXT("small") : Case == 3 ? TEXT("error") : TEXT("large"), TEXT("{}"),
			Case >= 2 ? Mutation : FProject_JBackendRequestContext(), [this, State, Case, Mutation, Tracker](const auto& Response)
		{
			TestTrue(TEXT("Actual transport completion runs on GT"), IsInGameThread());
			if (Case == 0)
			{
				TestTrue(TEXT("Small real HTTP body succeeds"), Response.bSucceeded);
				TestEqual(TEXT("Streaming retains response bytes"), Response.ResponseData, FString(TEXT("{\"ok\":true}")));
			}
			else
			{
				TestFalse(TEXT("Rejected response cannot report transport success"), Response.bSucceeded);
				TestEqual(TEXT("Oversize query versus uncertain mutation has explicit outcome"), Response.FailureKind,
					Case == 1 ? EProject_JBackendFailureKind::ResponseTooLarge : EProject_JBackendFailureKind::UnknownOutcome);
				TestEqual(TEXT("Only dispatched mutations require reconciliation"), Response.bRequiresReconciliation, Case >= 2);
				if (Case >= 2) TestEqual(TEXT("Replay retains original mutation key"), Response.RequestContext.IdempotencyKey.Value, Mutation.IdempotencyKey.Value);
			}
			++State->Completed;
			if (State->Completed == 4) TestEqual(TEXT("Completion frees every real HTTP ticket"), Tracker->Num(), 0);
		});
	}
	ADD_LATENT_AUTOMATION_COMMAND(FWaitForGatewayHTTP(this, State));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJOutboxGatewayHTTPTest, "ProjectJ.Online.OutboxGatewayHTTP",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJOutboxGatewayHTTPTest::RunTest(const FString&)
{
	auto* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	auto* Listener = Sockets->CreateSocket(NAME_Stream, TEXT("ProjectJOutboxHTTP"), false);
	if (!TestNotNull(TEXT("Outbox loopback listener"), Listener)) return false;
	auto Address = Sockets->CreateInternetAddr();
	bool bValid = false;
	Address->SetIp(TEXT("127.0.0.1"), bValid);
	Address->SetPort(0);
	if (!bValid || !Listener->Bind(*Address) || !Listener->Listen(2))
	{
		Sockets->DestroySocket(Listener);
		AddError(TEXT("Cannot listen on an ephemeral loopback port"));
		return false;
	}
	Listener->GetAddress(*Address);
	Listener->SetNonBlocking(true);

	struct FExchange
	{
		TArray<FString> Requests;
		std::atomic<bool> ServerDone{false};
		TStrongObjectPtr<UGameInstance> GameInstance{NewObject<UGameInstance>(GEngine)};
		FGuid Key = FGuid::NewGuid();
		FString OriginalRuntimeKey;
		bool bConfirmed = false;
		int32 Completed = 0;
		double Deadline = FPlatformTime::Seconds() + 15;
	};
	auto State = MakeShared<FExchange, ESPMode::ThreadSafe>();
	auto* GI = State->GameInstance.Get();
	GI->InitializeStandalone();
	auto* World = GI->GetWorld();
	World->WorldType = EWorldType::PIE;
	World->SetPlayInEditorInitialNetMode(NM_DedicatedServer);
	auto* Gateway = GI->GetSubsystem<UProject_JGatewaySubsystem>();
	auto* Port = GI->GetSubsystem<UProject_JNakamaServicePortSubsystem>();
	auto* Outbox = GI->GetSubsystem<UProject_JOnlineWorldSubsystem>();
	Port->SetRuntimeMode(EProject_JBackendRuntimeMode::LocalDockerNakama);
	const auto Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("defeat_id"), TEXT("wave3-검증"));
	bool bMapped = false;
	Gateway->GatewayUrl.Reset();
	UProject_JOnlineWorldSubsystem::Submit(World, TEXT("Reward"), TEXT("Boss"), Body, State->Key,
		[&](const auto& Reply) { bMapped = Reply && Reply->GetStringField(TEXT("status")) == TEXT("PRE_DISPATCH_REJECTED"); });
	TestTrue(TEXT("Real ServicePort maps the synchronous unconfigured Gateway rejection"), bMapped);
	Gateway->GatewayUrl = FString::Printf(TEXT("http://127.0.0.1:%d"), Address->GetPort());
	Gateway->RequestTracker = MakeShared<ProjectJ::MMO::FRequestTracker, ESPMode::ThreadSafe>(1);
	State->OriginalRuntimeKey = FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_J_NAKAMA_HTTP_KEY"));
	FPlatformMisc::SetEnvironmentVar(TEXT("PROJECT_J_NAKAMA_HTTP_KEY"), TEXT("local-wave3-audit-key"));

	Async(EAsyncExecution::Thread, [State, Listener, Sockets]
	{
		while (State->Requests.Num() < 2 && FPlatformTime::Seconds() < State->Deadline)
		{
			bool bPending = false;
			Listener->WaitForPendingConnection(bPending, FTimespan::FromMilliseconds(50));
			if (!bPending) continue;
			auto* Client = Listener->Accept(TEXT("ProjectJOutboxClient"));
			if (!Client) continue;
			Client->SetNonBlocking(true);
			TArray<uint8> Bytes;
			FString Request;
			const double End = FPlatformTime::Seconds() + 2;
			while (FPlatformTime::Seconds() < End && Bytes.Num() < 8192)
			{
				Client->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(50));
				uint8 Chunk[1024]; int32 Read = 0;
				if (!Client->Recv(Chunk, sizeof(Chunk), Read) || Read <= 0) continue;
				Bytes.Append(Chunk, Read);
				const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
				Request = FString(Text.Length(), Text.Get());
				const int32 Separator = Request.Find(TEXT("\r\n\r\n"));
				const int32 LengthHeader = Request.Find(TEXT("Content-Length:"));
				if (Separator != INDEX_NONE && LengthHeader != INDEX_NONE &&
					Bytes.Num() >= Separator + 4 + FCString::Atoi(*Request + LengthHeader + 15)) break;
			}
			const bool bFirst = State->Requests.IsEmpty();
			State->Requests.Add(MoveTemp(Request));
			const FString Reply = bFirst ? TEXT("{\"temporary\":true}") : TEXT("{\"status\":\"REPLAYED\"}");
			const FString Wire = FString::Printf(TEXT("HTTP/1.1 %s\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s"),
				bFirst ? TEXT("503 Unavailable") : TEXT("200 OK"), Reply.Len(), *Reply);
			FTCHARToUTF8 UTF8(*Wire);
			for (int32 Sent = 0; Sent < UTF8.Length() && FPlatformTime::Seconds() < End;)
			{
				int32 Count = 0;
				if (!Client->Send(reinterpret_cast<const uint8*>(UTF8.Get()) + Sent, UTF8.Length() - Sent, Count)) break;
				Sent += Count;
			}
			Client->Close();
			Sockets->DestroySocket(Client);
		}
		Listener->Close();
		Sockets->DestroySocket(Listener);
		State->ServerDone = true;
	});

	FGuid Holder;
	TestEqual(TEXT("Reserve Gateway's only slot"), Gateway->RequestTracker->Begin(Holder), ProjectJ::MMO::EAdmission::Accepted);
	TestTrue(TEXT("Real outbox admits while Gateway rejects synchronously"),
		UProject_JOnlineWorldSubsystem::EnqueueDurableMutation(World, TEXT("Reward"), TEXT("Boss"), Body, State->Key, TEXT("wave3-http"),
			[State](bool bConfirmed, const auto&) { State->bConfirmed = bConfirmed; ++State->Completed; }));
	TestEqual(TEXT("Synchronous Gateway overload schedules one retry"), Outbox->GetOutboxStats().RetriedTotal, int64(1));
	TestEqual(TEXT("Synchronous Gateway rejection releases the outbox slot"), Outbox->GetOutboxStats().InFlightCount, 0);
	Gateway->RequestTracker->Complete(Holder);

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State, World, Outbox]()
	{
		if (!State->ServerDone || (!State->Completed && FPlatformTime::Seconds() < State->Deadline)) return false;
		TestTrue(TEXT("Real HTTP 503 followed by REPLAYED confirms exactly one reward"), State->bConfirmed && State->Completed == 1);
		TestEqual(TEXT("Admission failure and uncertain HTTP response both retried"), Outbox->GetOutboxStats().RetriedTotal, int64(2));
		FGuid FirstRequest;
		for (const FString& Request : State->Requests)
		{
			TestTrue(TEXT("ServicePort used the unwrapped Nakama runtime route"), Request.StartsWith(TEXT("POST /v2/rpc/j_reward_boss?unwrap ")));
			TSharedPtr<FJsonObject> Envelope, Intent;
			const int32 Separator = Request.Find(TEXT("\r\n\r\n"));
			const bool bParsed = Separator != INDEX_NONE && FJsonSerializer::Deserialize(
				TJsonReaderFactory<>::Create(Request.Mid(Separator + 4)), Envelope) && Envelope.IsValid();
			if (!TestTrue(TEXT("Actual request body is a JSON envelope"), bParsed)) continue;
			TestEqual(TEXT("Retries send the same idempotency key"), Envelope->GetStringField(TEXT("idempotency_key")),
				UProject_JOnlineWorldSubsystem::GuidText(State->Key));
			FGuid RequestId;
			TestTrue(TEXT("Each transport attempt has a valid distinct request ID"),
				FGuid::Parse(Envelope->GetStringField(TEXT("request_id")), RequestId) && FirstRequest != RequestId);
			if (!FirstRequest.IsValid()) FirstRequest = RequestId;
			FString Payload;
			TestTrue(TEXT("UTF-8 intent round trips through the service envelope"), FBase64::Decode(Envelope->GetStringField(TEXT("payload_b64")), Payload)
				&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Payload), Intent) && Intent.IsValid()
				&& Intent->GetStringField(TEXT("defeat_id")) == TEXT("wave3-검증"));
		}
		TestEqual(TEXT("Only the two dispatched attempts reached TCP"), State->Requests.Num(), 2);
		FPlatformMisc::SetEnvironmentVar(TEXT("PROJECT_J_NAKAMA_HTTP_KEY"), *State->OriginalRuntimeKey);
		State->GameInstance->Shutdown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		return true;
	}));
	return true;
}
#endif
