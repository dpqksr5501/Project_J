#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Backend/Project_JLocalProfileSubsystem.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Player.h"
#include "Engine/World.h"
#include "Economy/Project_JEconomySubsystem.h"
#include "Components/Project_JInventoryComponent.h"
#include "Game/Project_JGameMode.h"
#include "Game/Project_JPlayerState.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Project_JAbilitySystemComponent.h"
#include "Testing/Project_JNetworkLoadFixture.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
class FTrustedProfileLifecycle : public IAutomationLatentCommand
{
	FAutomationTestBase* Test;
	TStrongObjectPtr<UGameInstance> GI;
	UWorld* World;
	AProject_JNetworkLoadGameMode* Mode;
	UProject_JLocalProfileSubsystem* Profiles;
	APlayerController* FirstPC;
	APlayerController* SecondPC;
	AProject_JPlayerState* First;
	AProject_JPlayerState* Second;
	TStrongObjectPtr<UProject_JCharacterClassDefinition> ClassDef;
	FProject_JAccountId Account;
	FProject_JCharacterId Character;
	ProjectJ::MMO::FServerSessionLease FirstLease;
	ProjectJ::MMO::FServerSessionLease SecondLease;
	FString TempDir;
	double StartTime = FPlatformTime::Seconds();
	int32 Step = 0;
	int32 CommittedLevel = 0;
	int64 CommittedExperience = 0;
	bool bOpenDone = false;
	bool bOpenOk = false;
	bool bBackupDone = false;
	bool bBackupOk = false;
	FString BackupPath;
	FGuid MailId, AttachmentId;
	bool bMailDone = false;
	bool bMailProjected = false;

public:
	FTrustedProfileLifecycle(FAutomationTestBase* InTest, UGameInstance* InGI, AProject_JNetworkLoadGameMode* InMode,
		UProject_JLocalProfileSubsystem* InProfiles, APlayerController* InFirstPC, APlayerController* InSecondPC,
		UProject_JCharacterClassDefinition* InClass, FProject_JAccountId InAccount, FProject_JCharacterId InCharacter,
		ProjectJ::MMO::FServerSessionLease InFirstLease, FString InTempDir)
		: Test(InTest), GI(InGI), World(InGI->GetWorld()), Mode(InMode), Profiles(InProfiles),
		  FirstPC(InFirstPC), SecondPC(InSecondPC),
		  First(InFirstPC->GetPlayerState<AProject_JPlayerState>()),
		  Second(InSecondPC->GetPlayerState<AProject_JPlayerState>()),
		  ClassDef(InClass), Account(InAccount), Character(InCharacter), FirstLease(InFirstLease),
		  TempDir(MoveTemp(InTempDir))
	{
	}

	~FTrustedProfileLifecycle()
	{
		if (World && World->HasBegunPlay())
			World->EndPlay(EEndPlayReason::LevelTransition);
		if (GI.IsValid())
			GI->Shutdown();
		if (World)
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
		if (!TempDir.IsEmpty())
			IFileManager::Get().DeleteDirectory(*TempDir, false, true);
	}

	bool Update() override
	{
		if (FPlatformTime::Seconds() - StartTime > 30.0)
		{
			Test->AddError(TEXT("Trusted session profile lifecycle test timed out."));
			return true;
		}

		if (Step == 0)
		{
			bool bRejectedLocal = false;
			Profiles->OpenProfile(First, TEXT("HijackAttempt"), [&](bool Ok) { bRejectedLocal = !Ok; });
			Test->TestTrue(TEXT("Standalone OpenProfile rejects a trusted-session PlayerState"), bRejectedLocal);

			bool bRejectedForeign = false;
			Profiles->OpenTrustedProfile(Second, FirstLease, [&](bool Ok) { bRejectedForeign = !Ok; });
			Test->TestTrue(TEXT("OpenTrustedProfile rejects a foreign lease on another PlayerState"), bRejectedForeign);

			bool bRejectedEmpty = false;
			Profiles->OpenTrustedProfile(First, ProjectJ::MMO::FServerSessionLease{}, [&](bool Ok) { bRejectedEmpty = !Ok; });
			Test->TestTrue(TEXT("OpenTrustedProfile rejects an empty lease"), bRejectedEmpty);

			bOpenDone = bOpenOk = false;
			Profiles->OpenTrustedProfile(First, FirstLease, [this](bool Ok) { bOpenDone = true; bOpenOk = Ok; });
			Test->TestTrue(TEXT("Trusted profile is bound while opening"), Profiles->IsBound(First));
			Test->TestTrue(TEXT("Trusted profile reports IsOpening before hydration completes"), Profiles->IsOpening(First));
			Test->TestFalse(TEXT("Trusted profile is not ready before hydration completes"), Profiles->IsReady(First));

			Mode->HandleStartingNewPlayer(FirstPC);
			Test->TestNull(TEXT("GameMode holds Pawn spawn while trusted profile is hydrating"), FirstPC->GetPawn());
			Step = 1;
			return false;
		}

		if (Step == 1)
		{
			if (!bOpenDone) return false;
			if (!Test->TestTrue(TEXT("Initial trusted profile open succeeds"), bOpenOk)) return true;
			Test->TestTrue(TEXT("Trusted profile reports IsReady after hydration"), Profiles->IsReady(First));
			Test->TestFalse(TEXT("Trusted profile is no longer opening after completion"), Profiles->IsOpening(First));

			First->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(First, nullptr);
			auto* Progression = First->FindComponentByClass<UProject_JProgressionComponent>();
			if (!Test->TestNotNull(TEXT("First progression component"), Progression)) return true;
			Progression->InitializeDefaults(nullptr, nullptr, 1);
			Step = 2;
			return false;
		}

		if (Step == 2)
		{
			if (Profiles->IsCheckpointPending(First)) return false;
			auto* Progression = First->FindComponentByClass<UProject_JProgressionComponent>();
			if (!Test->TestTrue(TEXT("Atomic class initialization admitted through trusted profile"), Progression->InitializeClass(ClassDef.Get()))) return true;
			Step = 3;
			return false;
		}

		if (Step == 3)
		{
			if (Profiles->IsCheckpointPending(First)) return false;
			auto* Progression = First->FindComponentByClass<UProject_JProgressionComponent>();
			if (!Test->TestEqual(TEXT("Confirmed class installed on trusted session"), Progression->GetState().ClassDefinition.Get(), ClassDef.Get())) return true;
			if (!Test->TestTrue(TEXT("Grant experience on trusted session"), Progression->GrantExperience(350))) return true;
			Step = 4;
			return false;
		}

		if (Step == 4)
		{
			auto* Economy = GI->GetSubsystem<UProject_JEconomySubsystem>();
			if (!MailId.IsValid())
			{
				if (Profiles->IsCheckpointPending(First) || Profiles->GetCommittedRevision(First) < 1) return false;
				MailId = Economy->SendSystemRewardItemMail(Character.Value, TEXT("MailIntegration"), TEXT("GOLD"), 10, TEXT("HealthPotion"), 2);
				const auto Inbox = Economy->GetInbox(Character.Value);
				const auto* Mail = Inbox.FindByPredicate([this](const auto& Row) { return Row.MailId == MailId; });
				if (!Test->TestTrue(TEXT("Local economy created a real item attachment"), Mail && Mail->AttachedItemInstanceId.IsValid())) return true;
				AttachmentId = Mail->AttachedItemInstanceId;
				Test->TestFalse(TEXT("A different PlayerState cannot claim the attachment"), Economy->SubmitMailClaim(MailId, Second, {}));
				Profiles->LoseNextCommitAcknowledgementForTest(First);
				Profiles->HoldCommandCompletionsForTest(true);
				if (!Test->TestTrue(TEXT("Attach the exact item to the trusted SQLite profile"), Economy->SubmitMailClaim(MailId, First, [this](bool Ok)
				{
					FProject_JItemInstanceData Item;
					bMailProjected = Ok && First->GetInventoryComponent()->FindItemInstance(AttachmentId, Item) && Item.StackCount == 2;
					bMailDone = true;
				}))) return true;
				Test->TestFalse(TEXT("Duplicate claim while committing is rejected"), Economy->SubmitMailClaim(MailId, First, {}));
				return false;
			}
			if (!bMailDone)
			{
				if (Profiles->GetHeldCompletionCountForTest() == 0) return false;
				FProject_JItemInstanceData Unpublished;
				Test->TestFalse(TEXT("A committed receipt cannot acknowledge a still-unpublished item"),
					bMailDone || First->GetInventoryComponent()->FindItemInstance(AttachmentId, Unpublished));
				Profiles->HoldCommandCompletionsForTest(false);
				return false;
			}
			Test->TestTrue(TEXT("Lost commit reply reconciles and acknowledges only the published item"), bMailProjected);
			Test->TestEqual(TEXT("The gold in the same mail settles once"), Economy->GetLedger().GetBalance(Character.Value, TEXT("GOLD")), int64(10));
			Test->TestFalse(TEXT("Already claimed mail cannot add another item"), Economy->SubmitMailClaim(MailId, First, {}));
			if (Profiles->IsCheckpointPending(First) || Profiles->GetCommittedRevision(First) < 1) return false;
			auto* Progression = First->FindComponentByClass<UProject_JProgressionComponent>();
			CommittedLevel = Progression->GetState().Level;
			CommittedExperience = Progression->GetExperience();
			Test->TestTrue(TEXT("Trusted session checkpoint committed to SQLite with level advancement"),
				Profiles->GetCommittedRevision(First) >= 1 && CommittedLevel > 1);

			bBackupDone = bBackupOk = false;
			Profiles->RequestBackup([this](bool Ok, FString Path) {
				bBackupDone = true;
				bBackupOk = Ok;
				BackupPath = MoveTemp(Path);
			});
			Step = 5;
			return false;
		}

		if (Step == 5)
		{
			if (!bBackupDone) return false;
			Test->TestTrue(TEXT("Authority SQLite backup succeeds"), bBackupOk && !BackupPath.IsEmpty() && IFileManager::Get().FileExists(*BackupPath));

			ProjectJ::MMO::FServerSessionAttempt ReconnectAttempt;
			if (!Test->TestTrue(TEXT("Begin reconnect trusted session"), Mode->BeginTrustedSession(SecondPC, Account, Character, ReconnectAttempt))) return true;
			if (!Test->TestTrue(TEXT("Complete reconnect takeover"), Mode->CompleteTrustedSession(ReconnectAttempt, true, SecondLease))) return true;
			Test->TestFalse(TEXT("Old PlayerState binding is detached on takeover"), Profiles->IsBound(First));

			bool bRejectedRevoked = false;
			Profiles->OpenTrustedProfile(First, FirstLease, [&](bool Ok) { bRejectedRevoked = !Ok; });
			Test->TestTrue(TEXT("Revoked lease cannot reopen trusted profile"), bRejectedRevoked);

			bOpenDone = bOpenOk = false;
			Profiles->OpenTrustedProfile(Second, SecondLease, [this](bool Ok) { bOpenDone = true; bOpenOk = Ok; });
			Step = 6;
			return false;
		}

		if (Step == 6)
		{
			if (!bOpenDone) return false;
			if (!Test->TestTrue(TEXT("Reconnected trusted profile restores cleanly"), bOpenOk)) return true;
			auto* SecondProgression = Second->FindComponentByClass<UProject_JProgressionComponent>();
			if (!Test->TestNotNull(TEXT("Second progression component"), SecondProgression)) return true;
			Test->TestEqual(TEXT("Restored level matches committed value across reconnect"), SecondProgression->GetState().Level, CommittedLevel);
			Test->TestEqual(TEXT("Restored XP matches committed value across reconnect"), SecondProgression->GetExperience(), CommittedExperience);
			Test->TestEqual(TEXT("Restored class matches committed class"), SecondProgression->GetState().ClassDefinition.Get(), ClassDef.Get());
			Test->TestTrue(TEXT("Reconnected profile is ready"), Profiles->IsReady(Second));
			FProject_JItemInstanceData Restored;
			Test->TestTrue(TEXT("Mail attachment restores with the same identifier and count from SQLite"),
				Second->GetInventoryComponent()->FindItemInstance(AttachmentId, Restored) && Restored.StackCount == 2);
			return true;
		}

		return true;
	}
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJTrustedSessionProfileTest, "ProjectJ.MMO.Persistence.TrustedSessionProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJTrustedSessionProfileTest::RunTest(const FString&)
{
	using namespace ProjectJ::MMO;
	const TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
	Instance->InitializeStandalone();
	UWorld* World = Instance->GetWorld();
	auto CleanupOnEarlyFail = [&]() {
		if (World && World->HasBegunPlay())
			World->EndPlay(EEndPlayReason::LevelTransition);
		Instance->Shutdown();
		if (World)
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
	};

	auto* Data = Instance->GetSubsystem<UProject_JCharacterDataSubsystem>();
	if (!TestNotNull(TEXT("CharacterDataSubsystem"), Data) ||
		!TestTrue(TEXT("Load primary asset definitions"), Data->LoadPrimaryAssetDefinitions()))
	{
		CleanupOnEarlyFail();
		return false;
	}
	auto* ClassDef = LoadObject<UProject_JCharacterClassDefinition>(
		nullptr, TEXT("/Game/DataAssetSets/Class/DA_Class_Greatsword.DA_Class_Greatsword"));
	if (!TestNotNull(TEXT("Canonical class definition loaded"), ClassDef))
	{
		CleanupOnEarlyFail();
		return false;
	}

	FURL URL;
	URL.AddOption(TEXT("game=/Script/Project_J.Project_JNetworkLoadGameMode"));
	if (!TestTrue(TEXT("Install authoritative GameMode"), World->SetGameMode(URL)))
	{
		CleanupOnEarlyFail();
		return false;
	}
	World->InitializeActorsForPlay(URL);
	World->GetWorldSettings()->NotifyBeginPlay();

	auto* Mode = Cast<AProject_JNetworkLoadGameMode>(World->GetAuthGameMode());
	auto* Profiles = Instance->GetSubsystem<UProject_JLocalProfileSubsystem>();
	if (!TestNotNull(TEXT("GameMode"), Mode) || !TestNotNull(TEXT("LocalProfileSubsystem"), Profiles))
	{
		CleanupOnEarlyFail();
		return false;
	}

	const FString TempDir = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("Validation/TrustedSessionProfile") / FGuid::NewGuid().ToString());
	IFileManager::Get().MakeDirectory(*TempDir, true);
	Profiles->DatabasePath = TempDir / TEXT("Profiles.sqlite");

	auto* FirstPC = World->SpawnActor<APlayerController>();
	auto* SecondPC = World->SpawnActor<APlayerController>();
	FirstPC->SetAsLocalPlayerController();
	SecondPC->SetAsLocalPlayerController();
	if (!FirstPC->GetPlayerState<AProject_JPlayerState>()) FirstPC->InitPlayerState();
	if (!SecondPC->GetPlayerState<AProject_JPlayerState>()) SecondPC->InitPlayerState();
	FirstPC->SetPlayer(NewObject<UPlayer>(Instance.Get()));
	SecondPC->SetPlayer(NewObject<UPlayer>(Instance.Get()));

	Mode->bRequireTrustedSessions = true;
	Mode->PostLogin(FirstPC);
	Mode->PostLogin(SecondPC);

	const auto Account = FProject_JAccountId::NewId();
	const auto Character = FProject_JCharacterId::NewId();
	FServerSessionAttempt Attempt;
	FServerSessionLease FirstLease;
	if (!TestTrue(TEXT("Begin initial trusted session"), Mode->BeginTrustedSession(FirstPC, Account, Character, Attempt)) ||
		!TestTrue(TEXT("Complete initial trusted session"), Mode->CompleteTrustedSession(Attempt, true, FirstLease)))
	{
		CleanupOnEarlyFail();
		IFileManager::Get().DeleteDirectory(*TempDir, false, true);
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FTrustedProfileLifecycle(
		this, Instance.Get(), Mode, Profiles, FirstPC, SecondPC, ClassDef, Account, Character, FirstLease, TempDir));
	return true;
}
#endif
