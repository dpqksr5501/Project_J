#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Backend/Project_JLocalProfileSubsystem.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Player.h"
#include "Engine/World.h"
#include "Game/Project_JGameMode.h"
#include "Game/Project_JPlayerState.h"
#include "Game/Project_JQuestComponent.h"
#include "Components/Project_JInventoryComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Project_JAbilitySystemComponent.h"
#include "UObject/StrongObjectPtr.h"
#include <atomic>

struct FProjectJBusyReconnectTestAccess
{
    static void SetPath(UProject_JLocalProfileSubsystem& Profiles, const FString& Path) { Profiles.DatabasePath = Path; }
};
namespace
{
struct FBusyReply { bool Done = false, OK = false; };
struct FBlockedProfileWriter
{
    FEvent* Release = FPlatformProcess::GetSynchEventFromPool(true);
    std::atomic<bool> Entered{false}, TimedOut{false};
    ~FBlockedProfileWriter() { FPlatformProcess::ReturnSynchEventToPool(Release); }
};
class FBusyReconnect : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    TStrongObjectPtr<UGameInstance> GI;
    UWorld* World;
    UProject_JLocalProfileSubsystem* Profiles;
    FString OriginalCommandLine;
    FProject_JAccountId Account = FProject_JAccountId::NewId();
    FProject_JCharacterId Character = FProject_JCharacterId::NewId();
    TWeakObjectPtr<AProject_JPlayerState> Player;
    TSharedPtr<FBusyReply> Ready, RetiredReady, OldCommit, NewCommit;
    TSharedPtr<FBlockedProfileWriter, ESPMode::ThreadSafe> Block = MakeShared<FBlockedProfileWriter, ESPMode::ThreadSafe>();
    double Started = FPlatformTime::Seconds();
    int32 Step = 0;
    bool Check(bool OK, const TCHAR* Text) { return Test->TestTrue(Text, OK); }
    bool OpenReplacement()
    {
        auto* PC = World->SpawnActor<APlayerController>();
        if (!PC) return false;
        PC->SetAsLocalPlayerController();
        if (!PC->GetPlayerState<AProject_JPlayerState>()) PC->InitPlayerState();
        PC->SetPlayer(NewObject<UPlayer>(GI.Get()));
        auto* PS = PC->GetPlayerState<AProject_JPlayerState>();
        if (!PS) return false;
        auto* Mode = CastChecked<AProject_JGameMode>(World->GetAuthGameMode());
        ProjectJ::MMO::FServerSessionAttempt Attempt; ProjectJ::MMO::FServerSessionLease Lease;
        if (!Mode->BeginTrustedSession(PC, Account, Character, Attempt) || !Mode->CompleteTrustedSession(Attempt, true, Lease)) return false;
        Player = PS; Ready = MakeShared<FBusyReply>(); const auto Out = Ready;
        Profiles->OpenTrustedDevelopmentProfile(PS, [Out](bool OK) { Out->Done = true; Out->OK = OK; });
        return true;
    }
public:
    FBusyReconnect(FAutomationTestBase* InTest, UGameInstance* InGI)
        : Test(InTest), GI(InGI), World(InGI->GetWorld()), Profiles(InGI->GetSubsystem<UProject_JLocalProfileSubsystem>())
    {
        OriginalCommandLine = FCommandLine::Get();
        FCommandLine::Set(*(OriginalCommandLine + TEXT(" -ProjectJDevelopmentServerProfiles")));
    }
    ~FBusyReconnect()
    {
        Block->Release->Trigger(); // Always release test worker before shutdown waits, including failed assertions.
        if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
        GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
        FCommandLine::Set(*OriginalCommandLine);
    }
    bool Update() override
    {
        if (FPlatformTime::Seconds() - Started > 60) { Test->AddError(TEXT("Busy reconnect fixture timed out")); return true; }
        if (Step == 0) { if (!Check(OpenReplacement(), TEXT("Initial verified profile opens"))) return true; Step = 1; return false; }
        auto* PS = Player.Get();
        if (Step == 1)
        {
            if (!Ready->Done) return false;
            if (!Check(Ready->OK && Profiles->IsReady(PS), TEXT("Initial profile restored"))) return true;
            PS->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(PS, nullptr);
            PS->FindComponentByClass<UProject_JProgressionComponent>()->InitializeDefaults(nullptr, nullptr, 1);
            Step = 2; return false;
        }
        if (Step == 2)
        {
            if (Profiles->IsCheckpointPending(PS)) return false;
            const auto Worker = Block;
            Profiles->SetCommandStageHookForTest(PS, [Worker](const TCHAR* Stage)
            {
                if (!FCString::Strcmp(Stage, TEXT("BeforeCommandEncode")))
                {
                    Worker->Entered.store(true);
                    if (!Worker->Release->Wait(30000)) Worker->TimedOut.store(true);
                }
            });
            FProject_JPersistentEdit Edit; Edit.Kind = EProject_JPersistentEdit::Experience;
            Edit.Request = Profiles->NewRequestId(PS); Edit.Amount = 1;
            OldCommit = MakeShared<FBusyReply>(); const auto Out = OldCommit;
            if (!Check(Profiles->SubmitEdit(PS, Edit, [Out](bool OK, bool) { Out->Done = true; Out->OK = OK; }), TEXT("Old epoch command accepted before disconnect"))) return true;
            Step = 3; return false;
        }
        if (Step == 3)
        {
            if (!Block->Entered.load()) return false;
            if (!Check(OpenReplacement(), TEXT("Replacement session accepted while old worker owns gate"))) return true;
            if (!Check(!Ready->Done && Profiles->HasProfileBinding(Player.Get()) && !Profiles->IsReady(Player.Get()) &&
                Profiles->IsCheckpointPending(Player.Get()), TEXT("Gate Busy queues exact trusted open, neither fails callback nor exposes ready defaults"))) return true;
            auto* PendingPS = Player.Get();
            const auto* Progress = PendingPS->FindComponentByClass<UProject_JProgressionComponent>();
            const auto* Quests = PendingPS->FindComponentByClass<UProject_JQuestComponent>();
            const auto* Inventory = PendingPS->GetInventoryComponent();
            if (!Check(Progress->PersistentAdmission && !Progress->PersistentAdmission() &&
                Quests->PersistentAdmission && !Quests->PersistentAdmission() &&
                Inventory->PersistentAdmission && !Inventory->PersistentAdmission(),
                TEXT("Queued trusted profile denies component mutations instead of falling back to prototype writers"))) return true;
            RetiredReady = Ready;
            // Replacement while queued must not inherit the predecessor's open callback/lease.
            if (!Check(OpenReplacement(), TEXT("Newer replacement retires an already queued open"))) return true;
            if (!Check(!Ready->Done && !RetiredReady->Done && Profiles->HasProfileBinding(Player.Get()), TEXT("Only current pending owner waits for writer drain"))) return true;
            Block->Release->Trigger(); Step = 4; return false;
        }
        if (Step == 4)
        {
            if (!Ready->Done || Profiles->IsCheckpointPending(PS)) return false;
            if (!Check(!Block->TimedOut.load() && Ready->OK && Profiles->IsReady(PS) && !OldCommit->Done && !RetiredReady->Done &&
                PS->FindComponentByClass<UProject_JProgressionComponent>()->GetExperience() == 1,
                TEXT("Writer-drain notification restores committed XP only to newest epoch without manual retry"))) return true;
            FProject_JPersistentEdit Edit; Edit.Kind = EProject_JPersistentEdit::Experience;
            Edit.Request = Profiles->NewRequestId(PS); Edit.Amount = 1;
            NewCommit = MakeShared<FBusyReply>(); const auto Out = NewCommit;
            if (!Check(Profiles->SubmitEdit(PS, Edit, [Out](bool OK, bool) { Out->Done = true; Out->OK = OK; }), TEXT("Recovered session accepts normal command"))) return true;
            Step = 5; return false;
        }
        if (Step == 5)
        {
            if (!NewCommit->Done) return false;
            Check(NewCommit->OK && PS->FindComponentByClass<UProject_JProgressionComponent>()->GetExperience() == 2,
                TEXT("Recovered profile commits normally after busy reconnect")); return true;
        }
        return false;
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJTrustedProfileBusyReconnectTest, "ProjectJ.MMO.Persistence.TrustedProfileBusyReconnect",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJTrustedProfileBusyReconnectTest::RunTest(const FString&)
{
    auto* GI = NewObject<UGameInstance>(GEngine); GI->InitializeStandalone(); auto* World = GI->GetWorld();
    auto* Data = GI->GetSubsystem<UProject_JCharacterDataSubsystem>();
    FURL URL; URL.AddOption(TEXT("game=/Script/Project_J.Project_JNetworkLoadGameMode"));
    if (!TestTrue(TEXT("Trusted native test world available"), Data && Data->LoadPrimaryAssetDefinitions() && World->SetGameMode(URL)))
    { GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return false; }
    CastChecked<AProject_JGameMode>(World->GetAuthGameMode())->bRequireTrustedSessions = true;
    FProjectJBusyReconnectTestAccess::SetPath(*GI->GetSubsystem<UProject_JLocalProfileSubsystem>(),
        FPaths::ProjectSavedDir() / TEXT("Validation/TrustedBusyReconnect") / FGuid::NewGuid().ToString() / TEXT("Local.sqlite"));
    World->InitializeActorsForPlay(URL); World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
    ADD_LATENT_AUTOMATION_COMMAND(FBusyReconnect(this, GI)); return true;
}
#endif
