#pragma once

#include "CoreMinimal.h"

/** Immutable values only. No UObject, database connection or live mutation writer. */
namespace ProjectJ::QuestHistory
{
inline constexpr int32 IdsPerPage = 64;
inline constexpr int32 MaxPages = 1024;
inline constexpr int32 MaxIds = IdsPerPage * MaxPages;
inline constexpr int32 MaxIdLength = 128;
inline constexpr int32 QueryPageSize = 32;
/** Portable existing FSHA1 integrity checksum, not an authentication/signature primitive. */
inline constexpr int32 DigestLength = 40;

struct FHistoryBuilder;
class FPage;
class FView;
using FPagePtr = TSharedPtr<const FPage, ESPMode::ThreadSafe>;
using FViewPtr = TSharedPtr<const FView, ESPMode::ThreadSafe>;

class PROJECT_J_API FPage
{
public:
    FPage() = default;
    const TArray<FName>& GetIds() const { return Ids; }
    /** Hash of this page's canonical ID bytes only. */
    const FString& GetDigest() const { return Digest; }
    /** Sealed prefix before this page; empty for ordinal0. */
    const FString& GetPreviousRootDigest() const { return PreviousRootDigest; }
    /** Prefix+ordinal+count+page digest. The last page root is the view root. */
    const FString& GetRootDigest() const { return RootDigest; }
    int32 GetOrdinal() const { return Ordinal; }
private:
    friend struct FHistoryBuilder;
    int32 Ordinal = 0;
    TArray<FName> Ids;
    TSet<FName> Membership;
    FString Digest, PreviousRootDigest, RootDigest;
};

class PROJECT_J_API FView
{
public:
    FView() = default;
    int32 GetCount() const { return CompletedCount; }
    int32 GetPageCount() const { return Pages.Num(); }
    const FString& GetRootDigest() const { return Digest; }
private:
    friend struct FHistoryBuilder;
    TArray<FPagePtr> Pages;
    int32 CompletedCount = 0;
    FString Digest;
};

PROJECT_J_API FViewPtr Empty();
PROJECT_J_API int32 Count(const FViewPtr& View);
PROJECT_J_API const FString& RootDigest(const FViewPtr& View);
PROJECT_J_API bool IsValidId(FName Id);
/** At most1024 page-set probes; never called by active travel/event iteration. */
PROJECT_J_API bool Contains(const FViewPtr& View, FName Id);
PROJECT_J_API FPagePtr GetPage(const FViewPtr& View, int32 Ordinal);
PROJECT_J_API bool GetPrefixDigest(const FViewPtr& View, int32 FullPageCount, FString& OutDigest);
/** Offset0 is the most recently appended completion. Limit1..32; failure preserves Out. */
PROJECT_J_API bool GetRecentPage(const FViewPtr& View, int32 OffsetFromNewest, int32 Limit, TArray<FName>& Out);
/** Copies only the shared page-pointer vector and one tail of at most64 IDs. Failure preserves Out. */
PROJECT_J_API bool Append(const FViewPtr& Before, FName Id, FViewPtr& Out, FString& Failure);
/** Independent page codec validation. Cross-page uniqueness is checked by the view builders. */
PROJECT_J_API bool BuildPageValidated(int32 Ordinal, const FString& PreviousRootDigest,
    const TArray<FName>& Ids, const FString& ExpectedDigest, const FString& ExpectedRootDigest,
    FPagePtr& Out, FString& Failure);
/** Empty ExpectedRootDigest derives the checksum for trusted producers; stored positive heads must supply their checksum. */
PROJECT_J_API bool BuildValidated(const TArray<TArray<FName>>& Pages, int32 ExpectedCount,
    const FString& ExpectedRootDigest, FViewPtr& Out, FString& Failure);
/** Reuses sealed known prefix. Input starts at Count(Before)/64; include the old partial tail if present. */
PROJECT_J_API bool BuildExtendedValidated(const FViewPtr& Before, const TArray<TArray<FName>>& TailAndNewPages,
    int32 ExpectedCount, const FString& ExpectedRootDigest, FViewPtr& Out, FString& Failure);
}
