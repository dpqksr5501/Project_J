#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Backend/Project_JGatewaySubsystem.h"
#include "MMO/Coordination.h"
#include "Engine/GameInstance.h"
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
#endif
