#include "Game/Project_JQuestHistory.h"
#include "Misc/SecureHash.h"
#include "Containers/StringConv.h"

namespace ProjectJ::QuestHistory
{
namespace
{
const FString EmptyDigest;
bool IsDigest(const FString& Value)
{
    if (Value.Len() != DigestLength) return false;
    for (TCHAR C : Value) if (!((C >= TEXT('0') && C <= TEXT('9')) || (C >= TEXT('A') && C <= TEXT('F')))) return false;
    return true;
}
void AddInt(TArray<uint8>& Bytes, uint32 Value)
{
    for (int32 Shift = 0; Shift < 32; Shift += 8) Bytes.Add(static_cast<uint8>(Value >> Shift));
}
void AddString(TArray<uint8>& Bytes, const FString& Value)
{
    FTCHARToUTF8 Utf8(*Value);
    AddInt(Bytes, static_cast<uint32>(Utf8.Length()));
    Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
}
FString Hash(const TArray<uint8>& Bytes)
{
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num(), Digest);
    return BytesToHex(Digest, UE_ARRAY_COUNT(Digest));
}
bool Fail(FString& Failure, const TCHAR* Message) { Failure = Message; return false; }
}

struct FHistoryBuilder
{
    static FPagePtr Page(int32 Ordinal, const FString& Prefix, const TArray<FName>& Ids, FString& Failure)
    {
        if (Ordinal < 0 || Ordinal >= MaxPages || Ids.IsEmpty() || Ids.Num() > IdsPerPage
            || (Ordinal == 0 ? !Prefix.IsEmpty() : !IsDigest(Prefix)))
        { Fail(Failure, TEXT("Invalid completion page shape or prefix.")); return nullptr; }
        auto Result = MakeShared<FPage, ESPMode::ThreadSafe>();
        Result->Ordinal = Ordinal;
        Result->PreviousRootDigest = Prefix;
        TArray<uint8> Bytes;
        AddString(Bytes, TEXT("ProjectJ.QuestHistory.Page.1"));
        AddInt(Bytes, static_cast<uint32>(Ids.Num()));
        for (FName Id : Ids)
        {
            if (!IsValidId(Id) || Result->Membership.Contains(Id))
            { Fail(Failure, TEXT("Invalid or duplicate completion ID.")); return nullptr; }
            Result->Membership.Add(Id);
            AddString(Bytes, Id.ToString().ToLower());
        }
        Result->Ids = Ids;
        Result->Digest = Hash(Bytes);
        Bytes.Reset();
        AddString(Bytes, TEXT("ProjectJ.QuestHistory.Chain.1"));
        AddString(Bytes, Prefix);
        AddInt(Bytes, static_cast<uint32>(Ordinal));
        AddInt(Bytes, static_cast<uint32>(Ids.Num()));
        AddString(Bytes, Result->Digest);
        Result->RootDigest = Hash(Bytes);
        return Result;
    }
    static FViewPtr View(TArray<FPagePtr>&& Pages, int32 CompletedCount)
    {
        auto Result = MakeShared<FView, ESPMode::ThreadSafe>();
        Result->Pages = MoveTemp(Pages);
        Result->CompletedCount = CompletedCount;
        if (!Result->Pages.IsEmpty()) Result->Digest = Result->Pages.Last()->RootDigest;
        return Result;
    }
    static bool Has(const FViewPtr& View, FName Id)
    {
        if (View) for (const auto& Page : View->Pages) if (Page->Membership.Contains(Id)) return true;
        return false;
    }
    static FPagePtr At(const FViewPtr& View, int32 Ordinal)
    { return View && View->Pages.IsValidIndex(Ordinal) ? View->Pages[Ordinal] : nullptr; }
    static TArray<FPagePtr> CopyPages(const FViewPtr& View)
    { return View ? View->Pages : TArray<FPagePtr>(); }
};

FViewPtr Empty()
{
    static const FViewPtr Value = MakeShared<FView, ESPMode::ThreadSafe>();
    return Value;
}
int32 Count(const FViewPtr& View) { return View ? View->GetCount() : 0; }
const FString& RootDigest(const FViewPtr& View) { return View ? View->GetRootDigest() : EmptyDigest; }
bool IsValidId(FName Id) { return !Id.IsNone() && Id.ToString().Len() <= MaxIdLength; }
bool Contains(const FViewPtr& View, FName Id) { return !Id.IsNone() && FHistoryBuilder::Has(View, Id); }
FPagePtr GetPage(const FViewPtr& View, int32 Ordinal) { return FHistoryBuilder::At(View, Ordinal); }
bool GetPrefixDigest(const FViewPtr& View, int32 FullPageCount, FString& OutDigest)
{
    if (FullPageCount < 0 || FullPageCount > Count(View) / IdsPerPage) return false;
    FString Result;
    if (FullPageCount) Result = GetPage(View, FullPageCount - 1)->GetRootDigest();
    OutDigest = MoveTemp(Result);
    return true;
}
bool GetRecentPage(const FViewPtr& View, int32 OffsetFromNewest, int32 Limit, TArray<FName>& Out)
{
    const int32 Size = Count(View);
    if (Limit < 1 || Limit > QueryPageSize || OffsetFromNewest < 0
        || (Size == 0 ? OffsetFromNewest != 0 : OffsetFromNewest >= Size)) return false;
    TArray<FName> Result;
    for (int32 Index = Size - 1 - OffsetFromNewest; Index >= 0 && Result.Num() < Limit; --Index)
        Result.Add(GetPage(View, Index / IdsPerPage)->GetIds()[Index % IdsPerPage]);
    Out = MoveTemp(Result);
    return true;
}
bool Append(const FViewPtr& Before, FName Id, FViewPtr& Out, FString& Failure)
{
    if (!IsValidId(Id) || Count(Before) >= MaxIds || Contains(Before, Id))
        return Fail(Failure, TEXT("Completion ID is invalid, repeated or at capacity."));
    TArray<FPagePtr> Pages = FHistoryBuilder::CopyPages(Before);
    const int32 Ordinal = Count(Before) / IdsPerPage;
    TArray<FName> Ids;
    FString Prefix = RootDigest(Before);
    if (Count(Before) % IdsPerPage)
    {
        Ids = Pages.Last()->GetIds();
        Prefix = Pages.Last()->GetPreviousRootDigest();
        Pages.Pop(EAllowShrinking::No);
    }
    Ids.Add(Id);
    FPagePtr Page = FHistoryBuilder::Page(Ordinal, Prefix, Ids, Failure);
    if (!Page) return false;
    Pages.Add(MoveTemp(Page));
    Out = FHistoryBuilder::View(MoveTemp(Pages), Count(Before) + 1);
    Failure.Reset();
    return true;
}
bool BuildPageValidated(int32 Ordinal, const FString& PreviousRootDigest, const TArray<FName>& Ids,
    const FString& ExpectedDigest, const FString& ExpectedRootDigest, FPagePtr& Out, FString& Failure)
{
    FPagePtr Page = FHistoryBuilder::Page(Ordinal, PreviousRootDigest, Ids, Failure);
    if (!Page) return false;
    if (Page->GetDigest() != ExpectedDigest || Page->GetRootDigest() != ExpectedRootDigest)
        return Fail(Failure, TEXT("Completion page checksum mismatch."));
    Out = MoveTemp(Page);
    Failure.Reset();
    return true;
}
bool BuildValidated(const TArray<TArray<FName>>& RawPages, int32 ExpectedCount,
    const FString& ExpectedRootDigest, FViewPtr& Out, FString& Failure)
{
    if (ExpectedCount < 0 || ExpectedCount > MaxIds || RawPages.Num() != (ExpectedCount + IdsPerPage - 1) / IdsPerPage
        || (!ExpectedRootDigest.IsEmpty() && !IsDigest(ExpectedRootDigest)))
        return Fail(Failure, TEXT("Invalid completion history head."));
    TArray<FPagePtr> Pages;
    TSet<FName> Seen;
    FString Prefix;
    for (int32 Ordinal = 0; Ordinal < RawPages.Num(); ++Ordinal)
    {
        const auto& Ids = RawPages[Ordinal];
        if (Ids.Num() != FMath::Min(IdsPerPage, ExpectedCount - Ordinal * IdsPerPage))
            return Fail(Failure, TEXT("Noncanonical completion page length."));
        for (FName Id : Ids)
        {
            if (Seen.Contains(Id)) return Fail(Failure, TEXT("Repeated completion ID across pages."));
            Seen.Add(Id);
        }
        FPagePtr Page = FHistoryBuilder::Page(Ordinal, Prefix, Ids, Failure);
        if (!Page) return false;
        Prefix = Page->GetRootDigest();
        Pages.Add(MoveTemp(Page));
    }
    if (!ExpectedRootDigest.IsEmpty() && Prefix != ExpectedRootDigest)
        return Fail(Failure, TEXT("Completion history checksum mismatch."));
    Out = FHistoryBuilder::View(MoveTemp(Pages), ExpectedCount);
    Failure.Reset();
    return true;
}
bool BuildExtendedValidated(const FViewPtr& Before, const TArray<TArray<FName>>& TailAndNewPages,
    int32 ExpectedCount, const FString& ExpectedRootDigest, FViewPtr& Out, FString& Failure)
{
    const int32 OldCount = Count(Before), First = OldCount / IdsPerPage;
    if (ExpectedCount < OldCount || ExpectedCount > MaxIds || (ExpectedCount == 0 ? !ExpectedRootDigest.IsEmpty() : !IsDigest(ExpectedRootDigest)))
        return Fail(Failure, TEXT("Invalid extended completion history head."));
    const int32 PageCount = (ExpectedCount + IdsPerPage - 1) / IdsPerPage;
    if (TailAndNewPages.Num() != PageCount - First)
        return Fail(Failure, TEXT("Invalid extended completion page count."));
    TArray<FPagePtr> Pages;
    for (int32 Index = 0; Index < First; ++Index) Pages.Add(GetPage(Before, Index));
    FString Prefix = Pages.IsEmpty() ? FString() : Pages.Last()->GetRootDigest();
    TSet<FName> Added;
    for (int32 Local = 0; Local < TailAndNewPages.Num(); ++Local)
    {
        const int32 Ordinal = First + Local;
        const auto& Ids = TailAndNewPages[Local];
        if (Ids.Num() != FMath::Min(IdsPerPage, ExpectedCount - Ordinal * IdsPerPage))
            return Fail(Failure, TEXT("Noncanonical extended page length."));
        const int32 Preserved = Local == 0 ? OldCount % IdsPerPage : 0;
        for (int32 Index = 0; Index < Ids.Num(); ++Index)
        {
            if (Index < Preserved)
            {
                if (Ids[Index] != GetPage(Before, First)->GetIds()[Index])
                    return Fail(Failure, TEXT("The known completion tail was changed."));
            }
            else
            {
                if (Contains(Before, Ids[Index]) || Added.Contains(Ids[Index]))
                    return Fail(Failure, TEXT("Extended history repeats a completion ID."));
                Added.Add(Ids[Index]);
            }
        }
        FPagePtr Page = FHistoryBuilder::Page(Ordinal, Prefix, Ids, Failure);
        if (!Page) return false;
        Prefix = Page->GetRootDigest();
        Pages.Add(MoveTemp(Page));
    }
    if (Prefix != ExpectedRootDigest) return Fail(Failure, TEXT("Extended completion checksum mismatch."));
    Out = FHistoryBuilder::View(MoveTemp(Pages), ExpectedCount);
    Failure.Reset();
    return true;
}
}
