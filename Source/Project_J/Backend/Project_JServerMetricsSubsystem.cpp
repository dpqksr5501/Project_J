#include "Backend/Project_JServerMetricsSubsystem.h"

#include "Backend/Project_JOnlineWorldSubsystem.h"
#include "Backend/Project_JServerDrainSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Network/Project_JCrowdDensityNetObjectFilter.h"
#include "Optimization/Project_JServerLoadGovernorSubsystem.h"
#include "Security/Project_JServerIntegritySubsystem.h"
#if WITH_EDITOR || UE_SERVER
#include "IPAddress.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogProjectJServerMetrics, Log, All);

void UProject_JServerMetricsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UProject_JServerLoadGovernorSubsystem>();
	Collection.InitializeDependency<UProject_JServerIntegritySubsystem>();
	Collection.InitializeDependency<UProject_JOnlineWorldSubsystem>();
	Collection.InitializeDependency<UProject_JServerDrainSubsystem>();

	int32 TargetPort = MetricsPort;
	int32 CliPort = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("ProjectJMetricsPort="), CliPort) && CliPort > 0 && CliPort <= 65535)
	{
		TargetPort = CliPort;
	}
	if (IsRunningDedicatedServer() && TargetPort > 0 && TargetPort <= 65535)
	{
		FString BindAddress = TEXT("127.0.0.1");
		FParse::Value(FCommandLine::Get(), TEXT("ProjectJMetricsBindAddress="), BindAddress);
		StartMetricsEndpoint(TargetPort, BindAddress);
	}
}

void UProject_JServerMetricsSubsystem::Deinitialize()
{
	StopMetricsEndpoint();
	Super::Deinitialize();
}

bool UProject_JServerMetricsSubsystem::StartMetricsEndpoint(int32 InPort, const FString& BindAddress)
{
	check(IsInGameThread());
	StopMetricsEndpoint();
#if WITH_EDITOR || UE_SERVER
	if (InPort < 0 || InPort > 65535 || (BindAddress != TEXT("127.0.0.1") && BindAddress != TEXT("0.0.0.0"))) return false;
	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!Sockets) return false;

	FSocket* Listener = Sockets->CreateSocket(NAME_Stream, TEXT("ProjectJMetricsHTTP"), false);
	if (!Listener) return false;
	Listener->SetReuseAddr(true);
	Listener->SetNonBlocking(true);

	TSharedRef<FInternetAddr> Addr = Sockets->CreateInternetAddr();
	bool bValidIp = false;
	Addr->SetIp(*BindAddress, bValidIp);
	Addr->SetPort(InPort);
	if (!bValidIp || !Listener->Bind(*Addr) || !Listener->Listen(16))
	{
		Listener->Close();
		Sockets->DestroySocket(Listener);
		UE_LOG(LogProjectJServerMetrics, Warning, TEXT("Failed to bind Prometheus metrics endpoint on %s:%d"), *BindAddress, InPort);
		return false;
	}
	Listener->GetAddress(*Addr);
	ListenerSocket = Listener;
	BoundPort = Addr->GetPort();

	TWeakObjectPtr<UProject_JServerMetricsSubsystem> WeakSelf(this);
	PollTicker = FTSTicker::GetCoreTicker().AddTicker(
		TEXT("ProjectJMetricsPoll"),
		0.1f,
		[WeakSelf](float) -> bool
		{
			if (auto* Self = WeakSelf.Get())
			{
				Self->PollMetricsEndpoint();
				return Self->IsEndpointRunning();
			}
			return false;
		});
	UE_LOG(LogProjectJServerMetrics, Display, TEXT("Prometheus metrics endpoint listening on http://%s:%d/metrics"), *BindAddress, BoundPort);
	return true;
#else
	return false;
#endif
}

void UProject_JServerMetricsSubsystem::StopMetricsEndpoint()
{
	if (PollTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PollTicker);
		PollTicker.Reset();
	}
#if WITH_EDITOR || UE_SERVER
	for (const auto& Client : Clients)
	{
		Client.Socket->Close();
		if (auto* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
			Sockets->DestroySocket(Client.Socket);
	}
	Clients.Reset();
	if (ListenerSocket)
	{
		ListenerSocket->Close();
		if (ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
		{
			Sockets->DestroySocket(ListenerSocket);
		}
		ListenerSocket = nullptr;
	}
#endif
	BoundPort = 0;
}

FString UProject_JServerMetricsSubsystem::RenderPrometheusMetricsText() const
{
	const UGameInstance* GI = GetGameInstance();
	const auto* Gov = GI ? GI->GetSubsystem<UProject_JServerLoadGovernorSubsystem>() : nullptr;
	const auto* Integrity = GI ? GI->GetSubsystem<UProject_JServerIntegritySubsystem>() : nullptr;
	const auto* Online = GI ? GI->GetSubsystem<UProject_JOnlineWorldSubsystem>() : nullptr;
	const auto* Drain = GI ? GI->GetSubsystem<UProject_JServerDrainSubsystem>() : nullptr;

	const FProject_JServerGovernorSnapshot GovSnap = Gov ? Gov->GetSnapshot() : FProject_JServerGovernorSnapshot();
	const FProject_JRuntimeTelemetrySnapshot IntSnap = Integrity ? Integrity->GetTelemetrySnapshot() : FProject_JRuntimeTelemetrySnapshot();
	const FProject_JOnlineOutboxStats OutboxSnap = Online ? Online->GetOutboxStats() : FProject_JOnlineOutboxStats();
	const FProject_JCrowdDensityStats CrowdSnap = UProject_JCrowdDensityNetObjectFilter::GetLatestStats(GI ? GI->GetWorld() : nullptr);
	const int32 DrainState = Drain ? static_cast<int32>(Drain->GetDrainState()) : 0;

	FString Out;
	Out.Reserve(2048);
	Out += TEXT("# HELP projectj_server_tick_ms Rolling percentile of server world tick elapsed work in milliseconds.\n");
	Out += TEXT("# TYPE projectj_server_tick_ms gauge\n");
	Out += FString::Printf(TEXT("projectj_server_tick_ms{quantile=\"0.50\"} %.3f\n"), (FMath::IsFinite(GovSnap.TickP50Ms) ? GovSnap.TickP50Ms : 0.));
	Out += FString::Printf(TEXT("projectj_server_tick_ms{quantile=\"0.95\"} %.3f\n"), (FMath::IsFinite(GovSnap.TickP95Ms) ? GovSnap.TickP95Ms : 0.));
	Out += FString::Printf(TEXT("projectj_server_tick_ms{quantile=\"0.99\"} %.3f\n"), (FMath::IsFinite(GovSnap.TickP99Ms) ? GovSnap.TickP99Ms : 0.));
	Out += TEXT("# HELP projectj_server_over_budget_ticks_total Server over budget ticks total.\n");
	Out += TEXT("# TYPE projectj_server_over_budget_ticks_total counter\n");
	Out += FString::Printf(TEXT("projectj_server_over_budget_ticks_total %lld\n"), GovSnap.OverBudgetTicksTotal);
	Out += TEXT("# HELP projectj_server_load_tier Server load tier.\n");
	Out += TEXT("# TYPE projectj_server_load_tier gauge\n");
	Out += FString::Printf(TEXT("projectj_server_load_tier %d\n"), static_cast<int32>(GovSnap.Tier));
	Out += TEXT("# HELP projectj_server_active_ccu Server active ccu.\n");
	Out += TEXT("# TYPE projectj_server_active_ccu gauge\n");
	Out += FString::Printf(TEXT("projectj_server_active_ccu %d\n"), GovSnap.ActiveCCU);
	Out += TEXT("# HELP projectj_server_drain_state Server drain state.\n");
	Out += TEXT("# TYPE projectj_server_drain_state gauge\n");
	Out += FString::Printf(TEXT("projectj_server_drain_state %d\n"), DrainState);

	Out += TEXT("# HELP projectj_iris_crowd_tracked_objects Iris crowd tracked objects.\n");
	Out += TEXT("# TYPE projectj_iris_crowd_tracked_objects gauge\n");
	Out += FString::Printf(TEXT("projectj_iris_crowd_tracked_objects %d\n"), CrowdSnap.TrackedObjects);
	Out += TEXT("# HELP projectj_iris_crowd_effective_cap Iris crowd effective cap.\n");
	Out += TEXT("# TYPE projectj_iris_crowd_effective_cap gauge\n");
	Out += FString::Printf(TEXT("projectj_iris_crowd_effective_cap %d\n"), CrowdSnap.EffectiveCap);
	Out += TEXT("# HELP projectj_iris_crowd_allowed_pairs Iris crowd allowed pairs.\n");
	Out += TEXT("# TYPE projectj_iris_crowd_allowed_pairs gauge\n");
	Out += FString::Printf(TEXT("projectj_iris_crowd_allowed_pairs %lld\n"), CrowdSnap.LastPassAllowedPairs);
	Out += TEXT("# HELP projectj_iris_crowd_culled_pairs Iris crowd culled pairs.\n");
	Out += TEXT("# TYPE projectj_iris_crowd_culled_pairs gauge\n");
	Out += FString::Printf(TEXT("projectj_iris_crowd_culled_pairs %lld\n"), CrowdSnap.LastPassCulledPairs);

	Out += TEXT("# HELP projectj_outbox_pending Outbox pending.\n");
	Out += TEXT("# TYPE projectj_outbox_pending gauge\n");
	Out += FString::Printf(TEXT("projectj_outbox_pending %d\n"), OutboxSnap.PendingCount);
	Out += TEXT("# HELP projectj_outbox_in_flight Outbox in flight.\n");
	Out += TEXT("# TYPE projectj_outbox_in_flight gauge\n");
	Out += FString::Printf(TEXT("projectj_outbox_in_flight %d\n"), OutboxSnap.InFlightCount);
	Out += TEXT("# HELP projectj_outbox_dead_letters Outbox dead letters.\n");
	Out += TEXT("# TYPE projectj_outbox_dead_letters gauge\n");
	Out += FString::Printf(TEXT("projectj_outbox_dead_letters %d\n"), OutboxSnap.DeadLetterCount);
	Out += TEXT("# HELP projectj_outbox_confirmed_total Outbox confirmed total.\n");
	Out += TEXT("# TYPE projectj_outbox_confirmed_total counter\n");
	Out += FString::Printf(TEXT("projectj_outbox_confirmed_total %lld\n"), OutboxSnap.ConfirmedTotal);
	Out += TEXT("# HELP projectj_outbox_retried_total Outbox retried total.\n");
	Out += TEXT("# TYPE projectj_outbox_retried_total counter\n");
	Out += FString::Printf(TEXT("projectj_outbox_retried_total %lld\n"), OutboxSnap.RetriedTotal);
	Out += TEXT("# HELP projectj_outbox_recovered_total Outbox recovered total.\n");
	Out += TEXT("# TYPE projectj_outbox_recovered_total counter\n");
	Out += FString::Printf(TEXT("projectj_outbox_recovered_total %lld\n"), OutboxSnap.RecoveredTotal);
	Out += TEXT("# HELP projectj_outbox_store_errors_total Outbox store errors total.\n");
	Out += TEXT("# TYPE projectj_outbox_store_errors_total counter\n");
	Out += FString::Printf(TEXT("projectj_outbox_store_errors_total %lld\n"), OutboxSnap.StoreErrorTotal);

	Out += TEXT("# HELP projectj_integrity_movement_violations_total Integrity movement violations total.\n");
	Out += TEXT("# TYPE projectj_integrity_movement_violations_total counter\n");
	Out += FString::Printf(TEXT("projectj_integrity_movement_violations_total %lld\n"), IntSnap.MovementViolations);
	Out += TEXT("# HELP projectj_integrity_combat_violations_total Integrity combat violations total.\n");
	Out += TEXT("# TYPE projectj_integrity_combat_violations_total counter\n");
	Out += FString::Printf(TEXT("projectj_integrity_combat_violations_total %lld\n"), IntSnap.CombatViolations);
	Out += TEXT("# HELP projectj_integrity_economy_alerts_total Integrity economy alerts total.\n");
	Out += TEXT("# TYPE projectj_integrity_economy_alerts_total counter\n");
	Out += FString::Printf(TEXT("projectj_integrity_economy_alerts_total %lld\n"), IntSnap.EconomyAlerts);
	return Out;
}

bool UProject_JServerMetricsSubsystem::RenderHealthJson(FString& OutJson) const
{
	const UGameInstance* GI = GetGameInstance();
	const auto* Drain = GI ? GI->GetSubsystem<UProject_JServerDrainSubsystem>() : nullptr;
	const auto* Online = GI ? GI->GetSubsystem<UProject_JOnlineWorldSubsystem>() : nullptr;
	const auto* Gov = GI ? GI->GetSubsystem<UProject_JServerLoadGovernorSubsystem>() : nullptr;

	const bool bDraining = Drain && Drain->IsDraining();
	const bool bOutboxBroken = Online && Online->HasOutboxStoreFailed();
	const UWorld* World = GI ? GI->GetWorld() : nullptr;
	const bool bListening = !IsRunningDedicatedServer() || (World && World->HasBegunPlay() && World->GetNetDriver() && !World->bIsTearingDown);
	const bool bHealthy = bListening && !bDraining && !bOutboxBroken;
	const int32 Tier = Gov ? static_cast<int32>(Gov->GetCurrentTier()) : 0;

	OutJson = FString::Printf(
		TEXT("{\"status\":\"%s\",\"draining\":%s,\"outbox_failed\":%s,\"load_tier\":%d}"),
		bHealthy ? TEXT("OK") : TEXT("UNAVAILABLE"),
		bDraining ? TEXT("true") : TEXT("false"),
		bOutboxBroken ? TEXT("true") : TEXT("false"),
		Tier);
	return bHealthy;
}

void UProject_JServerMetricsSubsystem::PollMetricsEndpoint()
{
	check(IsInGameThread());
#if WITH_EDITOR || UE_SERVER
	if (!ListenerSocket) return;
	auto* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!Sockets) return;
	constexpr int32 MaxClients = 32;
	constexpr int32 MaxHeaderBytes = 4096;
	const double Now = FPlatformTime::Seconds();
	for (int32 Accepted = 0; Accepted < 8 && Clients.Num() < MaxClients; ++Accepted)
	{
		bool bPending = false;
		if (!ListenerSocket->HasPendingConnection(bPending) || !bPending) break;
		auto* Socket = ListenerSocket->Accept(TEXT("ProjectJMetricsClient"));
		if (!Socket) break;
		if (!Socket->SetNonBlocking(true))
		{
			Sockets->DestroySocket(Socket);
			continue;
		}
		auto& Client = Clients.AddDefaulted_GetRef();
		Client.Socket = Socket;
		Client.Deadline = Now + 2.;
	}

	for (int32 Index = Clients.Num() - 1; Index >= 0; --Index)
	{
		auto& Client = Clients[Index];
		if (Now < Client.Deadline)
		{
			if (Client.Response.IsEmpty())
			{
				uint8 Buffer[1024];
				int32 Read = 0;
				Client.Socket->Recv(Buffer, sizeof(Buffer), Read);
				if (Read > 0) Client.Request.Append(Buffer, Read);
				if (Client.Request.IsEmpty()) continue;
				const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Client.Request.GetData()), Client.Request.Num());
				const FString Request(Text.Length(), Text.Get());
				const int32 End = Request.Find(TEXT("\r\n\r\n"));
				if (End == INDEX_NONE && Client.Request.Num() < MaxHeaderBytes) continue;

				const TCHAR* Status = TEXT("404 Not Found");
				const TCHAR* Type = TEXT("text/plain; charset=utf-8");
				FString Body = TEXT("Not Found\n");
				const FString FirstLine = Request.Left(Request.Find(TEXT("\r\n")));
				FString Method, TargetAndVersion, Target, Version;
				if (End == INDEX_NONE || End + 4 > MaxHeaderBytes
					|| !FirstLine.Split(TEXT(" "), &Method, &TargetAndVersion) || !TargetAndVersion.Split(TEXT(" "), &Target, &Version)
					|| (Version != TEXT("HTTP/1.1") && Version != TEXT("HTTP/1.0")))
				{
					Status = TEXT("400 Bad Request");
					Body = TEXT("Bad Request\n");
				}
				else if (Method == TEXT("GET") && Target == TEXT("/healthz"))
				{
					Type = TEXT("application/json");
					Status = RenderHealthJson(Body) ? TEXT("200 OK") : TEXT("503 Service Unavailable");
				}
				else if (Method == TEXT("GET") && Target == TEXT("/metrics"))
				{
					Status = TEXT("200 OK");
					Type = TEXT("text/plain; version=0.0.4; charset=utf-8");
					Body = RenderPrometheusMetricsText();
				}
				const FTCHARToUTF8 Bytes(*Body);
				const FString Header = FString::Printf(TEXT("HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %d\r\nConnection: close\r\n\r\n"),
					Status, Type, Bytes.Length());
				const FTCHARToUTF8 HeaderBytes(*Header);
				Client.Response.Append(reinterpret_cast<const uint8*>(HeaderBytes.Get()), HeaderBytes.Length());
				Client.Response.Append(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length());
				Client.Request.Reset();
			}

			int32 Sent = 0;
			Client.Socket->Send(Client.Response.GetData() + Client.Sent, Client.Response.Num() - Client.Sent, Sent);
			Client.Sent += FMath::Max(0, Sent);
			if (Client.Sent < Client.Response.Num()) continue;
		}
		Client.Socket->Close();
		Sockets->DestroySocket(Client.Socket);
		Clients.RemoveAtSwap(Index, EAllowShrinking::No);
	}
#endif
}
