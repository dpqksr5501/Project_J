#include "Backend/Project_JLocalProfileSubsystem.h"
#include "Async/Async.h"
#include "Backend/Project_JLocalProfileSnapshot.h"
#include "Backend/Project_JSQLiteRepository.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JInventoryComponent.h"
#include "Engine/World.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Game/Project_JPlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Inventory/Project_JConsumableDefinition.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/ScopeLock.h"
#include "Misc/SecureHash.h"
#include "Project_JAbilitySystemComponent.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/StrongObjectPtr.h"

using namespace ProjectJ::MMO;
DEFINE_LOG_CATEGORY_STATIC(LogProjectJLocalProfile, Log, All);

static FAutoConsoleCommandWithWorld GLocalProfileBackup(
	TEXT("ProjectJ.LocalProfile.Backup"), TEXT("Create a new verified local checkpoint database backup."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld *World) {
		if (auto *Profiles = World && World->GetGameInstance()
								 ? World->GetGameInstance()->GetSubsystem<UProject_JLocalProfileSubsystem>()
								 : nullptr)
			Profiles->RequestBackup([](bool Success, FString Path) {
				UE_LOG(LogProjectJLocalProfile, Display, TEXT("Local backup %s: %s"),
					   Success ? TEXT("verified") : TEXT("failed"), *Path);
			});
	}));

namespace
{
FAggregateKey ProfileKey(FGuid Id)
{
	return {TEXT("LocalProfile"), Id};
}
FAggregateKey LeaseKey(FGuid Id)
{
	return {TEXT("LocalProfileLease"), Id};
}
bool Accepted(ECommitStatus Status)
{
	return Status == ECommitStatus::Committed || Status == ECommitStatus::Replayed;
}
TArray<uint8> SessionBytes(FGuid Session)
{
	TArray<uint8> Out;
	FMemoryWriter W(Out);
	W << Session;
	return Out;
}
bool ResolveDefinitions(UGameInstance *GI, const FProject_JProgressionSnapshot &Snapshot,
						UProject_JCharacterClassDefinition *&Class,
						TArray<UProject_JCharacterAdvancementDefinition *> &History)
{
	auto *Data = GI ? GI->GetSubsystem<UProject_JCharacterDataSubsystem>() : nullptr;
	if (!Data)
		return false;
	Class = Snapshot.ClassId.IsNone() ? nullptr : Data->GetClassDefinition(Snapshot.ClassId);
	if (!Snapshot.ClassId.IsNone() && !Class)
		return false;
	for (FName Id : Snapshot.AdvancementHistory)
	{
		auto *Definition = Data->GetAdvancementDefinition(Id);
		if (!Definition)
			return false;
		History.Add(Definition);
	}
	return true;
}
bool ResolveItems(AProject_JPlayerState *Player, const FProject_JLocalProfileSnapshot &Snapshot,
				  TArray<FProject_JItemInstanceData> &Bag, TArray<FProject_JEquipmentArrayItem> &Slots,
				  bool bTrustedApply = false, FString *Failure = nullptr)
{
	if (Snapshot.Format != 2)
		return false;
	auto *Data = Player->GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>();
	auto *Inventory = Player->GetInventoryComponent();
	auto *Equipment = Player->GetEquipmentManagerComponent();
	if (!Data || !Inventory || !Equipment)
		return false;
	for (const auto &Value : Snapshot.Items)
	{
		auto *Def = Data->GetItemDefinition(Value.Definition, Value.DefinitionVersion);
		if (!Def)
		{
			if (Failure)
				*Failure = FString::Printf(TEXT("아이템 정의 ID/version 불일치: %s v%d. 저장 원형을 보존하세요"),
										   *Value.Definition.ToString(), Value.DefinitionVersion);
			return false;
		}
		FProject_JItemInstanceData Item;
		Item.InstanceId = Value.Instance;
		Item.ItemDef = Def;
		Item.ItemLevel = Value.Level;
		Item.StackCount = Value.Count;
		Item.BagOrder = Value.Order;
		Item.bIsLocked = Item.bIsEquipped = Value.bInBag && Value.Slot != EProject_JEquipmentSlot::None;
		if (Value.bInBag)
			Bag.Add(Item);
		if (Value.Slot != EProject_JEquipmentSlot::None)
		{
			auto &Entry = Slots.AddDefaulted_GetRef();
			Entry.ItemDef = Cast<UProject_JEquipmentItemDefinition>(Def);
			Entry.ItemInstance = Item;
			Entry.Slot = Value.Slot;
		}
	}
	return Inventory->ValidatePersistentItems(Bag, bTrustedApply) &&
		   Equipment->ValidatePersistentEquipment(Slots, Bag, bTrustedApply);
}
bool CaptureItems(AProject_JPlayerState *Player, FProject_JLocalProfileSnapshot &Snapshot)
{
	auto *Data = Player->GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>();
	auto *Inventory = Player->GetInventoryComponent();
	auto *Equipment = Player->GetEquipmentManagerComponent();
	if (!Data || !Inventory || !Equipment)
		return false;
	for (const auto &Item : Inventory->GetItemInstances())
	{
		if (!Item.ItemDef || Data->GetItemDefinition(Item.ItemDef->ItemId, Item.ItemDef->SchemaVersion) != Item.ItemDef)
			return false;
		FProject_JPersistentItem Value;
		Value.Instance = Item.InstanceId;
		Value.Definition = Item.ItemDef->ItemId;
		Value.DefinitionVersion = Item.ItemDef->SchemaVersion;
		Value.Level = Item.ItemLevel;
		Value.Count = Item.StackCount;
		Value.Order = Item.BagOrder;
		Snapshot.Items.Add(Value);
	}
	for (const auto &Entry : Equipment->CapturePersistentEquipment())
	{
		const auto &Item = Entry.ItemInstance;
		if (!Item.ItemDef || Data->GetItemDefinition(Item.ItemDef->ItemId, Item.ItemDef->SchemaVersion) != Item.ItemDef)
			return false;
		if (auto *Found = Snapshot.Items.FindByPredicate([&](const auto &V) { return V.Instance == Item.InstanceId; }))
		{
			if (Found->Definition != Item.ItemDef->ItemId || Found->Level != Item.ItemLevel || Found->Count != 1 ||
				Found->Slot != EProject_JEquipmentSlot::None)
				return false;
			Found->Slot = Entry.Slot;
		}
		else
		{
			FProject_JPersistentItem Value;
			Value.Instance = Item.InstanceId;
			Value.Definition = Item.ItemDef->ItemId;
			Value.DefinitionVersion = Item.ItemDef->SchemaVersion;
			Value.Level = Item.ItemLevel;
			Value.Count = 1;
			Value.bInBag = false;
			Value.Slot = Entry.Slot;
			Snapshot.Items.Add(Value);
		}
	}
	TArray<FProject_JItemInstanceData> Bag;
	TArray<FProject_JEquipmentArrayItem> Slots;
	return ResolveItems(Player, Snapshot, Bag, Slots);
}
FString Fingerprint(const UProject_JProgressionComponent &P, const UProject_JQuestComponent &Q,
					const UProject_JCharacterClassDefinition *Class,
					const TArray<UProject_JCharacterAdvancementDefinition *> &History)
{
	TArray<uint8> Bytes;
	FMemoryWriter W(Bytes);
	int32 Maximum = P.MaximumLevel;
	auto Requirements = P.ExperienceRequirements;
	W << Maximum << Requirements;
	FString ClassId = Class ? Class->ClassId.ToString().ToLower() : TEXT("none");
	int32 ClassVersion = Class ? Class->SchemaVersion : 1, Floor = Class ? Class->StartingLevel : 1;
	if (ClassVersion < 1 || Maximum < 1 || Maximum > 10000)
		return {};
	W << ClassId << ClassVersion << Floor;
	for (const auto *Adv : History)
	{
		if (!Adv || Adv->SchemaVersion < 1)
			return {};
		FString Id = Adv->AdvancementId.ToString().ToLower();
		int32 Version = Adv->SchemaVersion;
		W << Id << Version;
	}
	TArray<UProject_JQuestDefinition *> Definitions;
	for (const auto &Def : Q.Definitions)
		if (Def)
			Definitions.Add(Def.Get());
	Definitions.Sort([](const auto &A, const auto &B) { return A.QuestId.LexicalLess(B.QuestId); });
	for (const auto *Def : Definitions)
	{
		if (Q.FindDefinition(Def->QuestId) != Def)
			return {};
		FString Id = Def->QuestId.ToString().ToLower(), Event = Def->ObjectiveEvent.ToString().ToLower();
		int32 Version = Def->SchemaVersion, Required = Def->RequiredCount;
		int64 Reward = Def->RewardExperience;
		bool Travel = Def->bTravelObjective;
		W << Id << Version << Event << Required << Reward << Travel;
	}
	uint8 Digest[20];
	FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num(), Digest);
	return BytesToHex(Digest, 20);
}
} // namespace

/** Native mailbox: one current commit plus one newest pending checkpoint, not an unbounded queue.
 * Once accepted, owned values drain on logout/shutdown. No worker dereferences a UObject.
 */
struct FProject_JLocalProfileWork
{
	mutable FCriticalSection Mutex;
	TSharedPtr<FProject_JSQLiteRepository, ESPMode::ThreadSafe> Repository;
	TSharedPtr<FAggregateGate, ESPMode::ThreadSafe> Gate;
	FGuid Character, Session;
	int64 LeaseRevision = 0, ProfileRevision = 0;
	FProject_JRepositoryUsage Usage;
	ECommitStatus LastResult = ECommitStatus::Committed;
	bool bUsageKnown = false;
	bool bRunning = false, bAccepting = true, bFaulted = false;
	TOptional<TArray<uint8>> Pending;
	TArray<uint8> LastQueued;
	bool PendingIO() const
	{
		FScopeLock Lock(&Mutex);
		return bRunning || Pending.IsSet();
	}
	bool Healthy() const
	{
		FScopeLock Lock(&Mutex);
		return !bFaulted;
	}
	int64 Revision() const
	{
		FScopeLock Lock(&Mutex);
		return ProfileRevision;
	}
	void Seal()
	{
		FScopeLock Lock(&Mutex);
		bAccepting = false;
	}
	void Drain(FGuid Ticket)
	{
		check(!IsInGameThread());
		for (;;)
		{
			TArray<uint8> Payload;
			{
				FScopeLock Lock(&Mutex);
				if (Pending.IsSet())
				{
					Payload = MoveTemp(Pending.GetValue());
					Pending.Reset();
				}
			}
			if (Payload.IsEmpty())
			{
				Repository->Close(); // Do not hold the mailbox lock during disk I/O.
				FScopeLock Lock(&Mutex);
				if (Pending.IsSet())
					continue; // A newer owned snapshot arrived while closing.
				bRunning = false;
				Gate->Release(Ticket);
				return;
			}
			FCommitRequest Request;
			Request.IdempotencyKey = FGuid::NewGuid();
			Request.Operation = TEXT("LocalProfileCheckpoint");
			Request.Mutations.Add({LeaseKey(Character), LeaseRevision, 1, SessionBytes(Session), false});
			Request.Mutations.Add({ProfileKey(Character), ProfileRevision, 1, MoveTemp(Payload), false});
			auto Result = Repository->CommitProfile(Request, {Character, Session, 0});
			// Retry only the exact original command after an ambiguous native commit.
			if (Result.Status == ECommitStatus::UnknownOutcome)
				Result = Repository->CommitProfile(Request, {Character, Session, 0});
			if (!Accepted(Result.Status) || Result.Revisions.Num() != 2)
			{
				FProject_JRepositoryUsage CurrentUsage;
				const bool Known = Repository->GetUsage(CurrentUsage);
				Repository->Close();
				FScopeLock Lock(&Mutex);
				LastResult = Result.Status;
				Usage = CurrentUsage;
				bUsageKnown = Known;
				bFaulted = true;
				bRunning = false;
				bAccepting = false;
				Pending.Reset();
				Gate->Release(Ticket);
				return;
			}
			FProject_JRepositoryUsage CurrentUsage;
			const bool Known = Repository->GetUsage(CurrentUsage);
			FScopeLock Lock(&Mutex);
			LastResult = Result.Status;
			Usage = CurrentUsage;
			bUsageKnown = Known;
			LeaseRevision = Result.Revisions[0];
			ProfileRevision = Result.Revisions[1];
		}
	}
};

struct FProject_JLocalProfileBinding
{
	TWeakObjectPtr<AProject_JPlayerState> Player;
	FGuid Account, Character;
	TSharedPtr<FProject_JLocalProfileWork, ESPMode::ThreadSafe> Work;
	TFunction<void(bool)> OnReady;
	FDelegateHandle ProgressionChanged, QuestChanged, IdentityChanged;
	bool bLoaded = false, bLoadSucceeded = false, bReady = false, bRetired = false;
	bool bCommandPending = false;
	uint32 NextRequest = 0;
	FProject_JPersistentCompletion CommandComplete;
	FString LastAdmissionReason;
	TArray<uint8> LoadedPayload;
};

void UProject_JLocalProfileSubsystem::Initialize(FSubsystemCollectionBase &Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UProject_JCharacterDataSubsystem>();
	IOGate = MakeShared<FAggregateGate, ESPMode::ThreadSafe>(32);
	DatabasePath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LocalProfiles/Profiles.sqlite")));
}
void UProject_JLocalProfileSubsystem::Deinitialize()
{
	bClosing = true;
	for (const auto &Pair : Bindings)
	{
		Pair.Value->bRetired = true;
		Pair.Value->Work->Seal();
	}
	TArray<TWeakObjectPtr<AProject_JPlayerState>> Players;
	Bindings.GetKeys(Players);
	for (const auto &Player : Players)
		Detach(Player.Get());
	Bindings.Reset();
	if (IOGate)
		IOGate->Close(); // Active workers retain their reservations until actual completion.
	// Workers own only native values and never wait for a game-thread callback.
	// Drain accepted writes before engine shutdown can tear down its worker pool.
	for (auto &Job : Jobs)
		Job.Wait();
	Jobs.Reset();
#if WITH_DEV_AUTOMATION_TESTS
	HeldCompletionsForTest.Reset();
#endif
	Super::Deinitialize();
}
bool UProject_JLocalProfileSubsystem::ResolveLocalIdentity(const FString &Profile, FGuid &Account, FGuid &Character)
{
	Account.Invalidate();
	Character.Invalidate();
	if (Profile.IsEmpty() || Profile.Len() > 32)
		return false;
	for (TCHAR Ch : Profile)
		if (!((Ch >= 'a' && Ch <= 'z') || (Ch >= 'A' && Ch <= 'Z') || (Ch >= '0' && Ch <= '9') || Ch == '_' ||
			  Ch == '-'))
			return false;
	const FString Name = Profile.ToLower();
	Account = FGuid::NewDeterministicGuid(TEXT("ProjectJ.LocalAccount.") + Name);
	Character = FGuid::NewDeterministicGuid(TEXT("ProjectJ.LocalCharacter.") + Name);
	return true; // Stable developer IDs; deliberately not credentials or authentication.
}
bool UProject_JLocalProfileSubsystem::IsCurrent(
	const TSharedPtr<FProject_JLocalProfileBinding, ESPMode::ThreadSafe> &B) const
{
	const auto *Player = B ? B->Player.Get() : nullptr;
	const auto *Found = Player ? Bindings.Find(B->Player) : nullptr;
	return !bClosing && B && !B->bRetired && IsValid(Player) && !Player->IsActorBeingDestroyed() &&
		   Player->HasAuthority() && Player->GetGameInstance() == GetGameInstance() && Player->GetWorld() &&
		   !Player->GetWorld()->bIsTearingDown && Player->GetAccountId().Value == B->Account &&
		   Player->GetCharacterId().Value == B->Character && Found && *Found == B;
}
void UProject_JLocalProfileSubsystem::OpenProfile(AProject_JPlayerState *Player, const FString &Profile,
												  TFunction<void(bool)> OnReady)
{
	check(IsInGameThread());
	FGuid Account, Character;
	if (bClosing || !IOGate || !IsValid(Player) || !Player->HasAuthority() || Player->IsActorBeingDestroyed() ||
		!Player->GetWorld() || Player->GetWorld()->GetNetMode() != NM_Standalone ||
		Player->GetGameInstance() != GetGameInstance() || Player->GetPawn() ||
		!Player->FindComponentByClass<UProject_JProgressionComponent>() ||
		Player->FindComponentByClass<UProject_JProgressionComponent>()->GetState().Revision != 0 ||
		Bindings.Num() >= 32 || Bindings.Num() >= 32 || Bindings.Contains(Player) ||
		!ResolveLocalIdentity(Profile, Account, Character))
	{
		OnReady(false);
		return;
	}
	for (const auto &Pair : Bindings)
		if (Pair.Value->Character == Character)
		{
			OnReady(false);
			return;
		} // Reject duplicate active local profiles.
	auto *Definitions = GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>();
	if (!Definitions || !Definitions->LoadPrimaryAssetDefinitions())
	{
		OnReady(false);
		return;
	}
	FGuid Ticket;
	if (IOGate->TryAcquire({ProfileKey(Character)}, Ticket) != EAdmission::Accepted)
	{
		OnReady(false);
		return;
	}
	auto B = MakeShared<FProject_JLocalProfileBinding, ESPMode::ThreadSafe>();
	B->Player = Player;
	B->Account = Account;
	B->Character = Character;
	B->OnReady = MoveTemp(OnReady);
	auto Work = MakeShared<FProject_JLocalProfileWork, ESPMode::ThreadSafe>();
	Work->Repository = MakeShared<FProject_JSQLiteRepository, ESPMode::ThreadSafe>(DatabasePath);
	Work->Gate = IOGate;
	Work->Character = Character;
	Work->Session = FGuid::NewGuid();
	B->Work = Work;
	Bindings.Add(Player, B);
	Player->SetIdentity(FProject_JAccountId{Account}, FProject_JCharacterId{Character});
	if (!IsCurrent(B))
	{
		Detach(Player);
		IOGate->Release(Ticket);
		return;
	}
	TWeakObjectPtr<UProject_JLocalProfileSubsystem> WeakThis(this);
	B->IdentityChanged = Player->OnCharacterIdentityChanged.AddWeakLambda(this, [WeakThis, WeakPlayer = B->Player]() {
		if (auto *Self = WeakThis.Get())
			Self->Detach(WeakPlayer.Get());
	});
	Jobs.RemoveAll([](const auto &Job) { return Job.IsReady(); });
	Jobs.Add(Async(EAsyncExecution::ThreadPool, [WeakThis, B, Work, Ticket] {
		FRecord Lease, Record;
		const auto LeaseRead = Work->Repository->ReadChecked(LeaseKey(Work->Character), Lease);
		bool Success = LeaseRead != EProject_JRecordRead::Unavailable &&
					   (LeaseRead == EProject_JRecordRead::Missing ||
						(!Lease.bDeleted && Lease.SchemaVersion == 1 && Lease.Payload.Num() == 16));
		FString Error = Success ? FString()
						: LeaseRead == EProject_JRecordRead::Unavailable
							? Work->Repository->DescribeFailure()
							: TEXT("저장 세션 schema/lease 형식이 지원되지 않습니다");
		if (Success)
		{
			FCommitRequest Request{
				FGuid::NewGuid(),
				TEXT("LocalProfileSession"),
				{{LeaseKey(Work->Character), Lease.Revision, 1, SessionBytes(Work->Session), false}}};
			auto Result = Work->Repository->CommitProfile(Request, {Work->Character, Work->Session, -1});
			if (Result.Status == ECommitStatus::UnknownOutcome)
				Result = Work->Repository->CommitProfile(Request, {Work->Character, Work->Session, -1});
			Success = Accepted(Result.Status) && Result.Revisions.Num() == 1;
			if (!Success)
				Error = Result.Status == ECommitStatus::Capacity
							? TEXT("입장 거부: 보관 프로필 record 한도 도달. 기존 캐릭터를 보존하고 추가 프로필 보관 "
								   "위치를 확인하세요")
							: Work->Repository->DescribeFailure();
			if (Success)
			{
				FScopeLock Lock(&Work->Mutex);
				Work->LeaseRevision = Result.Revisions[0];
			}
		}
		if (Success)
		{
			const auto Read = Work->Repository->ReadChecked(ProfileKey(Work->Character), Record);
			Success =
				Read == EProject_JRecordRead::Missing || (Read == EProject_JRecordRead::Found && !Record.bDeleted &&
														  Record.SchemaVersion == 1 && !Record.Payload.IsEmpty());
			if (!Success)
				Error = Read == EProject_JRecordRead::Unavailable
							? Work->Repository->DescribeFailure()
							: TEXT("프로필 record 버전/삭제 상태가 호환되지 않습니다");
			{
				FScopeLock Lock(&Work->Mutex);
				Work->ProfileRevision = Record.Revision;
			}
		}
		FProject_JRepositoryUsage Usage;
		const bool Known = Work->Repository->GetUsage(Usage);
		{
			FScopeLock Lock(&Work->Mutex);
			Work->Usage = Usage;
			Work->bUsageKnown = Known;
		}
		Work->Repository->Close();
		Work->Gate->Release(Ticket);
		AsyncTask(ENamedThreads::GameThread,
				  [WeakThis, B, Success, Error = MoveTemp(Error), Payload = MoveTemp(Record.Payload)]() mutable {
					  if (auto *Self = WeakThis.Get(); Self && Self->IsCurrent(B))
					  {
						  B->bLoaded = true;
						  B->bLoadSucceeded = Success;
						  B->LastAdmissionReason = MoveTemp(Error);
						  B->LoadedPayload = MoveTemp(Payload);
						  Self->CompleteOpen(B);
					  }
				  });
	}));
}
void UProject_JLocalProfileSubsystem::PlayerBegunPlay(AProject_JPlayerState *Player)
{
	if (auto *B = Bindings.Find(Player))
		CompleteOpen(*B);
}
void UProject_JLocalProfileSubsystem::CompleteOpen(
	const TSharedPtr<FProject_JLocalProfileBinding, ESPMode::ThreadSafe> &B)
{
	if (!IsCurrent(B) || !B->bLoaded || !B->OnReady || !B->Player->HasActorBegunPlay())
		return;
	auto *Player = B->Player.Get();
	auto *P = Player->FindComponentByClass<UProject_JProgressionComponent>();
	auto *Q = Player->FindComponentByClass<UProject_JQuestComponent>();
	const TStrongObjectPtr<UProject_JLocalProfileSubsystem> KeepSelf(this);
	const TStrongObjectPtr<AProject_JPlayerState> KeepPlayer(Player);
	bool Success = B->bLoadSucceeded && P && Q && !Player->GetPawn() && P->GetState().Revision == 0;
	if (Success && !B->LoadedPayload.IsEmpty())
	{
		FProject_JLocalProfileSnapshot Snapshot;
		UProject_JCharacterClassDefinition *Class = nullptr;
		TArray<UProject_JCharacterAdvancementDefinition *> History;
		TArray<FProject_JItemInstanceData> Bag;
		TArray<FProject_JEquipmentArrayItem> Slots;
		const bool Decoded = FProject_JLocalProfileSnapshot::Decode(B->LoadedPayload, Snapshot);
		Success = Decoded && Snapshot.Account == B->Account && Snapshot.Character == B->Character &&
				  ResolveDefinitions(GetGameInstance(), Snapshot.Progression, Class, History) &&
				  Snapshot.RulesFingerprint == Fingerprint(*P, *Q, Class, History) &&
				  P->CanRestoreSnapshot(Snapshot.Progression, Class, History) &&
				  Q->CanRestoreSnapshot(Snapshot.Quests, true) &&
				  ResolveItems(Player, Snapshot, Bag, Slots, false, &B->LastAdmissionReason);
		if (!Success && B->LastAdmissionReason.IsEmpty())
			B->LastAdmissionReason =
				!Decoded ? TEXT("프로필 JSON 형식/범위 오류: 원형 보존 필요")
				: Snapshot.Format == 1
					? TEXT("진행도 전용 v1 프로필: 인벤토리 없는 상태를 추측하지 않습니다. 명시적 v2 이관 필요")
					: TEXT("프로필의 정의·규칙 버전·범위·소유권 검증 실패: 기존 저장을 보존했습니다");
		if (Success)
		{
			const TStrongObjectPtr<UProject_JProgressionComponent> KeepProgress(P);
			const TStrongObjectPtr<UProject_JQuestComponent> KeepQuests(Q);
			const TStrongObjectPtr<UProject_JInventoryComponent> KeepInventory(Player->GetInventoryComponent());
			const TStrongObjectPtr<UProject_JEquipmentManagerComponent> KeepEquipment(
				Player->GetEquipmentManagerComponent());
			// Loading an existing owner has no bootstrap mutation window. Admission stays
			// closed through all initial notifications; trusted installers do no I/O.
			BindPersistentCommands(B);
			Player->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(Player, nullptr);
			Bag.Reset();
			Slots.Reset();
			Success = IsCurrent(B) &&
					  P->ValidateCommittedProgression(Snapshot.Progression, Class, History, true, true) &&
					  Q->ValidateCommittedQuests(Snapshot.Quests, true) &&
					  ResolveItems(Player, Snapshot, Bag, Slots, true, &B->LastAdmissionReason);
			if (Success)
			{
				auto *I = KeepInventory.Get();
				auto *E = KeepEquipment.Get();
				const auto OldBag = I->GetItemInstances();
				const auto OldSlots = E->CapturePersistentEquipment();
				P->InstallCommittedProgression(Snapshot.Progression, Class, History);
				Q->InstallCommittedQuests(Snapshot.Quests);
				I->InstallPersistentItems(Bag);
				E->InstallPersistentEquipment(Slots);
				E->bPersistentProfileBound = true;
				P->PublishCommittedExperience();
				if (IsCurrent(B))
					Q->PublishCommittedQuests();
				if (IsCurrent(B))
					I->PublishPersistentItems(OldBag);
				if (IsCurrent(B))
					E->PublishPersistentEquipment(OldSlots);
			}
		}
	}
	if (!IsCurrent(B))
		return;
	B->bReady = Success;
	// An unchanged loaded profile needs no second receipt merely for spawning.
	if (Success && !B->LoadedPayload.IsEmpty())
	{
		FScopeLock Lock(&B->Work->Mutex);
		B->Work->LastQueued = B->LoadedPayload;
	}
	if (Success)
	{
		TWeakObjectPtr<UProject_JLocalProfileSubsystem> WeakThis(this);
		TWeakPtr<FProject_JLocalProfileBinding, ESPMode::ThreadSafe> WeakB(B);
		auto Changed = [WeakThis, WeakB]() {
			if (auto *Self = WeakThis.Get())
				if (auto Current = WeakB.Pin(); Self->IsCurrent(Current))
					Self->RequestCheckpoint(Current->Player.Get());
		};
		B->ProgressionChanged = P->OnChanged.AddWeakLambda(this, Changed);
		B->QuestChanged = Q->OnChanged.AddWeakLambda(this, Changed);
	}
	else
	{
		B->Work->Seal();
		UE_LOG(LogProjectJLocalProfile, Error,
			   TEXT("Local profile load rejected; no new default state will overwrite it."));
	}
	auto Callback = MoveTemp(B->OnReady);
	if (!Success && IsCurrent(B))
		BindPersistentCommands(B);
	B->LoadedPayload.Reset();
	Callback(Success);
	if (Success && IsCurrent(B))
		Player->GetEquipmentManagerComponent()->bPersistentProfileBound = true;
	if (Success && IsCurrent(B))
		BindPersistentCommands(B);
	if (Success && IsCurrent(B))
		RequestCheckpoint(Player);
}
void UProject_JLocalProfileSubsystem::RequestCheckpoint(AProject_JPlayerState *Player)
{
	check(IsInGameThread());
	auto *Found = Bindings.Find(Player);
	if (!Found || !IsCurrent(*Found) || !(*Found)->bReady || (*Found)->bCommandPending)
		return;
	auto B = *Found;
	auto *P = Player->FindComponentByClass<UProject_JProgressionComponent>();
	auto *Q = Player->FindComponentByClass<UProject_JQuestComponent>();
	if (!P || !Q || P->GetState().Revision == 0)
		return; // Pawn defaults must initialize a new profile first.
	FProject_JLocalProfileSnapshot Snapshot;
	Snapshot.Account = B->Account;
	Snapshot.Character = B->Character;
	Snapshot.Progression = P->CaptureSnapshot();
	Snapshot.Quests = Q->CaptureSnapshot();
	UProject_JCharacterClassDefinition *Class = nullptr;
	TArray<UProject_JCharacterAdvancementDefinition *> History;
	TArray<uint8> Payload;
	if (!ResolveDefinitions(GetGameInstance(), Snapshot.Progression, Class, History) ||
		P->GetState().ClassDefinition != Class ||
		P->GetState().Advancement != (History.IsEmpty() ? nullptr : History.Last()) ||
		(Snapshot.RulesFingerprint = Fingerprint(*P, *Q, Class, History)).IsEmpty() ||
		!CaptureItems(Player, Snapshot) || !Snapshot.Encode(Payload))
	{
		B->bReady = false;
		B->Work->Seal();
		UE_LOG(LogProjectJLocalProfile, Error,
			   TEXT("Local checkpoint rejected: unresolved IDs, incompatible rules, or payload limit."));
		return;
	}
	auto Work = B->Work;
	FGuid Ticket;
	{
		FScopeLock Lock(&Work->Mutex);
		if (!Work->bAccepting || Work->bFaulted || Work->LastQueued == Payload)
			return;
		if (!Work->bRunning && Work->Gate->TryAcquire({ProfileKey(B->Character)}, Ticket) != EAdmission::Accepted)
		{
			B->bReady = false;
			UE_LOG(LogProjectJLocalProfile, Error,
				   TEXT("Local checkpoint admission unavailable; checkpoint not confirmed."));
			return;
		}
		Work->LastQueued = Payload;
		Work->Pending = MoveTemp(Payload);
		if (Work->bRunning)
			return;
		Work->bRunning = true;
	}
	TWeakObjectPtr<UProject_JLocalProfileSubsystem> WeakThis(this);
	TWeakPtr<FProject_JLocalProfileBinding, ESPMode::ThreadSafe> WeakB(B);
	Jobs.RemoveAll([](const auto &Job) { return Job.IsReady(); });
	Jobs.Add(Async(EAsyncExecution::ThreadPool, [Work, Ticket, WeakThis, WeakB] {
		Work->Drain(Ticket);
		if (!Work->Healthy())
			AsyncTask(ENamedThreads::GameThread, [WeakThis, WeakB]() {
				if (auto *Self = WeakThis.Get())
					if (auto Current = WeakB.Pin(); Self->IsCurrent(Current))
					{
						Current->bReady = false;
						UE_LOG(LogProjectJLocalProfile, Error,
							   TEXT("Local checkpoint failed or its writer was fenced; reload is required before "
									"further saves."));
					}
			});
	}));
}
void UProject_JLocalProfileSubsystem::RequestBackup(TFunction<void(bool, FString)> OnComplete)
{
	check(IsInGameThread());
	if (bClosing || !IOGate || !GetWorld() || GetWorld()->GetNetMode() != NM_Standalone)
	{
		OnComplete(false, {});
		return;
	}
	FGuid Ticket;
	if (IOGate->TryAcquire({{TEXT("LocalProfileBackup"), FGuid::NewDeterministicGuid(DatabasePath)}}, Ticket) !=
		EAdmission::Accepted)
	{
		OnComplete(false, {});
		return;
	}
	const FString Path =
		FPaths::Combine(FPaths::GetPath(DatabasePath), TEXT("Backups"), FGuid::NewGuid().ToString() + TEXT(".sqlite"));
	auto Repository = MakeShared<FProject_JSQLiteRepository, ESPMode::ThreadSafe>(DatabasePath);
	auto Gate = IOGate;
	TWeakObjectPtr<UProject_JLocalProfileSubsystem> WeakThis(this);
	Jobs.RemoveAll([](const auto &Job) { return Job.IsReady(); });
	Jobs.Add(Async(EAsyncExecution::ThreadPool, [Repository, Gate, Ticket, Path, WeakThis,
												 Callback = MoveTemp(OnComplete)]() mutable {
		const bool Success = Repository->BackupToNewFile(Path, 8);
		Repository->Close();
		Gate->Release(Ticket);
		AsyncTask(ENamedThreads::GameThread, [Success, Path, WeakThis, Callback = MoveTemp(Callback)]() mutable {
			if (auto *Self = WeakThis.Get(); Self && !Self->bClosing)
				Callback(Success, Path);
		});
	}));
}
void UProject_JLocalProfileSubsystem::Detach(AProject_JPlayerState *Player)
{
	check(IsInGameThread());
	auto *Found = Bindings.Find(Player);
	if (!Found)
		return;
	auto B = *Found;
	B->bRetired = true;
	B->OnReady = {};
	B->Work->Seal();
	if (IsValid(Player))
	{
		Player->OnCharacterIdentityChanged.Remove(B->IdentityChanged);
		if (auto *P = Player->FindComponentByClass<UProject_JProgressionComponent>())
			P->OnChanged.Remove(B->ProgressionChanged);
		if (auto *Q = Player->FindComponentByClass<UProject_JQuestComponent>())
			Q->OnChanged.Remove(B->QuestChanged);
	}
	Bindings.Remove(Player);
}
bool UProject_JLocalProfileSubsystem::IsReady(const AProject_JPlayerState *Player) const
{
	const auto *Found = Bindings.Find(const_cast<AProject_JPlayerState *>(Player));
	return Found && IsCurrent(*Found) && (*Found)->bReady && (*Found)->Work->Healthy();
}
bool UProject_JLocalProfileSubsystem::IsCheckpointPending(const AProject_JPlayerState *Player) const
{
	const auto *Found = Bindings.Find(const_cast<AProject_JPlayerState *>(Player));
	return Found && ((*Found)->bCommandPending || (*Found)->Work->PendingIO());
}
int64 UProject_JLocalProfileSubsystem::GetCommittedRevision(const AProject_JPlayerState *Player) const
{
	const auto *Found = Bindings.Find(const_cast<AProject_JPlayerState *>(Player));
	return Found ? (*Found)->Work->Revision() : 0;
}
FText UProject_JLocalProfileSubsystem::GetStatusText(const AProject_JPlayerState *Player) const
{
	const auto *Found = Bindings.Find(const_cast<AProject_JPlayerState *>(Player));
	if (!Found)
		return FText::GetEmpty();
	const auto &B = **Found;
	const auto Work = B.Work;
	FScopeLock Lock(&Work->Mutex);
	FString State;
	if (!B.bLoaded)
		State = TEXT("프로필 읽는 중 · 입장 대기");
	else if (!B.bLoadSucceeded || (!B.bReady && !Work->bFaulted))
		State = TEXT("입장/저장 거부 · 파일 접근·정의/버전·복구 상태 확인 필요. 기존 데이터 보존");
	else if (Work->bFaulted)
		State = Work->LastResult == ECommitStatus::Capacity
					? TEXT("저장 한도 도달 · 저장 중지. 마지막 확인 이후 변경은 미확정")
				: Work->LastResult == ECommitStatus::Conflict
					? TEXT("다른 세션이 저장 권한을 인수함 · 재입장 필요")
					: TEXT("저장 실패/결과 불확실 · 메모리 변경은 디스크 확정 아님. 재입장/복구 필요");
	else if (B.bCommandPending || Work->bRunning || Work->Pending.IsSet())
		State = TEXT("디스크 저장 중 · 현재 요청/메모리 변경은 아직 미확정. 정상 종료 시 대기 작업 완료");
	else
		State = TEXT("디스크 체크포인트 확인됨");
	const FString Counts =
		Work->bUsageKnown ? FString::Printf(TEXT("records %lld/%d · receipts %lld/%d"), Work->Usage.Records,
											Work->Usage.RecordLimit, Work->Usage.Receipts, Work->Usage.ReceiptLimit)
						  : TEXT("사용량 확인 불가");
	if (!B.LastAdmissionReason.IsEmpty())
		State += TEXT(" · ") + B.LastAdmissionReason;
	return FText::FromString(
		FString::Printf(TEXT("개발 프로필: %s\n마지막 확인 revision %lld · %s\n자동 반복 재시도 없음 · 정상 종료 후 "
							 "백업/복구: 로컬 저장 문서 · 콘솔 ProjectJ.LocalProfile.Backup"),
						*State, Work->ProfileRevision, *Counts));
}

void UProject_JLocalProfileSubsystem::BindPersistentCommands(
	const TSharedPtr<FProject_JLocalProfileBinding, ESPMode::ThreadSafe> &B)
{
	TWeakObjectPtr<UProject_JLocalProfileSubsystem> WeakThis(this);
	TWeakPtr<FProject_JLocalProfileBinding, ESPMode::ThreadSafe> WeakB(B);
	FProject_JPersistentSubmit Submit = [WeakThis, WeakB](const FProject_JPersistentEdit &Edit,
														  FProject_JPersistentCompletion Complete) {
		auto *Self = WeakThis.Get();
		auto Current = WeakB.Pin();
		return Self && Self->IsCurrent(Current) && Self->SubmitEdit(Current->Player.Get(), Edit, MoveTemp(Complete));
	};
	auto CreateRequest = [WeakThis, WeakB]() {
		auto *Self = WeakThis.Get();
		auto B = WeakB.Pin();
		return Self && Self->IsCurrent(B) ? Self->NewRequestId(B->Player.Get()) : FGuid();
	};
	auto Admission = [WeakThis, WeakB]() {
		auto *Self = WeakThis.Get();
		auto Current = WeakB.Pin();
		return Self && Self->IsCurrent(Current) && Current->bReady && Current->Work->Healthy() &&
			   !Current->bCommandPending;
	};
	auto *Player = B->Player.Get();
	auto *P = Player->FindComponentByClass<UProject_JProgressionComponent>();
	auto *Q = Player->FindComponentByClass<UProject_JQuestComponent>();
	P->SubmitPersistentEdit = Submit;
	P->CreatePersistentRequest = CreateRequest;
	P->PersistentAdmission = Admission;
	Q->SubmitPersistentEdit = Submit;
	Q->CreatePersistentRequest = CreateRequest;
	Q->PersistentAdmission = Admission;
	Player->GetInventoryComponent()->SubmitPersistentEdit = Submit;
	Player->GetInventoryComponent()->CreatePersistentRequest = CreateRequest;
	Player->GetInventoryComponent()->PersistentAdmission = Admission;
	Player->GetEquipmentManagerComponent()->SubmitPersistentEdit = Submit;
	Player->GetEquipmentManagerComponent()->CreatePersistentRequest = CreateRequest;
	Player->GetEquipmentManagerComponent()->PersistentAdmission = Admission;
}

FGuid UProject_JLocalProfileSubsystem::NewRequestId(AProject_JPlayerState *Player)
{
	check(IsInGameThread());
	auto *B = Bindings.Find(Player);
	if (!B || !IsCurrent(*B) || !(*B)->bReady || (*B)->NextRequest == MAX_uint32)
		return {};
	auto Session = (*B)->Work->Session;
	Session.D = ++(*B)->NextRequest;
	return Session;
}
bool UProject_JLocalProfileSubsystem::SubmitEdit(AProject_JPlayerState *Player, const FProject_JPersistentEdit &Edit,
												 FProject_JPersistentCompletion Complete)
{
	check(IsInGameThread());
	auto *Found = Bindings.Find(Player);
	if (!Found || !IsCurrent(*Found) || !(*Found)->bReady || !Edit.Request.IsValid() || !(*Found)->Work->Healthy())
		return false;
	if ((*Found)->bCommandPending || (*Found)->Work->PendingIO())
	{
		(*Found)->LastAdmissionReason = TEXT("이전 저장 확인 중: 새 변경을 수락하지 않음");
		return false;
	}
	auto B = *Found;
	auto Work = B->Work;
	if (Edit.Request.A != Work->Session.A || Edit.Request.B != Work->Session.B || Edit.Request.C != Work->Session.C ||
		Edit.Request.D == 0 || Edit.Request.D > B->NextRequest)
	{
		B->LastAdmissionReason = TEXT("만료되었거나 발급되지 않은 요청: 현재 상태를 재동기화하세요");
		return false;
	}
	auto *P = Player->FindComponentByClass<UProject_JProgressionComponent>();
	auto *Q = Player->FindComponentByClass<UProject_JQuestComponent>();
	auto *I = Player->GetInventoryComponent();
	auto *E = Player->GetEquipmentManagerComponent();
	FProject_JLocalProfileSnapshot Before;
	Before.Account = B->Account;
	Before.Character = B->Character;
	Before.Progression = P->CaptureSnapshot();
	Before.Quests = Q->CaptureSnapshot();
	UProject_JCharacterClassDefinition *Class = nullptr;
	TArray<UProject_JCharacterAdvancementDefinition *> History;
	TArray<uint8> BeforeBytes;
	if (!ResolveDefinitions(GetGameInstance(), Before.Progression, Class, History) ||
		(Before.RulesFingerprint = Fingerprint(*P, *Q, Class, History)).IsEmpty() || !CaptureItems(Player, Before) ||
		!Before.Encode(BeforeBytes))
		return false;
	// Exact logical intent is receipt data, independent of later aggregate revisions.
	// Keep Operation bounded; unique payloads must not grow the global FName pool.
	TArray<uint8> Intent;
	FMemoryWriter W(Intent);
	uint8 Kind = uint8(Edit.Kind);
	FGuid Instance = Edit.Instance, Target = Edit.Target;
	FString Definition = Edit.Definition ? Edit.Definition->ItemId.ToString().ToLower() : TEXT("none"),
			Quest = Edit.Quest.ToString().ToLower();
	int32 Version = Edit.Definition ? Edit.Definition->SchemaVersion : 0;
	int64 Amount = Edit.Amount;
	int32 Level = Edit.Level, Operation = Edit.Operation, SC = Edit.SourceCount, TC = Edit.TargetCount,
		  SO = Edit.SourceOrder, TO = Edit.TargetOrder;
	uint8 Slot = uint8(Edit.Slot);
	FString ProgressionDefinition = Edit.ProgressionDefinition.ToString().ToLower();
	W << ProgressionDefinition;
	W << Kind << Instance << Target << Definition << Version << Quest << Amount << Level << Operation << SC << TC << SO
	  << TO << Slot;
	if (Intent.Num() > 440)
		return false;
	const FName IntentOperation(*FString::Printf(TEXT("ProfileEdit.%d"), int32(Edit.Kind)));
	const FGuid Key = Edit.Request;
	auto Proposed = Before;
	auto Find = [&](FGuid Id) {
		return Proposed.Items.IndexOfByPredicate([&](const auto &Item) { return Item.Instance == Id; });
	};
	auto NextOrder = [&]() {
		int64 Order = 0;
		for (const auto &Item : Proposed.Items)
			if (Item.bInBag)
				Order = FMath::Max(Order, int64(Item.Order) + 1);
		return Order < MAX_int32 ? int32(Order) : INDEX_NONE;
	};
	auto Plan = [&]() -> bool {
		using K = EProject_JPersistentEdit;
		if (Edit.Kind == K::Class || Edit.Kind == K::Advancement)
		{
			auto *Data = GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>();
			return Edit.Kind == K::Class
					   ? P->PlanClass(Data->GetClassDefinition(Edit.ProgressionDefinition), Proposed.Progression)
					   : P->PlanAdvancement(Data->GetAdvancementDefinition(Edit.ProgressionDefinition),
											Proposed.Progression);
		}
		if (Edit.Kind == K::Experience)
			return P->PlanExperience(Edit.Amount, Proposed.Progression);
		if (Edit.Kind == K::Claim)
		{
			auto *State =
				Proposed.Quests.States.FindByPredicate([&](const auto &S) { return S.QuestId == Edit.Quest; });
			const auto *Def = Q->FindDefinition(Edit.Quest);
			if (!State || !Def || State->bClaimed || State->Count < Def->RequiredCount)
				return false;
			if (Def->RewardExperience > 0 && P->GetNextLevelExperience() > 0 &&
				!P->PlanExperience(Def->RewardExperience, Proposed.Progression))
				return false;
			State->bClaimed = true;
			State->bTracked = false;
			return true;
		}
		if (Edit.Kind == K::Add)
		{
			auto *Data = GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>();
			const auto *Def = Edit.Definition;
			if (!Def || Data->GetItemDefinition(Def->ItemId, Def->SchemaVersion) != Def || !Edit.Instance.IsValid() ||
				Find(Edit.Instance) != INDEX_NONE || Edit.Amount < 1 || Edit.Amount > Def->MaxStackCount ||
				Edit.Level < 1 || Edit.Level > 10000 || NextOrder() == INDEX_NONE)
				return false;
			FProject_JPersistentItem Item;
			Item.Instance = Edit.Instance;
			Item.Definition = Def->ItemId;
			Item.DefinitionVersion = Def->SchemaVersion;
			Item.Level = Edit.Level;
			Item.Count = int32(Edit.Amount);
			Item.Order = NextOrder();
			Proposed.Items.Add(Item);
			return true;
		}
		int32 Index = Find(Edit.Instance);
		if (Index == INDEX_NONE)
			return false;
		auto Item = Proposed.Items[Index];
		if (Edit.Kind == K::Unequip)
		{
			if (Item.Slot == EProject_JEquipmentSlot::None || Item.Slot != Edit.Slot)
				return false;
			if (Item.bInBag)
				Proposed.Items[Index].Slot = EProject_JEquipmentSlot::None;
			else
				Proposed.Items.RemoveAt(Index);
			return true;
		}
		if (!Item.bInBag || Item.Slot != EProject_JEquipmentSlot::None)
			return false;
		auto *Data = GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>();
		const auto *Def = Data->GetItemDefinition(Item.Definition, Item.DefinitionVersion);
		if (!Def)
			return false;
		if (Edit.Kind == K::Equip)
		{
			const auto *Gear = Cast<UProject_JEquipmentItemDefinition>(Def);
			if (!Gear || Gear->EquipmentSlot != Edit.Slot || Item.Count != 1)
				return false;
			for (int32 N = Proposed.Items.Num() - 1; N >= 0; --N)
				if (Proposed.Items[N].Slot == Edit.Slot)
				{
					if (Proposed.Items[N].bInBag)
						Proposed.Items[N].Slot = EProject_JEquipmentSlot::None;
					else
						Proposed.Items.RemoveAt(N);
				}
			Proposed.Items[Find(Edit.Instance)].Slot = Edit.Slot;
			return true;
		}
		if (Edit.Kind == K::Remove)
		{
			Proposed.Items.RemoveAt(Index);
			return true;
		}
		if (Edit.Kind == K::Use)
		{
			if (!I->CanUsePersistentItem(Edit.Instance))
				return false;
			if (--Proposed.Items[Index].Count == 0)
				Proposed.Items.RemoveAt(Index);
			return true;
		}
		if (Edit.Kind == K::Stack)
		{
			if (Edit.Amount < 0 || Edit.Amount > Def->MaxStackCount)
				return false;
			if (!Edit.Amount)
				Proposed.Items.RemoveAt(Index);
			else
				Proposed.Items[Index].Count = int32(Edit.Amount);
			return true;
		}
		if (Edit.Kind != K::Bag || Item.Count != Edit.SourceCount || Item.Order != Edit.SourceOrder)
			return false;
		if (Edit.Operation == int32(EProject_JBagOperation::Split))
		{
			if (Edit.Target.IsValid() || Edit.Amount < 1 || Edit.Amount >= Item.Count || Def->MaxStackCount <= 1 ||
				NextOrder() == INDEX_NONE)
				return false;
			Item.Instance = FGuid::NewGuid();
			Item.Count = int32(Edit.Amount);
			Item.Order = NextOrder();
			Proposed.Items[Index].Count -= int32(Edit.Amount);
			Proposed.Items.Add(Item);
			return true;
		}
		const int32 Other = Find(Edit.Target);
		if (Other == INDEX_NONE || Other == Index)
			return false;
		const auto TargetItem = Proposed.Items[Other];
		if (!TargetItem.bInBag || TargetItem.Slot != EProject_JEquipmentSlot::None ||
			TargetItem.Count != Edit.TargetCount || TargetItem.Order != Edit.TargetOrder)
			return false;
		if (Edit.Operation == int32(EProject_JBagOperation::Swap))
		{
			if (Edit.Amount || Item.Order == TargetItem.Order)
				return false;
			Swap(Proposed.Items[Index].Order, Proposed.Items[Other].Order);
			return true;
		}
		if (Edit.Operation != int32(EProject_JBagOperation::Merge) || Item.Definition != TargetItem.Definition ||
			Item.DefinitionVersion != TargetItem.DefinitionVersion || Item.Level != TargetItem.Level ||
			Def->MaxStackCount <= 1 || Edit.Amount < 1 || Edit.Amount > Item.Count ||
			Edit.Amount > int64(Def->MaxStackCount) - TargetItem.Count)
			return false;
		Proposed.Items[Other].Count += int32(Edit.Amount);
		Proposed.Items[Index].Count -= int32(Edit.Amount);
		if (!Proposed.Items[Index].Count)
			Proposed.Items.RemoveAt(Index);
		return true;
	};
	TArray<uint8> Payload;
	TArray<FProject_JItemInstanceData> PlannedBag;
	TArray<FProject_JEquipmentArrayItem> PlannedSlots;
	UProject_JCharacterClassDefinition *PlannedClass = nullptr;
	TArray<UProject_JCharacterAdvancementDefinition *> PlannedHistory;
	const bool Valid = Plan() &&
					   ResolveDefinitions(GetGameInstance(), Proposed.Progression, PlannedClass, PlannedHistory) &&
					   !(Proposed.RulesFingerprint = Fingerprint(*P, *Q, PlannedClass, PlannedHistory)).IsEmpty() &&
					   P->ValidateCommittedProgression(Proposed.Progression, PlannedClass, PlannedHistory) &&
					   Q->ValidateCommittedQuests(Proposed.Quests) &&
					   ResolveItems(Player, Proposed, PlannedBag, PlannedSlots) && Proposed.Encode(Payload);
	FGuid Ticket;
	if (IOGate->TryAcquire({ProfileKey(B->Character)}, Ticket) != EAdmission::Accepted)
		return false;
	B->bCommandPending = true;
	B->CommandComplete = MoveTemp(Complete);
	FCommitRequest Request{Key,
						   IntentOperation,
						   {{LeaseKey(B->Character), Work->LeaseRevision, 1, SessionBytes(Work->Session), false},
							{ProfileKey(B->Character), Work->ProfileRevision, 1, Payload, false}}};
	TWeakObjectPtr<UProject_JLocalProfileSubsystem> WeakThis(this);
	Jobs.RemoveAll([](const auto &Job) { return Job.IsReady(); });
	Jobs.Add(Async(EAsyncExecution::ThreadPool, [WeakThis, B, Work, Request = MoveTemp(Request),
												 Intent = MoveTemp(Intent), Valid, Ticket]() mutable {
		const FProject_JProfileStream Stream{Work->Character, Work->Session, Request.IdempotencyKey.D};
		auto Result = Work->Repository->FindProfileReceipt(Request.IdempotencyKey, Request.Operation, Stream, Intent);
		const bool IssuedNewCommit = Result.Status == ECommitStatus::Invalid && Valid;
		if (IssuedNewCommit)
		{
			Result = Work->Repository->CommitProfile(Request, Stream, Intent);
			if (Result.Status == ECommitStatus::UnknownOutcome)
				Result = Work->Repository->CommitProfile(Request, Stream, Intent);
		}
		FRecord Record, Lease;
		bool Confirmed = Accepted(Result.Status) && Work->Repository->Read(ProfileKey(Work->Character), Record) &&
						 Work->Repository->Read(LeaseKey(Work->Character), Lease) && !Record.bDeleted &&
						 !Lease.bDeleted && Record.SchemaVersion == 1 && Lease.SchemaVersion == 1 &&
						 Lease.Payload == SessionBytes(Work->Session);
		if (Accepted(Result.Status) && !Confirmed)
			Result.Status = ECommitStatus::Conflict;
		FProject_JRepositoryUsage Usage;
		const bool Known = Work->Repository->GetUsage(Usage);
		{
			FScopeLock Lock(&Work->Mutex);
			Work->Usage = Usage;
			Work->bUsageKnown = Known;
			Work->LastResult = Result.Status;
			if (Confirmed)
			{
				Work->ProfileRevision = Record.Revision;
				Work->LeaseRevision = Lease.Revision;
				Work->LastQueued = Record.Payload;
			}
		}
		Work->Repository->Close();
		Work->Gate->Release(Ticket);
		AsyncTask(ENamedThreads::GameThread, [WeakThis, B, Confirmed, FirstApplication = IssuedNewCommit && Confirmed,
											  Status = Result.Status, Payload = MoveTemp(Record.Payload)]() mutable {
			if (auto *Self = WeakThis.Get())
				Self->CompleteEdit(B, Confirmed, FirstApplication, Status, MoveTemp(Payload));
		});
	}));
	return true;
}
void UProject_JLocalProfileSubsystem::CompleteEdit(
	const TSharedPtr<FProject_JLocalProfileBinding, ESPMode::ThreadSafe> &B, bool Confirmed, bool FirstApplication,
	ECommitStatus Status, TArray<uint8> Payload)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (bHoldCompletionsForTest)
	{
		TWeakObjectPtr<UProject_JLocalProfileSubsystem> WeakThis(this);
		HeldCompletionsForTest.Add(
			[WeakThis, B, Confirmed, FirstApplication, Status, Payload = MoveTemp(Payload)]() mutable {
				if (auto *Self = WeakThis.Get())
					Self->CompleteEdit(B, Confirmed, FirstApplication, Status, MoveTemp(Payload));
			});
		return;
	}
#endif

	auto *Self = this;
	if (!Self || !Self->IsCurrent(B))
		return;
	const TStrongObjectPtr<UProject_JLocalProfileSubsystem> KeepSelf(Self);
	const TStrongObjectPtr<AProject_JPlayerState> KeepPlayer(B->Player.Get());
	bool OK = Confirmed;
	const bool First = FirstApplication;
	auto *PS = B->Player.Get();
	if (OK)
	{
		FProject_JLocalProfileSnapshot Snapshot;
		TArray<FProject_JItemInstanceData> Bag;
		TArray<FProject_JEquipmentArrayItem> Slots;
		auto *Progress = PS->FindComponentByClass<UProject_JProgressionComponent>();
		auto *Quests = PS->FindComponentByClass<UProject_JQuestComponent>();
		const TStrongObjectPtr<UProject_JProgressionComponent> KeepProgress(Progress);
		const TStrongObjectPtr<UProject_JQuestComponent> KeepQuests(Quests);
		const TStrongObjectPtr<UProject_JInventoryComponent> KeepInventory(PS->GetInventoryComponent());
		const TStrongObjectPtr<UProject_JEquipmentManagerComponent> KeepEquipment(PS->GetEquipmentManagerComponent());
		UProject_JCharacterClassDefinition *Class = nullptr;
		TArray<UProject_JCharacterAdvancementDefinition *> History;
		OK = FProject_JLocalProfileSnapshot::Decode(Payload, Snapshot) && Snapshot.Account == B->Account &&
			 Snapshot.Character == B->Character &&
			 ResolveDefinitions(GetGameInstance(), Snapshot.Progression, Class, History) &&
			 Snapshot.RulesFingerprint == Fingerprint(*Progress, *Quests, Class, History) &&
			 Progress->ValidateCommittedProgression(Snapshot.Progression, Class, History, true) &&
			 Quests->ValidateCommittedQuests(Snapshot.Quests, true) && ResolveItems(PS, Snapshot, Bag, Slots, true);
		if (OK)
		{
			auto *Inventory = PS->GetInventoryComponent();
			auto *Equipment = PS->GetEquipmentManagerComponent();
			const auto OldBag = Inventory->GetItemInstances();
			const auto OldSlots = Equipment->CapturePersistentEquipment();
			// All authoritative values are installed before the first notification.
			Progress->InstallCommittedProgression(Snapshot.Progression, Class, History);
			Quests->InstallCommittedQuests(Snapshot.Quests);
			Inventory->InstallPersistentItems(Bag);
			Equipment->InstallPersistentEquipment(Slots);
			Progress->PublishCommittedExperience();
			if (Self->IsCurrent(B))
				Quests->PublishCommittedQuests();
			if (Self->IsCurrent(B))
				Inventory->PublishPersistentItems(OldBag);
			if (Self->IsCurrent(B))
				Equipment->PublishPersistentEquipment(OldSlots);
		}
	}
	if (!Self->IsCurrent(B))
		return;
	B->bCommandPending = false;
	B->LastAdmissionReason = OK									  ? FString()
							 : Status == ECommitStatus::KeyReused ? TEXT("같은 요청 키의 다른 payload를 거부함")
							 : Status == ECommitStatus::Expired
								 ? TEXT("요청 유효 범위 만료: 현재 상태 재동기화 필요")
								 : TEXT("요청 검증/저장 실패: 소유권·수량·정의·버전·64KiB 한도 확인 필요");
	if (!OK && Status != ECommitStatus::Invalid && Status != ECommitStatus::KeyReused &&
		Status != ECommitStatus::Expired)
	{
		B->bReady = false;
		FScopeLock Lock(&B->Work->Mutex);
		B->Work->bFaulted = true;
		B->Work->bAccepting = false;
	}
	auto Completion = MoveTemp(B->CommandComplete);
	if (Completion)
		Completion(OK, First);
}

#if WITH_DEV_AUTOMATION_TESTS
void UProject_JLocalProfileSubsystem::LoseNextCommitAcknowledgementForTest(AProject_JPlayerState *Player)
{
	check(IsInGameThread());
	auto *B = Bindings.Find(Player);
	check(B && !IsCheckpointPending(Player));
	(*B)->Work->Repository->bTestLoseCommitResponse = true;
}
void UProject_JLocalProfileSubsystem::HoldCommandCompletionsForTest(bool Hold)
{
	check(IsInGameThread());
	bHoldCompletionsForTest = Hold;
	if (Hold)
		return;
	auto Completions = MoveTemp(HeldCompletionsForTest);
	HeldCompletionsForTest.Reset();
	for (auto &Complete : Completions)
		Complete();
}
void UProject_JLocalProfileSubsystem::FailCommandWritesForTest(AProject_JPlayerState *Player)
{
	check(IsInGameThread());
	auto *B = Bindings.Find(Player);
	check(B && !IsCheckpointPending(Player));
	(*B)->Work->Repository->TestHook = [](const TCHAR *Point, FProject_JProfileDatabase &Connection) {
		if (FCString::Strcmp(Point, TEXT("AfterBegin")) == 0)
			Connection.FailNextWriteForTest();
	};
}
#endif
bool UProject_JLocalProfileSubsystem::ValidateMigrationSnapshot(AProject_JPlayerState *Player,
																const FProject_JLocalProfileSnapshot &Snapshot,
																FString &Error)
{
	check(IsInGameThread());
	Error = TEXT("Unsupported profile, definitions, rules, inventory or equipment ownership");
	if (!Player || Snapshot.Format != 2)
		return false;
	auto *P = Player->FindComponentByClass<UProject_JProgressionComponent>();
	auto *Q = Player->FindComponentByClass<UProject_JQuestComponent>();
	UProject_JCharacterClassDefinition *Class = nullptr;
	TArray<UProject_JCharacterAdvancementDefinition *> History;
	TArray<FProject_JItemInstanceData> Bag;
	TArray<FProject_JEquipmentArrayItem> Slots;
	if (!P || !Q || !ResolveDefinitions(Player->GetGameInstance(), Snapshot.Progression, Class, History) ||
		Snapshot.RulesFingerprint != Fingerprint(*P, *Q, Class, History) ||
		!P->CanRestoreSnapshot(Snapshot.Progression, Class, History) || !Q->CanRestoreSnapshot(Snapshot.Quests, true) ||
		!ResolveItems(Player, Snapshot, Bag, Slots, false, &Error))
		return false;
	Error.Reset();
	return true;
}
