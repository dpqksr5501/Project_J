#include "Testing/Project_JLoopbackCohort.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/Project_JAnimationUpdateCoordinatorComponent.h"
#include "Components/Project_JModularMeshComponent.h"
#include "DynamicRHI.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Project_JPlayerCharacter.h"
#include "RenderTimer.h"
#include "UnrealClient.h"

namespace
{
constexpr double CoverageSampleSeconds = .25;
constexpr double PreStrikeSeconds = 2.;
constexpr double PostStrikeSeconds = 3.;

struct FRenderedFrame
{
	double At, Frame, Game, Draw, GPU;
	int32 Replicated, OnScreen, Unsuspended, Suspended, ClothIntervalTicks;
	int32 OwnerOriginalCloaks, RemoteProxyCloaks;
	bool bCaptureAdjacent;
};
}

struct FProject_JRenderedBossSample
{
	struct FClothOriginal
	{
		float Blend;
		bool bAllowed;
		bool bSuspended;
	};

	FString Path;
	TArray<FRenderedFrame> Frames;
	TMap<TWeakObjectPtr<UProject_JModularMeshComponent>, FClothOriginal> ClothPermissions;
	double StrikeAt = 0.;
	double PreviousFrame = 0.;
	bool bCaptureRequested = false;
	int32 CaptureFramesToMark = 0;
	double NextCoverageSample = 0.;
	int32 ExpectedPlayers = 0;
	int32 Replicated = 0;
	int32 OnScreen = 0;
	int32 Unsuspended = 0;
	int32 Suspended = 0;
	int32 ClothCap = -1;
	int32 ClothHertz = 0;
	bool bVerifyRemoteCloak = false;
	int32 OwnerOriginalCloaks = 0;
	int32 RemoteProxyCloaks = 0;
	TMap<TWeakObjectPtr<UProject_JModularMeshComponent>, double> IntervalCloths;
	bool bVerifyClothRestore = false;
	bool bClothRestoreRequested = false;
	bool bClothRestored = false;
	bool bSawOwnerCloth = false;
	bool bOwnerClothStayedActive = true;

	~FProject_JRenderedBossSample()
	{
		for (const auto& Entry : ClothPermissions)
		{
			if (auto* Mesh = Entry.Key.Get(); Mesh && Mesh->IsRegistered())
				Mesh->SetClothExecutionBudgetAllowed(Entry.Value.bAllowed);
		}
	}

	bool VerifyRestoredCloth() const
	{
		if (ClothPermissions.IsEmpty()) return false;
		for (const auto& Entry : ClothPermissions)
		{
			const auto* Mesh = Entry.Key.Get();
			if (!IsValid(Mesh) || !Mesh->IsRegistered() || Mesh->ClothBlendWeight != Entry.Value.Blend ||
				Mesh->IsClothingSimulationSuspended() != Entry.Value.bSuspended ||
				(!Entry.Value.bSuspended && !Mesh->HasQualifiedClothNativeFrame(GFrameCounter)))
				return false;
		}
		UE_LOG(LogTemp, Display, TEXT("COHORT_CLOTH_RESTORED parts=%d"), ClothPermissions.Num());
		return true;
	}

	void Finish(const TCHAR* Failure) const
	{
		FString Rows = TEXT("seconds_from_strike,frame_ms,game_ms,draw_ms,gpu_ms,replicated_players,on_screen_players,cloth_unsuspended,cloth_suspended,capture_adjacent,cloth_interval_ticks\n");
		int32 Count = 0;
		int32 TimedClothTicks = 0;
		int32 MinReplicated = ExpectedPlayers;
		int32 MinOwnerOriginal = 1;
		int32 MinRemoteProxy = ExpectedPlayers - 1;
		if (StrikeAt)
		{
			for (const auto& Frame : Frames)
			{
				const double Offset = Frame.At - StrikeAt;
				if (Offset < -PreStrikeSeconds || Offset > PostStrikeSeconds) continue;
				Rows += FString::Printf(TEXT("%.5f,%.5f,%.5f,%.5f,%.5f,%d,%d,%d,%d,%d,%d\n"),
					Offset, Frame.Frame, Frame.Game, Frame.Draw, Frame.GPU, Frame.Replicated, Frame.OnScreen,
					Frame.Unsuspended, Frame.Suspended, Frame.bCaptureAdjacent, Frame.ClothIntervalTicks);
				TimedClothTicks += Frame.ClothIntervalTicks;
				MinReplicated = FMath::Min(MinReplicated, Frame.Replicated);
				MinOwnerOriginal = FMath::Min(MinOwnerOriginal, Frame.OwnerOriginalCloaks);
				MinRemoteProxy = FMath::Min(MinRemoteProxy, Frame.RemoteProxyCloaks);
				++Count;
			}
		}
		const bool bSaved = FFileHelper::SaveStringToFile(Rows, *(Path / TEXT("frames.csv")));
		const bool bScreenshot = IFileManager::Get().FileExists(*(Path / TEXT("Strike.png")));
		const bool bCadenceVerified = !ClothHertz || (IntervalCloths.Num() == ExpectedPlayers - 1 && TimedClothTicks > 0);
		if (ClothHertz)
			UE_LOG(LogTemp, Display, TEXT("COHORT_CLOTH_INTERVAL hz=%d parts=%d ticks=%d"), ClothHertz, IntervalCloths.Num(), TimedClothTicks);
		const bool bRemoteCloakVerified = !bVerifyRemoteCloak ||
			(Count > 0 && MinOwnerOriginal == 1 && MinRemoteProxy == ExpectedPlayers - 1);
		if (bVerifyRemoteCloak)
			UE_LOG(LogTemp, Display, TEXT("COHORT_REMOTE_CLOAK verified=%d owner_original=%d remote_proxy=%d native_cadence=%d"),
				bRemoteCloakVerified, Count ? MinOwnerOriginal : 0, Count ? MinRemoteProxy : 0, bRemoteCloakVerified);
		const bool bPassed = !Failure && bSaved && bScreenshot && Count > 0 && MinReplicated == ExpectedPlayers &&
			bCadenceVerified && bRemoteCloakVerified;
		UE_LOG(LogTemp, Display, TEXT("COHORT_RENDERED_FINISHED passed=%d frames=%d min_replicated=%d expected=%d cloth_cap=%d screenshot=%d reason=%s"),
			bPassed, Count, Count ? MinReplicated : 0, ExpectedPlayers, ClothCap, bScreenshot,
			Failure ? Failure : TEXT("none"));
		FPlatformMisc::RequestExitWithStatus(false, bPassed ? 0 : 1);
	}
};

void AProject_JCohortController::BeginPlay()
{
	Super::BeginPlay();
	FString Output;
	const TCHAR* CommandLine = FCommandLine::Get();
	if (!IsLocalController() || GetNetMode() != NM_Client) return;
	bStandaloneCohortClient = FParse::Param(CommandLine, TEXT("ProjectJCohortStandaloneClient"));
	if (!FParse::Value(CommandLine, TEXT("ProjectJCohortRenderedOutput="), Output)) return;
	RenderedBossSample = MakeShared<FProject_JRenderedBossSample>();
	auto& Sample = *RenderedBossSample;
	Sample.Path = Output;
	Sample.Frames.Reserve(2048);
	FParse::Value(CommandLine, TEXT("ProjectJCohort="), Sample.ExpectedPlayers);
	FParse::Value(CommandLine, TEXT("ProjectJCohortClothVertexCap="), Sample.ClothCap);
	FParse::Value(CommandLine, TEXT("ProjectJCohortClothHz="), Sample.ClothHertz);
	Sample.bVerifyClothRestore = FParse::Param(CommandLine, TEXT("ProjectJCohortVerifyOptionalClothRestore"));
	Sample.bVerifyRemoteCloak = FParse::Param(CommandLine, TEXT("ProjectJCohortVerifyRemoteCloak"));
	if (Sample.ExpectedPlayers < 2 || !IFileManager::Get().DirectoryExists(*Output) || !GDynamicRHI ||
		(Sample.ClothHertz && Sample.ClothHertz != 10 && Sample.ClothHertz != 15 && Sample.ClothHertz != 20 && Sample.ClothHertz != 30))
		Sample.Finish(TEXT("Invalid measurement directory, participant count or RHI"));
}

void AProject_JCohortController::MarkRenderedStrike()
{
	if (RenderedBossSample) RenderedBossSample->StrikeAt = FPlatformTime::Seconds();
}

void AProject_JCohortController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (bStandaloneCohortClient && PollMeasuredRelease())
	{
		bStandaloneCohortClient = false;
		UE_LOG(LogTemp, Display, TEXT("COHORT_STANDALONE_CLIENT_FINISHED passed=1"));
		FPlatformMisc::RequestExitWithStatus(false, 0);
		return;
	}
	if (!RenderedBossSample) return;
	auto& Sample = *RenderedBossSample;
	const bool bReleased = PollMeasuredRelease();
	const auto* Target = ObservedBoss.Get();
	const APawn* LocalAvatar = GetPawn();
	if (!Target || !LocalAvatar)
	{
		if (bReleased) Sample.Finish(TEXT("Missing local pawn or replicated boss"));
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (!Sample.PreviousFrame)
	{
		// Fixed arena-relative framing keeps one camera across policies even if the local owner joins in a different slot.
		const FVector Position = Target->GetActorLocation() + FVector(-675, 0, 306);
		const FVector LookAt = Target->GetActorLocation() + FVector(0, 0, 35);
		auto* Camera = GetWorld()->SpawnActor<ACameraActor>(Position, (LookAt - Position).Rotation());
		if (!Camera)
		{
			Sample.Finish(TEXT("Could not create encounter view"));
			return;
		}
		Camera->GetCameraComponent()->SetFieldOfView(85.f);
		SetViewTarget(Camera);
		// The local fixed view never changes the owner's actual control rotation.
		UE_LOG(LogTemp, Display, TEXT("COHORT_RENDERED_VIEW map=%s rhi=%s position=%s target=%s"),
			*GetWorld()->GetMapName(), GDynamicRHI->GetName(), *Position.ToCompactString(), *LookAt.ToCompactString());
		Sample.PreviousFrame = Now;
		return;
	}
	if (Now >= Sample.NextCoverageSample)
	{
		Sample.NextCoverageSample = Now + CoverageSampleSeconds;
		Sample.Replicated = Sample.OnScreen = Sample.Unsuspended = Sample.Suspended = 0;
		Sample.OwnerOriginalCloaks = Sample.RemoteProxyCloaks = 0;
		int32 Width, Height;
		GetViewportSize(Width, Height);
		for (TActorIterator<AProject_JPlayerCharacter> It(GetWorld()); It; ++It)
		{
			if (It->GetLocalRole() != ROLE_AutonomousProxy && It->GetLocalRole() != ROLE_SimulatedProxy) continue;
			++Sample.Replicated;
			FVector2D Screen;
			const bool bInView = ProjectWorldLocationToScreen(It->GetActorLocation(), Screen) &&
				Screen.X >= 0 && Screen.Y >= 0 && Screen.X < Width && Screen.Y < Height;
			Sample.OnScreen += bInView && It->WasRecentlyRendered(.2f);
			for (auto* Mesh : TInlineComponentArray<UProject_JModularMeshComponent*>(*It))
			{
				const auto* Asset = Mesh->GetSkeletalMeshAsset();
				if (!Asset || !Asset->HasActiveClothingAssets()) continue;
				if (Sample.ClothHertz && *It != LocalAvatar && Mesh->GetFName() == TEXT("Cloak") &&
					!Sample.IntervalCloths.Contains(Mesh) && Mesh->SetClothTickRateForTest(Sample.ClothHertz))
				{
					Sample.IntervalCloths.Add(Mesh, Mesh->GetLastClothIntervalTickForTest());
				}
				if ((Sample.ClothCap >= 0 || Sample.bVerifyClothRestore) && *It != LocalAvatar && !Sample.ClothPermissions.Contains(Mesh))
				{
					Sample.ClothPermissions.Add(Mesh, {Mesh->ClothBlendWeight, Mesh->IsClothExecutionBudgetAllowed(),
						Mesh->IsClothingSimulationSuspended()});
					Mesh->SetClothExecutionBudgetAllowed(true);
				}
				const bool bSuspended = Mesh->IsClothingSimulationSuspended();
				if (Sample.bVerifyRemoteCloak && Mesh->GetFName() == TEXT("Cloak") && !bSuspended &&
					Mesh->UsesNativeClothCadenceForTest() && !Mesh->GetClothingSimulationInstances().IsEmpty())
				{
					const bool bLocal = *It == LocalAvatar;
					const TCHAR* Expected = bLocal
						? TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_cloak.SK_Spellblade_cloak")
						: TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_cloak_remote.SK_Spellblade_cloak_remote");
					if (Asset->GetPathName() == Expected)
					{
						if (bLocal) ++Sample.OwnerOriginalCloaks;
						else ++Sample.RemoteProxyCloaks;
					}
				}
				if (*It == LocalAvatar && Sample.bVerifyClothRestore && !Sample.bClothRestoreRequested)
				{
					Sample.bSawOwnerCloth = true;
					Sample.bOwnerClothStayedActive &= !bSuspended;
				}
				if (bSuspended) ++Sample.Suspended;
				else ++Sample.Unsuspended;
			}
		}
	}
	if (Sample.Frames.Num() && !Sample.StrikeAt && Sample.Frames[0].At < Now - PreStrikeSeconds - 1.)
	{
		int32 Obsolete = 0;
		while (Obsolete < Sample.Frames.Num() && Sample.Frames[Obsolete].At < Now - PreStrikeSeconds - .5) ++Obsolete;
		Sample.Frames.RemoveAt(0, Obsolete, EAllowShrinking::No);
	}
	if (!Sample.StrikeAt || Now <= Sample.StrikeAt + PostStrikeSeconds)
	{
		int32 ClothTicks = 0;
		for (auto& Entry : Sample.IntervalCloths)
		{
			if (const auto* Mesh = Entry.Key.Get())
			{
				const double IntervalTick = Mesh->GetLastClothIntervalTickForTest();
				ClothTicks += IntervalTick >= 0. && IntervalTick != Entry.Value;
				Entry.Value = IntervalTick;
			}
		}
		Sample.Frames.Add({Now, (Now - Sample.PreviousFrame) * 1000.,
			FPlatformTime::ToMilliseconds(GGameThreadTime), FPlatformTime::ToMilliseconds(GRenderThreadTime),
			FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()), Sample.Replicated, Sample.OnScreen,
			Sample.Unsuspended, Sample.Suspended, ClothTicks, Sample.OwnerOriginalCloaks, Sample.RemoteProxyCloaks,
			Sample.CaptureFramesToMark > 0});
	}
	if (Sample.CaptureFramesToMark > 0) --Sample.CaptureFramesToMark;
	Sample.PreviousFrame = Now;
	if (Sample.StrikeAt && !Sample.bCaptureRequested && Now >= Sample.StrikeAt + .65)
	{
		Sample.bCaptureRequested = true;
		Sample.CaptureFramesToMark = 2;
		FScreenshotRequest::RequestScreenshot(Sample.Path / TEXT("Strike.png"), false, false);
	}
	if (Sample.bVerifyClothRestore && !Sample.bClothRestoreRequested && Sample.StrikeAt && Now > Sample.StrikeAt + PostStrikeSeconds)
	{
		if (auto* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.ClothBudget.PauseOptInRemote")))
		{
			Variable->Set(0, ECVF_SetByConsole);
			Sample.bClothRestoreRequested = true;
			// The native completion observer publishes after actor ticks. Check
			// from the core ticker after that publication, without forcing a wait.
			FTSTicker::GetCoreTicker().AddTicker(TEXT("ProjectJCohortClothRestore"), 0.f,
				[WeakSample = TWeakPtr<FProject_JRenderedBossSample>(RenderedBossSample)](float)
				{
					const auto ActiveSample = WeakSample.Pin();
					if (!ActiveSample) return false;
					ActiveSample->bClothRestored = ActiveSample->VerifyRestoredCloth();
					return !ActiveSample->bClothRestored;
				});
		}
	}
	if (bReleased)
	{
		const bool bCapMatches = Sample.ClothCap < 0 ||
			UProject_JAnimationUpdateCoordinatorComponent::GetClothExecutionVertexCap() == Sample.ClothCap;
		const TCHAR* Failure = !bCapMatches ? TEXT("Effective cloth cap did not match the run") : nullptr;
		if (Sample.bVerifyClothRestore && !Sample.bClothRestored) Failure = TEXT("Optional cloth mode did not restore native simulation");
		if (Sample.bVerifyClothRestore && (!Sample.bSawOwnerCloth || !Sample.bOwnerClothStayedActive))
			Failure = TEXT("The local player's original cloth did not stay active");
		Sample.Finish(Failure);
		RenderedBossSample.Reset();
	}
}
#endif
