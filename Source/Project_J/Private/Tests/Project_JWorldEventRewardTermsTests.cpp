#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "World/Project_JWorldEncounter.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJWorldEventRewardTermsTest, "ProjectJ.World.EventRewardTerms",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJWorldEventRewardTermsTest::RunTest(const FString&)
{
    const FString PreviousCommandLine(FCommandLine::Get());
    FCommandLine::Set(TEXT("-ProjectJDevelopmentServerProfiles"));
    ON_SCOPE_EXIT { FCommandLine::Set(*PreviousCommandLine); };
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Event = World->SpawnActor<AProject_JWorldEncounter>();
    if (!TestNotNull(TEXT("Native event owner"), Event)) return false;
    const FGuid Round = FGuid::NewGuid(), Character = FGuid::NewGuid();
    Event->Contributions.Begin(Round);
    Event->FrozenDevelopmentRewardXP.Add(Round, 25);
    int64 XP = -1;
    TestFalse(TEXT("Unfinished round never pays"), Event->GetDevelopmentRewardTerms(Round, Character, XP));
    TestEqual(TEXT("Failure clears XP output"), XP, int64(0));
    Event->Contributions.Record(Round, Character, 1); Event->Contributions.Close(Round, true);
    TestTrue(TEXT("Closed eligible round reads frozen terms"), Event->GetDevelopmentRewardTerms(Round, Character, XP));
    TestEqual(TEXT("Frozen test reward"), XP, int64(25));
    Event->DevelopmentRewardExperience = 999;
    TestTrue(TEXT("Editing authoring value does not revise settled terms"), Event->GetDevelopmentRewardTerms(Round, Character, XP));
    TestEqual(TEXT("Settled XP unchanged"), XP, int64(25));
    TestFalse(TEXT("Different character cannot claim"), Event->GetDevelopmentRewardTerms(Round, FGuid::NewGuid(), XP));
    TestFalse(TEXT("Different round cannot claim"), Event->GetDevelopmentRewardTerms(FGuid::NewGuid(), Character, XP));
    FCommandLine::Set(TEXT(""));
    TestFalse(TEXT("Development flag required at read time"), Event->GetDevelopmentRewardTerms(Round, Character, XP));
    FCommandLine::Set(TEXT("-ProjectJDevelopmentServerProfiles"));
    Event->Contributions.Invalidate(Round);
    TestFalse(TEXT("Incomplete proof never pays"), Event->GetDevelopmentRewardTerms(Round, Character, XP));
    return true;
}
#endif
