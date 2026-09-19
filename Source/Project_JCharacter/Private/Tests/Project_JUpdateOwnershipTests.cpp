#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Combat/Project_JServerSideRewindComponent.h"
#include "Components/Project_JAnimationUpdateCoordinatorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "TimerManager.h"
#include <limits>

namespace
{
struct FUpdateOwnershipWorld
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FUpdateOwnershipWorld() { GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World); }
	void BeginPlay()
	{
		World->InitializeActorsForPlay(FURL());
		World->GetWorldSettings()->NotifyBeginPlay();
		World->GetWorldSettings()->NotifyMatchStarted();
	}
	void Step(float Delta)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, Delta);
	}
	~FUpdateOwnershipWorld()
	{
		if (World->GetBegunPlay()) { World->EndPlay(EEndPlayReason::LevelTransition); }
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
};
constexpr auto UpdateTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJRewindSchedulingTest, "ProjectJ.Internal.Updates.RewindScheduling", UpdateTestFlags)
bool FProjectJRewindSchedulingTest::RunTest(const FString&)
{
	FUpdateOwnershipWorld Scope;
	auto* Owner = Scope.World->SpawnActor<ACharacter>();
	Owner->GetCharacterMovement()->SetComponentTickEnabled(false);
	auto* Rewind = NewObject<UProject_JServerSideRewindComponent>(Owner);
	Rewind->MaxRecordTime = 10.0f;
	Rewind->RecordRateHz = 30.0f;
	Rewind->RegisterComponent();
	auto* Remote = Scope.World->SpawnActor<ACharacter>();
	Remote->SetReplicates(true);
	Remote->SwapRoles(); // Spawn has already consumed ExchangeNetRoles' one-shot guard.
	TestEqual(TEXT("Fixture models a simulated proxy"), Remote->GetLocalRole(), ROLE_SimulatedProxy);
	auto* RemoteRewind = NewObject<UProject_JServerSideRewindComponent>(Remote);
	RemoteRewind->RegisterComponent();
	Scope.BeginPlay();
	TestFalse(TEXT("Remote actors do not schedule capture"), RemoteRewind->IsComponentTickEnabled());
	TestEqual(TEXT("Remote actors allocate no history"), RemoteRewind->PoseHistory.Num(), 0);
	TestEqual(TEXT("Capture follows movement and physics"), Rewind->PrimaryComponentTick.TickGroup.GetValue(), TG_PostPhysics);
	TestTrue(TEXT("Engine owns the authored record interval"), FMath::IsNearlyEqual(Rewind->GetComponentTickInterval(), 1.0f / 30));
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Owner->SetActorLocation(FVector(Frame, 0, 100));
		Scope.Step(1.0f / 120);
	}
	TestTrue(TEXT("120 frames produce approximately 30 real poses, not 120 polls"), Rewind->PoseHistoryCount >= 28 && Rewind->PoseHistoryCount <= 32);
	const int32 BeforeHitch = Rewind->PoseHistoryCount;
	Scope.Step(0.2f);
	TestEqual(TEXT("A hitch records one actual pose, without fabricated catch-up history"), Rewind->PoseHistoryCount, BeforeHitch + 1);
	Rewind->ResetHistory();
	TestEqual(TEXT("Teleport reset clears history"), Rewind->PoseHistoryCount, 0);
	for (int32 Frame = 0; Frame < 12; ++Frame) { Scope.Step(1.0f / 120); }
	TestTrue(TEXT("The interval resumes after reset"), Rewind->PoseHistoryCount > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJUrgentAnimationWindowTest, "ProjectJ.Internal.Updates.UrgentAnimationWindow", UpdateTestFlags)
bool FProjectJUrgentAnimationWindowTest::RunTest(const FString&)
{
	FUpdateOwnershipWorld Scope;
	auto* Owner = Scope.World->SpawnActor<ACharacter>();
	Owner->SetReplicates(true);
	Owner->SwapRoles();
	TestEqual(TEXT("Urgent-window fixture models a simulated proxy"), Owner->GetLocalRole(), ROLE_SimulatedProxy);
	auto* Coordinator = NewObject<UProject_JAnimationUpdateCoordinatorComponent>(Owner);
	Coordinator->RegisterComponent();
	Scope.BeginPlay();
	Owner->GetMesh()->bEnableUpdateRateOptimizations = true;
	Coordinator->RequestUrgentRemoteAnimationUpdate(0.5f);
	TestFalse(TEXT("Urgent presentation temporarily bypasses URO"), Owner->GetMesh()->bEnableUpdateRateOptimizations);
	Scope.Step(0.1f);
	Coordinator->RequestUrgentRemoteAnimationUpdate(0.01f);
	Coordinator->RequestUrgentRemoteAnimationUpdate(0.0f);
	Coordinator->RequestUrgentRemoteAnimationUpdate(-1.0f);
	Coordinator->RequestUrgentRemoteAnimationUpdate(std::numeric_limits<float>::quiet_NaN());
	Coordinator->RequestUrgentRemoteAnimationUpdate(std::numeric_limits<float>::infinity());
	TestTrue(TEXT("Short and invalid requests cannot shorten the active window"),
		Scope.World->GetTimerManager().GetTimerRemaining(Coordinator->RestoreAnimationUpdateRateTimer) >= 0.39f);
	for (int32 Frame = 0; Frame < 8; ++Frame) { Scope.Step(0.1f); }
	TestTrue(TEXT("The original mesh policy is restored on expiry"), Owner->GetMesh()->bEnableUpdateRateOptimizations);
	Owner->GetMesh()->bEnableUpdateRateOptimizations = false;
	Coordinator->RequestUrgentRemoteAnimationUpdate(0.1f);
	for (int32 Frame = 0; Frame < 3; ++Frame) { Scope.Step(0.1f); }
	TestFalse(TEXT("An originally disabled URO policy stays disabled"), Owner->GetMesh()->bEnableUpdateRateOptimizations);
	Owner->GetMesh()->bEnableUpdateRateOptimizations = true;
	Coordinator->RequestUrgentRemoteAnimationUpdate(1.0f);
	Coordinator->DestroyComponent();
	TestTrue(TEXT("Component teardown releases its override immediately"), Owner->GetMesh()->bEnableUpdateRateOptimizations);
	return true;
}
#endif
