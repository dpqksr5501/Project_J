#include "Backend/Project_JQuestHistoryStorage.h"
#include "Backend/Project_JSQLiteRepository.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace ProjectJ::LocalProfileHistory
{
using namespace ProjectJ::MMO;
namespace QH = ProjectJ::QuestHistory;
namespace
{
constexpr TCHAR PageKindPrefix[] = TEXT("LocalProfileQuestPage");
FAggregateKey CoreKey(FGuid Character) { return {TEXT("LocalProfile"), Character}; }
bool Fail(FString& Failure, const TCHAR* Message) { Failure = Message; return false; }
int32 PagesFor(int32 Count) { return (Count + QH::IdsPerPage - 1) / QH::IdsPerPage; }
bool Number(const FJsonObject& Object, const TCHAR* Field, int32& Out)
{
    double Value = 0;
    if (!Object.TryGetNumberField(Field, Value) || !FMath::IsFinite(Value) || Value < 0 || Value > MAX_int32 || Value != FMath::FloorToDouble(Value)) return false;
    Out = int32(Value); return true;
}
bool SamePage(const QH::FPagePtr& A, const QH::FPagePtr& B)
{
    return A == B || (A && B && A->GetOrdinal() == B->GetOrdinal() && A->GetIds() == B->GetIds() &&
        A->GetDigest() == B->GetDigest() && A->GetPreviousRootDigest() == B->GetPreviousRootDigest() && A->GetRootDigest() == B->GetRootDigest());
}
bool DecodePage(const FRecord& Record, FGuid Character, int32 Index, QH::FPagePtr& Out, FString& Failure)
{
    if (Record.Revision <= 0 || Record.bDeleted || Record.SchemaVersion != 1 || Record.Payload.IsEmpty() || Record.Payload.Num() > 65536)
        return Fail(Failure, TEXT("Invalid history page record"));
    FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Record.Payload.GetData()), Record.Payload.Num());
    auto Reader = TJsonReaderFactory<>::Create(FString(Text.Length(), Text.Get()));
    TSharedPtr<FJsonObject> Object;
    FString Owner, Previous, Digest, Root;
    int32 Schema = 0, Ordinal = 0;
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    FGuid Parsed;
    if (!FJsonSerializer::Deserialize(Reader, Object) || !Object || Object->Values.Num() != 7 ||
        !Number(*Object, TEXT("schema"), Schema) || Schema != 1 || !Number(*Object, TEXT("index"), Ordinal) || Ordinal != Index ||
        !Object->TryGetStringField(TEXT("character"), Owner) || !FGuid::ParseExact(Owner, EGuidFormats::DigitsWithHyphens, Parsed) || Parsed != Character ||
        !Object->TryGetStringField(TEXT("previous"), Previous) || !Object->TryGetStringField(TEXT("digest"), Digest) ||
        !Object->TryGetStringField(TEXT("root"), Root) || !Object->TryGetArrayField(TEXT("ids"), Values) || !Values || Values->IsEmpty() || Values->Num() > QH::IdsPerPage)
        return Fail(Failure, TEXT("Invalid history page shape/owner/index"));
    TArray<FName> Ids;
    for (const auto& Value : *Values)
    {
        FString Id;
        if (!Value || !Value->TryGetString(Id) || Id.IsEmpty() || Id.Len() > QH::MaxIdLength)
            return Fail(Failure, TEXT("Invalid history ID"));
        for (TCHAR Ch : Id) if (!Ch) return Fail(Failure, TEXT("Invalid history ID"));
        Ids.Add(FName(*Id));
    }
    return QH::BuildPageValidated(Index, Previous, Ids, Digest, Root, Out, Failure);
}
bool EncodePage(FGuid Character, const QH::FPagePtr& Page, TArray<uint8>& Out, FString& Failure)
{
    if (!Character.IsValid() || !Page) return Fail(Failure, TEXT("Missing history page value"));
    auto Object = MakeShared<FJsonObject>();
    Object->SetNumberField(TEXT("schema"), 1); Object->SetNumberField(TEXT("index"), Page->GetOrdinal());
    Object->SetStringField(TEXT("character"), Character.ToString(EGuidFormats::DigitsWithHyphens));
    Object->SetStringField(TEXT("previous"), Page->GetPreviousRootDigest()); Object->SetStringField(TEXT("digest"), Page->GetDigest());
    Object->SetStringField(TEXT("root"), Page->GetRootDigest());
    TArray<TSharedPtr<FJsonValue>> Ids;
    for (FName Id : Page->GetIds()) Ids.Add(MakeShared<FJsonValueString>(Id.ToString().ToLower()));
    Object->SetArrayField(TEXT("ids"), Ids);
    FString Text;
    auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
    if (!FJsonSerializer::Serialize(Object, Writer)) return Fail(Failure, TEXT("History page serialization failed"));
    FTCHARToUTF8 Bytes(*Text);
    if (Bytes.Length() > 65536) return Fail(Failure, TEXT("History page byte limit"));
    FRecord Test; Test.Revision = 1; Test.Payload.Append(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length());
    QH::FPagePtr Decoded;
    if (!DecodePage(Test, Character, Page->GetOrdinal(), Decoded, Failure) || !SamePage(Page, Decoded)) return false;
    Out = MoveTemp(Test.Payload); return true;
}
bool Prefix(const QH::FViewPtr& Before, const QH::FViewPtr& After, FString& Failure)
{
    if (QH::Count(Before) > QH::Count(After)) return Fail(Failure, TEXT("Completion history cannot retract"));
    const int32 Sealed = QH::Count(Before) / QH::IdsPerPage;
    for (int32 Index = 0; Index < Sealed; ++Index)
        if (!SamePage(QH::GetPage(Before, Index), QH::GetPage(After, Index))) return Fail(Failure, TEXT("Sealed completion page changed"));
    if (QH::Count(Before) % QH::IdsPerPage)
    {
        const auto Old = QH::GetPage(Before, Sealed), New = QH::GetPage(After, Sealed);
        if (!Old || !New || Old->GetIds().Num() > New->GetIds().Num()) return Fail(Failure, TEXT("Completion tail changed"));
        for (int32 Index = 0; Index < Old->GetIds().Num(); ++Index)
            if (Old->GetIds()[Index] != New->GetIds()[Index]) return Fail(Failure, TEXT("Completion tail prefix changed"));
    }
    return true;
}
}

FAggregateKey PageKey(FGuid Character, int32 Index)
{
    if (!Character.IsValid() || Index < 0 || Index >= QH::MaxPages) return {};
    // The only dynamically constructed names in this domain have exactly1024 possible values.
    static const TArray<FName> Kinds = [] { TArray<FName> Out; Out.Reserve(QH::MaxPages);
        for (int32 I = 0; I < QH::MaxPages; ++I) Out.Add(FName(*FString::Printf(TEXT("%s%04d"), PageKindPrefix, I))); return Out; }();
    return {Kinds[Index], Character};
}
int32 PageIndex(const FAggregateKey& Key)
{
    const FString Kind = Key.Kind.ToString(); const int32 PrefixLength = static_cast<int32>(UE_ARRAY_COUNT(PageKindPrefix)) - 1;
    if (Kind.Len() != PrefixLength + 4 || !Kind.StartsWith(PageKindPrefix, ESearchCase::IgnoreCase)) return INDEX_NONE;
    int32 Index = 0;
    for (int32 I = PrefixLength; I < Kind.Len(); ++I) { if (Kind[I] < '0' || Kind[I] > '9') return INDEX_NONE; Index = Index * 10 + Kind[I] - '0'; }
    return Index < QH::MaxPages ? Index : INDEX_NONE;
}

FProfileValues HydrateProfileValues(FGuid Character, const TArray<uint8>& CoreBytes,
    const TArray<FRecord>& Records, const FProfileValues& Known)
{
    FProject_JStoredProfileCore Core;
    if (!FProject_JLocalProfileSnapshot::DecodeStoredCore(CoreBytes, Core) || Core.HotValues.Character != Character) return {};
    auto& Values = Core.HotValues;
    const bool KnownValid = Known && Known->Character == Character && Known->Quests.HasHydratedHistory();
    const auto Previous = KnownValid ? Known->Quests.History : QH::FViewPtr();
    const int32 Count = Values.Quests.HistoryCount, BeforeCount = QH::Count(Previous);
    FString Failure;
    TArray<TArray<FName>> ReadIds;
    if ((Count == 0 && (!Records.IsEmpty() || BeforeCount > 0)) ||
        (KnownValid && Count == BeforeCount && Values.Quests.HistoryRootDigest == QH::RootDigest(Previous) && !Records.IsEmpty())) return {};
    if (Count > 0)
    {
        if (KnownValid && Count == BeforeCount && Values.Quests.HistoryRootDigest == QH::RootDigest(Previous)) Values.Quests.History = Previous;
        else
        {
            if (KnownValid && Count <= BeforeCount) return {};
            const int32 First = KnownValid ? BeforeCount / QH::IdsPerPage : 0;
            if (Records.Num() != PagesFor(Count) - First) return {};
            FString PreviousRoot;
            if (KnownValid && !QH::GetPrefixDigest(Previous, First, PreviousRoot)) return {};
            for (int32 I = 0; I < Records.Num(); ++I)
            {
                QH::FPagePtr Page; const int32 Ordinal = First + I;
                const int32 Expected = Ordinal + 1 < PagesFor(Count) ? QH::IdsPerPage : (Count - Ordinal * QH::IdsPerPage);
                if (!DecodePage(Records[I], Character, Ordinal, Page, Failure) || Page->GetIds().Num() != Expected || Page->GetPreviousRootDigest() != PreviousRoot) return {};
                PreviousRoot = Page->GetRootDigest(); ReadIds.Add(Page->GetIds());
            }
            const bool Built = KnownValid ? QH::BuildExtendedValidated(Previous, ReadIds, Count, Values.Quests.HistoryRootDigest, Values.Quests.History, Failure)
                : QH::BuildValidated(ReadIds, Count, Values.Quests.HistoryRootDigest, Values.Quests.History, Failure);
            if (!Built) return {};
        }
    }
    if (!Values.Quests.HasHydratedHistory()) return {};
    // Unchanged active identities reuse the already-validated disjointness fact.
    TSet<FName> OldActive, CurrentActive;
    if (KnownValid) for (const auto& State : Known->Quests.States) OldActive.Add(State.QuestId);
    for (const auto& State : Values.Quests.States)
    {
        CurrentActive.Add(State.QuestId);
        if (!OldActive.Contains(State.QuestId) && QH::Contains(Values.Quests.History, State.QuestId)) return {};
    }
    if (KnownValid) for (int32 I = 0; I < ReadIds.Num(); ++I) for (FName Id : ReadIds[I])
        if (CurrentActive.Contains(Id)) return {};
    return MakeShared<FProject_JLocalProfileSnapshot, ESPMode::ThreadSafe>(MoveTemp(Values));
}

FProfileValues ReadProfileValues(FProject_JSQLiteRepository& Repository, FGuid Character,
    const TArray<uint8>& CoreBytes, const FProfileValues& Known, int64 ExpectedCoreRevision)
{
    check(!IsInGameThread());
    FProject_JStoredProfileCore Core;
    if (!FProject_JLocalProfileSnapshot::DecodeStoredCore(CoreBytes, Core) || Core.HotValues.Character != Character) return {};
    const bool KnownValid = Known && Known->Character == Character && Known->Quests.HasHydratedHistory();
    const bool Same = KnownValid && Known->Quests.HistoryCount == Core.HotValues.Quests.HistoryCount &&
        Known->Quests.HistoryRootDigest == Core.HotValues.Quests.HistoryRootDigest;
    TArray<FRecord> Records;
    if (Core.HotValues.Quests.HistoryCount > 0 && !Same)
    {
        const int32 First = KnownValid ? Known->Quests.HistoryCount / QH::IdsPerPage : 0;
        if (!Repository.ReadProfilePages(Character, CoreBytes, First, PagesFor(Core.HotValues.Quests.HistoryCount) - First, Records, ExpectedCoreRevision)) return {};
    }
    auto Values = HydrateProfileValues(Character, CoreBytes, Records, Known);
    if (!Values) return {};
    // A pointer-reuse path performs no history I/O, but must still name the actual current core.
    FRecord Current;
    if (!Repository.Read(CoreKey(Character), Current) || Current.bDeleted || Current.SchemaVersion != 1 || Current.Payload != CoreBytes ||
        (ExpectedCoreRevision >= 0 && Current.Revision != ExpectedCoreRevision)) return {};
    return Values;
}

bool PrepareHistoryMutations(FProject_JSQLiteRepository& Repository, FGuid Character,
    const FProject_JLocalProfileSnapshot& Before, const FProject_JLocalProfileSnapshot& After,
    FCommitRequest& Request, FString& Failure)
{
    check(!IsInGameThread());
    if (After.Character != Character || Before.Character != Character || !Before.Quests.HasHydratedHistory() || !After.Quests.HasHydratedHistory())
        return Fail(Failure, TEXT("Invalid history append input"));
    for (const auto& State : Before.Quests.States) if (State.bClaimed && !QH::Contains(After.Quests.History, State.QuestId) &&
        !After.Quests.States.ContainsByPredicate([&](const auto& Kept) { return Kept == State; }))
        return Fail(Failure, TEXT("Legacy completion would be lost"));
    if (Before.Quests.HistoryCount == After.Quests.HistoryCount && Before.Quests.HistoryRootDigest == After.Quests.HistoryRootDigest) return true;
    if (!Prefix(Before.Quests.History, After.Quests.History, Failure)) return false;
    FProject_JStoredProfileCore EncodedCore;
    if (Request.Mutations.Num() < 2 || Request.Mutations[0].Key.Kind != TEXT("LocalProfileLease") || Request.Mutations[0].Key.Id != Character ||
        !(Request.Mutations[1].Key == CoreKey(Character)) || Request.Mutations[0].SchemaVersion != 1 || Request.Mutations[1].SchemaVersion != 1 ||
        Request.Mutations[0].bDelete || Request.Mutations[1].bDelete || !FProject_JLocalProfileSnapshot::DecodeStoredCore(Request.Mutations[1].Payload, EncodedCore) ||
        EncodedCore.HotValues.Character != Character || EncodedCore.HotValues.Quests.HistoryCount != After.Quests.HistoryCount ||
        EncodedCore.HotValues.Quests.HistoryRootDigest != After.Quests.HistoryRootDigest)
        return Fail(Failure, TEXT("History append requires its exact lease/core prefix"));
    FCommitRequest Prepared = Request;
    const int32 First = QH::Count(Before.Quests.History) / QH::IdsPerPage;
    for (int32 Index = First; Index < PagesFor(After.Quests.HistoryCount); ++Index)
    {
        FRecord Existing; const auto Key = PageKey(Character, Index);
        const auto Read = Repository.ReadChecked(Key, Existing);
        if (Read == EProject_JRecordRead::Unavailable) return Fail(Failure, TEXT("History page read unavailable"));
        const auto Old = QH::GetPage(Before.Quests.History, Index);
        if (Old)
        {
            QH::FPagePtr Stored;
            if (Read != EProject_JRecordRead::Found || !DecodePage(Existing, Character, Index, Stored, Failure) || !SamePage(Old, Stored))
                return Fail(Failure, TEXT("Stored history tail differs from admitted value"));
        }
        else if (Read != EProject_JRecordRead::Missing) return Fail(Failure, TEXT("Unexpected pre-existing history page"));
        TArray<uint8> Payload;
        if (!EncodePage(Character, QH::GetPage(After.Quests.History, Index), Payload, Failure)) return false;
        Prepared.Mutations.Add({Key, Existing.Revision, 1, MoveTemp(Payload), false});
    }
    if (!IsValidCommitRequest(Prepared)) return Fail(Failure, TEXT("History transaction bounds exceeded"));
    Request = MoveTemp(Prepared); return true;
}

bool ValidateCommit(const FCommitRequest& Request, FGuid Character, const FRecord* OldCore,
    const TMap<int32, FRecord>& OldPages, FString& Failure)
{
    const FMutation* CoreMutation = nullptr;
    TMap<int32, const FMutation*> Pages;
    bool Lease = false;
    for (const auto& M : Request.Mutations)
    {
        if (M.Key.Kind == TEXT("LocalProfile")) CoreMutation = &M;
        else if (M.Key.Kind == TEXT("LocalProfileLease")) Lease = true;
        else if (const int32 Index = PageIndex(M.Key); Index != INDEX_NONE) Pages.Add(Index, &M);
    }
    if (Pages.IsEmpty())
    {
        if (!CoreMutation) return true; // Session acquisition has only its existing lease.
    }
    if (!CoreMutation || !Lease) return Fail(Failure, TEXT("History writes require core and lease"));
    FProject_JStoredProfileCore Next, Previous;
    const bool NextDecoded = FProject_JLocalProfileSnapshot::DecodeStoredCore(CoreMutation->Payload, Next);
    const bool HasOld = OldCore && OldCore->Revision > 0;
    const bool PreviousDecoded = HasOld && !OldCore->bDeleted && OldCore->SchemaVersion == 1 &&
        FProject_JLocalProfileSnapshot::DecodeStoredCore(OldCore->Payload, Previous);
    // Preserve the original opaque, nonhistory profile-stream port and its bounded-recovery tests.
    // Once a paged head exists, even a page-free command cannot erase or replace it with opaque/legacy bytes.
    if (Pages.IsEmpty() && (!PreviousDecoded || Previous.HotValues.Quests.HistoryCount == 0) &&
        (!NextDecoded || Next.HotValues.Format != 4)) return true;
    if (!NextDecoded || Next.HotValues.Character != Character) return Fail(Failure, TEXT("Invalid profile core"));
    if (HasOld && !PreviousDecoded) return Fail(Failure, TEXT("Invalid prior profile core"));
    const int32 OldCount = Previous.HotValues.Quests.HistoryCount, NewCount = Next.HotValues.Quests.HistoryCount;
    if (OldCount == NewCount && Previous.HotValues.Quests.HistoryRootDigest == Next.HotValues.Quests.HistoryRootDigest)
        return Pages.IsEmpty() || Fail(Failure, TEXT("Unchanged history cannot rewrite pages"));
    if (NewCount <= OldCount) return Fail(Failure, TEXT("History cannot retract or rewrite"));
    const int32 First = OldCount / QH::IdsPerPage, Last = PagesFor(NewCount);
    if (Pages.Num() != Last - First) return Fail(Failure, TEXT("History mutation range mismatch"));
    FString PreviousRoot = Previous.HotValues.Quests.HistoryRootDigest;
    TArray<FName> OldTail;
    if (OldCount % QH::IdsPerPage)
    {
        const auto* Record = OldPages.Find(First); QH::FPagePtr Tail;
        if (!Record || !DecodePage(*Record, Character, First, Tail, Failure) || Tail->GetRootDigest() != PreviousRoot || Tail->GetIds().Num() != OldCount % QH::IdsPerPage)
            return Fail(Failure, TEXT("Prior tail does not match core"));
        OldTail = Tail->GetIds(); PreviousRoot = Tail->GetPreviousRootDigest();
    }
    TSet<FName> NewIds;
    for (int32 Index = First; Index < Last; ++Index)
    {
        const auto* M = Pages.FindRef(Index); if (!M) return Fail(Failure, TEXT("Missing history mutation"));
        FRecord Proposed; Proposed.Revision = M->ExpectedRevision + 1; Proposed.SchemaVersion = M->SchemaVersion; Proposed.Payload = M->Payload; Proposed.bDeleted = M->bDelete;
        QH::FPagePtr Page;
        const int32 ExpectedCount = Index + 1 < Last ? QH::IdsPerPage : NewCount - Index * QH::IdsPerPage;
        if (!DecodePage(Proposed, Character, Index, Page, Failure) || Page->GetPreviousRootDigest() != PreviousRoot || Page->GetIds().Num() != ExpectedCount)
            return Fail(Failure, TEXT("Page/core chain mismatch"));
        if (Index == First) for (int32 I = 0; I < OldTail.Num(); ++I)
            if (Page->GetIds()[I] != OldTail[I]) return Fail(Failure, TEXT("Stored completion prefix changed"));
        if ((Index != First || OldTail.IsEmpty()) && M->ExpectedRevision != 0) return Fail(Failure, TEXT("New history page must be absent"));
        for (FName Id : Page->GetIds()) { if (NewIds.Contains(Id)) return Fail(Failure, TEXT("Duplicate completion page IDs")); NewIds.Add(Id); }
        PreviousRoot = Page->GetRootDigest();
    }
    if (PreviousRoot != Next.HotValues.Quests.HistoryRootDigest) return Fail(Failure, TEXT("Final history root mismatch"));
    if (OldCount == 0) for (const auto& State : Previous.HotValues.Quests.States)
        if (State.bClaimed && !NewIds.Contains(State.QuestId)) return Fail(Failure, TEXT("Legacy completion would be lost"));
    for (const auto& State : Next.HotValues.Quests.States) if (NewIds.Contains(State.QuestId)) return Fail(Failure, TEXT("Claimed quest remains active"));
    return true;
}
}
