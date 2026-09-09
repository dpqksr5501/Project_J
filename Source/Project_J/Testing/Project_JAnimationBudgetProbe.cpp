// Editor-only animation workload. It never substitutes for a combat Character.
#if WITH_EDITOR
#include "Animation/AnimMontage.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "IAnimationBudgetAllocator.h"
#include "SkeletalMeshComponentBudgeted.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "System/Project_JCharacterAnimationBudgetSubsystem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJAnimationBudgetProbe, Log, All);
TRACE_DECLARE_INT_COUNTER(AnimProbeMode, TEXT("ProjectJ/AnimationBudgetProbe/Mode"));
TRACE_DECLARE_INT_COUNTER(AnimProbeActors, TEXT("ProjectJ/AnimationBudgetProbe/Actors"));
TRACE_DECLARE_INT_COUNTER(AnimProbeFinalized, TEXT("ProjectJ/AnimationBudgetProbe/BoneFinalizations"));
TRACE_DECLARE_INT_COUNTER(AnimProbeFrames, TEXT("ProjectJ/AnimationBudgetProbe/MeasuredFrames"));

namespace ProjectJAnimationBudgetProbe
{
struct FRun : TSharedFromThis<FRun>
{
	TWeakObjectPtr<UWorld> World;
	TStrongObjectPtr<USkeletalMesh> MeshAsset;
	TStrongObjectPtr<UAnimMontage> Montage;
	TArray<TWeakObjectPtr<AActor>> Actors;
	TArray<TWeakObjectPtr<USkeletalMeshComponentBudgeted>> Meshes;
	TArray<FDelegateHandle> BoneHandles;
	FDelegateHandle TickHandle, CleanupHandle;
	FVector Origin;
	int32 Count = 100, Mode = 0, Frames = 0, Finalizations = 0;
	float Elapsed = 0.0f, Duration = 10.0f;
	double Deadline = 0.0;
	bool bMeasured = false, bFinished = false, bPassed = false, bOwnAllocatorEnable = false;
	FString Directory;

	bool Start(UWorld* InWorld, int32 InMode, int32 InCount, float InDuration, FVector InOrigin)
	{
		check(IsInGameThread());
		if (!InWorld || !InWorld->IsGameWorld() || InWorld->GetNetMode() != NM_Standalone || InWorld->bIsTearingDown
			|| (InMode != 0 && InMode != 1) || InCount < 1 || InCount > 1000 || !FMath::IsFinite(InDuration)
			|| InDuration < 1.0f || InDuration > 120.0f) { return false; }
		// Do not share allocator ownership with existing budgeted meshes in any world.
		for (TObjectIterator<USkeletalMeshComponentBudgeted> It; It; ++It)
		{
			if (const auto* ProjectMesh = Cast<UProject_JBudgetedSkeletalMeshComponent>(*It); ProjectMesh && !ProjectMesh->IsManagedByBudget()) { continue; }
			if (!It->IsTemplate() && It->IsRegistered() && It->GetWorld() && It->GetWorld()->IsGameWorld())
			{
				UE_LOG(LogProjectJAnimationBudgetProbe, Warning, TEXT("Existing budgeted mesh found; refusing to change allocator ownership."));
				return false;
			}
		}
		IConsoleVariable* Enabled = IConsoleManager::Get().FindConsoleVariable(TEXT("a.Budget.Enabled"));
		if (!Enabled || Enabled->GetInt() != 1)
		{
			UE_LOG(LogProjectJAnimationBudgetProbe, Display, TEXT("Before starting PIE, set a.Budget.Enabled 1. The probe does not change process CVars."));
			return false;
		}
		IAnimationBudgetAllocator* Allocator = IAnimationBudgetAllocator::Get(InWorld);
		if (!Allocator || Allocator->GetEnabled())
		{
			UE_LOG(LogProjectJAnimationBudgetProbe, Display, TEXT("Probe requires an idle world allocator; refusing to take over an enabled allocator."));
			return false;
		}
		// Explicit fixture setup only. This cold/shared load is outside the measured interval.
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_AnimationBudgetProbe_LoadFixture);
			MeshAsset.Reset(LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple")));
			Montage.Reset(LoadObject<UAnimMontage>(nullptr, TEXT("/Game/Anim_Assets/Great_Sword/Animations/Sword/Montage/AM_Greatsword_LMB1.AM_Greatsword_LMB1")));
		}
		if (!MeshAsset || !Montage || Montage->SlotAnimTracks.IsEmpty()
			|| Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.IsEmpty()
			|| !Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference()) { return false; }
		World = InWorld; Mode = InMode; Count = InCount; Duration = InDuration; Origin = InOrigin;
		Deadline = FPlatformTime::Seconds() + 60.0 + Duration * 4.0;
		Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Profiling/AnimationBudgetProbe"),
			FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S")) + TEXT("_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
		if (Mode == 1) { Allocator->SetEnabled(true); bOwnAllocatorEnable = true; }
		TickHandle = FWorldDelegates::OnWorldPostActorTick.AddSP(AsShared(), &FRun::Tick);
		CleanupHandle = FWorldDelegates::OnWorldCleanup.AddSP(AsShared(), &FRun::OnCleanup);
		TRACE_COUNTER_SET_ALWAYS(AnimProbeMode, Mode);
		TRACE_COUNTER_SET_ALWAYS(AnimProbeActors, 0);
		TRACE_COUNTER_SET_ALWAYS(AnimProbeFinalized, 0);
		TRACE_COUNTER_SET_ALWAYS(AnimProbeFrames, 0);
		TRACE_BOOKMARK(TEXT("AnimationBudgetProbe Setup Mode=%d Count=%d"), Mode, Count);
		UE_LOG(LogProjectJAnimationBudgetProbe, Display, TEXT("Started Mode=%d Count=%d Duration=%.1fs. Pose-only probes: no GAS, movement, hit or VFX; existing assets remain unchanged."), Mode, Count, Duration);
		return true;
	}

	bool SpawnOne()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_AnimationBudgetProbe_Spawn);
		UWorld* W = World.Get();
		AActor* Actor = W->SpawnActor<AActor>();
		if (!Actor) { return false; }
		Actors.Add(Actor);
		auto* Mesh = NewObject<USkeletalMeshComponentBudgeted>(Actor);
		Mesh->SetAutoRegisterWithBudgetAllocator(false);
		Mesh->SetAutoCalculateSignificance(false);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->bEnableUpdateRateOptimizations = false;
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		// The borrowed animation may contain gameplay notifies. This is deliberately not a combat test.
		Mesh->bSuppressNotifyEventDispatch = true;
		Mesh->SetSkeletalMeshAsset(MeshAsset.Get());
		Actor->AddInstanceComponent(Mesh);
		Actor->SetRootComponent(Mesh);
		Mesh->RegisterComponent();
		const int32 Index = Meshes.Num();
		Mesh->SetWorldLocation(Origin + FVector((Index % 10) * 250.0f, (Index / 10) * 250.0f, 0.0f));
		Mesh->PlayAnimation(Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference(), true);
		if (!Mesh->GetSingleNodeInstance()) { return false; }
		Mesh->GetSingleNodeInstance()->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
		Meshes.Add(Mesh);
		BoneHandles.Add(Mesh->RegisterOnBoneTransformsFinalizedDelegate(FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateSP(
			AsShared(), &FRun::OnBonesFinalized)));
		if (Mode == 1)
		{
			IAnimationBudgetAllocator* Allocator = IAnimationBudgetAllocator::Get(W);
			Allocator->RegisterComponent(Mesh);
			// Include offscreen probes for reproducibility. Only optional poses may be throttled.
			Allocator->SetComponentSignificance(Mesh, 1.0f / float(Index + 1), false, true, false, false);
		}
		return true;
	}

	void OnBonesFinalized() { check(IsInGameThread()); if (bMeasured && !bFinished) { ++Finalizations; } }

	void Tick(UWorld* InWorld, ELevelTick, float DeltaSeconds)
	{
		if (bFinished || InWorld != World.Get()) { return; }
		TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_AnimationBudgetProbe_Driver);
		if (InWorld->bIsTearingDown) { Finish(false, TEXT("Teardown")); return; }
		if (FPlatformTime::Seconds() > Deadline) { Finish(false, TEXT("WallClockTimeout")); return; }
		if (Meshes.Num() < Count)
		{
			for (int32 I = 0; I < 5 && Meshes.Num() < Count; ++I)
			{
				if (!SpawnOne()) { Finish(false, TEXT("SpawnFailed")); return; }
			}
			TRACE_COUNTER_SET(AnimProbeActors, Meshes.Num());
			return;
		}
		Elapsed += DeltaSeconds;
		if (!bMeasured && Elapsed >= 2.0f)
		{
			bMeasured = true; Elapsed = 0.0f;
			TRACE_BOOKMARK(TEXT("AnimationBudgetProbe MeasureBegin Mode=%d Count=%d"), Mode, Count);
			return;
		}
		if (bMeasured)
		{
			++Frames;
			TRACE_COUNTER_SET(AnimProbeFrames, Frames);
			TRACE_COUNTER_SET(AnimProbeFinalized, Finalizations);
			if (Elapsed >= Duration) { Finish(Finalizations > 0, TEXT("Completed")); }
		}
	}

	void OnCleanup(UWorld* InWorld, bool, bool) { if (InWorld == World.Get()) { Finish(false, TEXT("WorldCleanup")); } }
	void Finish(bool bSuccess, const TCHAR* Reason)
	{
		if (bFinished) { return; }
		bFinished = true; bPassed = bSuccess;
		FWorldDelegates::OnWorldPostActorTick.Remove(TickHandle);
		FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);
		TRACE_BOOKMARK(TEXT("AnimationBudgetProbe MeasureEnd Mode=%d Success=%d Frames=%d Finalizations=%d"), Mode, bPassed, Frames, Finalizations);
		UWorld* W = World.Get();
		// OnWorldCleanup precedes the engine allocator's OnPostWorldCleanup deletion.
		IAnimationBudgetAllocator* Allocator = W ? IAnimationBudgetAllocator::Get(W) : nullptr;
		for (int32 Index = 0; Index < Meshes.Num(); ++Index)
		{
			if (auto* Mesh = Meshes[Index].Get())
			{
				Mesh->UnregisterOnBoneTransformsFinalizedDelegate(BoneHandles[Index]);
				if (Mode == 1 && Allocator) { Allocator->UnregisterComponent(Mesh); }
			}
		}
		for (const auto& Actor : Actors) { if (Actor.IsValid()) { Actor->Destroy(); } }
		if (bOwnAllocatorEnable && Allocator) { Allocator->SetEnabled(false); }
		bOwnAllocatorEnable = false;
		MeshAsset.Reset(); Montage.Reset();
		TRACE_COUNTER_SET_ALWAYS(AnimProbeActors, 0);
		const FString Summary = FString::Printf(TEXT("Success=%d Reason=%s Mode=%d Count=%d Frames=%d SimulationSeconds=%.6f BoneFinalizations=%d\nPose-only single-node animation; no combat/Notify/root-motion/ABP correctness claim. Bone finalization includes interpolation and is not an evaluation-task count.\n"),
			bPassed, Reason, Mode, Count, Frames, Elapsed, Finalizations);
		IFileManager::Get().MakeDirectory(*Directory, true);
		FFileHelper::SaveStringToFile(Summary, *FPaths::Combine(Directory, TEXT("Summary.txt")));
		UE_LOG(LogProjectJAnimationBudgetProbe, Display, TEXT("%sReport=%s"), *Summary, *Directory);
	}
};

TSharedPtr<FRun> Active;
bool Start(UWorld* World, int32 Mode, int32 Count = 100, float Duration = 10.0f)
{
	if (Active && !Active->bFinished) { return false; }
	FVector Origin(0, 0, 100);
	if (World && World->GetFirstPlayerController() && World->GetFirstPlayerController()->GetPawn())
	{
		Origin = World->GetFirstPlayerController()->GetPawn()->GetActorLocation() + FVector(1000, -1200, 0);
	}
	auto NewRun = MakeShared<FRun>();
	if (!NewRun->Start(World, Mode, Count, Duration, Origin)) { return false; }
	Active = NewRun;
	return true;
}

FAutoConsoleCommandWithWorldAndArgs StartCommand(TEXT("ProjectJ.AnimationBudgetProbe.Start"),
	TEXT("[Mode 0=full-rate 1=ABA] [Count=100 max=1000] [Seconds=10 max=120]. Standalone editor pose-only comparison; set a.Budget.Enabled 1 before PIE."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!Start(World, Args.IsEmpty() ? 0 : FCString::Atoi(*Args[0]), Args.Num() < 2 ? 100 : FCString::Atoi(*Args[1]),
			Args.Num() < 3 ? 10.0f : FCString::Atof(*Args[2]))) { UE_LOG(LogProjectJAnimationBudgetProbe, Display, TEXT("Start rejected; check mode, world, allocator ownership and readiness.")); }
	}));
FAutoConsoleCommand StopCommand(TEXT("ProjectJ.AnimationBudgetProbe.Stop"), TEXT("Cancel and remove only animation probe actors. Leaves external trace and process CVars unchanged."),
	FConsoleCommandDelegate::CreateLambda([] { if (Active) { Active->Finish(false, TEXT("UserStopped")); } }));
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
namespace ProjectJAnimationBudgetProbe
{
class FSmoke : public IAutomationLatentCommand
{
	FAutomationTestBase* Test;
	UWorld* World = nullptr;
	int32 Mode;
	bool bCancel;
public:
	FSmoke(FAutomationTestBase* InTest, int32 InMode, bool InCancel) : Test(InTest), Mode(InMode), bCancel(InCancel) {}
	virtual bool Update() override
	{
		if (!World)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
			World->GetSubsystem<UProject_JCharacterAnimationBudgetSubsystem>()->SetEnabledOverride(false);
			if (!Start(World, Mode, 100, 3.0f))
			{
				Test->AddError(TEXT("Probe failed to start. Launch with a.Budget.Enabled=1 before worlds start."));
				World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return true;
			}
		}
		World->Tick(LEVELTICK_All, 1.0f / 60.0f);
		if (bCancel && !Active->bFinished && Active->Meshes.Num() >= 10)
		{
			World->BeginTearingDown(); World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false); GEngine->DestroyWorldContext(World); World = nullptr;
			Test->TestTrue(TEXT("World cleanup terminates probe"), Active->bFinished);
			Test->TestFalse(TEXT("World cleanup is not successful completion"), Active->bPassed);
		}
		else if (!Active->bFinished) { return false; }
		else { Test->TestTrue(TEXT("100 animation probes produce poses and complete"), Active->bPassed); }
		for (const auto& Actor : Active->Actors) { Test->TestFalse(TEXT("Probe actor released"), Actor.IsValid()); }
		Test->TestFalse(TEXT("Allocator ownership released"), Active->bOwnAllocatorEnable);
		if (World) { World->BeginTearingDown(); World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
		return true;
	}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAnimationBudgetProbeBaseline, "ProjectJ.GroupB.AnimationProbe.FullRate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAnimationBudgetProbeBaseline::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(ProjectJAnimationBudgetProbe::FSmoke(this, 0, false)); return true; }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAnimationBudgetProbeBudgeted, "ProjectJ.GroupB.AnimationProbe.Budgeted", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAnimationBudgetProbeBudgeted::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(ProjectJAnimationBudgetProbe::FSmoke(this, 1, false)); return true; }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAnimationBudgetProbeCleanup, "ProjectJ.GroupB.AnimationProbe.WorldCleanup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAnimationBudgetProbeCleanup::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(ProjectJAnimationBudgetProbe::FSmoke(this, 1, true)); return true; }
#include "Testing/Project_JAnimationBudgetCombatTests.inl"
#include "Testing/Project_JCharacterBudgetTests.inl"
#endif
#endif
