#include "Backend/Project_JServerDrainSubsystem.h"

#include "Backend/Project_JOnlineWorldSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/NetConnection.h"
#include "GameFramework/GameSession.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#if PLATFORM_LINUX
#include <signal.h>
#endif
#include "Game/Project_JGameMode.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/App.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJServerDrain, Log, All);

namespace
{
#if PLATFORM_LINUX
volatile sig_atomic_t GProjectJTerminationPending = 0;
struct sigaction GProjectJPreviousTerm {};
void RequestDrainFromSignal(int) { GProjectJTerminationPending = 1; }
#endif

TAutoConsoleVariable<int32> CVarAutoDrainOnOutboxFailure(
	TEXT("ProjectJ.Server.AutoDrainOnOutboxFailure"),
	1,
	TEXT("Automatically initiates Graceful Server Drain on dedicated servers if the persistent Outbox store fails (1 = enabled, 0 = disabled)."),
	ECVF_Default);

FAutoConsoleCommandWithWorldAndArgs GProjectJBeginGracefulDrainCmd(
	TEXT("ProjectJ.Server.BeginGracefulDrain"),
	TEXT("Starts zero-downtime Graceful Server Drain. Usage: ProjectJ.Server.BeginGracefulDrain [Reason] [GraceSeconds] [AlternateEndpoint]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		auto* Drain = GI ? GI->GetSubsystem<UProject_JServerDrainSubsystem>() : nullptr;
		if (!Drain)
		{
			UE_LOG(LogProjectJServerDrain, Warning, TEXT("BeginGracefulDrain: no active UProject_JServerDrainSubsystem."));
			return;
		}
		const FString Reason = Args.Num() >= 1 ? Args[0] : TEXT("OperatorRequested");
		float GraceSeconds = 15.0f;
		if (Args.Num() >= 2)
		{
			float Parsed = 0.0f;
			if (LexTryParseString(Parsed, *Args[1]) && FMath::IsFinite(Parsed) && Parsed >= 0.0f)
			{
				GraceSeconds = Parsed;
			}
		}
		const FString Endpoint = Args.Num() >= 3 ? Args[2] : FString();
		Drain->BeginGracefulDrain(Reason, GraceSeconds, Endpoint, IsRunningDedicatedServer());
	}));
}

void UProject_JServerDrainSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UProject_JOnlineWorldSubsystem>();
	if (IsRunningDedicatedServer())
	{
		const FString Port = FPlatformMisc::GetEnvironmentVariable(TEXT("AGONES_SDK_HTTP_PORT"));
		if (!LexTryParseString(AgonesSdkPort, *Port) || AgonesSdkPort < 1 || AgonesSdkPort > 65535)
			AgonesSdkPort = 0;
	}
#if PLATFORM_LINUX
	if (IsRunningDedicatedServer() && FParse::Param(FCommandLine::Get(), TEXT("ProjectJDrainOnSigterm")))
	{
		struct sigaction Action {};
		Action.sa_handler = RequestDrainFromSignal;
		sigemptyset(&Action.sa_mask);
		Action.sa_flags = SA_RESTART;
		bOwnTerminationSignal = sigaction(SIGTERM, &Action, &GProjectJPreviousTerm) == 0;
		if (!bOwnTerminationSignal) UE_LOG(LogProjectJServerDrain, Error, TEXT("Failed to install Linux termination drain signal."));
	}
#endif

	TWeakObjectPtr<UProject_JServerDrainSubsystem> WeakSelf(this);
	DrainTicker = FTSTicker::GetCoreTicker().AddTicker(
		TEXT("ProjectJServerDrainMonitor"),
		0.25f,
		[WeakSelf](float) -> bool
		{
			if (auto* Self = WeakSelf.Get())
			{
				const double Now = FPlatformTime::Seconds();
				Self->TickDrain(Now);
				Self->TickAgones(Now);
				return true;
			}
			return false;
		});
}

void UProject_JServerDrainSubsystem::Deinitialize()
{
	if (DrainTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DrainTicker);
		DrainTicker.Reset();
	}
#if PLATFORM_LINUX
	if (bOwnTerminationSignal)
	{
		sigaction(SIGTERM, &GProjectJPreviousTerm, nullptr);
		bOwnTerminationSignal = false;
		GProjectJTerminationPending = 0;
	}
#endif
	if (AgonesRequest)
	{
		AgonesRequest->OnProcessRequestComplete().Unbind();
		AgonesRequest->CancelRequest();
		AgonesRequest.Reset();
	}
	OnDrainStateChanged.Clear();
	Super::Deinitialize();
}

void UProject_JServerDrainSubsystem::TickAgones(double NowSeconds)
{
	if (!AgonesSdkPort || AgonesRequest || NowSeconds < NextAgonesReportAt || State == EProject_JServerDrainState::ReadyToExit) return;
	const auto* GI = GetGameInstance();
	const UWorld* World = GI ? GI->GetWorld() : nullptr;
	const auto* Online = GI ? GI->GetSubsystem<UProject_JOnlineWorldSubsystem>() : nullptr;
	const bool bReportReady = !bAgonesReady && State == EProject_JServerDrainState::InService
		&& World && !World->bIsTearingDown && World->HasBegunPlay() && World->GetNetDriver()
		&& (!Online || !Online->HasOutboxStoreFailed());
	NextAgonesReportAt = NowSeconds + (bReportReady ? 1. : 2.);
	AgonesRequest = FHttpModule::Get().CreateRequest();
	AgonesRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
	AgonesRequest->SetURL(FString::Printf(TEXT("http://127.0.0.1:%d/%s"), AgonesSdkPort, bReportReady ? TEXT("ready") : TEXT("health")));
	AgonesRequest->SetVerb(TEXT("POST"));
	AgonesRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	AgonesRequest->SetContentAsString(TEXT("{}"));
	AgonesRequest->SetTimeout(1.5f);
	AgonesRequest->OnProcessRequestComplete().BindWeakLambda(this,
		[this, bReportReady](FHttpRequestPtr Request, FHttpResponsePtr Reply, bool bConnected)
		{
			if (AgonesRequest != Request) return;
			AgonesRequest.Reset();
			if (bReportReady && bConnected && Reply && EHttpResponseCodes::IsOk(Reply->GetResponseCode()))
				bAgonesReady = true;
		});
	if (!AgonesRequest->ProcessRequest()) AgonesRequest.Reset();
}

bool UProject_JServerDrainSubsystem::BeginGracefulDrain(
	const FString& Reason,
	float GraceSeconds,
	const FString& AlternateChannelEndpoint,
	bool bRequestProcessExitOnComplete)
{
	check(IsInGameThread());
	if (State != EProject_JServerDrainState::InService)
	{
		return false;
	}
	DrainReason = Reason.IsEmpty() ? TEXT("Maintenance") : Reason;
	AlternateEndpoint = AlternateChannelEndpoint;
	bRequestExitWhenReady = bRequestProcessExitOnComplete;
	bEvacuationDispatched = false;
	const double Now = FPlatformTime::Seconds();
	const float Grace = FMath::IsFinite(GraceSeconds) ? FMath::Clamp(GraceSeconds, 0.f, 90.f) : 15.f;
	DrainDeadlineAt = Now + Grace;

	SetDrainState(EProject_JServerDrainState::Draining);
	UE_LOG(LogProjectJServerDrain, Warning,
		TEXT("Graceful Server Drain started reason=%s grace=%.1fs alternate=%s players=%d"),
		*DrainReason, Grace, *AlternateEndpoint, CountConnectedPlayers());

	// Notify GameMode immediately so heartbeat flips draining=true and PreLogin blocks new players.
	if (UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		if (auto* Mode = World->GetAuthGameMode<AProject_JGameMode>())
		{
			Mode->NotifyServerDraining();
		}
	}

	EvacuateConnectedPlayers(false);
	TickDrain(Now);
	return true;
}

void UProject_JServerDrainSubsystem::TickDrain(double NowSeconds)
{
	check(IsInGameThread());
#if PLATFORM_LINUX
	if (bOwnTerminationSignal && GProjectJTerminationPending)
	{
		GProjectJTerminationPending = 0;
		if (State == EProject_JServerDrainState::InService)
		{
			BeginGracefulDrain(TEXT("ProcessTermination"), 15.f, FString(), true);
			return;
		}
		bRequestExitWhenReady = true;
		if (State == EProject_JServerDrainState::ReadyToExit) FPlatformMisc::RequestExit(false);
	}
#endif
	if (State == EProject_JServerDrainState::InService)
	{
		if (IsRunningDedicatedServer() && CVarAutoDrainOnOutboxFailure.GetValueOnGameThread() != 0)
		{
			if (const auto* Online = GetGameInstance() ? GetGameInstance()->GetSubsystem<UProject_JOnlineWorldSubsystem>() : nullptr)
			{
				if (Online->HasOutboxStoreFailed())
				{
					BeginGracefulDrain(TEXT("OutboxStoreFailure"), 10.0f, FString(), true);
				}
			}
		}
		return;
	}

	if (State == EProject_JServerDrainState::Draining)
	{
		if (NowSeconds >= DrainDeadlineAt && !bForcedDisconnect && CountConnectedPlayers() > 0)
		{
			bForcedDisconnect = true;
			EvacuateConnectedPlayers(true);
			// Logout can enqueue the last checkpoint. Revisit on the next tick
			// after player removal has run, before checking the outbox.
			return;
		}
		if (CountConnectedPlayers() == 0)
		{
			FlushDeadlineAt = NowSeconds + 10.;
			SetDrainState(EProject_JServerDrainState::Flushing);
		}
	}

	if (State == EProject_JServerDrainState::Flushing)
	{
		bool bOutboxSettled = true;
		if (const auto* Online = GetGameInstance() ? GetGameInstance()->GetSubsystem<UProject_JOnlineWorldSubsystem>() : nullptr)
		{
			bOutboxSettled = !Online->HasOutstandingDrainWork();
		}
		if (bOutboxSettled || NowSeconds >= FlushDeadlineAt)
		{
			SetDrainState(EProject_JServerDrainState::ReadyToExit);
			UE_LOG(LogProjectJServerDrain, Display,
				TEXT("Graceful Server Drain completed (evacuated=%d outboxSettled=%d). Ready to exit."),
				EvacuatedPlayersTotal, bOutboxSettled ? 1 : 0);
			if (bRequestExitWhenReady && IsRunningDedicatedServer())
			{
				FPlatformMisc::RequestExit(false);
			}
		}
	}
}

void UProject_JServerDrainSubsystem::SetDrainState(EProject_JServerDrainState NewState)
{
	if (State == NewState) return;
	State = NewState;
	OnDrainStateChanged.Broadcast(State, DrainReason);
}

int32 UProject_JServerDrainSubsystem::CountConnectedPlayers() const
{
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World || World->bIsTearingDown) return 0;
	int32 Count = 0;
	for (auto It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (IsValid(It->Get()) && !It->Get()->IsLocalController() && !It->Get()->IsActorBeingDestroyed()) ++Count;
	}
	return Count;
}

void UProject_JServerDrainSubsystem::EvacuateConnectedPlayers(bool bForceDisconnectRemaining)
{
	if (!bForceDisconnectRemaining && bEvacuationDispatched) return;
	bEvacuationDispatched = true;

	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World || World->bIsTearingDown) return;

	TArray<TWeakObjectPtr<APlayerController>> Players;
	for (auto It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (IsValid(It->Get()) && !It->Get()->IsLocalController()) Players.Add(It->Get());
	}
	for (auto Player : Players)
	{
		auto* PC = Player.Get();
		if (!PC || PC->IsActorBeingDestroyed()) continue;
		if (!AlternateEndpoint.IsEmpty() && !bForceDisconnectRemaining)
		{
			PC->ClientTravel(AlternateEndpoint, TRAVEL_Absolute);
			++EvacuatedPlayersTotal;
		}
		else if (bForceDisconnectRemaining)
		{
			const auto Reason = FText::Format(NSLOCTEXT("ProjectJDrain", "ServerDraining", "Server shutting down for maintenance ({0})."),
				FText::FromString(DrainReason));
			const TWeakObjectPtr<UNetConnection> Connection(PC->GetNetConnection());
			auto* Mode = World->GetAuthGameMode();
			if (!Mode || !Mode->GameSession || !Mode->GameSession->KickPlayer(PC, Reason))
			{
				PC->ClientWasKicked(Reason);
				PC->Destroy();
			}
			if (auto* NetConnection = Connection.Get()) NetConnection->Close();
			++EvacuatedPlayersTotal;
		}
	}
}
