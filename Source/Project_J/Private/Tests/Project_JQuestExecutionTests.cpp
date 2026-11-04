#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Game/Project_JPlayerState.h"
#include "Game/Project_JQuestComponent.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "UObject/GarbageCollection.h"
#include "AIController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJQuestExecutionTest, "ProjectJ.Foundation.Quests.IndexedTravelExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJQuestExecutionTest::RunTest(const FString&)
{
	auto* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Owner = World->SpawnActor<AProject_JPlayerState>();
	auto* Quest = Owner->FindComponentByClass<UProject_JQuestComponent>();
	const auto AddDefinition = [&](FName Id, FName Event, bool Travel)
	{
		auto* Def = NewObject<UProject_JQuestDefinition>(Quest);
		Def->QuestId = Id; Def->ObjectiveEvent = Event; Def->bTravelObjective = Travel; Def->RequiredCount = 10000;
		Quest->Definitions.Add(Def); return Def;
	};
	auto* First = AddDefinition(TEXT("TravelA"), TEXT("Travel.EventA"), true);
	AddDefinition(TEXT("TravelB"), TEXT("Travel.EventB"), true);
	AddDefinition(TEXT("InactiveTravel"), TEXT("Interaction.Event"), true);
	AddDefinition(TEXT("Interaction"), TEXT("Interaction.Event"), false);
	for (int32 I = 0; I < 508; ++I) AddDefinition(FName(*FString::Printf(TEXT("Unused%d"), I)), TEXT("Other.Event"), false);
	TestTrue(TEXT("First active objective"), Quest->Accept(TEXT("TravelA")));
	TestTrue(TEXT("Second active objective"), Quest->Accept(TEXT("TravelB")));
	TestTrue(TEXT("Interaction objective"), Quest->Accept(TEXT("Interaction")));
	FProject_JQuestDefinitionIndex Index;
	Quest->BuildDefinitionIndex(Index);
	for (const auto& Def : Quest->Definitions) TestEqual(TEXT("Scoped index matches existing lookup"), Index.FindRef(Def->QuestId), Quest->FindDefinition(Def->QuestId));
	auto* Duplicate = AddDefinition(TEXT("TravelA"), TEXT("Other.Event"), false);
	Quest->BuildDefinitionIndex(Index);
	TestNull(TEXT("Duplicate ID cannot select an arbitrary definition"), Index.FindRef(TEXT("TravelA")));
	TestFalse(TEXT("Duplicate invalidates committed progress admission"), Quest->ValidateCommittedQuests(Quest->CaptureSnapshot()));
	Quest->Definitions.Remove(Duplicate);
	First->SchemaVersion = 0; Quest->BuildDefinitionIndex(Index);
	TestNull(TEXT("Live metadata change cannot leave a valid stale cache"), Index.FindRef(TEXT("TravelA")));
	First->SchemaVersion = 1;

	auto* Controller = World->SpawnActor<AAIController>(); Controller->SetPlayerState(Owner);
	auto* Pawn = World->SpawnActor<ACharacter>(); Controller->Possess(Pawn);
	Pawn->GetCharacterMovement()->Velocity = FVector(5000, 0, 0);
	Quest->SampleTravelAt(0);
	int32 Notifications = 0;
	Quest->OnChanged.AddLambda([&] { ++Notifications; });
	Pawn->SetActorLocation(FVector(2500, 0, 0)); Quest->SampleTravelAt(0.5);
	TestEqual(TEXT("Fast travel above the former 10m cutoff counts metres"), Quest->States[0].Count, 25);
	TestEqual(TEXT("All active objectives see the same sample"), Quest->States[1].Count, 25);
	TestEqual(TEXT("One sample publishes once for all changed objectives"), Notifications, 1);
	TestEqual(TEXT("An unaccepted travel definition cannot emit an unrelated event"), Quest->States[2].Count, 0);
	Quest->RecordObjective(TEXT("Interaction.Event"), 2);
	TestEqual(TEXT("Trusted gameplay event still progresses interaction"), Quest->States[2].Count, 2);

	const auto Committed = Quest->CaptureSnapshot();
	TestFalse(TEXT("Identical committed quest state does not request publication"), Quest->InstallCommittedQuests(Committed));
	bool bAdmitted = false; Quest->PersistentAdmission = [&] { return bAdmitted; };
	for (int32 I = 1; I <= 3; ++I) { Pawn->SetActorLocation(FVector(2500 + I * 2500, 0, 0)); Quest->SampleTravelAt(0.5 + I * 0.5); }
	TestEqual(TEXT("In-flight commit snapshot cannot be mutated by travel"), Quest->States[0].Count, 25);
	Quest->InstallCommittedQuests(Committed); bAdmitted = true; Quest->SampleTravelAt(2.5);
	TestEqual(TEXT("Bounded pending distance applies after committed installation"), Quest->States[0].Count, 100);
	const int32 BeforeWarp = Quest->States[0].Count;
	Pawn->SetActorLocation(FVector(1000000, 0, 0)); Quest->SampleTravelAt(3.0);
	TestEqual(TEXT("Warp-sized segment is rejected"), Quest->States[0].Count, BeforeWarp);
	Pawn->SetActorLocation(FVector(1002500, 0, 0)); Quest->SampleTravelAt(3.5);
	TestEqual(TEXT("Normal movement resumes after rebaseline"), Quest->States[0].Count, BeforeWarp + 25);
	Pawn->SetActorLocation(FVector(1010000, 0, 0)); Quest->SampleTravelAt(10.0);
	TestEqual(TEXT("Long unsampled gap is not extrapolated into progress"), Quest->States[0].Count, BeforeWarp + 25);
	const int32 BeforeStop = Quest->States[0].Count;
	Pawn->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	Pawn->SetActorLocation(FVector(1010100, 0, 0)); Quest->SampleTravelAt(10.5);
	TestEqual(TEXT("Final segment before stopping retains previous authoritative speed"), Quest->States[0].Count, BeforeStop + 1);
	auto* Compound = NewObject<UProject_JQuestDefinition>(Quest);
	Compound->QuestId = TEXT("CompoundExecution"); Compound->RewardExperience = 7;
	auto& Gather = Compound->Objectives.AddDefaulted_GetRef(); Gather.ObjectiveId = TEXT("Collect");
	Gather.Kind = EProject_JQuestObjectiveKind::GatherItem; Gather.TargetId = TEXT("HealthPotion"); Gather.RequiredCount = 2;
	auto& Craft = Compound->Objectives.AddDefaulted_GetRef(); Craft.ObjectiveId = TEXT("Brew");
	Craft.Kind = EProject_JQuestObjectiveKind::CraftRecipe; Craft.TargetId = TEXT("Prototype.Alchemy.HealthToMana");
	Quest->Definitions.Add(Compound); Quest->BuildDefinitionIndex(Index);
	TestTrue(TEXT("Real component accepts the compound AND quest"), Quest->Accept(Compound->QuestId));
	auto Proposed = Quest->CaptureSnapshot();
	TestTrue(TEXT("Gather plans on a captured value"), Quest->PlanActivityProgress(Proposed, EProject_JQuestObjectiveKind::GatherItem, TEXT("HealthPotion"), 1, 2, Index));
	TestEqual(TEXT("Planning leaves live component untouched"), Quest->States.Last().GetObjectiveCount(TEXT("Collect")), 0);
	TestTrue(TEXT("Validated captured progress installs once"), Quest->ValidateCommittedQuests(Proposed) && Quest->InstallCommittedQuests(Proposed));
	TestFalse(TEXT("First complete objective alone cannot claim an AND quest"), Quest->Claim(Compound->QuestId));
	TestTrue(TEXT("Craft plans the remaining objective"), Quest->PlanActivityProgress(Proposed, EProject_JQuestObjectiveKind::CraftRecipe, TEXT("Prototype.Alchemy.HealthToMana"), 1, 1, Index));
	Quest->InstallCommittedQuests(Proposed);
	Owner->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(Owner, Owner);
	auto* Progress = Owner->FindComponentByClass<UProject_JProgressionComponent>(); Progress->InitializeDefaults(nullptr, nullptr, 1);
	TestTrue(TEXT("Complete AND quest pays once through the existing standalone grant"), Quest->Claim(Compound->QuestId));
	TestEqual(TEXT("Single reward amount is preserved"), Progress->GetExperience(), int64(7));
	TestFalse(TEXT("Standalone repeated claim cannot pay again"), Quest->Claim(Compound->QuestId));
	TestTrue(TEXT("Claim archives its completion and retires only its current detail"), Quest->HasCompleted(Compound->QuestId)
        && !Quest->States.ContainsByPredicate([Compound](const auto& State) { return State.QuestId == Compound->QuestId; }));
    // Legacy files retain their original claimed rows until a real edit atomically promotes them.
    auto* HistoryOwner = World->SpawnActor<AProject_JPlayerState>();
    auto* HistoryQuest = HistoryOwner->FindComponentByClass<UProject_JQuestComponent>();
    FProject_JQuestSnapshot LegacyHistory;
    for (int32 HistoryIndex = 0; HistoryIndex < UProject_JQuestComponent::MaxCurrentQuestStates; ++HistoryIndex)
    {
        auto* HistoryDefinition = NewObject<UProject_JQuestDefinition>(HistoryQuest);
        HistoryDefinition->QuestId = FName(*FString::Printf(TEXT("Completed.%03d"), HistoryIndex));
        HistoryDefinition->ObjectiveEvent = TEXT("History.Event");
        HistoryQuest->Definitions.Add(HistoryDefinition);
        FProject_JQuestState HistoryState; HistoryState.QuestId = HistoryDefinition->QuestId;
        HistoryState.Count = 1; HistoryState.bClaimed = true; HistoryState.bTracked = false;
        LegacyHistory.States.Add(HistoryState);
    }
    auto* NextHistoryDefinition = NewObject<UProject_JQuestDefinition>(HistoryQuest);
    NextHistoryDefinition->QuestId = TEXT("BeyondLegacy128"); NextHistoryDefinition->ObjectiveEvent = TEXT("NextHistory.Event");
    NextHistoryDefinition->RewardExperience = 9; HistoryQuest->Definitions.Add(NextHistoryDefinition);
    TestTrue(TEXT("Original legacy128 claimed rows still validate without implicit migration"), HistoryQuest->ValidateCommittedQuests(LegacyHistory));
    TestTrue(TEXT("Original legacy history installs through the existing public contract"), HistoryQuest->InstallCommittedQuests(LegacyHistory));
    TestEqual(TEXT("Capture preserves original legacy schema until a mutation"), HistoryQuest->CaptureSnapshot().Version, 1);
    TestEqual(TEXT("Legacy completions are visible before durable promotion"), HistoryQuest->GetCompletedCount(), 128);
    TestTrue(TEXT("Legacy known completion requires no query or reaccept"), HistoryQuest->HasCompleted(TEXT("Completed.000")));
    int32 HistoryGameplayNotifications = 0, HistoryPageNotifications = 0;
    const auto HistoryGameplayHandle = HistoryQuest->OnChanged.AddLambda([&] { ++HistoryGameplayNotifications; });
    const auto HistoryPageHandle = HistoryQuest->OnHistoryPageChanged.AddLambda([&] { ++HistoryPageNotifications; });
    ON_SCOPE_EXIT { HistoryQuest->OnChanged.Remove(HistoryGameplayHandle); HistoryQuest->OnHistoryPageChanged.Remove(HistoryPageHandle); };
    const FGuid HistoryRequest = HistoryQuest->RequestCompletedPage(0);
    TestTrue(TEXT("Public query traverses actual native request and response handlers"), HistoryRequest.IsValid());
    auto HistoryPage = HistoryQuest->GetHistoryPage();
    TestTrue(TEXT("Legacy history query returns a coherent bounded newest32 page"), HistoryPage.Status == EProject_JQuestHistoryPageStatus::Fresh
        && HistoryPage.Ids.Num() == 32 && HistoryPage.Ids[0] == FName(TEXT("Completed.127")));
    TestTrue(TEXT("A confirmed page exposes the actual accepted response nonce and ordinal"),
        HistoryPage.Request == HistoryRequest && HistoryPage.RequestedOrdinal == 0);
    TestEqual(TEXT("Read-only history page does not wake gameplay/checkpoint listeners"), HistoryGameplayNotifications, 0);
    FGuid CancelledHistoryNonce;
    const auto CancelDuringPending = HistoryQuest->OnHistoryPageChanged.AddLambda([&]
    {
        const auto PendingPage = HistoryQuest->GetHistoryPage();
        if (PendingPage.Status != EProject_JQuestHistoryPageStatus::Pending) return;
        CancelledHistoryNonce = PendingPage.Request;
        const int32 BeforeExactCancel = HistoryPageNotifications;
        HistoryQuest->CancelHistoryPageRequest(FGuid::NewGuid());
        TestTrue(TEXT("Wrong nonce cannot cancel another UI's pending history request"), HistoryQuest->GetHistoryPage().Request == PendingPage.Request);
        HistoryQuest->CancelHistoryPageRequest(PendingPage.Request);
        TestEqual(TEXT("Exact source cleanup emits no reentrant query notification"), HistoryPageNotifications, BeforeExactCancel);
    });
    const FGuid CancelRequest = HistoryQuest->RequestCompletedPage(1);
    HistoryQuest->OnHistoryPageChanged.Remove(CancelDuringPending);
    TestTrue(TEXT("Pending callback can cancel the exact request before native dispatch"), CancelRequest.IsValid() && CancelRequest == CancelledHistoryNonce);
    TestTrue(TEXT("Exact cancellation keeps the last confirmed page readable"), HistoryQuest->GetHistoryPage().bHasPage);
    TestEqual(TEXT("Cancelling a pending request preserves the last confirmed response identity"),
        HistoryQuest->GetHistoryPage().Request, HistoryRequest);
    auto InvalidLegacy = LegacyHistory; InvalidLegacy.States[0].Count = 0;
    HistoryQuest->InstallCommittedQuests(InvalidLegacy);
    TestFalse(TEXT("Acceptance cannot hide a corrupt claimed legacy detail by normalization"), HistoryQuest->Accept(NextHistoryDefinition->QuestId));
    TestEqual(TEXT("Rejected legacy promotion leaves original detail count intact"), HistoryQuest->States.Num(), 128);
    HistoryQuest->InstallCommittedQuests(LegacyHistory);
    TestTrue(TEXT("128 old completions do not consume current detail capacity"), HistoryQuest->Accept(NextHistoryDefinition->QuestId));
    auto PromotedHistory = HistoryQuest->CaptureSnapshot();
    TestTrue(TEXT("Acceptance publishes schema3 with shared immutable history and one current detail"), PromotedHistory.Version == 3
        && PromotedHistory.HistoryCount == 128 && PromotedHistory.States.Num() == 1 && PromotedHistory.HasHydratedHistory());
    TestFalse(TEXT("Archived non-repeatable quest cannot be accepted again"), HistoryQuest->Accept(TEXT("Completed.000")));
    HistoryQuest->RecordObjective(TEXT("NextHistory.Event"));
    HistoryOwner->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(HistoryOwner, HistoryOwner);
    auto* HistoryProgression = HistoryOwner->FindComponentByClass<UProject_JProgressionComponent>();
    HistoryProgression->InitializeDefaults(nullptr, nullptr, 1);
    TestTrue(TEXT("Next standalone claim archives detail and grants XP once"), HistoryQuest->Claim(NextHistoryDefinition->QuestId));
    TestEqual(TEXT("Completion history extends beyond original lifetime128"), HistoryQuest->GetCompletedCount(), 129);
    TestEqual(TEXT("No claimed detail remains in current journal"), HistoryQuest->States.Num(), 0);
    TestEqual(TEXT("Archived claim keeps exact reward amount"), HistoryProgression->GetExperience(), int64(9));
    TestFalse(TEXT("Standalone archived repeat claim preserves existing false contract"), HistoryQuest->Claim(NextHistoryDefinition->QuestId));
    const auto CommittedHistory = HistoryQuest->CaptureSnapshot();
    HistoryQuest->Definitions.Reset();
    TestTrue(TEXT("Retired completion definitions are not required for schema3 state validation"), HistoryQuest->ValidateCommittedQuests(CommittedHistory));
    TestTrue(TEXT("Completed membership survives removal of old definitions"), HistoryQuest->HasCompleted(TEXT("Completed.000")));
    int32 ArchivedSubmissions = 0;
    HistoryQuest->SubmitPersistentEdit = [&](const auto&, auto) { ++ArchivedSubmissions; return false; };
    TestTrue(TEXT("Persistent archived repeat claim preserves existing idempotent true contract"), HistoryQuest->Claim(TEXT("Completed.000")));
    TestEqual(TEXT("Idempotent archived claim queues no new reward edit"), ArchivedSubmissions, 0);
    HistoryQuest->SubmitPersistentEdit = {};
	// Bootstrap callbacks may replace the authored array and collect old defaults.
	auto* BootstrapOwner = World->SpawnActor<AProject_JPlayerState>();
	auto* Bootstrap = BootstrapOwner->FindComponentByClass<UProject_JQuestComponent>();
	auto* A = NewObject<UProject_JQuestDefinition>(Bootstrap); A->QuestId = TEXT("BootstrapA"); A->ObjectiveEvent = TEXT("Metres"); A->bTravelObjective = true;
	auto* B = NewObject<UProject_JQuestDefinition>(Bootstrap); B->QuestId = TEXT("BootstrapB"); B->ObjectiveEvent = TEXT("Metres"); B->bTravelObjective = true;
	Bootstrap->Definitions = {A, B};
	Bootstrap->OnChanged.AddLambda([Bootstrap] { Bootstrap->Definitions.Reset(); CollectGarbage(RF_NoFlags, true); });
	TestTrue(TEXT("Definition replacement/GC cannot invalidate native bootstrap IDs"), Bootstrap->PrepareForProfile());
	TestEqual(TEXT("Retired later defaults are not automatically accepted"), Bootstrap->States.Num(), 1);
	Quest->DestroyComponent();
	const int32 BeforeLate = Quest->States[0].Count;
	Quest->RecordObjective(TEXT("Travel.EventA"), 100);
	TestEqual(TEXT("Retired component cannot consume late events"), Quest->States[0].Count, BeforeLate);
	return true;
}
#endif
