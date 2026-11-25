#include "Backend/Project_JOnlineWorldSubsystem.h"

#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "Backend/Project_JNakamaServicePort.h"
#include "Backend/Project_JOnlineCrafting.h"
#include "Backend/Project_JOnlineQuests.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
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
	if (OutboxStore) return Store->GetFilename().Equals(OutboxStore->GetFilename(), ESearchCase::IgnoreCase) && !bStoreLoadFailed;
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

int32 UProject_JOnlineWorldSubsystem::GetTotalActiveOutboxCount() const
{
	return ActiveOutboxKeys.Num() + ReservedRedrive;
}

FProject_JPersistedOutboxRecord UProject_JOnlineWorldSubsystem::MakeStoreRecord(const FOutboxEntry& Entry, const FString& LastStatus)
{
	FProject_JPersistedOutboxRecord Record;
	Record.IdempotencyKey = Entry.IdempotencyKey;
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
		bStoreWorkerActive = true;
		ActiveStoreTask = Async(EAsyncExecution::ThreadPool, [WeakSelf, Generation, Store, Limit, AfterSeq]
		{
			TArray<FProject_JPersistedOutboxRecord> Rows;
			int32 DeadLetters = 0;
			const bool bOk = Store->OpenAndLoadPending(Limit, Rows, DeadLetters, AfterSeq);
			FString Error = bOk ? FString() : Store->GetLastFailure();
			AsyncTask(ENamedThreads::GameThread, [WeakSelf, Generation, Limit, bOk, Error = MoveTemp(Error), Rows = MoveTemp(Rows), DeadLetters]() mutable
			{
				if (auto* Self = WeakSelf.Get()) Self->HandleStoreOpenLoaded(Generation, Limit, bOk, MoveTemp(Error), MoveTemp(Rows), DeadLetters);
			});
		});
		return;
	}
	if (!QueuedStoreOps.IsEmpty())
	{
		bStoreWorkerActive = true;
		auto Batch = MakeShared<FStoreBatch, ESPMode::ThreadSafe>();
		Batch->Ops = MoveTemp(QueuedStoreOps);
		ActiveStoreBatch = Batch;
		InFlightPersistCount = QueuedPersistEntries.Num();
		ActiveStoreTask = Async(EAsyncExecution::ThreadPool, [WeakSelf, Generation, Store, Batch, Entries = MoveTemp(QueuedPersistEntries)]() mutable
		{
			Batch->bCommitted = Store->ApplyBatch(Batch->Ops, Batch->DeadLetters, &Batch->Existing);
			if (!Batch->bCommitted) Batch->Failure = Store->GetLastFailure();
			AsyncTask(ENamedThreads::GameThread, [WeakSelf, Generation, Batch, Entries = MoveTemp(Entries)]() mutable
			{
				if (auto* Self = WeakSelf.Get()) Self->HandleStoreBatchApplied(Generation, *Batch, MoveTemp(Entries));
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
	TArray<FProject_JPersistedOutboxRecord> Recovered, int32 DeadLetters)
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
		KickStoreWorker();
		return;
	}
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

void UProject_JOnlineWorldSubsystem::HandleStoreBatchApplied(uint64 Generation, const FStoreBatch& Batch, TArray<FOutboxEntry> Entries)
{
	check(IsInGameThread());
	if (Generation != OutboxGeneration || bOutboxClosed) return;
	bStoreWorkerActive = false;
	ActiveStoreBatch.Reset();
	InFlightPersistCount = 0;
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
	if (ActiveStoreBatch && !ActiveStoreBatch->bCommitted) FinalOps = MoveTemp(ActiveStoreBatch->Ops);
	ActiveStoreBatch.Reset();
	FinalOps.Append(MoveTemp(QueuedStoreOps));
	QueuedPersistEntries.Reset();
	InFlightPersistCount = 0;
	PendingRedriveLimit = 0;
	ReservedRedrive = 0;
	bStoreWorkerActive = false;
	if (auto Store = MoveTemp(OutboxStore))
	{
		return Async(EAsyncExecution::ThreadPool, [Store, FinalOps = MoveTemp(FinalOps)]
		{
			int32 DeadLetters = 0;
			const bool bOk = FinalOps.IsEmpty() || Store->ApplyBatch(FinalOps, DeadLetters);
			if (!bOk) UE_LOG(LogProjectJOnlineWorld, Error, TEXT("Outbox shutdown could not commit %d operations: %s"),
				FinalOps.Num(), *Store->GetLastFailure());
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
	if (ActiveOutboxKeys.Contains(Entry.IdempotencyKey)) return;
	ActiveOutboxKeys.Add(Entry.IdempotencyKey);
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
			if (Self->PendingOutbox.IsEmpty() && Self->QueuedStoreOps.IsEmpty())
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
		if (!bStoreWorkerActive && QueuedStoreOps.IsEmpty())
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
	if (!Response.TryGetStringField(TEXT("quest_revision"), QuestRevisionText)
		|| !LexTryParseString(QuestRevision, *QuestRevisionText) || QuestRevision < 0) return false;
	auto* Quests = Player->FindComponentByClass<UProject_JQuestComponent>();
	FProject_JQuestSnapshot QuestSnapshot;
	const bool bInstallQuests = QuestRevision > Player->OnlineQuestRevision;
	if (bInstallQuests && (!Quests || !ProjectJ::OnlineQuests::Read(*Quests, Response, QuestSnapshot))) return false;
	if (Revision <= Player->OnlineInventoryRevision)
	{
		if (bInstallQuests && Current())
		{
			Player->OnlineQuestRevision = QuestRevision;
			if (Quests->InstallCommittedQuests(QuestSnapshot)) Quests->PublishCommittedQuests();
		}
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
	bool bQuestsChanged = false;
	if (bInstallQuests)
	{
		Player->OnlineQuestRevision = QuestRevision;
		bQuestsChanged = Quests->InstallCommittedQuests(QuestSnapshot);
	}
	Inventory->PublishPersistentItems(Before, Current);
	if (Current() && Player->OnlineInventoryRevision == Revision) Equipment->PublishPersistentEquipment(BeforeEquipment);
	if (Current() && Player->OnlineInventoryRevision == Revision) CraftingJobs->BroadcastCommittedJobs();
	if (bQuestsChanged && Current() && Player->OnlineQuestRevision == QuestRevision) Quests->PublishCommittedQuests();
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

bool UProject_JOnlineWorldSubsystem::RequestLogin(APlayerController* Controller, const FString& AccessToken,
	FGuid Character, const FString& Zone, int32 Channel, const FString& TravelUrl)
{
	if (!IsValid(Controller) || !Controller->IsLocalController() || !Character.IsValid() || Channel < 1 || Zone.IsEmpty()
		|| AccessToken.Len() < 16 || AccessToken.Len() > 4096 || AccessToken.Contains(TEXT("\n")) || AccessToken.Contains(TEXT("\r"))
		|| NakamaClientUrl.Contains(TEXT("?")) || NakamaClientUrl.Contains(TEXT("#"))
		|| (!IsLoopbackHttp(NakamaClientUrl) && !NakamaClientUrl.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase))) return false;
	auto Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("character_id"), GuidText(Character));
	Body->SetStringField(TEXT("zone"), Zone);
	Body->SetNumberField(TEXT("channel"), Channel);
	const uint64 Generation = ++ConnectGeneration;
	PendingLogin.Invalidate();
	PendingEndpoint.Reset();
	const auto Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(NakamaClientUrl / TEXT("v2/rpc/j_world_login_ticket?unwrap"));
	Request->SetVerb(TEXT("POST"));
	Request->SetTimeout(8.f);
	Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + AccessToken);
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetContentAsString(EncodeJson(Body));
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[this, Generation, TravelUrl, WeakPlayer = TWeakObjectPtr<APlayerController>(Controller)](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			auto* Player = WeakPlayer.Get();
			if (!IsValid(Player) || Generation != ConnectGeneration) return;
			const auto Data = bConnected && Response.IsValid() && Response->GetResponseCode() == 200 && Response->GetContent().Num() <= 4096
				? ReadJson(Response->GetContentAsString()) : nullptr;
			FString Status, Ticket, Endpoint;
			FGuid Parsed;
			const bool bReady = Data.IsValid() && Data->TryGetStringField(TEXT("status"), Status) && Status == TEXT("OK")
				&& Data->TryGetStringField(TEXT("ticket"), Ticket) && FGuid::Parse(Ticket, Parsed)
				&& Data->TryGetStringField(TEXT("endpoint"), Endpoint) && !Endpoint.IsEmpty() && !Endpoint.Contains(TEXT("?"))
				&& (TravelUrl.IsEmpty() || TravelUrl.StartsWith(Endpoint + TEXT("?"), ESearchCase::CaseSensitive));
			if (bReady)
			{
				PendingLogin = Parsed;
				PendingEndpoint = Endpoint;
				Player->ClientTravel(TravelUrl.IsEmpty() ? Endpoint : TravelUrl, TRAVEL_Absolute);
			}
			else UE_LOG(LogProjectJOnlineWorld, Warning, TEXT("The configured service did not issue a usable world login ticket (HTTP %d)."),
				Response.IsValid() ? Response->GetResponseCode() : 0);
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
	++ConnectGeneration;
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
