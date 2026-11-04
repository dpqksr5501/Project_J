#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Backend/Project_JLocalProfileSnapshot.h"
#include "Game/Project_JQuestHistory.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJQuestHistoryValuesTest,
    "ProjectJ.Foundation.Quests.PagedHistoryValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJQuestHistoryValuesTest::RunTest(const FString&)
{
    namespace QH = ProjectJ::QuestHistory;
    // One cold bulk build represents a long-lived character, rather than 65K
    // per-quest gameplay executions or an artificial micro-timing benchmark.
    TArray<TArray<FName>> Pages;
    Pages.SetNum(QH::MaxPages);
    for (int32 Index = 0; Index < QH::MaxIds; ++Index)
        Pages[Index / QH::IdsPerPage].Add(FName(*FString::Printf(TEXT("LongLivedQuest_%05d"), Index)));
    QH::FViewPtr Full;
    FString Failure;
    if (!TestTrue(TEXT("The full declared lifetime budget builds as bounded immutable pages"),
        QH::BuildValidated(Pages, QH::MaxIds, {}, Full, Failure))) return false;
    TestEqual(TEXT("Lifetime completions are independent of the active128 and former proposed1024 caps"), QH::Count(Full), 65536);
    TestEqual(TEXT("The declared maximum uses exactly1024 bounded pages"), Full->GetPageCount(), 1024);
    TestTrue(TEXT("Both oldest and newest completed identities remain exact membership facts"),
        QH::Contains(Full, Pages[0][0]) && QH::Contains(Full, Pages.Last().Last()));
    auto Unchanged = Full;
    TestFalse(TEXT("Capacity rejects another completion without discarding old history"),
        QH::Append(Full, TEXT("PastLifetimeBudget"), Unchanged, Failure));
    TestTrue(TEXT("A failed append preserves the caller's output view"), Unchanged == Full);

    TArray<TArray<FName>> InitialPages{Pages[0], {Pages[1][0]}};
    QH::FViewPtr Before;
    if (!TestTrue(TEXT("A two-page partial archive builds through the normal value contract"),
        QH::BuildValidated(InitialPages, 65, {}, Before, Failure))) return false;
    const auto Sealed = QH::GetPage(Before, 0);
    const auto OldTail = QH::GetPage(Before, 1);
    const FString OriginalRoot = QH::RootDigest(Before);
    QH::FViewPtr After;
    if (!TestTrue(TEXT("A normal completion appends to the partial tail"),
        QH::Append(Before, Pages[1][1], After, Failure))) return false;
    TestTrue(TEXT("Append reuses the sealed page and replaces only the partial tail"),
        QH::GetPage(After, 0) == Sealed && QH::GetPage(After, 1) != OldTail);
    TestTrue(TEXT("The preceding snapshot and its tail are immutable after append"),
        QH::Count(Before) == 65 && OldTail->GetIds().Num() == 1 && QH::RootDigest(Before) == OriginalRoot &&
        !QH::Contains(Before, Pages[1][1]) && QH::Contains(After, Pages[1][1]));
    Unchanged = Before;
    TestFalse(TEXT("Duplicate completion cannot append or alter an output snapshot"),
        QH::Append(Before, Pages[0][0], Unchanged, Failure));
    TestTrue(TEXT("Duplicate rejection leaves the supplied output untouched"), Unchanged == Before);

    TArray<TArray<FName>> NewTail{{Pages[1][0], Pages[1][1]}};
    QH::FViewPtr Extended;
    TestTrue(TEXT("Hydrating a changed tail reuses an already verified immutable prefix"),
        QH::BuildExtendedValidated(Before, NewTail, 66, QH::RootDigest(After), Extended, Failure) &&
        QH::GetPage(Extended, 0) == Sealed);
    NewTail[0][1] = Pages[0][0];
    Unchanged = Before;
    TestFalse(TEXT("A replacement tail cannot repeat a completed ID from the sealed prefix"),
        QH::BuildExtendedValidated(Before, NewTail, 66, QH::RootDigest(After), Unchanged, Failure));
    TestTrue(TEXT("Invalid extension preserves output and the old sealed page"), Unchanged == Before && QH::GetPage(Before, 0) == Sealed);
    TArray<FName> Recent;
    TestTrue(TEXT("A read selects only32 most recent completions without flattening the history"),
        QH::GetRecentPage(Full, 0, 32, Recent) && Recent.Num() == 32 && Recent[0] == Pages.Last().Last());
    const auto ConfirmedPage = Recent;
    TestFalse(TEXT("An out-of-range history query cannot discard the last confirmed page"),
        QH::GetRecentPage(Full, QH::MaxIds + 1, 32, Recent));
    TestTrue(TEXT("Failed history reads preserve their bounded output"), Recent == ConfirmedPage);

    FProject_JLocalProfileSnapshot SmallCore;
    SmallCore.Account = FGuid::NewGuid(); SmallCore.Character = FGuid::NewGuid();
    SmallCore.RulesFingerprint = FString::ChrN(40, 'a');
    SmallCore.Quests.Version = 3; SmallCore.Quests.History = Before;
    SmallCore.Quests.HistoryCount = QH::Count(Before); SmallCore.Quests.HistoryRootDigest = QH::RootDigest(Before);
    auto LargeCore = SmallCore;
    LargeCore.Quests.History = Full; LargeCore.Quests.HistoryCount = QH::Count(Full);
    LargeCore.Quests.HistoryRootDigest = QH::RootDigest(Full);
    auto HotEdit = LargeCore;
    ++HotEdit.Progression.Experience;
    TestTrue(TEXT("Ordinary snapshot/XP edits share the full immutable history instead of copying its IDs"),
        HotEdit.Quests.History == Full && LargeCore.Quests.History == Full);
    TArray<uint8> SmallBytes, LargeBytes, EditedBytes;
    if (!TestTrue(TEXT("Small, large and ordinary-edited profiles encode the bounded hot core"),
        SmallCore.Encode(SmallBytes) && LargeCore.Encode(LargeBytes) && HotEdit.Encode(EditedBytes))) return false;
    TestTrue(TEXT("Adding more than65K completions grows only the core's count digits, not its history payload"),
        LargeBytes.Num() < 1024 && FMath::Abs(LargeBytes.Num() - SmallBytes.Num()) <= 3 &&
        FMath::Abs(EditedBytes.Num() - LargeBytes.Num()) <= 1);
    FProject_JStoredProfileCore Stored;
    TestTrue(TEXT("The explicit core decoder exposes the expected metadata as not yet hydrated"),
        FProject_JLocalProfileSnapshot::DecodeStoredCore(LargeBytes, Stored) && Stored.HotValues.Format == 4 &&
        Stored.HotValues.Quests.HistoryCount == QH::MaxIds && !Stored.HotValues.Quests.HasHydratedHistory());
    auto Sentinel = SmallCore;
    TestFalse(TEXT("The old full-value decoder cannot publish a core without its referenced history pages"),
        FProject_JLocalProfileSnapshot::Decode(LargeBytes, Sentinel));
    TestTrue(TEXT("A failed full decode preserves the original hydrated output"), Sentinel.Quests.History == Before);
    return true;
}
#endif
