#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "Backend/Project_JOnlineWorldSubsystem.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "Project_JSpellbladeCharacter.h"
#include "UI/Project_JGraphicsSettings.h"

namespace
{
constexpr auto Wave3TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJOnlineDurableOutboxTest, "ProjectJ.Online.DurableOutboxBurstAndRetry", Wave3TestFlags)
bool FProjectJOnlineDurableOutboxTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UGameInstance* GI = NewObject<UGameInstance>(GEngine);
	GI->AddToRoot();
	GI->Init();
	World->SetGameInstance(GI);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		GI->Shutdown();
		GI->RemoveFromRoot();
	};

	auto* Online = GI->GetSubsystem<UProject_JOnlineWorldSubsystem>();
	if (!TestNotNull(TEXT("OnlineWorldSubsystem exists"), Online)) { return false; }

	struct FPendingReply
	{
		FGuid Key;
		TFunction<void(TSharedPtr<FJsonObject>)> Callback;
	};
	TArray<FPendingReply> InFlightReplies;
	TMap<FGuid, int32> KeyAttemptCounts;
	int32 PeakInFlight = 0;

	Online->SetOutboxDispatchHookForTest(
		[&](FName, FName, const FGuid& Key, const TArray<uint8>&, TFunction<void(TSharedPtr<FJsonObject>)> ReplyCb)
		{
			++KeyAttemptCounts.FindOrAdd(Key);
			InFlightReplies.Add({Key, MoveTemp(ReplyCb)});
			PeakInFlight = FMath::Max(PeakInFlight, InFlightReplies.Num());
		});

	// Enqueue 400 boss rewards (2 concurrent 200-player boss kills) in one tick.
	constexpr int32 TotalRewards = 400;
	int32 ConfirmedCallbacks = 0;
	TArray<FGuid> Keys;
	Keys.Reserve(TotalRewards);
	for (int32 i = 0; i < TotalRewards; ++i)
	{
		const FGuid Key = FGuid::NewGuid();
		Keys.Add(Key);
		auto Body = MakeShared<FJsonObject>();
		Body->SetNumberField(TEXT("reward_index"), i);
		const bool bAccepted = UProject_JOnlineWorldSubsystem::EnqueueDurableMutation(
			World, TEXT("Reward"), TEXT("Boss"), Body, Key, FString::Printf(TEXT("test:%d"), i),
			[&ConfirmedCallbacks](bool bConfirmed, const TSharedPtr<FJsonObject>&)
			{
				if (bConfirmed) { ++ConfirmedCallbacks; }
			});
		TestTrue(TEXT("Boss reward enqueued cleanly into durable outbox"), bAccepted);
	}

	TestEqual(TEXT("Outbox caps concurrent in-flight at MaxOutboxInFlight (64)"),
		PeakInFlight, UProject_JOnlineWorldSubsystem::MaxOutboxInFlight);
	TestEqual(TEXT("Remaining 336 mutations stay queued instead of flooding Gateway"),
		Online->GetOutboxStats().PendingCount, TotalRewards - UProject_JOnlineWorldSubsystem::MaxOutboxInFlight);

	// Drain all waves: fail attempt 1 of every 5th key with PRE_DISPATCH_REJECTED (Overloaded)
	// and every 7th key with nullptr (timeout), then confirm with REPLAYED / COMMITTED on retry.
	int32 Guard = 0;
	while ((!InFlightReplies.IsEmpty() || Online->GetOutboxStats().PendingCount > 0) && ++Guard < 2000)
	{
		if (InFlightReplies.IsEmpty())
		{
			Online->PumpOutboxForTest(FPlatformTime::Seconds() + 120.0);
			continue;
		}
		TArray<FPendingReply> Batch = MoveTemp(InFlightReplies);
		InFlightReplies.Reset();
		for (auto& Item : Batch)
		{
			const int32 Attempt = KeyAttemptCounts.FindRef(Item.Key);
			const int32 KeyIdx = Keys.IndexOfByKey(Item.Key);
			if (Attempt == 1 && KeyIdx >= 0 && (KeyIdx % 5 == 0))
			{
				auto Rejected = MakeShared<FJsonObject>();
				Rejected->SetStringField(TEXT("status"), TEXT("PRE_DISPATCH_REJECTED"));
				Item.Callback(Rejected);
			}
			else if (Attempt == 1 && KeyIdx >= 0 && (KeyIdx % 7 == 0))
			{
				Item.Callback(nullptr);
			}
			else
			{
				auto Ok = MakeShared<FJsonObject>();
				Ok->SetStringField(TEXT("status"), Attempt > 1 ? TEXT("REPLAYED") : TEXT("COMMITTED"));
				Item.Callback(Ok);
			}
		}
		Online->PumpOutboxForTest(FPlatformTime::Seconds() + 120.0);
	}

	const FProject_JOnlineOutboxStats Stats = Online->GetOutboxStats();
	TestEqual(TEXT("All 400 concurrent boss rewards confirmed without drops"), ConfirmedCallbacks, TotalRewards);
	TestEqual(TEXT("Outbox ConfirmedTotal == 400"), Stats.ConfirmedTotal, static_cast<int64>(TotalRewards));
	TestEqual(TEXT("Outbox FailedTotal == 0"), Stats.FailedTotal, static_cast<int64>(0));
	TestTrue(TEXT("Retries occurred and reused identical idempotency keys"), Stats.RetriedTotal >= 100);
	TestEqual(TEXT("Zero entries stranded in Pending or InFlight"), Stats.PendingCount + Stats.InFlightCount, 0);
	TestTrue(TEXT("Peak concurrency never exceeded MaxOutboxInFlight across retries"),
		PeakInFlight <= UProject_JOnlineWorldSubsystem::MaxOutboxInFlight);

	// Verify explicit non-retryable rejection fails permanently on attempt 1 without wasting retries.
	AddExpectedMessage(TEXT("Durable outbox failed permanently"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	bool bInvalidConfirmed = true;
	const FGuid BadKey = FGuid::NewGuid();
	auto BadBody = MakeShared<FJsonObject>();
	UProject_JOnlineWorldSubsystem::EnqueueDurableMutation(
		World, TEXT("Reward"), TEXT("Boss"), BadBody, BadKey, TEXT("invalid_case"),
		[&bInvalidConfirmed](bool bConfirmed, const TSharedPtr<FJsonObject>&) { bInvalidConfirmed = bConfirmed; });
	TestEqual(TEXT("Non-retryable case dispatched once"), InFlightReplies.Num(), 1);
	if (!InFlightReplies.IsEmpty())
	{
		auto Invalid = MakeShared<FJsonObject>();
		Invalid->SetStringField(TEXT("status"), TEXT("INVALID"));
		auto Cb = MoveTemp(InFlightReplies[0].Callback);
		InFlightReplies.Reset();
		Cb(Invalid);
	}
	TestFalse(TEXT("Non-retryable INVALID reports failure"), bInvalidConfirmed);
	TestEqual(TEXT("Non-retryable INVALID is not retried"), KeyAttemptCounts.FindRef(BadKey), 1);
	TestEqual(TEXT("FailedTotal incremented to 1"), Online->GetOutboxStats().FailedTotal, static_cast<int64>(1));

	// Backoff is measured from the reply. Use a short and a past-deadline
	// sample per attempt with the queue clock rather than sleeping.
	const FGuid ExpiredKey = FGuid::NewGuid();
	int32 ExhaustedCallbacks = 0;
	bool bExhaustedConfirmed = true;
	UProject_JOnlineWorldSubsystem::EnqueueDurableMutation(
		World, TEXT("Reward"), TEXT("Boss"), BadBody, ExpiredKey, TEXT("retry_exhausted"),
		[&](bool bConfirmed, const auto&) { ++ExhaustedCallbacks; bExhaustedConfirmed = bConfirmed; });
	for (int32 Attempt = 0; Attempt <= UProject_JOnlineWorldSubsystem::MaxOutboxRetries; ++Attempt)
	{
		if (!TestEqual(TEXT("Only one active dispatch of the same key"), InFlightReplies.Num(), 1)) break;
		auto Reply = MoveTemp(InFlightReplies[0].Callback);
		InFlightReplies.Reset();
		const double RepliedAt = FPlatformTime::Seconds();
		Reply(nullptr);
		const double Backoff = UProject_JOnlineWorldSubsystem::OutboxInitialBackoffSeconds * (1 << Attempt);
		Online->PumpOutboxForTest(RepliedAt + Backoff * .5);
		TestTrue(TEXT("Backoff does not send early"), InFlightReplies.IsEmpty());
		Online->PumpOutboxForTest(RepliedAt + Backoff + .1);
	}
	TestEqual(TEXT("Timeouts allow six total attempts"), KeyAttemptCounts.FindRef(ExpiredKey), 6);
	TestEqual(TEXT("Retry exhaustion completes once"), ExhaustedCallbacks, 1);
	TestFalse(TEXT("Retry exhaustion remains unconfirmed"), bExhaustedConfirmed);

	Online->SetOutboxDispatchHookForTest(nullptr);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGraphicsPresetVsCustomTest, "ProjectJ.UI.GraphicsSettingsPresetVsCustomOverride", Wave3TestFlags)
bool FProjectJGraphicsPresetVsCustomTest::RunTest(const FString&)
{
	auto* OpacityCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Combat.Presentation.OtherPlayerOpacityScale"));
	auto* OptionalCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Combat.Presentation.OtherPlayerOptionalEffects"));
	if (!TestNotNull(TEXT("OtherPlayerOpacityScale CVar exists"), OpacityCVar)
		|| !TestNotNull(TEXT("OtherPlayerOptionalEffects CVar exists"), OptionalCVar))
	{
		return false;
	}
	TestTrue(TEXT("Both variables can be driven by scalability INI"),
		(OpacityCVar->GetFlags() & ECVF_Scalability) && (OptionalCVar->GetFlags() & ECVF_Scalability));

	const float SavedOpacity = OpacityCVar->GetFloat();
	const int32 SavedOptional = OptionalCVar->GetInt();
	const EConsoleVariableFlags SavedOpacityFlags = static_cast<EConsoleVariableFlags>(OpacityCVar->GetFlags() & ECVF_SetByMask);
	const EConsoleVariableFlags SavedOptionalFlags = static_cast<EConsoleVariableFlags>(OptionalCVar->GetFlags() & ECVF_SetByMask);

	ON_SCOPE_EXIT
	{
		OpacityCVar->Unset(ECVF_SetByGameSetting);
		OptionalCVar->Unset(ECVF_SetByGameSetting);
		OpacityCVar->Unset(ECVF_SetByScalability);
		OptionalCVar->Unset(ECVF_SetByScalability);
		OpacityCVar->Set(SavedOpacity, SavedOpacityFlags);
		OptionalCVar->Set(SavedOptional, SavedOptionalFlags);
	};

	// Simulate EffectsQuality@1 (Medium) from DefaultScalability.ini.
	OpacityCVar->Unset(ECVF_SetByGameSetting);
	OptionalCVar->Unset(ECVF_SetByGameSetting);
	OpacityCVar->Set(0.35f, ECVF_SetByScalability);
	OptionalCVar->Set(0, ECVF_SetByScalability);

	UGameInstance* GI = NewObject<UGameInstance>(GEngine);
	GI->AddToRoot();
	ON_SCOPE_EXIT { GI->RemoveFromRoot(); };
	auto* Settings = NewObject<UProject_JGraphicsSettings>(GI);
	Settings->bUseCustomOtherPlayerEffects = false;
	Settings->OtherPlayerEffectOpacity = 1.0f;
	Settings->bShowOtherPlayerOptionalEffects = true;
	Settings->ApplyCurrentPolicy();

	TestTrue(TEXT("Default preset mode preserves Scalability opacity (0.35)"),
		FMath::IsNearlyEqual(OpacityCVar->GetFloat(), 0.35f, 0.001f));
	TestEqual(TEXT("Default preset mode preserves Scalability optional effects (0)"), OptionalCVar->GetInt(), 0);
	TestTrue(TEXT("Display opacity synced from active Scalability preset"),
		FMath::IsNearlyEqual(Settings->OtherPlayerEffectOpacity, 0.35f, 0.001f));
	TestFalse(TEXT("Display optional flag synced from active Scalability preset"), Settings->bShowOtherPlayerOptionalEffects);

	// Explicit user override takes precedence via ECVF_SetByGameSetting.
	Settings->bUseCustomOtherPlayerEffects = true;
	Settings->OtherPlayerEffectOpacity = 0.5f;
	Settings->bShowOtherPlayerOptionalEffects = true;
	Settings->ApplyCurrentPolicy();
	TestTrue(TEXT("Custom user opacity overrides Scalability (0.5)"),
		FMath::IsNearlyEqual(OpacityCVar->GetFloat(), 0.5f, 0.001f));
	TestEqual(TEXT("Custom user optional effects overrides Scalability (1)"), OptionalCVar->GetInt(), 1);

	// Returning to preset mode cleanly unsets ECVF_SetByGameSetting so Scalability resumes.
	Settings->bUseCustomOtherPlayerEffects = false;
	Settings->ApplyCurrentPolicy();
	TestTrue(TEXT("Unsetting custom override restores Scalability opacity (0.35)"),
		FMath::IsNearlyEqual(OpacityCVar->GetFloat(), 0.35f, 0.001f));
	TestEqual(TEXT("Unsetting custom override restores Scalability optional effects (0)"), OptionalCVar->GetInt(), 0);

	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJOutboxSynchronousReplyTest, "ProjectJ.Online.OutboxSynchronousReply", Wave3TestFlags)
bool FProjectJOutboxSynchronousReplyTest::RunTest(const FString&)
{
	auto* GI = NewObject<UGameInstance>(GEngine);
	GI->InitializeStandalone();
	auto* World = GI->GetWorld();
	ON_SCOPE_EXIT
	{
		GI->Shutdown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};
	auto* Outbox = GI->GetSubsystem<UProject_JOnlineWorldSubsystem>();
	if (!TestNotNull(TEXT("Online subsystem"), Outbox)) return false;

	TFunction<void(TSharedPtr<FJsonObject>)> FirstReply;
	TArray<FGuid> Dispatched;
	int32 Depth = 0, PeakDepth = 0, Confirmed = 0;
	Outbox->SetOutboxDispatchHookForTest([&](FName, FName, const FGuid& Key, const auto&, auto Reply)
	{
		Dispatched.Add(Key);
		FirstReply = MoveTemp(Reply);
	});
	const auto Body = MakeShared<FJsonObject>();
	TArray<FGuid> Keys;
	const auto Enqueue = [&]
	{
		const FGuid Key = FGuid::NewGuid();
		Keys.Add(Key);
		return UProject_JOnlineWorldSubsystem::EnqueueDurableMutation(World, TEXT("Reward"), TEXT("Boss"), Body,
			Key, TEXT("sync"), [&](bool bConfirmed, const auto&) { Confirmed += bConfirmed ? 1 : 0; });
	};
	TestTrue(TEXT("Initial async admission"), Enqueue());
	if (!TestTrue(TEXT("Initial dispatch supplied a callback"), !!FirstReply)) return false;
	// Queue 4095 entries from within one dispatch before its synchronous reply.
	Outbox->SetOutboxDispatchHookForTest([&](FName, FName, const FGuid& Key, const auto&, auto Reply)
	{
		TGuardValue<int32> Enter(Depth, Depth + 1);
		PeakDepth = FMath::Max(PeakDepth, Depth);
		Dispatched.Add(Key);
		if (Dispatched.Num() == 2)
		{
			for (int32 Index = 2; Index < UProject_JOnlineWorldSubsystem::MaxOutboxEntries; ++Index)
			{
				if (!Enqueue()) { AddError(TEXT("Queue rejected an in-bounds entry")); break; }
			}
			World->BeginTearingDown();
		}
		auto Status = MakeShared<FJsonObject>();
		Status->SetStringField(TEXT("status"), TEXT("COMMITTED"));
		Reply(Status);
		Reply(Status);
	});
	TestTrue(TEXT("Second admission runs the reentrant dispatch"), Enqueue());
	auto Committed = MakeShared<FJsonObject>();
	Committed->SetStringField(TEXT("status"), TEXT("COMMITTED"));
	FirstReply(Committed);
	for (int32 Pump = 0; Pump < 64 && Outbox->GetOutboxStats().PendingCount; ++Pump)
	{
		Outbox->PumpOutboxForTest(FPlatformTime::Seconds());
	}
	TestEqual(TEXT("Synchronous replies and enqueue callbacks never recurse into dispatch"), PeakDepth, 1);
	TestTrue(TEXT("Equal-due operations retain admission order"), Dispatched == Keys);
	TestEqual(TEXT("World teardown preserves accepted entries and callbacks are single-shot"), Confirmed, Keys.Num());
	TestEqual(TEXT("No stranded work"), Outbox->GetOutboxStats().PendingCount + Outbox->GetOutboxStats().InFlightCount, 0);
	return !HasAnyErrors();
}
#endif
