#include "Backend/Project_JOnlineWorldSubsystem.h"

#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "Backend/Project_JNakamaServicePort.h"
#include "Backend/Project_JOnlineCrafting.h"
#include "Backend/Project_JOnlineQuests.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "Components/Project_JCraftingJobsComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JInventoryComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Game/Project_JPlayerState.h"
#include "Game/Project_JGameMode.h"
#include "Game/Project_JPlayerController.h"
#include "Gameplay/Project_JLootSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Inventory/Project_JItemDefinition.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJOnlineWorld, Log, All);

namespace
{
FAutoConsoleCommandWithWorldAndArgs GProjectJRedriveOutboxDeadLettersCmd(
	TEXT("ProjectJ.Online.RedriveOutboxDeadLetters"),
	TEXT("Requeues quarantined durable outbox dead-letter records for replay. Usage: ProjectJ.Online.RedriveOutboxDeadLetters [MaxCount]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		auto* Online = GI ? GI->GetSubsystem<UProject_JOnlineWorldSubsystem>() : nullptr;
		if (!Online)
		{
			UE_LOG(LogProjectJOnlineWorld, Warning, TEXT("RedriveOutboxDeadLetters: no active UProject_JOnlineWorldSubsystem."));
			return;
		}
		int32 MaxCount = UProject_JOnlineWorldSubsystem::MaxOutboxEntries;
		if (Args.Num() > 1 || (Args.Num() == 1 && (!LexTryParseString(MaxCount, *Args[0]) || MaxCount <= 0)))
		{
			UE_LOG(LogProjectJOnlineWorld, Warning, TEXT("Usage: ProjectJ.Online.RedriveOutboxDeadLetters [positive count]"));
			return;
		}
		if (!Online->RedriveDeadLetters(MaxCount))
		{
			UE_LOG(LogProjectJOnlineWorld, Warning, TEXT("RedriveOutboxDeadLetters: persistent outbox is not active or closed."));
		}
	}));

FString EncodeJson(const TSharedRef<FJsonObject>& Object)
{
	FString Text;
	FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Text));
	return Text;
}

TSharedPtr<FJsonObject> ReadJson(const FString& Text)
{
	TSharedPtr<FJsonObject> Object;
	return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object) ? Object : nullptr;
}

TSharedPtr<FJsonObject> DecodeServiceReply(const ProjectJ::MMO::FServiceResponse& Response)
{
	using ProjectJ::MMO::EServiceStatus;
	if (Response.Status == EServiceStatus::Overloaded || Response.Status == EServiceStatus::Rejected)
	{
		auto Reply = MakeShared<FJsonObject>();
		Reply->SetStringField(TEXT("status"), TEXT("PRE_DISPATCH_REJECTED"));
		return Reply;
	}
	if (Response.Status != EServiceStatus::Success || Response.Payload.IsEmpty()) return nullptr;
	const FUTF8ToTCHAR Data(reinterpret_cast<const ANSICHAR*>(Response.Payload.GetData()), Response.Payload.Num());
	return ReadJson(FString(Data.Length(), Data.Get()));
}

bool IsLoopbackHttp(const FString& Url)
{
	const FString Prefix(TEXT("http://127.0.0.1:"));
	if (!Url.StartsWith(Prefix)) return false;
	const FString Suffix = Url.Mid(Prefix.Len());
	int32 Port = 0;
	return Suffix.IsNumeric() && LexTryParseString(Port, *Suffix) && Port > 0 && Port <= 65535;
}

FString DefaultDedicatedOutboxPath()
{
	int32 ServerPort = 0;
	const bool bHasPort = FParse::Value(FCommandLine::Get(), TEXT("Port="), ServerPort)
		&& ServerPort > 0 && ServerPort <= 65535;
	return FPaths::ProjectSavedDir() / TEXT("OnlineOutbox") /
		(bHasPort ? FString::Printf(TEXT("ServerOutbox_%d.sqlite"), ServerPort) : TEXT("ServerOutbox.sqlite"));
}

int64 OutboxUtcMilliseconds()
{
	return static_cast<int64>((FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalMilliseconds());
}

double RemainingBossCooldown(int64 ExpiresUtcMilliseconds)
{
	return FMath::Max(0.0, (ExpiresUtcMilliseconds - OutboxUtcMilliseconds()) / 1000.0);
}
}

void UProject_JOnlineWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UProject_JNakamaServicePortSubsystem>();

	FString DbPath;
	if (!FParse::Value(FCommandLine::Get(), TEXT("ProjectJOnlineOutboxPath="), DbPath) || DbPath.IsEmpty())
	{
		DbPath = OnlineOutboxDatabasePath;
	}
	if (DbPath.IsEmpty() && IsRunningDedicatedServer())
	{
		const auto* Port = GetGameInstance() ? GetGameInstance()->GetSubsystem<UProject_JNakamaServicePortSubsystem>() : nullptr;
		if (Port && Port->GetRuntimeMode() != EProject_JBackendRuntimeMode::StandaloneMock)
		{
			DbPath = DefaultDedicatedOutboxPath();
		}
	}

	if (!DbPath.IsEmpty())
	{
		ConfigurePersistentOutbox(DbPath);
	}
}

FString UProject_JOnlineWorldSubsystem::GuidText(FGuid Id)
{
	return Id.ToString(EGuidFormats::DigitsWithHyphensLower);
}

TSharedRef<FJsonObject> UProject_JOnlineWorldSubsystem::WriteLoot(const FProject_JRolledLootGrant& Grant)
{
	auto Reward = MakeShared<FJsonObject>();
	Reward->SetStringField(TEXT("gold"), LexToString(Grant.Gold));
	TArray<TSharedPtr<FJsonValue>> Items;
	for (const auto& Stack : Grant.Items)
	{
		auto Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("definition_id"), Stack.ItemDefinitionId.ToString());
		Item->SetNumberField(TEXT("definition_version"), Stack.ItemDefinitionVersion);
		Item->SetNumberField(TEXT("quantity"), Stack.Quantity);
		Items.Add(MakeShared<FJsonValueObject>(Item));
	}
	Reward->SetArrayField(TEXT("items"), MoveTemp(Items));
	return Reward;
}

bool UProject_JOnlineWorldSubsystem::UsesOnlineBackend(const UWorld* World)
{
	const UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
	const auto* Port = Instance ? Instance->GetSubsystem<UProject_JNakamaServicePortSubsystem>() : nullptr;
	return Port && World->GetNetMode() == NM_DedicatedServer && Port->GetRuntimeMode() != EProject_JBackendRuntimeMode::StandaloneMock;
}

bool UProject_JOnlineWorldSubsystem::Submit(UWorld* World, FName Service, FName Operation,
	const TSharedRef<FJsonObject>& Intent, FGuid Key, TFunction<void(TSharedPtr<FJsonObject>)> Completion)
{
	if (!UsesOnlineBackend(World) || !Completion) return false;
	using namespace ProjectJ::MMO;
	FServiceRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.IdempotencyKey = Key;
	Request.Intent = Key.IsValid() ? EServiceRequestIntent::Mutation : EServiceRequestIntent::Query;
	Request.Service = Service;
	Request.Operation = Operation;
	const FString Text = EncodeJson(Intent);
	const FTCHARToUTF8 Bytes(*Text);
	Request.Payload.Append(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length());
	auto* Port = World->GetGameInstance()->GetSubsystem<UProject_JNakamaServicePortSubsystem>();
	Port->Submit(MoveTemp(Request), [Done = MoveTemp(Completion)](FServiceResponse Response)
	{
		Done(DecodeServiceReply(Response));
	});
	return true;
}

bool UProject_JOnlineWorldSubsystem::ConfigurePersistentOutbox(const FString& DatabasePath)
{
	check(IsInGameThread());
	if (bOutboxClosed || bStoreLoadFailed || DatabasePath.IsEmpty())
	{
		return false;
	}
	if (!FPlatformProcess::SupportsMultithreading())
	{
		bStoreLoadFailed = true;
		++OutboxStats.StoreErrorTotal;
		UE_LOG(LogProjectJOnlineWorld, Error, TEXT("Persistent online outbox requires the worker thread pool"));
		return false;
	}
	auto Store = MakeShared<FProject_JOnlineOutboxStore, ESPMode::ThreadSafe>(DatabasePath);
	if (OutboxStore)
	{
		constexpr auto PathComparison = PLATFORM_WINDOWS ? ESearchCase::IgnoreCase : ESearchCase::CaseSensitive;
		return Store->GetFilename().Equals(OutboxStore->GetFilename(), PathComparison);
	}
	if (!ActiveOutboxKeys.IsEmpty()) return false;
	OutboxStore = MoveTemp(Store);
	KickStoreWorker();
	return true;
}

bool UProject_JOnlineWorldSubsystem::RedriveDeadLetters(int32 MaxToRedrive)
{
	check(IsInGameThread());
	if (bOutboxClosed || bStoreLoadFailed || !OutboxStore || MaxToRedrive <= 0)
	{
		return false;
	}
	PendingRedriveLimit = FMath::Max(PendingRedriveLimit, FMath::Min(MaxToRedrive, MaxOutboxEntries));
	KickStoreWorker();
	return true;
}

bool UProject_JOnlineWorldSubsystem::EnqueueDurableMutation(
	UWorld* World,
	FName Service,
	FName Operation,
	const TSharedRef<FJsonObject>& Intent,
	FGuid StableIdempotencyKey,
	const FString& DebugContext,
	TFunction<void(bool, TSharedPtr<FJsonObject>)> OnFinished)
{
	check(IsInGameThread());
	if (!World || World->bIsTearingDown || !StableIdempotencyKey.IsValid() || Service.IsNone() || Operation.IsNone())
	{
		return false;
	}
	UGameInstance* GI = World->GetGameInstance();
	auto* Self = GI ? GI->GetSubsystem<UProject_JOnlineWorldSubsystem>() : nullptr;
	if (!Self)
	{
		return false;
	}
	// Allow deterministic unit tests with an explicit dispatch hook without requiring NM_DedicatedServer.
	if (!Self->OutboxTestDispatchHook && !UsesOnlineBackend(World))
	{
		return false;
	}
	if (IsRunningDedicatedServer() && !Self->OutboxStore &&
		!Self->ConfigurePersistentOutbox(DefaultDedicatedOutboxPath()))
	{
		return false;
	}

	FOutboxEntry Entry;
	Entry.Service = Service;
	Entry.Operation = Operation;
	Entry.IdempotencyKey = StableIdempotencyKey;
	if (Service == TEXT("World") && Operation == TEXT("Quest_Progress"))
	{
		FString Character;
		if (!Intent->TryGetStringField(TEXT("character_id"), Character)
			|| !FGuid::Parse(Character, Entry.QuestCharacter) || !Entry.QuestCharacter.IsValid()) return false;
	}
	Entry.DebugContext = DebugContext;
	Entry.OnFinished = MoveTemp(OnFinished);
	const FString Text = EncodeJson(Intent);
	const FTCHARToUTF8 Bytes(*Text);
	Entry.SerializedPayload.Append(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length());
	return Self->EnqueueDurableMutationInternal(MoveTemp(Entry));
}

bool UProject_JOnlineWorldSubsystem::TryGetBossRewardCooldown(const FString& EncounterKey, double& OutRemainingSeconds) const
{
	check(IsInGameThread());
	if (!OutboxStore || !bStoreOpened || bStoreLoadFailed || bOutboxClosed) return false;
	OutRemainingSeconds = RemainingBossCooldown(BossRewardCooldowns.FindRef(EncounterKey));
	return true;
}

bool UProject_JOnlineWorldSubsystem::EnqueueBossRewards(UWorld* World, const FString& EncounterKey, FGuid DefeatId,
	double CooldownSeconds, TConstArrayView<FProject_JRolledLootGrant> Rewards, FBossCommit OnCommitted,
	const TMap<FGuid, TSharedPtr<FJsonObject>>& EarnedQuestCredits)
{
	check(IsInGameThread());
	if (!World || World->bIsTearingDown || EncounterKey.IsEmpty() || !DefeatId.IsValid() || !OnCommitted
		|| !FMath::IsFinite(CooldownSeconds) || CooldownSeconds < 1.0 || CooldownSeconds > (MAX_int64 / 2000.0)) return false;
	auto* GI = World->GetGameInstance();
	auto* Self = GI ? GI->GetSubsystem<UProject_JOnlineWorldSubsystem>() : nullptr;
	double Remaining = 0.0;
	if (!Self || (!Self->OutboxTestDispatchHook && !UsesOnlineBackend(World))
		|| !Self->TryGetBossRewardCooldown(EncounterKey, Remaining) || Self->SettlingBossEncounters.Contains(EncounterKey)) return false;
	if (EarnedQuestCredits.Num() > Rewards.Num() || static_cast<int64>(Rewards.Num()) + EarnedQuestCredits.Num() > MaxOutboxEntries - Self->GetTotalActiveOutboxCount()
		|| Self->SettlingBossEncounters.Num() >= MaxOutboxEntries || Self->bRecoverMore
		|| Self->OutboxStats.DeadLetterCount >= FProject_JOnlineOutboxStore::DeadLetterAdmissionLimit)
	{
		++Self->OutboxStats.RejectedCapacityTotal;
		return false;
	}

	FPendingBossSettlement Pending;
	Pending.Settlement.EncounterKey = EncounterKey;
	Pending.Settlement.DefeatId = DefeatId;
	Pending.Settlement.CooldownMilliseconds = FMath::CeilToInt64(CooldownSeconds * 1000.0);
	Pending.OnCommitted = MoveTemp(OnCommitted);
	Pending.Entries.Reserve(Rewards.Num() + EarnedQuestCredits.Num());
	const FString DefeatText = GuidText(DefeatId);
	const int64 PendingSinceUtcMs = OutboxUtcMilliseconds();
	TSet<FGuid> Keys;
	const auto Append = [&](FName Service, FName Operation, const FString& StablePath, FGuid QuestCharacter,
		const TSharedRef<FJsonObject>& Body, const FString& Character)
	{
		const FGuid Key = FGuid::NewDeterministicGuid(StablePath);
		if (Keys.Contains(Key) || Self->ActiveOutboxKeys.Contains(Key))
		{
			++Self->OutboxStats.DeduplicatedTotal;
			return false;
		}
		Keys.Add(Key);
		const FString Text = EncodeJson(Body);
		const FTCHARToUTF8 Bytes(*Text);
		FOutboxEntry& Entry = Pending.Entries.AddDefaulted_GetRef();
		Entry.Service = Service;
		Entry.Operation = Operation;
		Entry.IdempotencyKey = Key;
		Entry.QuestCharacter = QuestCharacter;
		Entry.PendingSinceUtcMs = PendingSinceUtcMs;
		Entry.DebugContext = FString::Printf(TEXT("encounter=%s defeat=%s char=%s"), *EncounterKey, *DefeatText, *Character);
		Entry.SerializedPayload.Append(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length());
		return true;
	};
	int32 QuestCreditsAdded = 0;
	for (const FProject_JRolledLootGrant& Grant : Rewards)
	{
		if (!Grant.RecipientCharacterId.IsValid() || Grant.Gold < 0 || Grant.Items.Num() > 40) return false;
		for (const FProject_JLootItemStack& Item : Grant.Items)
		{
			if (Item.ItemDefinitionId.IsNone() || Item.ItemDefinitionId.ToString().Len() > 64
				|| Item.ItemDefinitionVersion <= 0 || Item.Quantity <= 0) return false;
		}
		const FString Character = GuidText(Grant.RecipientCharacterId);
		const FString StablePath = TEXT("ProjectJ.FieldBoss.") + DefeatText + TEXT(".") + Character;
		auto Body = WriteLoot(Grant);
		Body->SetStringField(TEXT("defeat_id"), DefeatText);
		Body->SetStringField(TEXT("character_id"), Character);
		if (!Append(TEXT("Reward"), TEXT("Boss"), StablePath, FGuid(), Body, Character)) return false;
		if (const auto* Quest = EarnedQuestCredits.Find(Grant.RecipientCharacterId))
		{
			FString QuestCharacter;
			if (!Quest->IsValid() || !(*Quest)->TryGetStringField(TEXT("character_id"), QuestCharacter) || QuestCharacter != Character
				|| !Append(TEXT("World"), TEXT("Quest_Progress"), StablePath + TEXT(".Quest"), Grant.RecipientCharacterId, Quest->ToSharedRef(), Character)) return false;
			++QuestCreditsAdded;
		}
	}
	if (QuestCreditsAdded != EarnedQuestCredits.Num()) return false;

	for (const FOutboxEntry& Entry : Pending.Entries) Self->ReserveOutboxKey(Entry);
	Self->OutboxStats.EnqueuedTotal += Pending.Entries.Num();
	Self->SettlingBossEncounters.Add(EncounterKey);
	Self->QueuedBossSettlements.Add(MoveTemp(Pending));
	Self->KickStoreWorker();
	return true;
}

int32 UProject_JOnlineWorldSubsystem::GetTotalActiveOutboxCount() const
{
	return ActiveOutboxKeys.Num() + ReservedRedrive;
}

FProject_JPersistedOutboxRecord UProject_JOnlineWorldSubsystem::MakeStoreRecord(const FOutboxEntry& Entry, const FString& LastStatus)
{
	FProject_JPersistedOutboxRecord Record;
	Record.IdempotencyKey = Entry.IdempotencyKey;
	Record.PendingSinceUtcMs = Entry.PendingSinceUtcMs;
	Record.Service = Entry.Service;
	Record.Operation = Entry.Operation;
	Record.SerializedPayload = Entry.SerializedPayload;
	Record.DebugContext = Entry.DebugContext;
	Record.TotalAttempts = Entry.Attempts;
	Record.LastStatus = LastStatus;
	return Record;
}

bool UProject_JOnlineWorldSubsystem::EnqueueDurableMutationInternal(FOutboxEntry&& Entry)
{
	check(IsInGameThread());
	if (bOutboxClosed || bStoreLoadFailed) return false;
	if (Entry.QuestCharacter.IsValid() && OutboxStore && (!bStoreOpened || bRecoverMore))
	{
		// Accept fresh ordered events only once the disk's earlier lanes are known.
		++OutboxStats.RejectedCapacityTotal;
		return false;
	}
	if (ActiveOutboxKeys.Contains(Entry.IdempotencyKey))
	{
		++OutboxStats.DeduplicatedTotal;
		UE_LOG(LogProjectJOnlineWorld, Verbose,
			TEXT("Durable outbox deduplicated active key=%s for %s.%s context=%s"),
			*GuidText(Entry.IdempotencyKey), *Entry.Service.ToString(), *Entry.Operation.ToString(), *Entry.DebugContext);
		return false;
	}
	if (GetTotalActiveOutboxCount() >= MaxOutboxEntries)
	{
		++OutboxStats.RejectedCapacityTotal;
		UE_LOG(LogProjectJOnlineWorld, Error,
			TEXT("Durable outbox capacity (%d) exceeded; rejecting %s.%s key=%s context=%s"),
			MaxOutboxEntries, *Entry.Service.ToString(), *Entry.Operation.ToString(),
			*GuidText(Entry.IdempotencyKey), *Entry.DebugContext);
		return false;
	}
	if (bRecoverMore || OutboxStats.DeadLetterCount >= FProject_JOnlineOutboxStore::DeadLetterAdmissionLimit)
	{
		// Older durable work must be able to drain even during continuous ingress.
		// Once quarantine fills, stop taking new work without deleting the evidence.
		++OutboxStats.RejectedCapacityTotal;
		return false;
	}

	++OutboxStats.EnqueuedTotal;
	Entry.PendingSinceUtcMs = OutboxUtcMilliseconds();
	ReserveOutboxKey(Entry);
	Entry.NextAttemptAt = FPlatformTime::Seconds();

	if (OutboxStore.IsValid())
	{
		FProject_JOutboxStoreOp Op;
		Op.Kind = EProject_JOutboxStoreOpKind::UpsertPending;
		Op.Record = MakeStoreRecord(Entry);
		QueuedStoreOps.Add(MoveTemp(Op));
		QueuedPersistEntries.Add(MoveTemp(Entry));
		KickStoreWorker();
		return true;
	}

	QueueOutboxEntry(MoveTemp(Entry));
	PumpOutbox(FPlatformTime::Seconds());
	if (!PendingOutbox.IsEmpty())
	{
		EnsureOutboxTicker();
	}
	return true;
}

void UProject_JOnlineWorldSubsystem::KickStoreWorker()
{
	check(IsInGameThread());
	if (bOutboxClosed || bStoreWorkerActive || !OutboxStore) return;
	if (StoreRetryAt > FPlatformTime::Seconds())
	{
		EnsureOutboxTicker();
		return;
	}
	const uint64 Generation = OutboxGeneration;
	const TWeakObjectPtr<UProject_JOnlineWorldSubsystem> WeakSelf(this);
	const auto Store = OutboxStore;
	const int32 FreeSlots = MaxOutboxEntries - GetTotalActiveOutboxCount();
	const bool bLoad = !bStoreOpened || (bRecoverMore && QueuedStoreOps.IsEmpty() && FreeSlots > 0);
	if (!bStoreLoadFailed && bLoad)
	{
		const int32 Limit = bStoreOpened ? FreeSlots : MaxOutboxEntries;
		const int64 AfterSeq = RecoveryAfterSeq;
		const bool bLoadCooldowns = !bStoreOpened;
		bStoreWorkerActive = true;
		ActiveStoreTask = Async(EAsyncExecution::ThreadPool, [WeakSelf, Generation, Store, Limit, AfterSeq, bLoadCooldowns]
		{
			TArray<FProject_JPersistedOutboxRecord> Rows;
			TMap<FString, int64> Cooldowns;
			int32 DeadLetters = 0;
			const bool bOk = Store->OpenAndLoadPending(Limit, Rows, DeadLetters, AfterSeq, bLoadCooldowns ? &Cooldowns : nullptr);
			FString Error = bOk ? FString() : Store->GetLastFailure();
			AsyncTask(ENamedThreads::GameThread, [WeakSelf, Generation, Limit, bOk, Error = MoveTemp(Error), Rows = MoveTemp(Rows), DeadLetters, Cooldowns = MoveTemp(Cooldowns)]() mutable
			{
				if (auto* Self = WeakSelf.Get()) Self->HandleStoreOpenLoaded(Generation, Limit, bOk, MoveTemp(Error), MoveTemp(Rows), DeadLetters, MoveTemp(Cooldowns));
			});
		});
		return;
	}
	if (!QueuedStoreOps.IsEmpty() || !QueuedBossSettlements.IsEmpty())
	{
		bStoreWorkerActive = true;
		auto Batch = MakeShared<FStoreBatch, ESPMode::ThreadSafe>();
		TArray<FOutboxEntry> Entries;
		FBossCommit OnBossCommitted;
		if (!QueuedBossSettlements.IsEmpty())
		{
			FPendingBossSettlement QueueHead = MoveTemp(QueuedBossSettlements[0]);
			QueuedBossSettlements.RemoveAt(0, EAllowShrinking::No);
			Batch->BossSettlement.Emplace(MoveTemp(QueueHead.Settlement));
			Entries = MoveTemp(QueueHead.Entries);
			Batch->Ops.Reserve(Entries.Num());
			for (const FOutboxEntry& Entry : Entries)
			{
				Batch->Ops.Add({EProject_JOutboxStoreOpKind::UpsertPending, MakeStoreRecord(Entry)});
			}
			OnBossCommitted = MoveTemp(QueueHead.OnCommitted);
		}
		else
		{
			Batch->Ops = MoveTemp(QueuedStoreOps);
			Entries = MoveTemp(QueuedPersistEntries);
		}
		ActiveStoreBatch = Batch;
		InFlightPersistCount = Entries.Num();
		ActiveStoreTask = Async(EAsyncExecution::ThreadPool, [WeakSelf, Generation, Store, Batch, Entries = MoveTemp(Entries), OnBossCommitted = MoveTemp(OnBossCommitted)]() mutable
		{
			Batch->bCommitted = Store->ApplyBatch(Batch->Ops, Batch->DeadLetters, &Batch->Existing,
				Batch->BossSettlement.IsSet() ? &Batch->BossSettlement.GetValue() : nullptr);
			if (!Batch->bCommitted) Batch->Failure = Store->GetLastFailure();
			AsyncTask(ENamedThreads::GameThread, [WeakSelf, Generation, Batch, Entries = MoveTemp(Entries), OnBossCommitted = MoveTemp(OnBossCommitted)]() mutable
			{
				if (auto* Self = WeakSelf.Get()) Self->HandleStoreBatchApplied(Generation, *Batch, MoveTemp(Entries), MoveTemp(OnBossCommitted));
			});
		});
		return;
	}
	if (!bStoreLoadFailed && !bRecoverMore && PendingRedriveLimit > 0 && FreeSlots > 0)
	{
		const int32 Limit = FMath::Min(PendingRedriveLimit, FreeSlots);
		PendingRedriveLimit = 0;
		ReservedRedrive = Limit;
		bStoreWorkerActive = true;
		ActiveStoreTask = Async(EAsyncExecution::ThreadPool, [WeakSelf, Generation, Store, Limit]
		{
			TArray<FProject_JPersistedOutboxRecord> Rows;
			int32 DeadLetters = 0;
			const bool bOk = Store->RedriveDeadLetters(Limit, Rows, DeadLetters);
			FString Error = bOk ? FString() : Store->GetLastFailure();
			AsyncTask(ENamedThreads::GameThread, [WeakSelf, Generation, bOk, Error = MoveTemp(Error), Rows = MoveTemp(Rows), DeadLetters]() mutable
			{
				if (auto* Self = WeakSelf.Get()) Self->HandleStoreRedriven(Generation, bOk, MoveTemp(Error), MoveTemp(Rows), DeadLetters);
			});
		});
	}
}

void UProject_JOnlineWorldSubsystem::ReportDeadLetterCount(int32 Count)
{
	if (Count >= FProject_JOnlineOutboxStore::DeadLetterAdmissionLimit
		&& OutboxStats.DeadLetterCount < FProject_JOnlineOutboxStore::DeadLetterAdmissionLimit)
	{
		UE_LOG(LogProjectJOnlineWorld, Warning, TEXT("Outbox has %d dead letters; review and redrive before storage grows further."), Count);
	}
	OutboxStats.DeadLetterCount = Count;
}

void UProject_JOnlineWorldSubsystem::HandleStoreOpenLoaded(uint64 Generation, int32 Limit, bool bOk, FString Error,
	TArray<FProject_JPersistedOutboxRecord> Recovered, int32 DeadLetters, TMap<FString, int64> Cooldowns)
{
	check(IsInGameThread());
	if (Generation != OutboxGeneration || bOutboxClosed) return;
	bStoreWorkerActive = false;
	if (!bOk)
	{
		bStoreLoadFailed = true;
		++OutboxStats.StoreErrorTotal;
		UE_LOG(LogProjectJOnlineWorld, Error, TEXT("Persistent online outbox open/load failed: %s"), *Error);
		TArray<FOutboxEntry> Rejected = MoveTemp(QueuedPersistEntries);
		QueuedStoreOps.RemoveAll([](const FProject_JOutboxStoreOp& Op) { return Op.Kind == EProject_JOutboxStoreOpKind::UpsertPending; });
		for (FOutboxEntry& Entry : Rejected)
		{
			ReleaseOutboxKey(Entry.IdempotencyKey);
			++OutboxStats.FailedTotal;
			if (!bOutboxClosed && Entry.OnFinished) Entry.OnFinished(false, nullptr);
		}
		TArray<FPendingBossSettlement> RejectedBosses = MoveTemp(QueuedBossSettlements);
		for (FPendingBossSettlement& Boss : RejectedBosses)
		{
			SettlingBossEncounters.Remove(Boss.Settlement.EncounterKey);
			for (FOutboxEntry& Entry : Boss.Entries) ReleaseOutboxKey(Entry.IdempotencyKey);
			OutboxStats.FailedTotal += Boss.Entries.Num();
			if (!bOutboxClosed && Boss.OnCommitted) Boss.OnCommitted(false, 0.0);
		}
		KickStoreWorker();
		return;
	}
	if (!bStoreOpened) BossRewardCooldowns = MoveTemp(Cooldowns);
	bStoreOpened = true;
	bRecoverMore = Recovered.Num() == Limit;
	ReportDeadLetterCount(DeadLetters);
	const double Now = FPlatformTime::Seconds();
	TMap<FGuid, FOutboxEntry*> WaitingByKey;
	for (FOutboxEntry& Entry : QueuedPersistEntries) WaitingByKey.Add(Entry.IdempotencyKey, &Entry);
	TSet<FGuid> Reconciled;
	TArray<TFunction<void(bool, TSharedPtr<FJsonObject>)>> RejectedCallbacks;
	for (FProject_JPersistedOutboxRecord& Record : Recovered)
	{
		if (FOutboxEntry** Waiting = WaitingByKey.Find(Record.IdempotencyKey))
		{
			Reconciled.Add(Record.IdempotencyKey);
			++OutboxStats.DeduplicatedTotal;
			++OutboxStats.RecoveredTotal;
			auto& Entry = **Waiting;
			if (Record.Service != Entry.Service || Record.Operation != Entry.Operation || Record.SerializedPayload != Entry.SerializedPayload)
			{
				++OutboxStats.FailedTotal;
				if (Entry.OnFinished) RejectedCallbacks.Add(MoveTemp(Entry.OnFinished));
			}
			RecoveryAfterSeq = Record.CreatedSeq;
			QueueRecoveredEntry(MoveTemp(Record), Now, MoveTemp(Entry.OnFinished));
			continue;
		}
		if (ActiveOutboxKeys.Contains(Record.IdempotencyKey))
		{
			RecoveryAfterSeq = Record.CreatedSeq;
			continue;
		}
		if (GetTotalActiveOutboxCount() >= MaxOutboxEntries)
		{
			bRecoverMore = true;
			break;
		}
		RecoveryAfterSeq = Record.CreatedSeq;
		QueueRecoveredEntry(MoveTemp(Record), Now);
		++OutboxStats.RecoveredTotal;
	}
	if (!Reconciled.IsEmpty())
	{
		QueuedPersistEntries.RemoveAll([&](const auto& Entry) { return Reconciled.Contains(Entry.IdempotencyKey); });
		QueuedStoreOps.RemoveAll([&](const auto& Op)
		{
			return Op.Kind == EProject_JOutboxStoreOpKind::UpsertPending && Reconciled.Contains(Op.Record.IdempotencyKey);
		});
	}
	for (const auto& Callback : RejectedCallbacks) if (!bOutboxClosed) Callback(false, nullptr);
	PumpOutbox(Now);
	if (!PendingOutbox.IsEmpty()) EnsureOutboxTicker();
	KickStoreWorker();
}

void UProject_JOnlineWorldSubsystem::HandleStoreBatchApplied(uint64 Generation, const FStoreBatch& Batch,
	TArray<FOutboxEntry> Entries, FBossCommit OnBossCommitted)
{
	check(IsInGameThread());
	if (Generation != OutboxGeneration || bOutboxClosed) return;
	bStoreWorkerActive = false;
	ActiveStoreBatch.Reset();
	InFlightPersistCount = 0;
	if (Batch.BossSettlement.IsSet())
	{
		const FProject_JBossRewardSettlement& Boss = Batch.BossSettlement.GetValue();
		SettlingBossEncounters.Remove(Boss.EncounterKey);
		if (!Batch.bCommitted)
		{
			for (const FOutboxEntry& Entry : Entries) ReleaseOutboxKey(Entry.IdempotencyKey);
			RecoveryAfterSeq = 0;
			bRecoverMore = true;
			++OutboxStats.StoreErrorTotal;
			OutboxStats.FailedTotal += Entries.Num();
			UE_LOG(LogProjectJOnlineWorld, Error, TEXT("Boss reward settlement write failed (encounter=%s, defeat=%s, recipients=%d): %s"),
				*Boss.EncounterKey, *GuidText(Boss.DefeatId), Entries.Num(), *Batch.Failure);
			OnBossCommitted(false, 0.0);
			KickStoreWorker();
			return;
		}
		ReportDeadLetterCount(Batch.DeadLetters);
		BossRewardCooldowns.Add(Boss.EncounterKey, Boss.ExpiresUtcMilliseconds);
		if (Boss.bAlreadyCommitted)
		{
			// A prior commit may have succeeded before its result reached the game thread.
			RecoveryAfterSeq = 0;
			bRecoverMore = true;
		}
		const double Now = FPlatformTime::Seconds();
		for (FOutboxEntry& Entry : Entries)
		{
			if (Boss.bAlreadyCommitted)
			{
				ReleaseOutboxKey(Entry.IdempotencyKey);
				++OutboxStats.DeduplicatedTotal;
			}
			else
			{
				Entry.NextAttemptAt = Now;
				QueueOutboxEntry(MoveTemp(Entry));
			}
		}
		OnBossCommitted(true, RemainingBossCooldown(Boss.ExpiresUtcMilliseconds));
		PumpOutbox(Now);
		if (!PendingOutbox.IsEmpty()) EnsureOutboxTicker();
		KickStoreWorker();
		return;
	}
	if (!Batch.bCommitted)
	{
		++OutboxStats.StoreErrorTotal;
		UE_LOG(LogProjectJOnlineWorld, Error, TEXT("Persistent online outbox batch write failed (%d mutations fail-closed without wire dispatch): %s"),
			Entries.Num(), *Batch.Failure);
		for (const auto& Op : Batch.Ops)
		{
			if (Op.Kind != EProject_JOutboxStoreOpKind::UpsertPending) QueuedStoreOps.Add(Op);
		}
		if (!Entries.IsEmpty())
		{
			// Some rejected candidates may have owned a key skipped by the loader.
			RecoveryAfterSeq = 0;
			bRecoverMore = true;
		}
		if (!QueuedStoreOps.IsEmpty())
		{
			StoreRetryAt = FPlatformTime::Seconds() + 1.0;
			EnsureOutboxTicker();
		}
		for (FOutboxEntry& Entry : Entries)
		{
			ReleaseOutboxKey(Entry.IdempotencyKey);
			++OutboxStats.FailedTotal;
			if (!bOutboxClosed && Entry.OnFinished) Entry.OnFinished(false, nullptr);
		}
		KickStoreWorker();
		return;
	}
	ReportDeadLetterCount(Batch.DeadLetters);
	for (const auto& Op : Batch.Ops)
	{
		if (Op.Kind == EProject_JOutboxStoreOpKind::UpsertPending) continue;
		ReleaseOutboxKey(Op.Record.IdempotencyKey);
		--FinalizingCount;
		if (Op.Kind == EProject_JOutboxStoreOpKind::MarkDeadLetter) ++OutboxStats.DeadLetteredTotal;
	}

	const double Now = FPlatformTime::Seconds();
	TArray<FOutboxEntry> Rejected;
	for (FOutboxEntry& Entry : Entries)
	{
		if (const auto* Existing = Batch.Existing.Find(Entry.IdempotencyKey))
		{
			++OutboxStats.DeduplicatedTotal;
			if (Existing->bDeadLetter || Existing->Service != Entry.Service || Existing->Operation != Entry.Operation
				|| Existing->SerializedPayload != Entry.SerializedPayload)
			{
				Rejected.Add(MoveTemp(Entry));
				ReleaseOutboxKey(Existing->IdempotencyKey);
				if (!Existing->bDeadLetter) QueueRecoveredEntry(FProject_JPersistedOutboxRecord(*Existing), Now);
				continue;
			}
			if (Existing->PendingSinceUtcMs > 0)
			{
				Entry.PendingSinceUtcMs = FMath::Min(Entry.PendingSinceUtcMs, Existing->PendingSinceUtcMs);
				ReserveOutboxKey(Entry);
			}
		}
		Entry.NextAttemptAt = Now;
		QueueOutboxEntry(MoveTemp(Entry));
	}
	for (FOutboxEntry& Entry : Rejected)
	{
		++OutboxStats.FailedTotal;
		if (!bOutboxClosed && Entry.OnFinished) Entry.OnFinished(false, nullptr);
	}
	PumpOutbox(Now);
	if (!PendingOutbox.IsEmpty()) EnsureOutboxTicker();
	KickStoreWorker();
}

void UProject_JOnlineWorldSubsystem::QueueRecoveredEntry(FProject_JPersistedOutboxRecord&& Record, double Now,
	TFunction<void(bool, TSharedPtr<FJsonObject>)> Done)
{
	FOutboxEntry Entry;
	Entry.Service = Record.Service;
	Entry.Operation = Record.Operation;
	Entry.IdempotencyKey = Record.IdempotencyKey;
	Entry.PendingSinceUtcMs = Record.PendingSinceUtcMs > 0 ? Record.PendingSinceUtcMs : OutboxUtcMilliseconds();
	Entry.SerializedPayload = MoveTemp(Record.SerializedPayload);
	Entry.DebugContext = MoveTemp(Record.DebugContext);
	Entry.OnFinished = MoveTemp(Done);
	if (Entry.Service == TEXT("World") && Entry.Operation == TEXT("Quest_Progress"))
	{
		const FUTF8ToTCHAR Data(reinterpret_cast<const ANSICHAR*>(Entry.SerializedPayload.GetData()), Entry.SerializedPayload.Num());
		const auto Intent = ReadJson(FString(Data.Length(), Data.Get()));
		FString Character;
		if (Intent && Intent->TryGetStringField(TEXT("character_id"), Character))
		{
			FGuid::Parse(Character, Entry.QuestCharacter);
		}
	}
	ReserveOutboxKey(Entry);
	Entry.NextAttemptAt = Now;
	QueueOutboxEntry(MoveTemp(Entry));
}

void UProject_JOnlineWorldSubsystem::HandleStoreRedriven(uint64 Generation, bool bOk, FString Error,
	TArray<FProject_JPersistedOutboxRecord> Redriven, int32 RemainingDeadLetters)
{
	check(IsInGameThread());
	if (Generation != OutboxGeneration || bOutboxClosed) return;
	bStoreWorkerActive = false;
	ReservedRedrive = 0;
	if (!bOk)
	{
		++OutboxStats.StoreErrorTotal;
		UE_LOG(LogProjectJOnlineWorld, Error, TEXT("Persistent online outbox dead-letter redrive failed: %s"), *Error);
		KickStoreWorker();
		return;
	}
	ReportDeadLetterCount(RemainingDeadLetters);
	const double Now = FPlatformTime::Seconds();
	for (auto& Record : Redriven)
	{
		++OutboxStats.RedrivenTotal;
		if (ActiveOutboxKeys.Contains(Record.IdempotencyKey)) continue;
		QueueRecoveredEntry(MoveTemp(Record), Now);
	}
	UE_LOG(LogProjectJOnlineWorld, Display,
		TEXT("Persistent online outbox selected %d dead letters for redrive (%d remaining)."), Redriven.Num(), RemainingDeadLetters);
	PumpOutbox(Now);
	if (!PendingOutbox.IsEmpty()) EnsureOutboxTicker();
	KickStoreWorker();
}

bool UProject_JOnlineWorldSubsystem::ClosePersistentStoreSync()
{
	check(IsInGameThread());
	// Shutdown owns the join. Store tasks post their result asynchronously and do
	// not wait for the game thread; the invalidated generation discards those callbacks.
	if (ActiveStoreTask.IsValid())
	{
		ActiveStoreTask.Wait();
		ActiveStoreTask = {};
	}
	TArray<FProject_JOutboxStoreOp> FinalOps;
	TOptional<FProject_JBossRewardSettlement> ActiveBoss;
	TArray<FProject_JOutboxStoreOp> ActiveBossOps;
	if (ActiveStoreBatch && !ActiveStoreBatch->bCommitted)
	{
		if (ActiveStoreBatch->BossSettlement.IsSet())
		{
			ActiveBoss = MoveTemp(ActiveStoreBatch->BossSettlement);
			ActiveBossOps = MoveTemp(ActiveStoreBatch->Ops);
		}
		else
		{
			FinalOps = MoveTemp(ActiveStoreBatch->Ops);
		}
	}
	ActiveStoreBatch.Reset();
	FinalOps.Append(MoveTemp(QueuedStoreOps));
	QueuedPersistEntries.Reset();
	InFlightPersistCount = 0;
	PendingRedriveLimit = 0;
	ReservedRedrive = 0;
	bStoreWorkerActive = false;
	if (auto Store = MoveTemp(OutboxStore))
	{
		return Async(EAsyncExecution::ThreadPool, [Store, FinalOps = MoveTemp(FinalOps), ActiveBoss = MoveTemp(ActiveBoss),
			ActiveBossOps = MoveTemp(ActiveBossOps), QueuedBosses = MoveTemp(QueuedBossSettlements)]() mutable
		{
			int32 DeadLetters = 0;
			bool bOk = FinalOps.IsEmpty() || Store->ApplyBatch(FinalOps, DeadLetters);
			if (!bOk) UE_LOG(LogProjectJOnlineWorld, Error, TEXT("Outbox shutdown could not commit %d operations: %s"),
				FinalOps.Num(), *Store->GetLastFailure());
			const auto CommitBoss = [&](FProject_JBossRewardSettlement& Settlement, TConstArrayView<FProject_JOutboxStoreOp> Ops)
			{
				if (Store->ApplyBatch(Ops, DeadLetters, nullptr, &Settlement)) return;
				UE_LOG(LogProjectJOnlineWorld, Error, TEXT("Outbox shutdown could not commit boss defeat %s for %s: %s"),
					*GuidText(Settlement.DefeatId), *Settlement.EncounterKey, *Store->GetLastFailure());
				bOk = false;
			};
			if (ActiveBoss.IsSet()) CommitBoss(ActiveBoss.GetValue(), ActiveBossOps);
			for (FPendingBossSettlement& Boss : QueuedBosses)
			{
				TArray<FProject_JOutboxStoreOp> Ops;
				Ops.Reserve(Boss.Entries.Num());
				for (const FOutboxEntry& Entry : Boss.Entries)
				{
					Ops.Add({EProject_JOutboxStoreOpKind::UpsertPending, MakeStoreRecord(Entry)});
				}
				CommitBoss(Boss.Settlement, Ops);
			}
			const bool bClosed = Store->Close();
			if (!bClosed) UE_LOG(LogProjectJOnlineWorld, Error, TEXT("Outbox could not close its SQLite connection."));
			return bOk && bClosed;
		}).Get();
	}
	return true;
}

bool UProject_JOnlineWorldSubsystem::IsOutboxEntryEarlier(const FOutboxEntry& A, const FOutboxEntry& B)
{
	return A.NextAttemptAt == B.NextAttemptAt ? A.QueueOrder < B.QueueOrder : A.NextAttemptAt < B.NextAttemptAt;
}

bool UProject_JOnlineWorldSubsystem::HasOutstandingQuestProgress(FGuid Character) const
{
	// Startup must recover older rows before a fresh direct write can pass them.
	return (OutboxStore && (!bStoreOpened || bRecoverMore || bStoreLoadFailed)) || QuestProgressLanes.Contains(Character);
}

void UProject_JOnlineWorldSubsystem::ReserveOutboxKey(const FOutboxEntry& Entry)
{
	if (int64* Existing = ActiveOutboxKeys.Find(Entry.IdempotencyKey))
	{
		*Existing = FMath::Min(*Existing, Entry.PendingSinceUtcMs);
		return;
	}
	ActiveOutboxKeys.Add(Entry.IdempotencyKey, Entry.PendingSinceUtcMs);
	if (Entry.QuestCharacter.IsValid())
	{
		QuestCharactersByKey.Add(Entry.IdempotencyKey, Entry.QuestCharacter);
		++QuestProgressLanes.FindOrAdd(Entry.QuestCharacter).ActiveCount;
	}
}

void UProject_JOnlineWorldSubsystem::ReleaseOutboxKey(FGuid Key)
{
	ActiveOutboxKeys.Remove(Key);
	FGuid Character;
	if (QuestCharactersByKey.RemoveAndCopyValue(Key, Character))
	{
		auto& Lane = QuestProgressLanes.FindChecked(Character);
		Lane.Ready.RemoveSingle(Key);
		if (--Lane.ActiveCount == 0) QuestProgressLanes.Remove(Character);
	}
}

bool UProject_JOnlineWorldSubsystem::CanDispatchOutboxEntry(const FOutboxEntry& Entry) const
{
	if (!Entry.QuestCharacter.IsValid()) return true;
	const auto& Ready = QuestProgressLanes.FindChecked(Entry.QuestCharacter).Ready;
	if (Ready.IsEmpty() || Ready[0] != Entry.IdempotencyKey) return false;
	const auto* World = GetGameInstance()->GetWorld();
	const auto* Mode = World ? World->GetAuthGameMode<AProject_JGameMode>() : nullptr;
	return !Mode || !Mode->HasOnlineSnapshotEdit(Entry.QuestCharacter);
}

void UProject_JOnlineWorldSubsystem::QueueOutboxEntry(FOutboxEntry&& Entry)
{
	if (Entry.QuestCharacter.IsValid() && Entry.Attempts == 0)
	{
		QuestProgressLanes.FindChecked(Entry.QuestCharacter).Ready.Add(Entry.IdempotencyKey);
	}
	Entry.QueueOrder = NextOutboxQueueOrder++;
	PendingOutbox.HeapPush(MoveTemp(Entry), IsOutboxEntryEarlier);
}

void UProject_JOnlineWorldSubsystem::EnsureOutboxTicker()
{
	if (bOutboxClosed || OutboxTicker.IsValid())
	{
		return;
	}
	TWeakObjectPtr<UProject_JOnlineWorldSubsystem> WeakSelf(this);
	OutboxTicker = FTSTicker::GetCoreTicker().AddTicker(
		TEXT("ProjectJOnlineDurableOutbox"),
		0.05f,
		[WeakSelf](float) -> bool
		{
			auto* Self = WeakSelf.Get();
			if (!Self)
			{
				return false;
			}
			Self->KickStoreWorker();
			Self->PumpOutbox(FPlatformTime::Seconds());
			if (Self->PendingOutbox.IsEmpty() && Self->QueuedStoreOps.IsEmpty() && Self->QueuedBossSettlements.IsEmpty())
			{
				Self->OutboxTicker.Reset();
				return false;
			}
			return true;
		});
}

void UProject_JOnlineWorldSubsystem::StopOutboxTicker()
{
	if (OutboxTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(OutboxTicker);
		OutboxTicker.Reset();
	}
}

void UProject_JOnlineWorldSubsystem::PumpOutbox(double NowSeconds)
{
	check(IsInGameThread());
	if (bOutboxClosed || bPumpingOutbox || PendingOutbox.IsEmpty() || InFlightOutbox >= MaxOutboxInFlight)
	{
		return;
	}

	TGuardValue<bool> Pumping(bPumpingOutbox, true);
	const uint64 ExpectedGeneration = OutboxGeneration;
	TWeakObjectPtr<UProject_JOnlineWorldSubsystem> WeakSelf(this);

	TArray<FOutboxEntry, TInlineAllocator<8>> Deferred;
	for (int32 Dispatched = 0; Dispatched < MaxOutboxInFlight && !bOutboxClosed && !PendingOutbox.IsEmpty()
		&& InFlightOutbox < MaxOutboxInFlight && PendingOutbox.HeapTop().NextAttemptAt <= NowSeconds;)
	{
		FOutboxEntry Entry;
		PendingOutbox.HeapPop(Entry, IsOutboxEntryEarlier, EAllowShrinking::No);
		if (!CanDispatchOutboxEntry(Entry))
		{
			Deferred.Add(MoveTemp(Entry));
			continue;
		}
		++Dispatched;
		++Entry.Attempts;
		++InFlightOutbox;

		auto CapturedEntry = MakeShared<TOptional<FOutboxEntry>>(MoveTemp(Entry));
		auto Complete = [WeakSelf, ExpectedGeneration, CapturedEntry](TSharedPtr<FJsonObject> Reply)
		{
			check(IsInGameThread());
			if (!CapturedEntry->IsSet()) return;
			FOutboxEntry Finished = MoveTemp(CapturedEntry->GetValue());
			CapturedEntry->Reset();
			if (auto* Self = WeakSelf.Get()) Self->HandleOutboxReply(ExpectedGeneration, MoveTemp(Finished), MoveTemp(Reply));
		};
		const FOutboxEntry& DispatchEntry = CapturedEntry->GetValue();
		if (OutboxTestDispatchHook)
		{
			const FGuid Key = DispatchEntry.IdempotencyKey;
			const TArray<uint8> PayloadCopy = DispatchEntry.SerializedPayload;
			const auto Dispatch = OutboxTestDispatchHook;
			Dispatch(DispatchEntry.Service, DispatchEntry.Operation, Key, PayloadCopy, MoveTemp(Complete));
			continue;
		}

		auto* Port = GetGameInstance() ? GetGameInstance()->GetSubsystem<UProject_JNakamaServicePortSubsystem>() : nullptr;
		if (!Port || Port->GetRuntimeMode() == EProject_JBackendRuntimeMode::StandaloneMock)
		{
			Complete(nullptr);
			continue;
		}

		using namespace ProjectJ::MMO;
		FServiceRequest Request;
		Request.RequestId = FGuid::NewGuid();
		Request.IdempotencyKey = DispatchEntry.IdempotencyKey;
		Request.Intent = EServiceRequestIntent::Mutation;
		Request.Service = DispatchEntry.Service;
		Request.Operation = DispatchEntry.Operation;
		Request.Payload = DispatchEntry.SerializedPayload;
		Port->Submit(MoveTemp(Request), [Complete = MoveTemp(Complete)](FServiceResponse Response)
		{
			Complete(DecodeServiceReply(Response));
		});
	}
	for (auto& Entry : Deferred)
	{
		if (bOutboxClosed) break;
		PendingOutbox.HeapPush(MoveTemp(Entry), IsOutboxEntryEarlier);
	}
}

void UProject_JOnlineWorldSubsystem::HandleOutboxReply(uint64 ExpectedGeneration, FOutboxEntry Entry, TSharedPtr<FJsonObject> Reply)
{
	check(IsInGameThread());
	if (ExpectedGeneration != OutboxGeneration)
	{
		return;
	}
	InFlightOutbox = FMath::Max(0, InFlightOutbox - 1);

	FString Status;
	const bool bHasStatus = Reply && Reply->TryGetStringField(TEXT("status"), Status);
	const bool bConfirmed = bHasStatus && (Status == TEXT("COMMITTED") || Status == TEXT("REPLAYED"));
	const bool bNonRetryable = bHasStatus && (Status == TEXT("INVALID") || Status == TEXT("KEY_REUSED")
		|| Status == TEXT("STALE") || Status == TEXT("CONFLICT"));

	if (bConfirmed)
	{
		++OutboxStats.ConfirmedTotal;
		if (OutboxStore)
		{
			++FinalizingCount;
			FProject_JOutboxStoreOp Op;
			Op.Kind = EProject_JOutboxStoreOpKind::DeleteConfirmed;
			Op.Record.IdempotencyKey = Entry.IdempotencyKey;
			QueuedStoreOps.Add(MoveTemp(Op));
			KickStoreWorker();
		}
		else ReleaseOutboxKey(Entry.IdempotencyKey);
		if (Entry.OnFinished)
		{
			Entry.OnFinished(true, Reply);
		}
		else if (Entry.QuestCharacter.IsValid())
		{
			const auto* World = GetGameInstance()->GetWorld();
			const auto* Mode = World ? World->GetAuthGameMode<AProject_JGameMode>() : nullptr;
			if (Mode) Mode->PublishRecoveredQuestProgress(Entry.QuestCharacter, *Reply);
		}
	}
	else if (!bNonRetryable && Entry.Attempts <= MaxOutboxRetries)
	{
		++OutboxStats.RetriedTotal;
		const double Backoff = OutboxInitialBackoffSeconds * static_cast<double>(1 << FMath::Clamp(Entry.Attempts - 1, 0, 8));
		Entry.NextAttemptAt = FPlatformTime::Seconds() + Backoff;
		QueueOutboxEntry(MoveTemp(Entry));
		EnsureOutboxTicker();
	}
	else
	{
		++OutboxStats.FailedTotal;
		const FString FinalStatus = bHasStatus ? Status : TEXT("EXHAUSTED");
		if (OutboxStore)
		{
			++FinalizingCount;
			FProject_JOutboxStoreOp Op;
			Op.Kind = EProject_JOutboxStoreOpKind::MarkDeadLetter;
			Op.Record = MakeStoreRecord(Entry, FinalStatus);
			QueuedStoreOps.Add(MoveTemp(Op));
			KickStoreWorker();
		}
		else ReleaseOutboxKey(Entry.IdempotencyKey);
		UE_LOG(LogProjectJOnlineWorld, Warning,
			TEXT("Durable outbox failed permanently for %s.%s key=%s attempts=%d status=%s context=%s"),
			*Entry.Service.ToString(), *Entry.Operation.ToString(), *GuidText(Entry.IdempotencyKey),
			Entry.Attempts, *FinalStatus, *Entry.DebugContext);
		if (Entry.OnFinished)
		{
			Entry.OnFinished(false, Reply);
		}
	}

	PumpOutbox(FPlatformTime::Seconds());
	if (PendingOutbox.IsEmpty() && QueuedStoreOps.IsEmpty())
	{
		StopOutboxTicker();
	}
}

FProject_JOnlineOutboxStats UProject_JOnlineWorldSubsystem::GetOutboxStats() const
{
	FProject_JOnlineOutboxStats Stats = OutboxStats;
	Stats.PendingCount = PendingOutbox.Num();
	Stats.InFlightCount = InFlightOutbox;
	Stats.FinalizingCount = FinalizingCount;
	Stats.PersistingCount = QueuedPersistEntries.Num() + InFlightPersistCount;
	for (const FPendingBossSettlement& Boss : QueuedBossSettlements) Stats.PersistingCount += Boss.Entries.Num();
	if (!ActiveOutboxKeys.IsEmpty())
	{
		int64 Oldest = MAX_int64;
		for (const auto& Pair : ActiveOutboxKeys) Oldest = FMath::Min(Oldest, Pair.Value);
		Stats.OldestActiveAgeSeconds = FMath::Max(0.0, (OutboxUtcMilliseconds() - Oldest) / 1000.0);
	}
	return Stats;
}

void UProject_JOnlineWorldSubsystem::SetOutboxDispatchHookForTest(FOutboxDispatchHook Hook)
{
	check(IsInGameThread());
	OutboxTestDispatchHook = MoveTemp(Hook);
}

void UProject_JOnlineWorldSubsystem::PumpOutboxForTest(double NowSeconds)
{
	PumpOutbox(NowSeconds);
}

void UProject_JOnlineWorldSubsystem::FlushPersistentOutboxForTest()
{
	check(IsInGameThread());
	for (int32 Guard = 0; Guard < 256; ++Guard)
	{
		if (ActiveStoreTask.IsValid())
		{
			ActiveStoreTask.Wait();
		}
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		StoreRetryAt = 0.;
		KickStoreWorker();
		if (!bStoreWorkerActive && QueuedStoreOps.IsEmpty() && QueuedBossSettlements.IsEmpty())
		{
			break;
		}
	}
}

#if WITH_DEV_AUTOMATION_TESTS
void UProject_JOnlineWorldSubsystem::FailNextOutboxStoreWriteForTest()
{
	check(IsInGameThread());
	if (OutboxStore.IsValid())
	{
		OutboxStore->FailNextWriteForTest();
	}
}
#endif

bool UProject_JOnlineWorldSubsystem::ApplyInventory(AProject_JPlayerState* Player, const FJsonObject& Response, TFunctionRef<bool()> Current)
{
	if (!IsValid(Player) || !Current()) return false;
	FString RevisionText;
	int64 Revision = 0;
	if (!Response.TryGetStringField(TEXT("inventory_revision"), RevisionText) || !LexTryParseString(Revision, *RevisionText) || Revision <= 0) return false;
	FString QuestRevisionText;
	int64 QuestRevision = 0;
	int32 CompletionCount = 0;
	if (!Response.TryGetStringField(TEXT("quest_revision"), QuestRevisionText)
		|| !LexTryParseString(QuestRevision, *QuestRevisionText) || QuestRevision < 0
		|| !Response.TryGetNumberField(TEXT("completed_quest_count"), CompletionCount)
		|| CompletionCount < 0 || CompletionCount > ProjectJ::QuestHistory::MaxIds) return false;
	FString ProfileRevisionText;
	int64 ProfileRevision = 0;
	if (!Response.TryGetStringField(TEXT("profile_revision"), ProfileRevisionText)
		|| !LexTryParseString(ProfileRevision, *ProfileRevisionText) || ProfileRevision <= 0) return false;
	auto* Quests = Player->FindComponentByClass<UProject_JQuestComponent>();
	FProject_JQuestSnapshot QuestSnapshot;
	const bool bInstallQuests = QuestRevision > Player->OnlineQuestRevision
		|| (QuestRevision >= Player->OnlineQuestRevision && Response.HasField(TEXT("completed_quest")));
	if (!Quests || (bInstallQuests && !ProjectJ::OnlineQuests::Read(*Quests, Response, QuestSnapshot))) return false;
	auto* Progression = Player->FindComponentByClass<UProject_JProgressionComponent>();
	FProject_JProgressionSnapshot ProgressionSnapshot;
	bool bExperienceChanged = false;
	const bool bInstallProgression = ProfileRevision > Player->OnlineProfileRevision;
	if (bInstallProgression)
	{
		if (!Progression) return false;
		ProgressionSnapshot = Progression->CaptureSnapshot();
		FString Experience;
		int32 Level = 0;
		int64 XP = 0;
		if (!Response.TryGetNumberField(TEXT("level"), Level) || !Response.TryGetStringField(TEXT("xp"), Experience)
			|| !LexTryParseString(XP, *Experience)) return false;
		bExperienceChanged = Level != ProgressionSnapshot.Level || XP != ProgressionSnapshot.Experience;
		ProgressionSnapshot.Level = Level;
		ProgressionSnapshot.Experience = XP;
		// Login already validated and installed identical values before its first publication.
		if (bExperienceChanged && !Progression->ValidateCommittedExperience(ProgressionSnapshot, true)) return false;
	}
	bool bQuestsChanged = false;
	bool bCompletionCountChanged = false;
	const auto InstallProfile = [&]
	{
		if (bInstallProgression)
		{
			if (bExperienceChanged) Progression->InstallPreparedExperience(ProgressionSnapshot);
			Player->OnlineProfileRevision = ProfileRevision;
		}
		if (bInstallQuests)
		{
			Player->OnlineQuestRevision = QuestRevision;
			bQuestsChanged = Quests->InstallCommittedQuests(QuestSnapshot);
		}
		bCompletionCountChanged = Quests->InstallOnlineCompletionCount(CompletionCount);
	};
	const auto PublishProfile = [&]
	{
		if (bExperienceChanged && Current() && IsValid(Progression) && Player->OnlineProfileRevision == ProfileRevision)
			Progression->PublishCommittedExperience(Current);
		if (bQuestsChanged && Current() && IsValid(Quests) && Player->OnlineQuestRevision == QuestRevision)
			Quests->PublishCommittedQuests();
		if (bCompletionCountChanged && Current() && IsValid(Quests) && Quests->GetCompletedCount() == CompletionCount)
			Quests->PublishOnlineCompletionCount();
	};
	if (Revision <= Player->OnlineInventoryRevision)
	{
		if (!Current()) return false;
		InstallProfile();
		PublishProfile();
		return Current();
	}
	auto* Inventory = Player->GetInventoryComponent();
	auto* Equipment = Player->GetEquipmentManagerComponent();
	auto* CraftingJobs = Player->GetCraftingJobsComponent();
	auto* Data = Player->GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>();
	const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
	if (!IsValid(Inventory) || !IsValid(Equipment) || !IsValid(CraftingJobs) || !IsValid(Data)
		|| !Response.TryGetArrayField(TEXT("items"), Rows) || Rows->Num() > 1024) return false;
	TArray<FProject_JItemInstanceData> Items;
	TArray<FProject_JPersistentItem> OwnedItems;
	TArray<FProject_JEquipmentArrayItem> Slots;
	TArray<ProjectJ::Crafting::FJobValues> Jobs;
	Items.Reserve(Rows->Num());
	OwnedItems.Reserve(Rows->Num());
	for (const auto& Value : *Rows)
	{
		const TSharedPtr<FJsonObject>* Row = nullptr;
		auto& Saved = OwnedItems.AddDefaulted_GetRef();
		if (!Value.IsValid() || !Value->TryGetObject(Row) || !Row
			|| !ProjectJ::OnlineCrafting::ReadItem(**Row, TEXT("CHAR_INV"), Saved)) return false;
		FProject_JItemInstanceData Item;
		Item.InstanceId = Saved.Instance;
		Item.ItemDef = Data->GetItemDefinition(Saved.Definition, Saved.DefinitionVersion);
		Item.StackCount = Saved.Count;
		Item.ItemLevel = Saved.Level;
		Item.BagOrder = Saved.Order;
		if (Saved.Slot != EProject_JEquipmentSlot::None)
		{
			auto* Gear = Cast<UProject_JEquipmentItemDefinition>(Item.ItemDef);
			if (!Gear) return false;
			Item.bIsEquipped = Item.bIsLocked = true;
			auto& Slot = Slots.AddDefaulted_GetRef();
			Slot.Slot = Saved.Slot;
			Slot.ItemDef = Gear;
			Slot.ItemInstance = Item;
		}
		Items.Add(MoveTemp(Item));
	}
	if (!ProjectJ::OnlineCrafting::ReadJobs(Response, Jobs) || !ProjectJ::Crafting::ValidateJobState(OwnedItems, Jobs)
		|| !Inventory->ValidatePersistentItems(Items, true) || !Equipment->ValidatePersistentEquipment(Slots, Items, true)
		|| !CraftingJobs->ValidateCommittedJobs(Jobs) || !Current()) return false;
	const auto Before = Inventory->GetItemInstances();
	const auto BeforeEquipment = Equipment->CapturePersistentEquipment();
	Inventory->InstallPersistentItems(Items);
	Equipment->InstallPersistentEquipment(Slots);
	CraftingJobs->InstallCommittedJobs(Jobs, false);
	Player->OnlineInventoryRevision = Revision;
	InstallProfile();
	Inventory->PublishPersistentItems(Before, Current);
	if (Current() && Player->OnlineInventoryRevision == Revision) Equipment->PublishPersistentEquipment(BeforeEquipment);
	if (Current() && Player->OnlineInventoryRevision == Revision) CraftingJobs->BroadcastCommittedJobs();
	PublishProfile();
	if (Current()) Player->ForceNetUpdate();
	return Current();
}

bool UProject_JOnlineWorldSubsystem::ConnectCharacter(APlayerController* Controller, const FString& AccessToken,
	FGuid Character, const FString& Zone, int32 Channel)
{
	if (!RequestLogin(Controller, AccessToken, Character, Zone, Channel, {})) return false;
	OnlineCharacter = Character;
	OnlineAccessToken = AccessToken;
	return true;
}

bool UProject_JOnlineWorldSubsystem::ConnectTravel(APlayerController* Controller, FName Zone, int32 Channel, const FString& TravelUrl)
{
	return HasOnlineClientSession() && RequestLogin(Controller, OnlineAccessToken, OnlineCharacter, Zone.ToString(), Channel, TravelUrl);
}

namespace
{
FHttpRequestRef MakeLoginRequest(const FString& BaseUrl, const FString& Token, FGuid Character,
	const FString& Zone, int32 Channel, bool bRouted, const FString& Slot, bool bCancel = false)
{
	auto Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("character_id"), UProject_JOnlineWorldSubsystem::GuidText(Character));
	Body->SetStringField(TEXT("zone"), Zone);
	Body->SetNumberField(TEXT("channel"), Channel);
	if (!Slot.IsEmpty()) Body->SetStringField(TEXT("slot"), Slot);
	if (bCancel) Body->SetBoolField(TEXT("cancel"), true);
	const auto Request = FHttpModule::Get().CreateRequest();
	Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
	Request->SetURL(BaseUrl / (bRouted ? TEXT("v2/rpc/j_world_player_route?unwrap") : TEXT("v2/rpc/j_world_login_ticket?unwrap")));
	Request->SetVerb(TEXT("POST"));
	Request->SetTimeout(8.f);
	Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Token);
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetContentAsString(EncodeJson(Body));
	return Request;
}
}

void UProject_JOnlineWorldSubsystem::CancelPendingLogin()
{
	++ConnectGeneration;
	FTSTicker::GetCoreTicker().RemoveTicker(LoginPoll);
	LoginPoll.Reset();
	const bool bQueued = !QueueSlot.IsEmpty();
	if (bQueued)
		MakeLoginRequest(NakamaClientUrl, OnlineAccessToken, OnlineCharacter, QueueZone, QueueChannel, true, QueueSlot, true)->ProcessRequest();
	ClearLoginQueue();
	PendingLogin.Invalidate();
	PendingEndpoint.Reset();
	if (bQueued) OnLoginQueueClosed.Broadcast();
}

void UProject_JOnlineWorldSubsystem::ClearLoginQueue()
{
	QueuedLoginController.Reset();
	QueueSlot.Reset();
	QueueZone.Reset();
	QueueChannel = 0;
	QueuePosition = 0;
}

bool UProject_JOnlineWorldSubsystem::RequestLogin(APlayerController* Controller, const FString& AccessToken,
	FGuid Character, const FString& Zone, int32 Channel, const FString& TravelUrl)
{
	if (!IsValid(Controller) || !Controller->IsLocalController() || !Character.IsValid() || Channel < 1 || Zone.IsEmpty()
		|| AccessToken.Len() < 16 || AccessToken.Len() > 4096 || AccessToken.Contains(TEXT("\n")) || AccessToken.Contains(TEXT("\r"))
		|| NakamaClientUrl.Contains(TEXT("?")) || NakamaClientUrl.Contains(TEXT("#"))
		|| (!IsLoopbackHttp(NakamaClientUrl) && !NakamaClientUrl.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase))) return false;
	if (TravelUrl.IsEmpty() && !QueueSlot.IsEmpty() && QueuedLoginController.Get() == Controller
		&& Character == OnlineCharacter && Zone == QueueZone)
	{
		// A repeated connect from the same login screen keeps its position.
		++ConnectGeneration;
		FTSTicker::GetCoreTicker().RemoveTicker(LoginPoll);
		LoginPoll.Reset();
	}
	else CancelPendingLogin();
	const uint64 Generation = ConnectGeneration;
	if (DispatchLogin(Controller, AccessToken, Character, Zone, Channel, TravelUrl, Generation)) return true;
	if (ConnectGeneration == Generation) CancelPendingLogin();
	return false;
}

bool UProject_JOnlineWorldSubsystem::DispatchLogin(APlayerController* Controller, const FString& AccessToken,
	FGuid Character, const FString& Zone, int32 Channel, const FString& TravelUrl, uint64 Generation)
{
	// Dungeon entry and return tickets are signed for one specific destination.
	const bool bRouted = TravelUrl.IsEmpty();
	const auto Request = MakeLoginRequest(NakamaClientUrl, AccessToken, Character, Zone, Channel, bRouted, QueueSlot);
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[this, Generation, TravelUrl, Zone, Channel, bRouted, WeakPlayer = TWeakObjectPtr<APlayerController>(Controller)](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			auto* Player = WeakPlayer.Get();
			if (Generation != ConnectGeneration) return;
			if (!IsValid(Player) || Player->IsActorBeingDestroyed() || !Player->IsLocalController())
			{
				CancelPendingLogin();
				return;
			}
			const auto Data = bConnected && Response.IsValid() && Response->GetResponseCode() == 200 && Response->GetContent().Num() <= 4096
				? ReadJson(Response->GetContentAsString()) : nullptr;
			FString Status, Ticket, Endpoint;
			FGuid Parsed;
			const bool bHasStatus = Data && Data->TryGetStringField(TEXT("status"), Status);
			int32 Position = 0, Delay = 0;
			FString Slot;
			if (bRouted && bHasStatus && Status == TEXT("QUEUED") && Data->TryGetStringField(TEXT("slot"), Slot)
				&& Slot.IsNumeric() && Slot.Len() <= 19 && Data->TryGetNumberField(TEXT("position"), Position) && Position > 0
				&& Data->TryGetNumberField(TEXT("retry_after_sec"), Delay) && Delay >= 1 && Delay <= 10)
			{
				QueueSlot = MoveTemp(Slot);
				QueueZone = Zone;
				QueueChannel = Channel;
				QueuePosition = Position;
				QueuedLoginController = Player;
				LoginPoll = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,
					[this, Generation, WeakPlayer](float)
					{
						LoginPoll.Reset();
						if (ConnectGeneration == Generation && (!WeakPlayer.IsValid() || !DispatchLogin(WeakPlayer.Get(),
							OnlineAccessToken, OnlineCharacter, QueueZone, QueueChannel, FString(), Generation)))
						{
							CancelPendingLogin();
							OnConnectDispatched.Broadcast(false);
						}
						return false;
					}), static_cast<float>(Delay));
				OnLoginQueued.Broadcast(Position);
				return;
			}
			const bool bReady = bHasStatus && Status == (bRouted ? TEXT("ROUTED") : TEXT("OK"))
				&& Data->TryGetStringField(TEXT("ticket"), Ticket) && FGuid::Parse(Ticket, Parsed)
				&& Data->TryGetStringField(TEXT("endpoint"), Endpoint) && !Endpoint.IsEmpty() && !Endpoint.Contains(TEXT("?"))
				&& (TravelUrl.IsEmpty() || TravelUrl.StartsWith(Endpoint + TEXT("?"), ESearchCase::CaseSensitive));
			if (bReady)
			{
				ClearLoginQueue();
				PendingLogin = Parsed;
				PendingEndpoint = Endpoint;
				Player->ClientTravel(TravelUrl.IsEmpty() ? Endpoint : TravelUrl, TRAVEL_Absolute);
			}
			else
			{
				CancelPendingLogin();
				UE_LOG(LogProjectJOnlineWorld, Warning, TEXT("The configured service did not issue a usable world login ticket (HTTP %d)."),
					Response.IsValid() ? Response->GetResponseCode() : 0);
			}
			OnConnectDispatched.Broadcast(bReady);
		});
	return Request->ProcessRequest();
}

void UProject_JOnlineWorldSubsystem::SendPendingLogin(APlayerController* Player)
{
	if (!IsValid(Player) || !Player->IsLocalController() || Player->GetNetMode() != NM_Client || !PendingLogin.IsValid()) return;
	const auto* Driver = Player->GetWorld()->GetNetDriver();
	const FURL Endpoint(nullptr, *PendingEndpoint, TRAVEL_Absolute);
	if (!Endpoint.Valid || !Driver || !Driver->ServerConnection || Driver->ServerConnection->URL.Host != Endpoint.Host
		|| Driver->ServerConnection->URL.Port != Endpoint.Port) return;
	if (auto* ProjectPlayer = Cast<AProject_JPlayerController>(Player))
	{
		const FGuid Ticket = PendingLogin;
		PendingLogin.Invalidate();
		PendingEndpoint.Reset();
		ProjectPlayer->ServerSubmitOnlineLogin(Ticket);
	}
}

void UProject_JOnlineWorldSubsystem::Deinitialize()
{
	CancelPendingLogin();
	++OutboxGeneration;
	bOutboxClosed = true;
	StopOutboxTicker();
	const int32 Unresolved = ActiveOutboxKeys.Num();
	const bool bHadPersistentStore = OutboxStore.IsValid();
	const FString StoreFilename = OutboxStore ? OutboxStore->GetFilename() : FString();
	const bool bStoreFlushed = ClosePersistentStoreSync();

	if (Unresolved > 0)
	{
		if (bHadPersistentStore && bStoreFlushed)
		{
			UE_LOG(LogProjectJOnlineWorld, Display,
				TEXT("Persistent online outbox shutdown with %d unconfirmed operations durably persisted to %s for restart recovery"),
				Unresolved, *StoreFilename);
		}
		else if (!bHadPersistentStore)
		{
			OutboxStats.FailedTotal += Unresolved;
			UE_LOG(LogProjectJOnlineWorld, Error, TEXT("Online mutation queue shutdown with %d unconfirmed operations"), Unresolved);
		}
	}
	PendingOutbox.Reset();
	ActiveOutboxKeys.Reset();
	SettlingBossEncounters.Reset();
	BossRewardCooldowns.Reset();
	QuestProgressLanes.Reset();
	QuestCharactersByKey.Reset();
	FinalizingCount = 0;
	InFlightOutbox = 0;
	OutboxTestDispatchHook = nullptr;
	PendingLogin.Invalidate();
	PendingEndpoint.Reset();
	OnlineAccessToken.Reset();
	OnlineCharacter.Invalidate();
	Super::Deinitialize();
}
