#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Testing/Project_JAuthoredAnimationFixture.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace
{
enum class EFlow { Steady, EnterSprint, LeaveSprint, Release, Side, Back, Dodge };
struct FScenario { const TCHAR* Name; bool bStrafe; float Offset, Rate; EFlow Flow = EFlow::Steady; };
const FScenario Scenarios[] = {
	{TEXT("OTM_FL_Gentle"), false, -45, -90}, {TEXT("OTM_FL_RapidLeft"), false, -45, -900},
	{TEXT("OTM_F_Gentle"), false, 0, 90}, {TEXT("OTM_F_RapidRight"), false, 0, 900},
	{TEXT("OTM_FR_Gentle"), false, 45, -90}, {TEXT("OTM_FR_RapidLeft"), false, 45, -900},
	{TEXT("Strafe_FL_Gentle"), true, -45, 90}, {TEXT("Strafe_FL_RapidRight"), true, -45, 900},
	{TEXT("Strafe_F_Gentle"), true, 0, -90}, {TEXT("Strafe_F_RapidLeft"), true, 0, -900},
	{TEXT("Strafe_FR_Gentle"), true, 45, 90}, {TEXT("Strafe_FR_RapidRight"), true, 45, 900},
	{TEXT("RunToSprint_Preparing"), false, 0, -900, EFlow::EnterSprint},
	{TEXT("SprintToRun_Preparing"), true, 0, -900, EFlow::LeaveSprint},
	{TEXT("Sprint_Release"), true, 0, 900, EFlow::Release},
	{TEXT("SprintToSide"), true, 0, 900, EFlow::Side}, {TEXT("SprintToBack"), true, 0, -900, EFlow::Back},
	{TEXT("OTM_F_ModerateLeft"), false, 0, -600}, {TEXT("OTM_F_ModerateRight"), false, 0, 600},
	{TEXT("Strafe_F_ModerateLeft"), true, 0, -600}, {TEXT("Strafe_F_ModerateRight"), true, 0, 600},
	{TEXT("OTM_FL_RapidRight"), false, -45, 900}, {TEXT("OTM_F_RapidLeft"), false, 0, -900},
	{TEXT("OTM_FR_RapidRight"), false, 45, 900}, {TEXT("Strafe_FL_RapidLeft"), true, -45, -900},
	{TEXT("Strafe_F_RapidRight"), true, 0, 900}, {TEXT("Strafe_FR_RapidLeft"), true, 45, -900},
	{TEXT("OTM_DodgeInterrupt"), false, 0, 900, EFlow::Dodge}, {TEXT("Strafe_DodgeInterrupt"), true, 0, -900, EFlow::Dodge}
};
class FSprintTurnPlayback : public IAutomationLatentCommand
{
	FAutomationTestBase* Test;
	UWorld* World = nullptr;
	AProject_JPlayerCharacter* Player = nullptr;
	APlayerController* Controller = nullptr;
	int32 Scene = 0, Frame = 0, Ground = 0, Sprint = 0, Acute = 0, General = 0, SelectedAcute = 0, WrongFamily = 0, FalseStop = 0;
	float CameraYaw = 0;
	FString Report = TEXT("Actual authored Blueprint/GAS/CMC on collision floor; saved Sprint data; no package saves.\n");
	bool IsStrafe() const { return Scenarios[Scene].bStrafe; }
	bool IsSlow() const { return FMath::Abs(Scenarios[Scene].Rate) <= 90; }
	float Offset() const { return Scenarios[Scene].Offset; }
	void Cleanup()
	{
		if (!World) return;
		if (Player) Player->StopSprint();
		World->BeginTearingDown(); World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World); World = nullptr; Player = nullptr; Controller = nullptr;
	}
	bool Setup()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		auto* Floor = World->SpawnActor<AActor>(); auto* Box = NewObject<UBoxComponent>(Floor);
		Floor->SetRootComponent(Box); Box->SetBoxExtent(FVector(10000, 10000, 100));
		Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent(); Floor->SetActorLocation(FVector(0, 0, -100));
		World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
		Controller = World->SpawnActor<APlayerController>(); Controller->SetAsLocalPlayerController();
		auto* State = World->SpawnActor<AProject_JPlayerState>(); State->SetOwner(Controller); Controller->SetPlayerState(State);
		Player = ProjectJAuthoredAnimationFixture::Spawn(World, Controller, State, FVector(0, 0, 110));
		if (!Player) return false;
		Player->SetActorEnableCollision(true); Player->GetCharacterMovement()->SetComponentTickEnabled(true);
		if (IsStrafe()) Player->GetAbilitySystemComponent()->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
		return true;
	}
public:
	explicit FSprintTurnPlayback(FAutomationTestBase* InTest) : Test(InTest) {}
	virtual ~FSprintTurnPlayback() override { Cleanup(); }
	virtual bool Update() override
	{
		if (!World && !Setup()) { Test->AddError(TEXT("Sprint CMC fixture failed")); Cleanup(); return true; }
		constexpr float Dt = 1.f / 120;
		const auto& Scenario = Scenarios[Scene];
		if (Frame >= 180) CameraYaw += Scenario.Rate * Dt;
		CameraYaw = FMath::Clamp(CameraYaw, -180.f, 180.f);
		Controller->SetControlRotation(FRotator(0, CameraYaw, 0));
		const bool bSide = (Scenario.Flow == EFlow::Side || Scenario.Flow == EFlow::Back) && Frame >= 190;
		const float MoveOffset = bSide ? (Scenario.Flow == EFlow::Side ? 90.f : 180.f) : Offset();
		const float Radians = FMath::DegreesToRadians(MoveOffset);
		const FVector2D Input(FMath::Sin(Radians), FMath::Cos(Radians));
		auto* State = Player->GetLocomotionAnimStateComponent();
		const bool bRelease = Scenario.Flow == EFlow::Release && Frame >= 190;
		if (bRelease) { State->ClearMoveInput(); Player->StopSprint(); }
		else
		{
			State->SetMoveInput(Input); Player->UpdateSprintInputFromMove(Input);
			Player->AddMovementInput(FRotator(0, CameraYaw + MoveOffset, 0).Vector());
			if ((Frame == 0 && Scenario.Flow != EFlow::EnterSprint) || (Scenario.Flow == EFlow::EnterSprint && Frame == 190)) Player->StartSprint();
			if (Scenario.Flow == EFlow::LeaveSprint && Frame == 190) Player->StopSprint();
		}
		Player->bIsDodging = Scenario.Flow == EFlow::Dodge && Frame >= 190 && Frame < 205;
		TInlineComponentArray<USkeletalMeshComponent*> Meshes(Player);
		for (auto* Mesh : Meshes) Mesh->SetLastRenderTime(World->GetTimeSeconds());
		World->Tick(LEVELTICK_All, Dt);
		auto* Anim = CastChecked<UProject_JCharacterAnimInstance>(Player->GetMesh()->GetAnimInstance());
		const auto MM = Anim->GetMotionMatchingDebugSnapshot();
		const auto& Request = State->GetTurnRequestSample();
		const auto* Set = IsStrafe() ? Player->GetCombatStrafeMotionMatchingAssetSet() : Player->GetMotionMatchingAssetSet();
		if (Frame >= 180)
		{
			Ground += Player->GetCharacterMovement()->IsMovingOnGround();
			Sprint += MM.SelectionContext.GaitIntent == EProject_JLocomotionGaitIntent::Sprint;
			Acute += MM.SelectionContext.bMovingTurn180; General += MM.SelectionContext.bGeneralTurnCandidates;
			SelectedAcute += MM.PostSelection.SelectedDatabase == Set->SprintDatabases.TurnRedirect->GetFName();
			WrongFamily += Frame >= 205 && MM.SelectionContext.GaitIntent == EProject_JLocomotionGaitIntent::Sprint &&
				MM.PostSelection.SelectedDatabase.ToString().Contains(TEXT("Run_")) && !Anim->GetThreadSafeStateControllerShouldOverrideMotionMatching();
			FalseStop += !bRelease && !bSide && Anim->GetThreadSafeStateControllerShouldOverrideMotionMatching() &&
				Anim->GetThreadSafeStateControllerPresentationState() == EProject_JStateControllerPresentationState::TransitionToIdle;
			Test->TestTrue(TEXT("At most two search databases"), Anim->GetThreadSafeMotionMatchingCandidateCount() <= 2);
			if ((MM.SelectionContext.bMovingTurn180 || MM.SelectionContext.bGeneralTurnCandidates) &&
				!Anim->GetThreadSafeStateControllerShouldOverrideMotionMatching())
				Test->TestEqual(TEXT("Sprint correction offers Turn and Cycle together"), Anim->GetThreadSafeMotionMatchingCandidateCount(), 2);
			if (bSide && Frame >= 193) Test->TestFalse(TEXT("Side/back cancels forward Sprint owner"), Request.bAcuteActive && Request.Gait == EProject_JLocomotionGaitIntent::Sprint);
			if (Player->bIsDodging || bRelease) Test->TestFalse(TEXT("Action/release cancels physical request immediately"), Request.bAcuteActive);
		}
		if (Frame >= 170 && Frame % 4 == 0)
			Report += FString::Printf(TEXT("Frame Scene=%d Frame=%d Strafe=%d Offset=%.0f Camera=%.1f Speed=%.1f Gait=%d Acute=%d Preparing=%d Sweep=%.1f Remaining=%.1f Guard=%s Reason=%s General=%d PSD=%s Clip=%s\n"),
				Scene, Frame, IsStrafe(), MoveOffset, CameraYaw, Player->GetVelocity().Size2D(), int32(Request.Gait), Request.bAcuteActive,
				Request.bPreparing, Request.RequestedSweep, Request.RemainingFacing, Request.EligibilityGuard, Request.AcuteReason,
				MM.SelectionContext.bGeneralTurnCandidates, *MM.PostSelection.SelectedDatabase.ToString(), *MM.PostSelection.SelectedAnimation.ToString());
		if (++Frame < 420) return false;
		Report += FString::Printf(TEXT("Summary Scene=%d Name=%s Strafe=%d Offset=%.0f Slow=%d Ground=%d Sprint=%d Acute=%d General=%d SelectedAcute=%d WrongFamily=%d FalseStop=%d\n"),
			Scene, Scenario.Name, IsStrafe(), Offset(), IsSlow(), Ground, Sprint, Acute, General, SelectedAcute, WrongFamily, FalseStop);
		Test->TestTrue(TEXT("Real CMC remains grounded"), Ground >= 235);
		Test->TestEqual(TEXT("Held correction never synthesizes Stop"), FalseStop, 0);
		Test->TestEqual(TEXT("Settled Sprint result never uses Run pool"), WrongFamily, 0);
		if (Scenario.Flow == EFlow::Steady)
		{
			Test->TestTrue(TEXT("Actual Sprint gait exercised"), Sprint >= 235);
			if (IsSlow()) { Test->TestEqual(TEXT("Gentle Sprint keeps acute closed"), Acute, 0); Test->TestEqual(TEXT("Gentle Sprint keeps general closed"), General, 0); }
			else
			{
				Test->TestTrue(TEXT("Sprint reversal offers a physical Turn correction"), Acute > 0 || General > 0);
				Test->TestTrue(TEXT("Sprint reversal can select authored 180 poses"), SelectedAcute > 0);
			}
		}
		Test->TestFalse(TEXT("Acute owner recovers"), Request.bAcuteActive);
		Cleanup(); Frame = Ground = Sprint = Acute = General = SelectedAcute = WrongFamily = FalseStop = 0; CameraYaw = 0;
		if (++Scene < UE_ARRAY_COUNT(Scenarios)) return false;
		const FString Dir = FPaths::ProjectSavedDir() / TEXT("Validation/SprintTurn_20261010");
		IFileManager::Get().MakeDirectory(*Dir, true); FFileHelper::SaveStringToFile(Report, *(Dir / TEXT("SprintPlayback.txt")));
		return true;
	}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSprintTurnPlaybackTest, "ProjectJ.Animation.Naturalness.SprintTurnCMC",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJSprintTurnPlaybackTest::RunTest(const FString&)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("ProjectJVerifySprintTurns"))) return true;
	for (const TCHAR* Path : {
		TEXT("/Game/DataAssetSets/Animation_Profiles/MMProfiles/DA_Player_Locomotion.DA_Player_Locomotion"),
		TEXT("/Game/DataAssetSets/Animation_Profiles/Combat_MMProfile/DA_Player_Combat_Strafe.DA_Player_Combat_Strafe")})
	{
		const auto* Set = LoadObject<UProject_JMotionMatchingAssetSet>(nullptr, Path);
		if (!Set || !Set->SprintDatabases.Cycle || !Set->SprintDatabases.GeneralTurn || !Set->SprintDatabases.TurnRedirect)
		{ AddError(TEXT("Saved Sprint Cycle/GeneralTurn/TurnRedirect assets must be connected before playback.")); return false; }
	}
	ADD_LATENT_AUTOMATION_COMMAND(FSprintTurnPlayback(this)); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSprintDataTest, "ProjectJ.Animation.Naturalness.SprintTurnData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJSprintDataTest::RunTest(const FString&)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("ProjectJVerifySprintTurns"))) return true;
	for (const TCHAR* Path : {
		TEXT("/Game/DataAssetSets/Animation_Profiles/MMProfiles/DA_Player_Locomotion.DA_Player_Locomotion"),
		TEXT("/Game/DataAssetSets/Animation_Profiles/Combat_MMProfile/DA_Player_Combat_Strafe.DA_Player_Combat_Strafe")})
	{
		const auto* Set = LoadObject<UProject_JMotionMatchingAssetSet>(nullptr, Path);
		if (!TestNotNull(TEXT("Authored asset set"), Set)) continue;
		const auto& Family = Set->SprintDatabases;
		if (!TestNotNull(TEXT("Sprint Cycle"), Family.Cycle.Get()) || !TestNotNull(TEXT("Sprint GeneralTurn"), Family.GeneralTurn.Get()) ||
			!TestNotNull(TEXT("Sprint TurnRedirect"), Family.TurnRedirect.Get())) continue;
		FProject_JMotionMatchingSelectionContext C; C.GaitIntent = EProject_JLocomotionGaitIntent::Sprint;
		C.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle; C.bGeneralTurnCandidates = true;
		TestEqual(TEXT("General candidates compete with same-gait Cycle"), Set->FindTurnCycleCompanion(C, Family.Cycle), Family.GeneralTurn.Get());
		C.PhaseFamily = EProject_JLocomotionPhaseFamily::Turn; C.bMovingTurn180 = true;
		TestEqual(TEXT("Acute candidates compete with same-gait Cycle"), Set->FindTurnCycleCompanion(C, Family.TurnRedirect), Family.Cycle.Get());
		TestTrue(TEXT("Saved acute pair certified for cooked runtime"), Family.bCookedTurnCycleCompatible);
		TestTrue(TEXT("Saved general pair certified for cooked runtime"), Family.bCookedGeneralTurnCompatible);
		for (const auto& Pair : {TPair<UPoseSearchDatabase*, bool>(Family.GeneralTurn.Get(), false), {Family.TurnRedirect.Get(), true}})
		{
			TestEqual(TEXT("Both feet, both turn directions"), Pair.Key->GetNumAnimationAssets(), 4);
			for (int32 I = 0; I < Pair.Key->GetNumAnimationAssets(); ++I)
			{
				const auto* Entry = Pair.Key->GetDatabaseAnimationAsset(I);
				if (!TestNotNull(TEXT("PoseSearch entry"), Entry) || !TestNotNull(TEXT("Animation reference"), Entry->GetAnimationAsset())) continue;
				TestTrue(TEXT("Copied active source entries"), Entry->IsEnabled());
				TestTrue(TEXT("Sprint-only authored coverage"), Entry->GetAnimationAsset()->GetName().Contains(TEXT("Sprint_Turn_")));
				TestEqual(TEXT("General and acute coverage separated"), Entry->GetAnimationAsset()->GetName().Contains(TEXT("180")), Pair.Value);
			}
		}
	}
	return true;
}
#endif
