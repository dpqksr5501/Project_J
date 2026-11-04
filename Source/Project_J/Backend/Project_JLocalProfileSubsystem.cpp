#include "Backend/Project_JLocalProfileSubsystem.h"
#include "Async/Async.h"
#include "Crafting/Project_JCraftingRecipeDefinition.h"
#include "Crafting/Project_JCraftingPlan.h"
#include "Backend/Project_JLocalProfileSnapshot.h"
#include "Inventory/Project_JOwnedItemProjection.h"
#include "Backend/Project_JProfileRules.h"
#include "Backend/Project_JQuestHistoryStorage.h"
#include "Backend/Project_JSQLiteRepository.h"
#include "Backend/Project_JWorldEventClaimStorage.h"
#include "World/Project_JWorldEncounter.h"
#include "World/Project_JWorldEventRegistry.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JCraftingJobsComponent.h"
#include "Misc/DateTime.h"
#include "Engine/World.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Companion/Project_JCompanionItemDefinition.h"
#include "Mount/Project_JMountItemDefinition.h"
#include "Game/Project_JPlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Inventory/Project_JConsumableDefinition.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "Misc/ScopeLock.h"
#include "Misc/SecureHash.h"
#include "Project_JAbilitySystemComponent.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/StrongObjectPtr.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

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
using FProfileValues = TSharedPtr<const FProject_JLocalProfileSnapshot, ESPMode::ThreadSafe>;
FProfileValues DecodeProfileValues(FProject_JSQLiteRepository& Repository, FGuid Character,
    const TArray<uint8>& Bytes, const FProfileValues& Known = {}, int64 ExpectedCoreRevision = -1)
{
	check(!IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_ProfileDecodeValues);
	return ProjectJ::LocalProfileHistory::ReadProfileValues(Repository, Character, Bytes, Known, ExpectedCoreRevision);
}
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
using ProjectJ::OwnedItems::CaptureItems;
using ProjectJ::OwnedItems::ResolveItems;
using ProjectJ::LocalProfileRules::Fingerprint;
using ProjectJ::LocalProfileRules::Matches;

} // namespace

/** Native mailbox: one current commit plus one newest pending checkpoint, not an unbounded queue.
 * Once accepted, owned values drain on logout/shutdown. No worker dereferences a UObject.
 */
struct FProject_JLocalProfileWork
{
	struct FCounterKey
	{
		FName Quest, Objective;
		bool operator==(const FCounterKey& Other) const { return Quest == Other.Quest && Objective == Other.Objective; }
		friend uint32 GetTypeHash(const FCounterKey& Key) { return HashCombine(GetTypeHash(Key.Quest), GetTypeHash(Key.Objective)); }
	};
	struct FCounterGoal { int32 Required = 0, Desired = 0, Confirmed = 0; };
	struct FProjection { FProfileValues Values; int64 Revision = 0; };
	mutable FCriticalSection Mutex;
	TSharedPtr<FProject_JSQLiteRepository, ESPMode::ThreadSafe> Repository;
	TSharedPtr<FAggregateGate, ESPMode::ThreadSafe> Gate;
	FGuid Character, Session;
	int64 LeaseRevision = 0, ProfileRevision = 0;
	FProject_JRepositoryUsage Usage;
	ECommitStatus LastResult = ECommitStatus::Committed;
	bool bUsageKnown = false;
	bool bRunning = false, bAccepting = true, bFaulted = false, bProgressOpen = false;
	TOptional<FProject_JLocalProfileSnapshot> Pending;
	TArray<uint8> LastQueued; // Confirmed bytes only; never a proposed encode.
	FProfileValues ConfirmedValues;
	TMap<FCounterKey, FCounterGoal> Progress;
	int64 BlockedProgressRevision = -1;
#if WITH_DEV_AUTOMATION_TESTS
	TFunction<void(const TCHAR*)> CommandStageForTest;
#endif
	bool PendingIO() const
	{
		FScopeLock Lock(&Mutex);
		return bRunning || Pending.IsSet();
	}
	bool Healthy() const { FScopeLock Lock(&Mutex); return !bFaulted; }
	int64 Revision() const { FScopeLock Lock(&Mutex); return ProfileRevision; }
	void Seal() { FScopeLock Lock(&Mutex); bAccepting = false; }
	void BeginCommand()
	{
		FScopeLock Lock(&Mutex);
		check(!bRunning);
		bRunning = bProgressOpen = true;
	}
	bool AddProgress(const TArray<FProject_JQuestProgressInput>& Inputs, int64 AppliedRevision,
		bool bCommandPending, bool bCommandObserved, FGuid& OutTicket, bool& bOutStart)
	{
		FScopeLock Lock(&Mutex);
		bOutStart = false;
		if (!bAccepting || bFaulted || (bRunning && !bProgressOpen) || BlockedProgressRevision == ProfileRevision) return false;
		int32 NewSlots = 0;
		bool bChanges = false;
		TSet<FCounterKey> Seen;
		for (const auto& Input : Inputs)
		{
			const FCounterKey Key{Input.Quest, Input.Objective};
			if (Input.Quest.IsNone() || Input.Required < 1 || Input.Required > 1000000 || Input.Current < 0 ||
				Input.Current > Input.Required || Input.Amount < 1 || Input.Amount > 1000000 || Seen.Contains(Key)) return false;
			Seen.Add(Key);
			const auto* Goal = Progress.Find(Key);
			if (Goal && Goal->Required != Input.Required) return false;
			NewSlots += Goal ? 0 : 1;
			bChanges |= FMath::Max(Input.Current, Goal ? Goal->Desired : 0) < Input.Required;
		}
		if (Progress.Num() + NewSlots > 128 * UProject_JQuestComponent::MaxObjectivesPerQuest) return false;
		if (!bChanges) return true;
		if (!bRunning)
		{
			// An older command must deliver its GT observer before another job can publish its values.
			if (!bCommandObserved && (bCommandPending || ProfileRevision != AppliedRevision)) return false;
			if (!ConfirmedValues || Gate->TryAcquire({ProfileKey(Character)}, OutTicket) != EAdmission::Accepted) return false;
			bRunning = bProgressOpen = bOutStart = true;
		}
		for (const auto& Input : Inputs)
		{
			auto& Goal = Progress.FindOrAdd({Input.Quest, Input.Objective});
			Goal.Required = Input.Required;
			Goal.Confirmed = FMath::Max(Goal.Confirmed, Input.Current);
			Goal.Desired = int32(FMath::Min<int64>(Input.Required,
				int64(FMath::Max(Goal.Desired, Goal.Confirmed)) + Input.Amount));
		}
		return true;
	}
	static int32* FindCounter(FProject_JQuestSnapshot& Quests, const FCounterKey& Key)
	{
		auto* State = Quests.States.FindByPredicate([&](const auto& Value) { return Value.QuestId == Key.Quest; });
		if (!State) return nullptr;
		if (Key.Objective.IsNone()) return State->Objectives.IsEmpty() ? &State->Count : nullptr;
		auto* Objective = State->Objectives.FindByPredicate([&](const auto& Value) { return Value.ObjectiveId == Key.Objective; });
		return Objective ? &Objective->Count : nullptr;
	}
	static int32 ReadCounter(const FProject_JQuestSnapshot& Quests, const FCounterKey& Key)
	{
		const auto* State = Quests.States.FindByPredicate([&](const auto& Value) { return Value.QuestId == Key.Quest; });
		if (!State) return 0;
		return Key.Objective.IsNone() ? State->Count : State->GetObjectiveCount(Key.Objective);
	}
	void Fault(ECommitStatus Status)
	{
		FScopeLock Lock(&Mutex);
		LastResult = Status;
		bFaulted = true;
		bAccepting = false;
		Pending.Reset();
	}
	FProjection FinishWriter(FGuid Ticket)
	{
		check(!IsInGameThread());
		TMap<FCounterKey, FCounterGoal> Captured;
		FProfileValues Baseline;
		int64 ExpectedLeaseRevision = 0, ExpectedProfileRevision = 0;
		{
			FScopeLock Lock(&Mutex);
			bProgressOpen = false; // One frozen tail only; later ingress must explicitly retry.
			Baseline = ConfirmedValues;
			ExpectedLeaseRevision = LeaseRevision;
			ExpectedProfileRevision = ProfileRevision;
			if (!bFaulted && BlockedProgressRevision != ProfileRevision)
				for (const auto& Pair : Progress)
					if (Pair.Value.Desired > Pair.Value.Confirmed) Captured.Add(Pair.Key, Pair.Value);
		}
		if (!Baseline && !Captured.IsEmpty())
		{
			FScopeLock Lock(&Mutex);
			BlockedProgressRevision = ProfileRevision;
		}
		if (Baseline && !Captured.IsEmpty())
		{
			auto Proposed = *Baseline;
			bool bValid = true, bChanged = false;
			for (const auto& Pair : Captured)
			{
				int32* Counter = FindCounter(Proposed.Quests, Pair.Key);
				if (!Counter || *Counter < 0 || *Counter > Pair.Value.Required) { bValid = false; break; }
				const int32 Desired = FMath::Max(*Counter, Pair.Value.Desired);
				bChanged |= Desired != *Counter;
				*Counter = Desired;
			}
			TArray<uint8> Payload;
			if (!bValid || (bChanged && !Proposed.Encode(Payload)))
			{
				FScopeLock Lock(&Mutex);
				BlockedProgressRevision = ProfileRevision; // No spin; a confirmed recovery revision may retry.
			}
			else
			{
				bool bConfirmed = !bChanged;
				if (bChanged)
				{
					FCommitRequest Request{FGuid::NewGuid(), TEXT("LocalProfileCheckpoint"),
						{{LeaseKey(Character), ExpectedLeaseRevision, 1, SessionBytes(Session), false},
						 {ProfileKey(Character), ExpectedProfileRevision, 1, MoveTemp(Payload), false}}};
					FString HistoryFailure;
					FCommitResult Result;
					if (ProjectJ::LocalProfileHistory::PrepareHistoryMutations(*Repository, Character, *Baseline, Proposed, Request, HistoryFailure))
						Result = Repository->CommitProfile(Request, {Character, Session, 0});
					if (Result.Status == ECommitStatus::UnknownOutcome) Result = Repository->CommitProfile(Request, {Character, Session, 0});
					FRecord Record, Lease;
					bConfirmed = Accepted(Result.Status) && Repository->Read(ProfileKey(Character), Record) &&
						Repository->Read(LeaseKey(Character), Lease) && !Record.bDeleted && !Lease.bDeleted &&
						Record.SchemaVersion == 1 && Lease.SchemaVersion == 1 && Lease.Payload == SessionBytes(Session);
					const FProfileValues Values = bConfirmed ? DecodeProfileValues(*Repository, Character, Record.Payload, Baseline, Record.Revision) : FProfileValues();
					bConfirmed &= Values.IsValid();
					if (bConfirmed)
					{
						FScopeLock Lock(&Mutex);
						ConfirmedValues = Values;
						LastQueued = MoveTemp(Record.Payload);
						ProfileRevision = Record.Revision;
						LeaseRevision = Lease.Revision;
					}
					else if (Result.Status == ECommitStatus::Invalid)
					{
						FScopeLock Lock(&Mutex);
						BlockedProgressRevision = ProfileRevision;
						}
					else Fault(Accepted(Result.Status) ? ECommitStatus::Conflict : Result.Status);
					FProject_JRepositoryUsage CurrentUsage;
					const bool Known = Repository->GetUsage(CurrentUsage);
					FScopeLock Lock(&Mutex);
					Usage = CurrentUsage;
					bUsageKnown = Known;
				}
				if (bConfirmed)
				{
					FScopeLock Lock(&Mutex);
					BlockedProgressRevision = -1;
					for (const auto& Pair : Captured)
						if (auto* Goal = Progress.Find(Pair.Key))
						{
							Goal->Confirmed = FMath::Max(Goal->Confirmed, ReadCounter(ConfirmedValues->Quests, Pair.Key));
							Goal->Desired = FMath::Max(Goal->Desired, Goal->Confirmed);
						}
				}
			}
		}
		Repository->Close();
		FScopeLock Lock(&Mutex);
		bRunning = false;
		Gate->Release(Ticket);
		return {ConfirmedValues, ProfileRevision};
	}
	FProjection Drain(FGuid Ticket)
	{
		check(!IsInGameThread());
		for (;;)
		{
			TOptional<FProject_JLocalProfileSnapshot> Snapshot;
			{
				FScopeLock Lock(&Mutex);
				if (Pending.IsSet()) { Snapshot = MoveTemp(Pending.GetValue()); Pending.Reset(); }
				else { bProgressOpen = false; break; }
				for (const auto& Pair : Progress)
					if (int32* Counter = FindCounter(Snapshot->Quests, Pair.Key)) *Counter = FMath::Max(*Counter, Pair.Value.Confirmed);
			}
			TArray<uint8> Payload;
			int64 ExpectedLeaseRevision = 0, ExpectedProfileRevision = 0;
			FProfileValues HistoryBaseline;
			bool bEncoded = false;
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_ProfileCheckpointEncode);
				bEncoded = Snapshot->Encode(Payload);
			}
			if (!bEncoded) { Fault(ECommitStatus::Invalid); break; }
			{
				FScopeLock Lock(&Mutex);
				if (LastQueued == Payload) continue;
				ExpectedLeaseRevision = LeaseRevision;
				ExpectedProfileRevision = ProfileRevision;
				HistoryBaseline = ConfirmedValues;
			}
			FCommitRequest Request{FGuid::NewGuid(), TEXT("LocalProfileCheckpoint"),
				{{LeaseKey(Character), ExpectedLeaseRevision, 1, SessionBytes(Session), false},
				 {ProfileKey(Character), ExpectedProfileRevision, 1, MoveTemp(Payload), false}}};
			FProject_JLocalProfileSnapshot EmptyBefore;
			EmptyBefore.Account = Snapshot->Account; EmptyBefore.Character = Character;
			FString HistoryFailure;
			if (!ProjectJ::LocalProfileHistory::PrepareHistoryMutations(*Repository, Character,
				HistoryBaseline ? *HistoryBaseline : EmptyBefore, *Snapshot, Request, HistoryFailure))
			{ Fault(ECommitStatus::Invalid); break; }
			auto Result = Repository->CommitProfile(Request, {Character, Session, 0});
			if (Result.Status == ECommitStatus::UnknownOutcome) Result = Repository->CommitProfile(Request, {Character, Session, 0});
			if (!Accepted(Result.Status) || Result.Revisions.Num() != Request.Mutations.Num()) { Fault(Result.Status); break; }
			FProject_JRepositoryUsage CurrentUsage;
			const bool Known = Repository->GetUsage(CurrentUsage);
			FScopeLock Lock(&Mutex);
			LastResult = Result.Status;
			Usage = CurrentUsage;
			bUsageKnown = Known;
			LeaseRevision = Result.Revisions[0];
			ProfileRevision = Result.Revisions[1];
			LastQueued = MoveTemp(Request.Mutations[1].Payload);
			ConfirmedValues = MakeShared<FProject_JLocalProfileSnapshot, ESPMode::ThreadSafe>(MoveTemp(Snapshot.GetValue()));
		}
		return FinishWriter(Ticket);
	}
};


struct FProject_JLocalProfileBinding
{
	TWeakObjectPtr<AProject_JPlayerState> Player;
	ProjectJ::MMO::FServerSessionLease PlayerSession;
	FGuid Account, Character;
	TSharedPtr<FProject_JLocalProfileWork, ESPMode::ThreadSafe> Work;
	TFunction<void(bool)> OnReady;
	FDelegateHandle ProgressionChanged, QuestChanged, IdentityChanged;
	bool bLoaded = false, bLoadSucceeded = false, bReady = false, bRetired = false;
	bool bCommandPending = false, bCommandObserved = false;
	int64 AppliedRevision = 0;
	uint32 NextRequest = 0, RetiredRequestThrough = 0;
	// GT-owned session observation: rejected admissions are not retained or automatically replayed.
	uint64 RejectedProgressAdmissions = 0;
	FProject_JPersistentCompletion CommandComplete;
	FString LastAdmissionReason;
	TArray<uint8> LoadedPayload;
	FProfileValues LoadedValues;
	ProjectJ::LocalProfileRules::FQuestGuard QuestRules;
};

namespace
{
bool RequireQuestRules(const TSharedPtr<FProject_JLocalProfileBinding, ESPMode::ThreadSafe>& B,
                       const FProject_JQuestDefinitionIndex& Index)
{
    const auto* Player = B->Player.Get();
    const auto* Data = Player && Player->GetGameInstance() ? Player->GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>() : nullptr;
    if (B->QuestRules.IsCurrent(Index, Data)) return true;
    B->bReady = false;
    B->LastAdmissionReason = TEXT("저장된 퀘스트 규칙 변경/삭제: 기존 저장 보존, 명시적 이관 필요");
    B->Work->Seal();
    return false;
}
}

/** GT-only transient open, retaining the original authority epoch and completion. */
struct FProject_JPendingTrustedProfileOpen
{
    TWeakObjectPtr<AProject_JPlayerState> Player;
    FServerSessionLease Lease;
    TFunction<void(bool)> OnReady;
};
void UProject_JLocalProfileSubsystem::RetryPendingTrustedOpens()
{
    check(IsInGameThread());
    if (bClosing || PendingTrustedOpens.IsEmpty()) return;
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    // Remove each exact request before retry: failed authority never migrates to a new lease,
    // and another busy writer may requeue it without an immediate retry loop.
    TArray<TSharedPtr<FProject_JPendingTrustedProfileOpen>> Pending;
    PendingTrustedOpens.GenerateValueArray(Pending);
    for (const auto& Open : Pending)
    {
        if (bClosing) break;
        const auto* Current = PendingTrustedOpens.Find(Open->Player);
        if (!Current || *Current != Open) continue;
        PendingTrustedOpens.Remove(Open->Player);
        auto* Player = Open->Player.Get();
        OpenResolvedProfile(Player, Open->Lease.AccountId, Open->Lease.CharacterId, Open->Lease, true, MoveTemp(Open->OnReady));
    }
}

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
    PendingTrustedOpens.Reset(); // Retire callbacks; shutdown must not create new gameplay work.
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
		   Player->GetCharacterId().Value == B->Character && Player->IsServerSessionCurrent(B->PlayerSession) && Found && *Found == B;
}
bool UProject_JLocalProfileSubsystem::IsTrustedDevelopmentEnabled()
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return FParse::Param(FCommandLine::Get(), TEXT("ProjectJDevelopmentServerProfiles"));
#endif
}
void UProject_JLocalProfileSubsystem::OpenTrustedDevelopmentProfile(AProject_JPlayerState* Player,
	TFunction<void(bool)> OnReady)
{
	check(IsInGameThread());
	const auto Lease = IsValid(Player) ? Player->CaptureServerSession() : FServerSessionLease{};
	if (!IsTrustedDevelopmentEnabled() || !Lease.IsValid() || !Player->IsServerSessionCurrent(Lease))
	{ if (OnReady) OnReady(false); return; }
	// A replaced native session must not leave an inert binding that blocks its
	// successor forever. Accepted I/O still owns its gate until it drains.
	TArray<TWeakObjectPtr<AProject_JPlayerState>> Retired;
	for (const auto& Pair : Bindings)
		if (Pair.Value->Character == Lease.CharacterId && !IsCurrent(Pair.Value)) Retired.Add(Pair.Key);
	for (const auto& Old : Retired) Detach(Old.Get());
	OpenResolvedProfile(Player, Lease.AccountId, Lease.CharacterId, Lease, true, MoveTemp(OnReady));
}
void UProject_JLocalProfileSubsystem::OpenProfile(AProject_JPlayerState* Player, const FString& Profile,
	TFunction<void(bool)> OnReady)
{
	check(IsInGameThread());
	FGuid Account, Character;
	if (!ResolveLocalIdentity(Profile, Account, Character)) { if (OnReady) OnReady(false); return; }
	OpenResolvedProfile(Player, Account, Character, IsValid(Player) ? Player->CaptureServerSession() : FServerSessionLease{},
		false, MoveTemp(OnReady));
}
void UProject_JLocalProfileSubsystem::OpenResolvedProfile(AProject_JPlayerState* Player, FGuid Account, FGuid Character,
	const FServerSessionLease& ExpectedSession, bool bTrustedDevelopment, TFunction<void(bool)> OnReady)
{
	check(IsInGameThread());
	const auto IdentityCurrent = [&]
	{
		return IsValid(Player) && Player->GetWorld() &&
			(bTrustedDevelopment
				? IsTrustedDevelopmentEnabled() && ExpectedSession.IsValid() && Player->IsServerSessionCurrent(ExpectedSession)
					&& Player->GetAccountId().Value == Account && Player->GetCharacterId().Value == Character
				: Player->GetWorld()->GetNetMode() == NM_Standalone && !ExpectedSession.IsValid()
					&& Player->IsServerSessionCurrent(ExpectedSession));
	};
	if (!OnReady) return;
	if (bClosing || !IOGate || !IdentityCurrent() || !Player->HasAuthority() || Player->IsActorBeingDestroyed() ||
		Player->GetGameInstance() != GetGameInstance() || Player->GetPawn() ||
		!Player->FindComponentByClass<UProject_JProgressionComponent>() ||
		Player->FindComponentByClass<UProject_JProgressionComponent>()->GetState().Revision != 0 ||
		Bindings.Num() + PendingTrustedOpens.Num() >= 32 || Bindings.Contains(Player) || PendingTrustedOpens.Contains(Player))
	{ OnReady(false); return; }

	for (const auto &Pair : Bindings)
		if (Pair.Value->Character == Character)
		{
			OnReady(false);
			return;
		} // Reject duplicate active local profiles.
	auto *Definitions = GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>();
	// GI bootstrap owns catalog refresh; opening a profile consumes its current publication.
	if (!Definitions || !Definitions->EnsurePrimaryAssetDefinitions())
	{
		OnReady(false);
		return;
	}
	const TStrongObjectPtr<UProject_JLocalProfileSubsystem> PreparingSelf(this);
	const TStrongObjectPtr<UGameInstance> PreparingInstance(GetGameInstance());
	const TStrongObjectPtr<UWorld> PreparingWorld(Player->GetWorld());
	const TStrongObjectPtr<AProject_JPlayerState> PreparingPlayer(Player);
	auto* PreparingQuests = Player->FindComponentByClass<UProject_JQuestComponent>();
	if (!PreparingQuests || !PreparingQuests->PrepareForProfile() || bClosing || !IOGate || !IsValid(Player) ||
		!Player->HasAuthority() || Player->IsActorBeingDestroyed() || !IsValid(PreparingWorld.Get()) ||
		PreparingWorld->bIsTearingDown || Player->GetWorld() != PreparingWorld.Get() ||
		!IdentityCurrent() || GetGameInstance() != PreparingInstance.Get() ||
		Player->GetGameInstance() != PreparingInstance.Get() || Player->GetPawn() ||
		Player->FindComponentByClass<UProject_JQuestComponent>() != PreparingQuests ||
		!Player->FindComponentByClass<UProject_JProgressionComponent>() ||
		Player->FindComponentByClass<UProject_JProgressionComponent>()->GetState().Revision != 0 ||
		Bindings.Num() + PendingTrustedOpens.Num() >= 32 || Bindings.Contains(Player) || PendingTrustedOpens.Contains(Player))
	{
		OnReady(false); return;
	}
	// Default acceptance publishes synchronously. A callback may have opened the
	// same identity through another PlayerState since the original admission.
	for (const auto& Pair : Bindings)
		if (Pair.Value->Character == Character) { OnReady(false); return; }
	FGuid Ticket;
    const auto Admission = IOGate->TryAcquire({ProfileKey(Character)}, Ticket);
    if (Admission != EAdmission::Accepted)
    {
        if (bTrustedDevelopment && Admission == EAdmission::Busy)
        {
            auto Pending = MakeShared<FProject_JPendingTrustedProfileOpen>();
            Pending->Player = Player; Pending->Lease = ExpectedSession; Pending->OnReady = MoveTemp(OnReady);
            PendingTrustedOpens.Add(Player, MoveTemp(Pending));
            // Reuse the normal command wiring with an intentionally retired capability.
            // Its weak delegates deny all edits/progress until a real binding replaces them;
            // retirement keeps denial installed rather than reopening prototype mutation.
            auto Fence = MakeShared<FProject_JLocalProfileBinding, ESPMode::ThreadSafe>();
            Fence->Player = Player; Fence->bRetired = true;
            BindPersistentCommands(Fence);
            // Every profile writer posts a GT drain notification after releasing its gate.
            // No callback failure, default spawn, timer, or fresh authority is inferred here.
            return;
        }
        OnReady(false); return;
    }
	auto B = MakeShared<FProject_JLocalProfileBinding, ESPMode::ThreadSafe>();
	B->Player = Player;
	B->PlayerSession = ExpectedSession;
	B->Account = Account;
	B->Character = Character;
	B->OnReady = MoveTemp(OnReady);
	auto Work = MakeShared<FProject_JLocalProfileWork, ESPMode::ThreadSafe>();
	// Developer profile storage can hold a complete 1024-page quest archive plus
	// its core/lease. This record budget is not a production player capacity.
	const FString StorePath = bTrustedDevelopment
		? FPaths::Combine(FPaths::GetPath(DatabasePath), TEXT("DevelopmentServerProfiles.sqlite")) : DatabasePath;
	Work->Repository = MakeShared<FProject_JSQLiteRepository, ESPMode::ThreadSafe>(StorePath, 4096);
	Work->Gate = IOGate;
	Work->Character = Character;
	Work->Session = FGuid::NewGuid();
	B->Work = Work;
	Bindings.Add(Player, B);
	if (!bTrustedDevelopment) Player->SetIdentity(FProject_JAccountId{Account}, FProject_JCharacterId{Character});
	if (!IsCurrent(B))
	{
		Detach(Player);
		IOGate->Release(Ticket);
		return;
	}
	// Close network mutations while the worker loads. The current ready flag gates
	// existing live writers; no parallel server-only inventory/progression exists.
	if (bTrustedDevelopment) BindPersistentCommands(B);
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
		// Parsing is part of the admitted native work, before returning its slot.
		const FProfileValues Values = Success && !Record.Payload.IsEmpty()
			? DecodeProfileValues(*Work->Repository, Work->Character, Record.Payload, {}, Record.Revision) : FProfileValues();
		if (Success && !Record.Payload.IsEmpty() && !Values)
		{
			Success = false;
			Error = TEXT("프로필 또는 완료 이력의 형식/참조가 호환되지 않습니다. 저장 원형을 보존하세요");
		}
		if (Values)
		{
			FScopeLock Lock(&Work->Mutex);
			Work->ConfirmedValues = Values;
			Work->LastQueued = Record.Payload;
		}
		Work->Repository->Close();
		Work->Gate->Release(Ticket);
		AsyncTask(ENamedThreads::GameThread,
				  [WeakThis, B, Success, Values, Error = MoveTemp(Error), Payload = MoveTemp(Record.Payload)]() mutable {
                      if (auto* Self = WeakThis.Get()) Self->RetryPendingTrustedOpens();
					  if (auto *Self = WeakThis.Get(); Self && Self->IsCurrent(B))
					  {
						  B->bLoaded = true;
						  B->bLoadSucceeded = Success;
						  B->LastAdmissionReason = MoveTemp(Error);
						  B->LoadedPayload = MoveTemp(Payload);
						  B->LoadedValues = Values;
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
	check(IsInGameThread());
	if (!IsCurrent(B) || !B->bLoaded || !B->OnReady || !B->Player->HasActorBegunPlay())
		return;
	auto *Player = B->Player.Get();
	auto *P = Player->FindComponentByClass<UProject_JProgressionComponent>();
	auto *Q = Player->FindComponentByClass<UProject_JQuestComponent>();
	const TStrongObjectPtr<UProject_JLocalProfileSubsystem> KeepSelf(this);
	const TStrongObjectPtr<AProject_JPlayerState> KeepPlayer(Player);
	const TStrongObjectPtr<UProject_JProgressionComponent> KeepProgress(P);
	const TStrongObjectPtr<UProject_JQuestComponent> KeepQuests(Q);
	const TStrongObjectPtr<UProject_JCraftingJobsComponent> KeepJobs(Player->GetCraftingJobsComponent());
	const TStrongObjectPtr<UProject_JInventoryComponent> KeepInventory(Player->GetInventoryComponent());
	const TStrongObjectPtr<UProject_JEquipmentManagerComponent> KeepEquipment(Player->GetEquipmentManagerComponent());
	FProject_JQuestDefinitionIndex QuestIndex;
	const bool QuestsPrepared = Q && Q->PrepareForProfile();
	if (QuestsPrepared) Q->BuildDefinitionIndex(QuestIndex);
	bool Success = B->bLoadSucceeded && P && Q && QuestsPrepared && !Player->GetPawn() && P->GetState().Revision == 0;
	if (Success && !B->LoadedPayload.IsEmpty() && !B->LoadedValues)
	{
		Success = false;
		if (B->LastAdmissionReason.IsEmpty()) B->LastAdmissionReason = TEXT("프로필 JSON 형식/범위 오류: 원형 보존 필요");
	}
	if (Success && B->LoadedValues)
	{
		const FProfileValues Values = B->LoadedValues;
		const auto& Snapshot = *Values;
		UProject_JCharacterClassDefinition *Class = nullptr;
		TArray<UProject_JCharacterAdvancementDefinition *> History;
		TArray<FProject_JItemInstanceData> Bag;
		TArray<FProject_JEquipmentArrayItem> Slots;
		ProjectJ::LocalProfileRules::FQuestGuard LoadedRules;
		Success = Snapshot.Account == B->Account && Snapshot.Character == B->Character &&
				  ResolveDefinitions(GetGameInstance(), Snapshot.Progression, Class, History) &&
				  Matches(Snapshot, *P, QuestIndex, Class, History) &&
				  P->CanRestoreSnapshot(Snapshot.Progression, Class, History) &&
				  Q->CanRestoreSnapshot(Snapshot.Quests, true) &&
				  ResolveItems(Player, Snapshot, Bag, Slots, false, &B->LastAdmissionReason) &&
				  LoadedRules.Capture(QuestIndex, Snapshot.Quests, GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>());
		if (!Success && B->LastAdmissionReason.IsEmpty())
			B->LastAdmissionReason =
				Snapshot.Format == 1
					? TEXT("진행도 전용 v1 프로필: 인벤토리 없는 상태를 추측하지 않습니다. 명시적 v2 이관 필요")
					: TEXT("프로필의 정의·규칙 버전·범위·소유권 검증 실패: 기존 저장을 보존했습니다");
		if (Success)
		{
			const auto ParticipantsCurrent = [&]
			{
				const auto Live = [&](const UActorComponent* Component)
				{
					return IsValid(Component) && !Component->IsBeingDestroyed() && Component->IsRegistered() &&
						Component->GetOwner() == Player && Component->GetWorld() == Player->GetWorld();
				};
				return IsCurrent(B) && B->Player.Get() == Player && Live(P) && Live(Q) &&
					Live(KeepInventory.Get()) && Live(KeepEquipment.Get()) && Live(KeepJobs.Get()) &&
					Player->FindComponentByClass<UProject_JProgressionComponent>() == P &&
					Player->FindComponentByClass<UProject_JQuestComponent>() == Q &&
					Player->GetInventoryComponent() == KeepInventory.Get() &&
					Player->GetEquipmentManagerComponent() == KeepEquipment.Get() &&
					Player->GetCraftingJobsComponent() == KeepJobs.Get();
			};
			const int64 PreviousAppliedRevision = B->AppliedRevision;
			const int64 ProjectionRevision = B->Work->Revision();
			if (!IsCurrent(B)) return; // A preflight getter may have retired this binding before policy installation.
			// Loading an existing owner has no bootstrap mutation window. Admission stays
			// closed through all initial notifications; trusted installers do no I/O.
			BindPersistentCommands(B);
			Player->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(Player, nullptr);
			Bag.Reset();
			Slots.Reset();
			Success = ParticipantsCurrent() && !Player->GetPawn() &&
					  P->ValidateCommittedProgression(Snapshot.Progression, Class, History, true, true) &&
					  Q->ValidateCommittedQuests(Snapshot.Quests, true) &&
					  ResolveItems(Player, Snapshot, Bag, Slots, true, &B->LastAdmissionReason);
			if (Success)
			{
				auto *I = KeepInventory.Get();
				auto *E = KeepEquipment.Get();
				const auto OldBag = I->GetItemInstances();
				const auto OldSlots = E->CapturePersistentEquipment();
				const auto CanInstall = [&]
				{
					if (!ParticipantsCurrent() || Player->GetPawn() || B->AppliedRevision != PreviousAppliedRevision) return false;
					FProject_JQuestDefinitionIndex CurrentIndex;
					Q->BuildDefinitionIndex(CurrentIndex);
					return Matches(Snapshot, *P, CurrentIndex, Class, History) &&
						LoadedRules.IsCurrent(CurrentIndex, GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>()) &&
						Q->ValidateCommittedQuests(Snapshot.Quests, true) && I->ValidatePersistentItems(Bag, true) &&
						E->ValidatePersistentEquipment(Slots, Bag, true) && KeepJobs->ValidateCommittedJobs(Snapshot.CraftingJobs);
				};
				bool bProgressionChanged = false;
				// The installer checks again after its last extensible ASC getter, before any value writes.
				Success = P->TryInstallCommittedProgression(Snapshot.Progression, Class, History, CanInstall, bProgressionChanged);
				if (Success)
				{
					B->QuestRules = MoveTemp(LoadedRules);
					Q->InstallCommittedQuests(Snapshot.Quests);
					I->InstallPersistentItems(Bag);
					E->InstallPersistentEquipment(Slots);
					KeepJobs->InstallCommittedJobs(Snapshot.CraftingJobs);
					E->bPersistentProfileBound = true;
					B->AppliedRevision = ProjectionRevision;
					const auto CanPublish = [&] { return ParticipantsCurrent() && B->AppliedRevision == ProjectionRevision; };
					P->PublishCommittedExperience(CanPublish);
					if (CanPublish())
						Q->PublishCommittedQuests();
					if (CanPublish())
						I->PublishPersistentItems(OldBag, CanPublish);
					if (CanPublish())
						E->PublishPersistentEquipment(OldSlots);
					if (CanPublish()) KeepJobs->BroadcastCommittedJobs();
				}
			}
		}
	}
	if (!IsCurrent(B))
		return;
	if (Success && !B->LoadedValues)
		Success = B->QuestRules.Capture(QuestIndex, Q->CaptureSnapshot(), GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>());
	B->AppliedRevision = B->Work->Revision();
	B->bReady = Success;
	// An unchanged loaded profile needs no second receipt merely for spawning.
	if (Success && !B->LoadedPayload.IsEmpty())
	{
		FScopeLock Lock(&B->Work->Mutex);
		B->Work->LastQueued = MoveTemp(B->LoadedPayload);
	}
	if (Success)
	{
		ObserveProfileChanges(B);
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
	B->LoadedValues.Reset();
	Callback(Success);
	if (Success && IsCurrent(B))
		Player->GetEquipmentManagerComponent()->bPersistentProfileBound = true;
	if (Success && IsCurrent(B))
		BindPersistentCommands(B);
	if (Success && IsCurrent(B))
		RequestCheckpoint(Player);
    if (Success && IsCurrent(B) && B->PlayerSession.IsValid())
        if (auto* Registry = Player->GetWorld()->GetSubsystem<UProject_JWorldEventRegistry>())
            Registry->NotifyTrustedProfileReady(Player);
}
void UProject_JLocalProfileSubsystem::ObserveProfileChanges(const TSharedPtr<FProject_JLocalProfileBinding, ESPMode::ThreadSafe>& B)
{
	TWeakObjectPtr<UProject_JLocalProfileSubsystem> WeakThis(this);
	TWeakPtr<FProject_JLocalProfileBinding, ESPMode::ThreadSafe> WeakB(B);
	auto Changed = [WeakThis, WeakB]() {
		if (auto* Self = WeakThis.Get())
			if (auto Current = WeakB.Pin(); Self->IsCurrent(Current)) Self->RequestCheckpoint(Current->Player.Get());
	};
	B->ProgressionChanged = B->Player->FindComponentByClass<UProject_JProgressionComponent>()->OnChanged.AddWeakLambda(this, Changed);
	B->QuestChanged = B->Player->FindComponentByClass<UProject_JQuestComponent>()->OnChanged.AddWeakLambda(this, Changed);
}
bool UProject_JLocalProfileSubsystem::CanRenewTrustedProfile(AProject_JPlayerState* Player) const
{
	check(IsInGameThread());
	const auto* Found = Bindings.Find(Player);
	if (!Found) return true;
	const auto B = *Found;
	return IsCurrent(B) && B->PlayerSession.IsValid() && B->bReady && !B->OnReady &&
		B->Work->Healthy() && !IsCheckpointPending(Player) && B->AppliedRevision > 0;
}
bool UProject_JLocalProfileSubsystem::RenewTrustedProfile(AProject_JPlayerState* Player,
	const FServerSessionLease& Previous, const FServerSessionLease& Current)
{
	check(IsInGameThread());
	const auto* Found = Bindings.Find(Player);
	if (!Found) return true;
	const auto Old = *Found;
	auto* Progression = IsValid(Player) ? Player->FindComponentByClass<UProject_JProgressionComponent>() : nullptr;
	auto* Quests = IsValid(Player) ? Player->FindComponentByClass<UProject_JQuestComponent>() : nullptr;
	if (!IsValid(Progression) || !IsValid(Quests) || Progression->IsBeingDestroyed() || Quests->IsBeingDestroyed()) return false;
	if (bClosing || !IsValid(Player) || Player->IsActorBeingDestroyed() || !Player->HasAuthority() ||
		Player->GetGameInstance() != GetGameInstance() || !Player->GetWorld() || Player->GetWorld()->bIsTearingDown ||
		!Previous.IsValid() || !Current.IsValid() || Previous == Current ||
		Previous.AccountId != Current.AccountId || Previous.CharacterId != Current.CharacterId ||
		!(Old->PlayerSession == Previous) || Old->bRetired || !Old->bReady || Old->OnReady ||
		!Player->IsServerSessionCurrent(Current) || Player->GetAccountId().Value != Old->Account ||
		Player->GetCharacterId().Value != Old->Character || !Old->Work->Healthy() || IsCheckpointPending(Player) ||
		Old->AppliedRevision <= 0) return false;
	// Nothing is copied into gameplay. The previous binding can never become current
	// again, even if a delayed native delegate retained its weak binding pointer.
	auto B = MakeShared<FProject_JLocalProfileBinding, ESPMode::ThreadSafe>();
	B->Player = Player; B->PlayerSession = Current; B->Account = Old->Account; B->Character = Old->Character;
	B->Work = Old->Work; B->bLoaded = B->bLoadSucceeded = B->bReady = true;
	B->AppliedRevision = Old->AppliedRevision; B->NextRequest = Old->NextRequest;
	B->RetiredRequestThrough = Old->NextRequest; B->QuestRules = Old->QuestRules;
	B->IdentityChanged = Old->IdentityChanged;
	Old->bRetired = true;
	Player->FindComponentByClass<UProject_JProgressionComponent>()->OnChanged.Remove(Old->ProgressionChanged);
	Player->FindComponentByClass<UProject_JQuestComponent>()->OnChanged.Remove(Old->QuestChanged);
	Bindings.Add(Player, B);
	BindPersistentCommands(B);
	ObserveProfileChanges(B);
	return IsCurrent(B);
}
void UProject_JLocalProfileSubsystem::RequestCheckpoint(AProject_JPlayerState *Player)
{
	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_ProfileCheckpointCapture);
	auto *Found = Bindings.Find(Player);
	if (!Found || !IsCurrent(*Found) || !(*Found)->bReady || (*Found)->bCommandPending ||
		(*Found)->Work->Revision() != (*Found)->AppliedRevision) return;
	auto B = *Found;
	auto *P = Player->FindComponentByClass<UProject_JProgressionComponent>();
	auto *Q = Player->FindComponentByClass<UProject_JQuestComponent>();
	if (!P || !Q || P->GetState().Revision == 0)
		return; // Pawn defaults must initialize a new profile first.
	FProject_JQuestDefinitionIndex QuestIndex;
	Q->BuildDefinitionIndex(QuestIndex);
	if (!RequireQuestRules(B, QuestIndex)) return;
	ProjectJ::LocalProfileRules::FQuestGuard CapturedRules;
	FProject_JLocalProfileSnapshot Snapshot;
	Snapshot.Account = B->Account;
	Snapshot.Character = B->Character;
	Snapshot.Progression = P->CaptureSnapshot();
	Snapshot.Quests = Q->CaptureSnapshot();
	UProject_JCharacterClassDefinition *Class = nullptr;
	TArray<UProject_JCharacterAdvancementDefinition *> History;
	// Verify the old accepted rules/states before dropping any claimed details.
	if (!Q->ValidateCommittedQuests(Snapshot.Quests) ||
		!UProject_JQuestComponent::NormalizeClaimed(Snapshot.Quests) ||
		!ResolveDefinitions(GetGameInstance(), Snapshot.Progression, Class, History) ||
		P->GetState().ClassDefinition != Class ||
		P->GetState().Advancement != (History.IsEmpty() ? nullptr : History.Last()) ||
		(Snapshot.RulesFingerprint = Fingerprint(*P, QuestIndex, Class, History, &Snapshot.Quests)).IsEmpty() ||
		!CapturedRules.Capture(QuestIndex, Snapshot.Quests, GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>()) ||
		!CaptureItems(Player, Snapshot))
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
		if (!Work->bAccepting || Work->bFaulted || (Work->bRunning && !Work->bProgressOpen)) return;
		if (!Work->bRunning && Work->Gate->TryAcquire({ProfileKey(B->Character)}, Ticket) != EAdmission::Accepted)
		{
			B->bReady = false;
			UE_LOG(LogProjectJLocalProfile, Error,
				   TEXT("Local checkpoint admission unavailable; checkpoint not confirmed."));
			return;
		}
		B->QuestRules = MoveTemp(CapturedRules);
		Work->Pending = MoveTemp(Snapshot);
		if (Work->bRunning)
			return;
		Work->bRunning = Work->bProgressOpen = true;
	}
	TWeakObjectPtr<UProject_JLocalProfileSubsystem> WeakThis(this);
	TWeakPtr<FProject_JLocalProfileBinding, ESPMode::ThreadSafe> WeakB(B);
	Jobs.RemoveAll([](const auto &Job) { return Job.IsReady(); });
	Jobs.Add(Async(EAsyncExecution::ThreadPool, [Work, Ticket, WeakThis, WeakB] {
		auto Projection = Work->Drain(Ticket);
		AsyncTask(ENamedThreads::GameThread, [WeakThis, WeakB, Projection = MoveTemp(Projection)] {
            if (auto* Self = WeakThis.Get()) Self->RetryPendingTrustedOpens();
			if (auto* Self = WeakThis.Get())
				if (auto Current = WeakB.Pin(); Self->IsCurrent(Current))
					Self->CompleteProgress(Current, Projection.Values, Projection.Revision);
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
    PendingTrustedOpens.Remove(Player); // A disconnected/replaced owner cannot resume its deferred open.
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
    if (PendingTrustedOpens.Contains(const_cast<AProject_JPlayerState*>(Player))) return true;
	const auto *Found = Bindings.Find(const_cast<AProject_JPlayerState *>(Player));
	return Found && ((*Found)->bCommandPending || (*Found)->Work->PendingIO() ||
		((*Found)->Work->Healthy() && (*Found)->Work->Revision() != (*Found)->AppliedRevision));
}
int64 UProject_JLocalProfileSubsystem::GetCommittedRevision(const AProject_JPlayerState *Player) const
{
	const auto *Found = Bindings.Find(const_cast<AProject_JPlayerState *>(Player));
	return Found ? (*Found)->Work->Revision() : 0;
}
bool UProject_JLocalProfileSubsystem::QueryCommittedQuest(AProject_JPlayerState* Player, FName Quest, int64& OutRevision) const
{
	check(IsInGameThread());
	OutRevision = 0;
	const auto* Found = Bindings.Find(Player);
	if (Quest.IsNone() || !Found || !IsReady(Player) || IsCheckpointPending(Player)) return false;
	const auto B = *Found;
	const auto* Quests = Player->FindComponentByClass<UProject_JQuestComponent>();
	if (!Quests || !Quests->HasCompleted(Quest) || B->AppliedRevision <= 0) return false;
	OutRevision = B->AppliedRevision;
	return true;
}
FText UProject_JLocalProfileSubsystem::GetStatusText(const AProject_JPlayerState *Player) const
{
    if (PendingTrustedOpens.Contains(const_cast<AProject_JPlayerState*>(Player)))
        return FText::FromString(TEXT("이전 세션 저장 완료 대기 · 완료 후 현재 세션의 프로필을 자동으로 읽습니다"));
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
	if (Work->BlockedProgressRevision == Work->ProfileRevision)
		State += TEXT(" · 퀘스트 진척 저장 용량 확인 필요: 감소/병합 후 다시 시도");
	if (B.RejectedProgressAdmissions)
		State += FString::Printf(TEXT(" · 진척 미수락 요청 %llu회(현재 세션 누적·자동 재전송 없음)"), B.RejectedProgressAdmissions);
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
			   !Current->bCommandPending && Current->Work->Revision() == Current->AppliedRevision;
	};
	auto *Player = B->Player.Get();
	auto *P = Player->FindComponentByClass<UProject_JProgressionComponent>();
	auto *Q = Player->FindComponentByClass<UProject_JQuestComponent>();
	P->SubmitPersistentEdit = Submit;
	P->CreatePersistentRequest = CreateRequest;
	P->PersistentAdmission = Admission;
	Q->SubmitPersistentEdit = Submit;
	Q->SubmitPersistentProgress = [WeakThis, WeakB](const TArray<FProject_JQuestProgressInput>& Inputs) {
		auto* Self = WeakThis.Get();
		auto Current = WeakB.Pin();
		return Self && Self->QueueQuestProgress(Current, Inputs);
	};
	Q->CreatePersistentRequest = CreateRequest;
	Q->PersistentAdmission = Admission;
	Player->GetInventoryComponent()->SubmitPersistentEdit = Submit;
	Player->GetInventoryComponent()->CreatePersistentRequest = CreateRequest;
	Player->GetInventoryComponent()->PersistentAdmission = Admission;
	Player->GetEquipmentManagerComponent()->SubmitPersistentEdit = Submit;
	Player->GetEquipmentManagerComponent()->CreatePersistentRequest = CreateRequest;
	Player->GetEquipmentManagerComponent()->PersistentAdmission = Admission;
	Player->GetCraftingJobsComponent()->SubmitPersistentEdit = Submit;
	Player->GetCraftingJobsComponent()->CreatePersistentRequest = CreateRequest;
	Player->GetCraftingJobsComponent()->PersistentAdmission = Admission;
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
bool UProject_JLocalProfileSubsystem::SubmitWorldEventReward(AProject_JPlayerState* Player,
    const AProject_JWorldEncounter* Encounter, FGuid Round, FProject_JPersistentCompletion Complete,
    FProject_JProfileDurableCompletion OnDurable)
{
    check(IsInGameThread());
    int64 Experience = 0;
    if (!IsTrustedDevelopmentEnabled() || !IsValid(Player) || !IsValid(Encounter) || !Encounter->HasAuthority() ||
        !Player->HasAuthority() || Player->GetWorld() != Encounter->GetWorld() ||
        !Player->CaptureServerSession().IsValid() ||
        !Encounter->GetDevelopmentRewardTerms(Round, Player->CaptureServerSession().CharacterId, Experience)) return false;
    FProject_JPersistentEdit Edit;
    Edit.Kind = EProject_JPersistentEdit::WorldEventExperience; Edit.Instance = Round; Edit.Amount = Experience;
    Edit.Request = NewRequestId(Player);
    return SubmitEditInternal(Player, Edit, MoveTemp(Complete), MoveTemp(OnDurable), true);
}
bool UProject_JLocalProfileSubsystem::SubmitEdit(AProject_JPlayerState *Player, const FProject_JPersistentEdit &Edit,
												 FProject_JPersistentCompletion Complete, FProject_JProfileDurableCompletion OnDurable)
{
    return SubmitEditInternal(Player, Edit, MoveTemp(Complete), MoveTemp(OnDurable), false);
}
bool UProject_JLocalProfileSubsystem::SubmitEditInternal(AProject_JPlayerState* Player,
    const FProject_JPersistentEdit& Edit, FProject_JPersistentCompletion Complete,
    FProject_JProfileDurableCompletion OnDurable, bool bAuthorizedWorldEvent)
{
	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_ProfileCommandCapture);
    const bool bWorldEvent = Edit.Kind == EProject_JPersistentEdit::WorldEventExperience;
    if (bWorldEvent && (!bAuthorizedWorldEvent || !IsTrustedDevelopmentEnabled() || !Player || !Player->CaptureServerSession().IsValid() ||
        !Edit.Instance.IsValid() || Edit.Amount <= 0)) return false;
	if (Edit.Kind == EProject_JPersistentEdit::Craft || Edit.Kind == EProject_JPersistentEdit::StartCraftJob)
	{
		if (Edit.CraftingRecipe.IsNone() || Edit.CraftingRecipe.ToString().Len() > 64 ||
			Edit.CraftingRecipeVersion < 1 || Edit.CraftingBatches < 1 || Edit.CraftingBatches > 100 ||
			Edit.CraftingRulesDigest.Len() != 40) return false;
		for (TCHAR Value : Edit.CraftingRulesDigest) if (!FChar::IsHexDigit(Value)) return false;
	}
	if ((Edit.Kind == EProject_JPersistentEdit::CollectCraftJob || Edit.Kind == EProject_JPersistentEdit::CancelCraftJob) &&
		!Edit.CraftingJob.IsValid()) return false;
	auto *Found = Bindings.Find(Player);
	if (!Found || !IsCurrent(*Found) || !(*Found)->bReady || !Edit.Request.IsValid() || !(*Found)->Work->Healthy())
		return false;
	if ((*Found)->bCommandPending || (*Found)->Work->PendingIO() || (*Found)->Work->Revision() != (*Found)->AppliedRevision)
	{
		(*Found)->LastAdmissionReason = TEXT("이전 저장 확인 중: 새 변경을 수락하지 않음");
		return false;
	}
	auto B = *Found;
	auto Work = B->Work;
	if (Edit.Request.A != Work->Session.A || Edit.Request.B != Work->Session.B || Edit.Request.C != Work->Session.C ||
		Edit.Request.D == 0 || Edit.Request.D <= B->RetiredRequestThrough || Edit.Request.D > B->NextRequest)
	{
		B->LastAdmissionReason = TEXT("만료되었거나 발급되지 않은 요청: 현재 상태를 재동기화하세요");
		return false;
	}
	auto *P = Player->FindComponentByClass<UProject_JProgressionComponent>();
	auto *Q = Player->FindComponentByClass<UProject_JQuestComponent>();
	auto *I = Player->GetInventoryComponent();
	auto *E = Player->GetEquipmentManagerComponent();
	FProject_JLocalProfileSnapshot Before;
	TSet<FGuid> LockedInstances;
	Before.Account = B->Account;
	Before.Character = B->Character;
	Before.Progression = P->CaptureSnapshot();
	Before.Quests = Q->CaptureSnapshot();
	FProject_JQuestDefinitionIndex QuestIndex;
	Q->BuildDefinitionIndex(QuestIndex);
	if (!RequireQuestRules(B, QuestIndex)) return false;
	UProject_JCharacterClassDefinition *Class = nullptr;
	TArray<UProject_JCharacterAdvancementDefinition *> History;
	if (!ResolveDefinitions(GetGameInstance(), Before.Progression, Class, History) ||
		(Before.RulesFingerprint = Fingerprint(*P, QuestIndex, Class, History, &Before.Quests)).IsEmpty() ||
		!CaptureItems(Player, Before, (Edit.Kind == EProject_JPersistentEdit::Claim || Edit.Kind == EProject_JPersistentEdit::Craft || Edit.Kind == EProject_JPersistentEdit::Gather ||
			Edit.Kind == EProject_JPersistentEdit::StartCraftJob || Edit.Kind == EProject_JPersistentEdit::CollectCraftJob ||
			Edit.Kind == EProject_JPersistentEdit::CancelCraftJob) ? &LockedInstances : nullptr))
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
	if (Edit.Kind == EProject_JPersistentEdit::Craft || Edit.Kind == EProject_JPersistentEdit::StartCraftJob)
	{
		FString Recipe = Edit.CraftingRecipe.ToString().ToLower(), Rules = Edit.CraftingRulesDigest.ToLower();
		int32 RecipeVersion = Edit.CraftingRecipeVersion, Batches = Edit.CraftingBatches;
		W << Recipe << RecipeVersion << Batches << Rules;
	}
	if (Edit.Kind == EProject_JPersistentEdit::CollectCraftJob || Edit.Kind == EProject_JPersistentEdit::CancelCraftJob)
	{ FGuid JobId = Edit.CraftingJob; W << JobId; }
	if (Intent.Num() > 440)
		return false;
	const FName IntentOperation(*FString::Printf(TEXT("ProfileEdit.%d"), int32(Edit.Kind)));
	const FGuid Key = Edit.Request;
	const bool bCraft = Edit.Kind == EProject_JPersistentEdit::Craft;
	const bool bStartJob = Edit.Kind == EProject_JPersistentEdit::StartCraftJob;
	const bool bCollectJob = Edit.Kind == EProject_JPersistentEdit::CollectCraftJob;
	const bool bCancelJob = Edit.Kind == EProject_JPersistentEdit::CancelCraftJob;
	const bool bCraftPlan = bCraft || bStartJob || bCollectJob || bCancelJob;
	const auto CraftKind = Edit.Kind;
	const FGuid CraftJobId = bStartJob ? Key : Edit.CraftingJob;
	const FString CraftRulesDigest = Edit.CraftingRulesDigest.ToLower();
	const int32 CraftBatches = Edit.CraftingBatches;
	ProjectJ::Crafting::FRecipeValues CraftRecipe;
    TArray<ProjectJ::Crafting::FResolvedLine> ClaimRewards;
	auto Proposed = bCraftPlan ? FProject_JLocalProfileSnapshot{} : Before;
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
		if (Edit.Kind == K::Experience || Edit.Kind == K::WorldEventExperience)
			return P->PlanExperience(Edit.Amount, Proposed.Progression);
		if (Edit.Kind == K::AcceptCompoundQuest)
			return Q->PlanAcceptance(Edit.Quest, Proposed.Quests);
		if (Edit.Kind == K::SetQuestTracking)
			return (Edit.Amount == 0 || Edit.Amount == 1) &&
				UProject_JQuestComponent::PlanTracking(Edit.Quest, Edit.Amount == 1, Proposed.Quests);
		if (Edit.Kind == K::Claim)
		{
			auto *State =
				Proposed.Quests.States.FindByPredicate([&](const auto &S) { return S.QuestId == Edit.Quest; });
			const auto *Def = Q->FindDefinition(Edit.Quest);
			if (!State || !Def || State->bClaimed || !Def->IsComplete(*State))
				return false;
            if (!Def->CaptureRewardItems(GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>(), ClaimRewards)) return false;
			if (Def->RewardExperience > 0 && P->GetNextLevelExperience() > 0 &&
				!P->PlanExperience(Def->RewardExperience, Proposed.Progression))
				return false;
			return UProject_JQuestComponent::PlanClaimCompletion(Edit.Quest, Proposed.Quests);
		}
		if (Edit.Kind == K::Add || Edit.Kind == K::Gather)
		{
			auto *Data = GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>();
			const auto *Def = Edit.Definition;
			if (!Def || Data->GetItemDefinition(Def->ItemId, Def->SchemaVersion) != Def || !Edit.Instance.IsValid() ||
				Find(Edit.Instance) != INDEX_NONE || Edit.Amount < 1 || Edit.Amount > Def->MaxStackCount ||
				Edit.Level < 1 || Edit.Level > 10000)
				return false;
			int64 Remaining = Edit.Amount;
			// Gather is a definition-only grant. Add deliberately keeps its explicit
			// new-instance/OnItemAdded contract; restore/transfer/equipment are unchanged.
			if (Edit.Kind == K::Gather && Def->MaxStackCount > 1 && !Def->IsA<UProject_JEquipmentItemDefinition>()
				&& !Def->IsA<UProject_JCompanionItemDefinition>() && !Def->IsA<UProject_JMountItemDefinition>())
			{
				for (auto& Existing : Proposed.Items)
				{
					if (!Existing.bInBag || Existing.Slot != EProject_JEquipmentSlot::None || LockedInstances.Contains(Existing.Instance)
						|| Existing.Definition != Def->ItemId || Existing.DefinitionVersion != Def->SchemaVersion || Existing.Level != Edit.Level
						|| Existing.Count >= Def->MaxStackCount) continue;
					const int64 Taken = FMath::Min(Remaining, int64(Def->MaxStackCount) - Existing.Count);
					Existing.Count += int32(Taken); Remaining -= Taken;
					if (!Remaining) break;
				}
			}
			if (Remaining)
			{
				const int32 Order = NextOrder();
				if (Order == INDEX_NONE) return false; // Full merge does not allocate an order.
				FProject_JPersistentItem Item;
				Item.Instance = Edit.Instance; // The same server-issued intent GUID, never a retry allocation.
				Item.Definition = Def->ItemId;
				Item.DefinitionVersion = Def->SchemaVersion;
				Item.Level = Edit.Level;
				Item.Count = int32(Remaining);
				Item.Order = Order;
				Proposed.Items.Add(Item);
			}
			return Edit.Kind != K::Gather || Q->PlanActivityProgress(Proposed.Quests, EProject_JQuestObjectiveKind::GatherItem,
				Def->ItemId, Def->SchemaVersion, Edit.Amount, QuestIndex);
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
	TArray<FProject_JItemInstanceData> PlannedBag;
	TArray<FProject_JEquipmentArrayItem> PlannedSlots;
	UProject_JCharacterClassDefinition *PlannedClass = nullptr;
	TArray<UProject_JCharacterAdvancementDefinition *> PlannedHistory;
	bool Valid = false;
	if (bCraftPlan)
	{
		// GT captures definitions/progress; worker owns receipt-first UTC/planning.
		auto* Data = GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>();
		Proposed.Quests = Before.Quests;
		const bool bQuestBaselineValid = Q->ValidateCommittedQuests(Before.Quests)
			&& UProject_JQuestComponent::NormalizeClaimed(Proposed.Quests);
		if (bCraft || bStartJob)
		{
			const auto* Recipe = Data ? Data->GetCraftingRecipe(Edit.CraftingRecipe, Edit.CraftingRecipeVersion) : nullptr;
			FString Rules;
			Valid = Recipe && Recipe->Capture(*Data, CraftRecipe, Rules) && Rules == CraftRulesDigest &&
				(bStartJob ? CraftRecipe.DurationSecondsPerBatch > 0 : CraftRecipe.DurationSecondsPerBatch == 0);
			if (Valid && bCraft)
				Valid = Q->PlanActivityProgress(Proposed.Quests, EProject_JQuestObjectiveKind::CraftRecipe,
					Edit.CraftingRecipe, Edit.CraftingRecipeVersion, CraftBatches, QuestIndex);
		}
		else
		{
			const auto* Job = Before.CraftingJobs.FindByPredicate([&](const auto& Value) { return Value.Id == CraftJobId; });
			Valid = Data && Job;
			if (Valid && bCollectJob)
			{
				for (const auto& Output : Job->Outputs)
				{
					const auto* Def = Data->GetItemDefinition(Output.Definition, Output.DefinitionVersion);
					if (!Def || Def->MaxStackCount != Output.MaxStackCount) { Valid = false; break; }
				}
				if (Valid) Valid = Q->PlanActivityProgress(Proposed.Quests, EProject_JQuestObjectiveKind::CraftRecipe,
					Job->RecipeId, Job->RecipeVersion, Job->Batches, QuestIndex);
			}
		}
		Valid = Valid && bQuestBaselineValid && !(Proposed.RulesFingerprint = Fingerprint(*P, QuestIndex, Class, History, &Proposed.Quests)).IsEmpty()
			&& P->ValidateCommittedProgression(Before.Progression, Class, History) && Q->ValidateCommittedQuests(Proposed.Quests);
		// An invalid new plan never bypasses the receipt lookup on a valid baseline.
	}
	else Valid = Q->ValidateCommittedQuests(Before.Quests) &&
					   UProject_JQuestComponent::NormalizeClaimed(Proposed.Quests) && Plan() &&
					   ResolveDefinitions(GetGameInstance(), Proposed.Progression, PlannedClass, PlannedHistory) &&
					   !(Proposed.RulesFingerprint = Fingerprint(*P, QuestIndex, PlannedClass, PlannedHistory, &Proposed.Quests)).IsEmpty() &&
					   P->ValidateCommittedProgression(Proposed.Progression, PlannedClass, PlannedHistory) &&
					   Q->ValidateCommittedQuests(Proposed.Quests) &&
					   ResolveItems(Player, Proposed, PlannedBag, PlannedSlots);
	FGuid Ticket;
	if (IOGate->TryAcquire({ProfileKey(B->Character)}, Ticket) != EAdmission::Accepted)
		return false;
	Work->BeginCommand();
	B->bCommandObserved = false;
	B->bCommandPending = true;
	B->CommandComplete = MoveTemp(Complete);
	int64 RequestLeaseRevision = 0, RequestProfileRevision = 0;
	{
		FScopeLock Lock(&Work->Mutex);
		RequestLeaseRevision = Work->LeaseRevision;
		RequestProfileRevision = Work->ProfileRevision;
	}
	FCommitRequest Request{Key,
						   IntentOperation,
						   {{LeaseKey(B->Character), RequestLeaseRevision, 1, SessionBytes(Work->Session), false},
							{ProfileKey(B->Character), RequestProfileRevision, 1, {}, false}}};
	TWeakObjectPtr<UProject_JLocalProfileSubsystem> WeakThis(this);
	Jobs.RemoveAll([](const auto &Job) { return Job.IsReady(); });
	Jobs.Add(Async(EAsyncExecution::ThreadPool, [WeakThis, B, Work, Request = MoveTemp(Request),
												 Intent = MoveTemp(Intent), Before = MoveTemp(Before), Proposed = MoveTemp(Proposed),
												 bCraftPlan, CraftKind, CraftJobId, CraftRulesDigest, CraftBatches,
                                                 bWorldEvent, EventRound = Edit.Instance, EventExperience = Edit.Amount,
												 CraftRecipe = MoveTemp(CraftRecipe), LockedInstances = MoveTemp(LockedInstances),
                                                 ClaimRewards = MoveTemp(ClaimRewards), Valid, Ticket, OnDurable = MoveTemp(OnDurable)]() mutable {
		check(!IsInGameThread());
		bool BaseValid = false, ProposedValid = false;
#if WITH_DEV_AUTOMATION_TESTS
		if (Work->CommandStageForTest) Work->CommandStageForTest(TEXT("BeforeCommandEncode"));
#endif
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_ProfileCommandEncode);
			TArray<uint8> BeforeBytes;
			BaseValid = Before.Encode(BeforeBytes);
		}
		const FProject_JProfileStream Stream{Work->Character, Work->Session, Request.IdempotencyKey.D};
		FCommitResult Result;
        bool bEventClaimCapacity = false;
		// Validate the captured baseline before looking up receipts, as the GT path
		// did. A known receipt remains replayable when the new plan is no longer valid.
		if (BaseValid) Result = Work->Repository->FindProfileReceipt(Request.IdempotencyKey, Request.Operation, Stream, Intent);
        // Epoch receipts may be gone after reconnect. The durable claim ledger never is.
        // Read under this character's writer gate; revision CAS still guards external writers.
        if (bWorldEvent && BaseValid && Result.Status == ECommitStatus::Invalid)
        {
            FRecord ClaimsRecord; TArray<ProjectJ::WorldEventClaims::FClaim> Claims;
            const auto Read = Work->Repository->ReadChecked(ProjectJ::WorldEventClaims::Key(Work->Character), ClaimsRecord);
            if (Read == EProject_JRecordRead::Unavailable || !ProjectJ::WorldEventClaims::Decode(ClaimsRecord, Claims))
                Result.Status = ECommitStatus::Unavailable;
            else if (const auto* Existing = Claims.FindByPredicate([&](const auto& C) { return C.Round == EventRound; }))
                Result.Status = Existing->Experience == EventExperience ? ECommitStatus::Replayed : ECommitStatus::KeyReused;
            else if (Claims.Num() >= ProjectJ::WorldEventClaims::MaxClaims)
            { Result.Status = ECommitStatus::Capacity; bEventClaimCapacity = true; }
            else
            {
                Claims.Add({EventRound, EventExperience}); TArray<uint8> Bytes;
                if (!ProjectJ::WorldEventClaims::Encode(Claims, Bytes)) Result.Status = ECommitStatus::Unavailable;
                else Request.Mutations.Add({ProjectJ::WorldEventClaims::Key(Work->Character), ClaimsRecord.Revision, 1, MoveTemp(Bytes), false});
            }
        }
		// A known receipt must not rerun a plan, allocate output GUIDs or encode a
		// replacement payload. Replay remains valid after recipe removal/change.
		if (Result.Status == ECommitStatus::Invalid && BaseValid && Valid)
		{
			bool Planned = true;
			if (bCraftPlan)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_ProfileCraftPlan);
#if WITH_DEV_AUTOMATION_TESTS
				if (Work->CommandStageForTest) Work->CommandStageForTest(TEXT("BeforeCraftPlan"));
#endif
				FProject_JLocalProfileSnapshot Crafted;
				using K = EProject_JPersistentEdit;
				ProjectJ::Crafting::EPlanResult PlannedResult = ProjectJ::Crafting::EPlanResult::InvalidRecipe;
				if (CraftKind == K::Craft)
					PlannedResult = ProjectJ::Crafting::Plan(CraftRecipe, CraftBatches, LockedInstances, Before, Crafted);
				else if (CraftKind == K::StartCraftJob)
					PlannedResult = ProjectJ::Crafting::StartJob(CraftRecipe, CraftBatches, CraftRulesDigest, CraftJobId,
						FDateTime::UtcNow().GetTicks(), LockedInstances, Before, Crafted);
				else if (CraftKind == K::CollectCraftJob)
					PlannedResult = ProjectJ::Crafting::CollectJob(CraftJobId, FDateTime::UtcNow().GetTicks(), LockedInstances, Before, Crafted);
				else if (CraftKind == K::CancelCraftJob)
					PlannedResult = ProjectJ::Crafting::CancelJob(CraftJobId, LockedInstances, Before, Crafted);
				Planned = PlannedResult == ProjectJ::Crafting::EPlanResult::Success;
				if (Planned)
				{
					// Item ownership and the GT-admitted activity progress are one CAS.
					Crafted.Quests = MoveTemp(Proposed.Quests);
					Crafted.RulesFingerprint = MoveTemp(Proposed.RulesFingerprint);
					Proposed = MoveTemp(Crafted);
				}
			}
            if (Planned && !ClaimRewards.IsEmpty())
            {
                TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_ProfileQuestRewardPlan);
#if WITH_DEV_AUTOMATION_TESTS
                if (Work->CommandStageForTest) Work->CommandStageForTest(TEXT("BeforeQuestRewardPlan"));
#endif
                FProject_JLocalProfileSnapshot Granted;
                Planned = ProjectJ::Crafting::PlanGrant(ClaimRewards, LockedInstances, Before, Granted) == ProjectJ::Crafting::EPlanResult::Success;
                if (Planned)
                {
#if WITH_DEV_AUTOMATION_TESTS
                    if (Work->CommandStageForTest) Work->CommandStageForTest(TEXT("AfterQuestRewardPlan"));
#endif
                    // Item ownership, XP and completion history share this one receipt/CAS.
                    Granted.Progression = MoveTemp(Proposed.Progression);
                    Granted.Quests = MoveTemp(Proposed.Quests);
                    Granted.RulesFingerprint = MoveTemp(Proposed.RulesFingerprint);
                    Proposed = MoveTemp(Granted);
                }
            }
			TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_ProfileCommandEncode);
			FString HistoryFailure;
            const bool Encoded = Planned && Proposed.Encode(Request.Mutations[1].Payload);
#if WITH_DEV_AUTOMATION_TESTS
            if (Planned && !Encoded && !ClaimRewards.IsEmpty() && Work->CommandStageForTest) Work->CommandStageForTest(TEXT("QuestRewardEncodeRejected"));
#endif
			ProposedValid = Encoded &&
				ProjectJ::LocalProfileHistory::PrepareHistoryMutations(*Work->Repository, Work->Character,
					Before, Proposed, Request, HistoryFailure);
		}
		const bool IssuedNewCommit = Result.Status == ECommitStatus::Invalid && BaseValid && ProposedValid;
		if (IssuedNewCommit)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_ProfileCommandCommit);
			Result = Work->Repository->CommitProfile(Request, Stream, Intent);
			if (Result.Status == ECommitStatus::UnknownOutcome)
				Result = Work->Repository->CommitProfile(Request, Stream, Intent);
		}
		FProject_JProfileDurableResult Durable;
		Durable.Request = Request.IdempotencyKey;
		Durable.StoreStatus = Result.Status; // Preserve evidence before projection/session checks can replace Status.
		if (OnDurable)
		{
			if (Accepted(Result.Status)) Durable.Outcome = EProject_JProfileDurableOutcome::Committed;
			else
			{
				// One read, not another mutation/retry. Invalid proves absence only in this current epoch/intent.
				Durable.bReceiptQueried = true;
				Durable.ReceiptStatus = Work->Repository->FindProfileReceipt(Request.IdempotencyKey, Request.Operation, Stream, Intent).Status;
				if (Accepted(Durable.ReceiptStatus)) Durable.Outcome = EProject_JProfileDurableOutcome::Committed;
				else if (Durable.ReceiptStatus == ECommitStatus::Invalid) Durable.Outcome = EProject_JProfileDurableOutcome::ProvenAbsent;
                if (bWorldEvent && Durable.Outcome != EProject_JProfileDurableOutcome::Committed)
                {
                    FRecord Marker; TArray<ProjectJ::WorldEventClaims::FClaim> Claims;
                    const auto Read = Work->Repository->ReadChecked(ProjectJ::WorldEventClaims::Key(Work->Character), Marker);
                    if (Read == EProject_JRecordRead::Unavailable || !ProjectJ::WorldEventClaims::Decode(Marker, Claims))
                        Durable.Outcome = EProject_JProfileDurableOutcome::Unknown;
                    else if (const auto* Claim = Claims.FindByPredicate([&](const auto& C) { return C.Round == EventRound; }))
                        Durable.Outcome = Claim->Experience == EventExperience ? EProject_JProfileDurableOutcome::Committed : EProject_JProfileDurableOutcome::Unknown;
                }
				// Expired/Unavailable/KeyReused are never proof of non-payment. Keep Unknown.
			}
		}
		FRecord Record, Lease;
		bool Confirmed = Accepted(Result.Status) && Work->Repository->Read(ProfileKey(Work->Character), Record) &&
						 Work->Repository->Read(LeaseKey(Work->Character), Lease) && !Record.bDeleted &&
						 !Lease.bDeleted && Record.SchemaVersion == 1 && Lease.SchemaVersion == 1 &&
						 Lease.Payload == SessionBytes(Work->Session);
		if (Accepted(Result.Status) && !Confirmed)
			Result.Status = ECommitStatus::Conflict;
		FProfileValues KnownValues;
		if (IssuedNewCommit) KnownValues = MakeShared<FProject_JLocalProfileSnapshot, ESPMode::ThreadSafe>(MoveTemp(Proposed));
		else { FScopeLock Lock(&Work->Mutex); KnownValues = Work->ConfirmedValues; }
		FProfileValues Values = Confirmed ? DecodeProfileValues(*Work->Repository, Work->Character, Record.Payload, KnownValues, Record.Revision) : FProfileValues();
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
				Work->ConfirmedValues = Values;
			}
		}
		if (Confirmed && !Values) Work->Fault(ECommitStatus::Conflict);
		if (!Confirmed && !bEventClaimCapacity && Result.Status != ECommitStatus::Invalid && Result.Status != ECommitStatus::KeyReused && Result.Status != ECommitStatus::Expired)
			Work->Fault(Result.Status);
		auto Projection = Work->FinishWriter(Ticket);
		AsyncTask(ENamedThreads::GameThread, [WeakThis, B, Confirmed, FirstApplication = IssuedNewCommit && Confirmed,
			Status = Result.Status, bEventClaimCapacity, Projection = MoveTemp(Projection), Durable, OnDurable = MoveTemp(OnDurable)]() mutable {
            if (auto* Self = WeakThis.Get()) Self->RetryPendingTrustedOpens();
			if (auto* Self = WeakThis.Get(); Self && Self->IsCurrent(B)) B->bCommandObserved = true;
			if (OnDurable)
			{
				auto Observe = MoveTemp(OnDurable);
				Observe(Durable); // Native result survives owner retirement; no UObject was read by the worker.
			}
			// Observer may reenter, end the world or collect garbage. Resolve the weak subsystem afterwards.
			if (auto *Self = WeakThis.Get())
			{
				Self->CompleteEdit(B, Confirmed, FirstApplication, Status, Projection.Values, Projection.Revision, bEventClaimCapacity);
			}
		});
	}));
	return true;
}
bool UProject_JLocalProfileSubsystem::ApplyConfirmedProjection(
	const TSharedPtr<FProject_JLocalProfileBinding, ESPMode::ThreadSafe>& B, FProfileValues Values, int64 ProjectionRevision)
{
	auto *Self = this;
	if (!Self->IsCurrent(B)) return false;
	if (ProjectionRevision <= B->AppliedRevision) return true;
	TGuardValue<bool> Applying(B->bCommandPending, true);
	const TStrongObjectPtr<UProject_JLocalProfileSubsystem> KeepSelf(Self);
	const TStrongObjectPtr<AProject_JPlayerState> KeepPlayer(B->Player.Get());
	bool OK = Values.IsValid();
	auto *PS = B->Player.Get();
	if (OK)
	{
		const auto& Snapshot = *Values;
		TArray<FProject_JItemInstanceData> Bag;
		TArray<FProject_JEquipmentArrayItem> Slots;
		auto *Progress = PS->FindComponentByClass<UProject_JProgressionComponent>();
		auto *Quests = PS->FindComponentByClass<UProject_JQuestComponent>();
		const TStrongObjectPtr<UProject_JProgressionComponent> KeepProgress(Progress);
		const TStrongObjectPtr<UProject_JQuestComponent> KeepQuests(Quests);
		const TStrongObjectPtr<UProject_JCraftingJobsComponent> KeepJobs(PS->GetCraftingJobsComponent());
		const TStrongObjectPtr<UProject_JInventoryComponent> KeepInventory(PS->GetInventoryComponent());
		const TStrongObjectPtr<UProject_JEquipmentManagerComponent> KeepEquipment(PS->GetEquipmentManagerComponent());
		const auto ParticipantsCurrent = [&]
		{
			const auto Live = [&](const UActorComponent* Component)
			{
				return IsValid(Component) && !Component->IsBeingDestroyed() && Component->IsRegistered() &&
					Component->GetOwner() == PS && Component->GetWorld() == PS->GetWorld();
			};
			return Self->IsCurrent(B) && B->Player.Get() == PS && Live(Progress) && Live(Quests) &&
				Live(KeepInventory.Get()) && Live(KeepEquipment.Get()) && Live(KeepJobs.Get()) &&
				PS->FindComponentByClass<UProject_JProgressionComponent>() == Progress &&
				PS->FindComponentByClass<UProject_JQuestComponent>() == Quests &&
				PS->GetInventoryComponent() == KeepInventory.Get() && PS->GetEquipmentManagerComponent() == KeepEquipment.Get() &&
				PS->GetCraftingJobsComponent() == KeepJobs.Get();
		};
		const int64 PreviousAppliedRevision = B->AppliedRevision;
		if (!ParticipantsCurrent()) return false;
		UProject_JCharacterClassDefinition *Class = nullptr;
		TArray<UProject_JCharacterAdvancementDefinition *> History;
		FProject_JQuestDefinitionIndex QuestIndex;
		Quests->BuildDefinitionIndex(QuestIndex);
		ProjectJ::LocalProfileRules::FQuestGuard AppliedRules;
		OK = Snapshot.Account == B->Account &&
			 Snapshot.Character == B->Character &&
			 ResolveDefinitions(GetGameInstance(), Snapshot.Progression, Class, History) &&
			 Matches(Snapshot, *Progress, QuestIndex, Class, History) &&
			 Progress->ValidateCommittedProgression(Snapshot.Progression, Class, History, true) &&
			 Quests->ValidateCommittedQuests(Snapshot.Quests, true) && ResolveItems(PS, Snapshot, Bag, Slots, true) &&
			 AppliedRules.Capture(QuestIndex, Snapshot.Quests, GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>());
		if (OK)
		{
			auto *Inventory = KeepInventory.Get();
			auto *Equipment = KeepEquipment.Get();
			const auto OldBag = Inventory->GetItemInstances();
			const auto OldSlots = Equipment->CapturePersistentEquipment();
			const auto CanInstall = [&]
			{
				if (!ParticipantsCurrent() || B->AppliedRevision != PreviousAppliedRevision) return false;
				FProject_JQuestDefinitionIndex CurrentIndex;
				Quests->BuildDefinitionIndex(CurrentIndex);
				return Matches(Snapshot, *Progress, CurrentIndex, Class, History) &&
					AppliedRules.IsCurrent(CurrentIndex, GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>()) &&
					Quests->ValidateCommittedQuests(Snapshot.Quests, true) && Inventory->ValidatePersistentItems(Bag, true) &&
					Equipment->ValidatePersistentEquipment(Slots, Bag, true) && KeepJobs->ValidateCommittedJobs(Snapshot.CraftingJobs);
			};
			bool bProgressionChanged = false;
			OK = Progress->TryInstallCommittedProgression(Snapshot.Progression, Class, History, CanInstall, bProgressionChanged);
			if (!OK) return false; // A paid commit remains durable; retirement never redirects it to a new owner.
			B->QuestRules = MoveTemp(AppliedRules);
			// All authoritative values are installed before the first notification.
			const bool bQuestChanged = Quests->InstallCommittedQuests(Snapshot.Quests);
			Inventory->InstallPersistentItems(Bag);
			Equipment->InstallPersistentEquipment(Slots);
			KeepJobs->InstallCommittedJobs(Snapshot.CraftingJobs);
			// Commit the installed revision before callbacks. A reentrant newer
			// progress projection owns subsequent publication and must not regress.
			B->AppliedRevision = ProjectionRevision;
			const auto CanPublish = [&] { return ParticipantsCurrent() && B->AppliedRevision == ProjectionRevision; };
			if (CanPublish() && bProgressionChanged)
				Progress->PublishCommittedExperience(CanPublish);
			if (CanPublish() && bQuestChanged)
				Quests->PublishCommittedQuests();
			if (CanPublish())
				Inventory->PublishPersistentItems(OldBag, CanPublish);
			if (CanPublish())
				Equipment->PublishPersistentEquipment(OldSlots);
			if (CanPublish()) KeepJobs->BroadcastCommittedJobs();
		}
	}
	return OK;
}
bool UProject_JLocalProfileSubsystem::QueueQuestProgress(
	const TSharedPtr<FProject_JLocalProfileBinding, ESPMode::ThreadSafe>& B,
	const TArray<FProject_JQuestProgressInput>& Inputs)
{
	check(IsInGameThread());
	if (!IsCurrent(B) || !B->bReady || !B->Work->Healthy() || Inputs.Num() > 128 * UProject_JQuestComponent::MaxObjectivesPerQuest) return false;
	if (Inputs.IsEmpty()) return true;
	auto* Quest = B->Player->FindComponentByClass<UProject_JQuestComponent>();
	FProject_JQuestDefinitionIndex Index;
	Quest->BuildDefinitionIndex(Index);
	if (!RequireQuestRules(B, Index)) return false;
	for (const auto& Input : Inputs)
	{
		const auto* State = Quest->States.FindByPredicate([&](const auto& Value) { return Value.QuestId == Input.Quest; });
		const auto* Definition = Index.FindRef(Input.Quest);
		if (!State || !Definition || State->bClaimed || !Definition->IsStateValid(*State)) return false;
		if (Input.Objective.IsNone())
		{
			if (!Definition->Objectives.IsEmpty() || Input.Required != Definition->RequiredCount || Input.Current != State->Count) return false;
		}
		else
		{
			const auto* Objective = Definition->Objectives.FindByPredicate([&](const auto& Value) { return Value.ObjectiveId == Input.Objective; });
			if (!Objective || (Objective->Kind != EProject_JQuestObjectiveKind::Event && Objective->Kind != EProject_JQuestObjectiveKind::Travel) ||
				Input.Required != Objective->RequiredCount || Input.Current != State->GetObjectiveCount(Input.Objective)) return false;
		}
	}
	auto Work = B->Work;
	FGuid Ticket;
	bool bStart = false;
	if (!Work->AddProgress(Inputs, B->AppliedRevision, B->bCommandPending, B->bCommandObserved, Ticket, bStart))
	{
		// Includes the compatible void producer: make transient rejection observable without a log or retry queue.
		if (B->RejectedProgressAdmissions < MAX_uint64) ++B->RejectedProgressAdmissions;
		return false;
	}
	if (bStart)
	{
		TWeakObjectPtr<UProject_JLocalProfileSubsystem> WeakThis(this);
		Jobs.RemoveAll([](const auto& Job) { return Job.IsReady(); });
		Jobs.Add(Async(EAsyncExecution::ThreadPool, [WeakThis, B, Work, Ticket] {
			auto Projection = Work->Drain(Ticket);
			AsyncTask(ENamedThreads::GameThread, [WeakThis, B, Projection = MoveTemp(Projection)] {
                if (auto* Self = WeakThis.Get()) Self->RetryPendingTrustedOpens();
				if (auto* Self = WeakThis.Get()) Self->CompleteProgress(B, Projection.Values, Projection.Revision);
			});
		}));
	}
	return true;
}
void UProject_JLocalProfileSubsystem::CompleteProgress(
	const TSharedPtr<FProject_JLocalProfileBinding, ESPMode::ThreadSafe>& B, FProfileValues Values, int64 ProjectionRevision)
{
	check(IsInGameThread());
	if (!IsCurrent(B)) return;
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	const TStrongObjectPtr<AProject_JPlayerState> KeepPlayer(B->Player.Get());
	const bool Applied = Values ? ApplyConfirmedProjection(B, Values, ProjectionRevision) : false;
	if (!IsCurrent(B)) return;
	if (!Applied && B->Work->Healthy()) B->Work->Fault(ECommitStatus::Conflict);
	if (!Applied || !B->Work->Healthy()) B->bReady = false;
}

void UProject_JLocalProfileSubsystem::CompleteEdit(
	const TSharedPtr<FProject_JLocalProfileBinding, ESPMode::ThreadSafe> &B, bool Confirmed, bool FirstApplication,
	ECommitStatus Status, FProfileValues Values, int64 ProjectionRevision, bool bEventClaimCapacity)
{
	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_ProfileCommandApply);
#if WITH_DEV_AUTOMATION_TESTS
	if (bHoldCompletionsForTest)
	{
		TWeakObjectPtr<UProject_JLocalProfileSubsystem> WeakThis(this);
		HeldCompletionsForTest.Add(
			[WeakThis, B, Confirmed, FirstApplication, Status, Values = MoveTemp(Values), ProjectionRevision, bEventClaimCapacity]() mutable {
				if (auto *Self = WeakThis.Get())
					Self->CompleteEdit(B, Confirmed, FirstApplication, Status, MoveTemp(Values), ProjectionRevision, bEventClaimCapacity);
			});
		return;
	}
#endif

	if (!IsCurrent(B)) return;
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	const TStrongObjectPtr<AProject_JPlayerState> KeepPlayer(B->Player.Get());
	const bool ProjectionOK = Values ? ApplyConfirmedProjection(B, Values, ProjectionRevision) : !Confirmed;
	if (!IsCurrent(B)) return;
	const bool OK = Confirmed && Values.IsValid() && ProjectionOK;
	const bool First = FirstApplication;
	B->bCommandPending = false;
	B->bCommandObserved = false;
	B->LastAdmissionReason = OK									  ? FString()
							 : bEventClaimCapacity ? TEXT("개발 이벤트 보상 기록 한도에 도달함: 기존 지급 기록은 보존됨")
							 : Status == ECommitStatus::KeyReused ? TEXT("같은 요청 키의 다른 payload를 거부함")
							 : Status == ECommitStatus::Expired
								 ? TEXT("요청 유효 범위 만료: 현재 상태 재동기화 필요")
								 : TEXT("요청 검증/저장 실패: 소유권·수량·정의·버전·64KiB 한도 확인 필요");
	if (!ProjectionOK || !B->Work->Healthy() || (!OK && !bEventClaimCapacity && Status != ECommitStatus::Invalid && Status != ECommitStatus::KeyReused &&
		Status != ECommitStatus::Expired))
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
void UProject_JLocalProfileSubsystem::SetCommandStageHookForTest(AProject_JPlayerState* Player, TFunction<void(const TCHAR*)> Hook)
{
	check(IsInGameThread());
	auto* B = Bindings.Find(Player);
	check(B && !IsCheckpointPending(Player));
	(*B)->Work->CommandStageForTest = Hook;
	(*B)->Work->Repository->TestHook = [Hook = MoveTemp(Hook)](const TCHAR* Point, FProject_JProfileDatabase&) {
		if (Hook) Hook(Point);
	};
}
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
	if (!Player || (Snapshot.Format != 2 && Snapshot.Format != 3 && Snapshot.Format != 4))
		return false;
	auto *P = Player->FindComponentByClass<UProject_JProgressionComponent>();
	auto *Q = Player->FindComponentByClass<UProject_JQuestComponent>();
	UProject_JCharacterClassDefinition *Class = nullptr;
	TArray<UProject_JCharacterAdvancementDefinition *> History;
	TArray<FProject_JItemInstanceData> Bag;
	TArray<FProject_JEquipmentArrayItem> Slots;
	FProject_JQuestDefinitionIndex QuestIndex;
	if (Q) Q->BuildDefinitionIndex(QuestIndex);
	if (!P || !Q || !ResolveDefinitions(Player->GetGameInstance(), Snapshot.Progression, Class, History) ||
		!Matches(Snapshot, *P, QuestIndex, Class, History) ||
		!P->CanRestoreSnapshot(Snapshot.Progression, Class, History) || !Q->CanRestoreSnapshot(Snapshot.Quests, true) ||
		!ResolveItems(Player, Snapshot, Bag, Slots, false, &Error))
		return false;
	Error.Reset();
	return true;
}
