#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Backend/Project_JNakamaServicePort.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/Project_JCheatManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Mount/Project_JMountMoverComponent.h"
#include "Project_JNPCCharacter.h"
#include "World/Project_JWorldStreamingComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPhase1NakamaServicePortTest, "ProjectJ.Systems.Phase1.NakamaServicePortIdempotencyAndFencing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPhase1NakamaServicePortTest::RunTest(const FString&)
{
	UGameInstance* GI = NewObject<UGameInstance>(GEngine);
	if (!TestNotNull(TEXT("GameInstance created"), GI)) return false;
	GI->Init();

	auto* Port = GI->GetSubsystem<UProject_JNakamaServicePortSubsystem>();
	if (!TestNotNull(TEXT("NakamaServicePortSubsystem created"), Port)) { GI->Shutdown(); return false; }
	Port->SetRuntimeMode(EProject_JBackendRuntimeMode::StandaloneMock);

	int32 HandlerExecutions = 0;
	Port->RegisterMockHandler(TEXT("Economy"), TEXT("Commit"),
		[&](const ProjectJ::MMO::FServiceRequest& Req)
		{
			++HandlerExecutions;
			ProjectJ::MMO::FServiceResponse Resp;
			Resp.Status = ProjectJ::MMO::EServiceStatus::Success;
			Resp.Payload = Req.Payload;
			return Resp;
		});

	const FGuid Key = FGuid::NewGuid();
	ProjectJ::MMO::FServiceRequest Req1;
	Req1.RequestId = FGuid::NewGuid();
	Req1.IdempotencyKey = Key;
	Req1.Service = TEXT("Economy");
	Req1.Operation = TEXT("Commit");
	Req1.Intent = ProjectJ::MMO::EServiceRequestIntent::Mutation;
	Req1.Payload = { 1, 2, 3, 4 };

	ProjectJ::MMO::EServiceStatus Status1 = ProjectJ::MMO::EServiceStatus::Unavailable;
	Port->Submit(Req1, [&](ProjectJ::MMO::FServiceResponse R) { Status1 = R.Status; });
	TestEqual(TEXT("First mutation succeeds"), Status1, ProjectJ::MMO::EServiceStatus::Success);
	TestEqual(TEXT("Handler executed exactly once"), HandlerExecutions, 1);

	// Duplicate replay with identical idempotency key and payload must replay cached receipt without re-running handler.
	ProjectJ::MMO::FServiceRequest Req2 = Req1;
	Req2.RequestId = FGuid::NewGuid();
	ProjectJ::MMO::EServiceStatus Status2 = ProjectJ::MMO::EServiceStatus::Unavailable;
	Port->Submit(Req2, [&](ProjectJ::MMO::FServiceResponse R) { Status2 = R.Status; });
	TestEqual(TEXT("Idempotent replay succeeds"), Status2, ProjectJ::MMO::EServiceStatus::Success);
	TestEqual(TEXT("Handler NOT executed twice on idempotent replay"), HandlerExecutions, 1);

	// Reusing the same idempotency key with a different payload must fail closed with Conflict.
	ProjectJ::MMO::FServiceRequest Req3 = Req1;
	Req3.RequestId = FGuid::NewGuid();
	Req3.Payload = { 9, 9, 9 };
	ProjectJ::MMO::EServiceStatus Status3 = ProjectJ::MMO::EServiceStatus::Unavailable;
	Port->Submit(Req3, [&](ProjectJ::MMO::FServiceResponse R) { Status3 = R.Status; });
	TestEqual(TEXT("Mismatched payload for reused key returns Conflict"), Status3, ProjectJ::MMO::EServiceStatus::Conflict);

	// Monotonic session epoch fencing prevents stale server overwrites.
	const FGuid CharId = FGuid::NewGuid();
	const int64 Epoch1 = Port->AcquireMockSessionEpoch(CharId);
	TestTrue(TEXT("Epoch 1 is current"), Port->IsMockSessionEpochCurrent(CharId, Epoch1));
	const int64 Epoch2 = Port->AcquireMockSessionEpoch(CharId);
	TestTrue(TEXT("Epoch increases monotonically"), Epoch2 > Epoch1);
	TestFalse(TEXT("Old epoch is fenced out"), Port->IsMockSessionEpochCurrent(CharId, Epoch1));
	TestTrue(TEXT("New epoch is current"), Port->IsMockSessionEpochCurrent(CharId, Epoch2));

	GI->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPhase1HighSpeedStreamingTest, "ProjectJ.Systems.Phase1.HighSpeedTwoStageStreaming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPhase1HighSpeedStreamingTest::RunTest(const FString&)
{
	FProject_JStreamingVelocityProfile Profile;
	UProject_JWorldStreamingComponent::FTwoStageStreamingPlan Plan;

	// Walking speed (< 600 cm/s) keeps high-speed 2-stage dormant.
	TestTrue(TEXT("Walking query valid"),
		UProject_JWorldStreamingComponent::BuildTwoStageVelocityShapes(FVector(300, 0, 1400), false, 0.f, {}, Profile, Plan));
	TestFalse(TEXT("Walking does not engage high-speed 2-stage"), Plan.bHighSpeedActive);

	// High-speed flying (3000 cm/s, altitude 200m, straight) engages Stage 1 Loaded ahead of Stage 2 Activated + detail suppression.
	TestTrue(TEXT("Flying query valid"),
		UProject_JWorldStreamingComponent::BuildTwoStageVelocityShapes(FVector(3000, 0, 0), true, 0.f, 20000.f, Profile, Plan));
	TestTrue(TEXT("High-speed 2-stage engaged"), Plan.bHighSpeedActive);
	TestTrue(TEXT("Altitude >= 150m suppresses Grid_Detail"), Plan.bSuppressDetailGrid);
	TestFalse(TEXT("Straight flight does not fan-widen"), Plan.bTurnWidened);
	TestTrue(TEXT("Stage 1 Loaded shapes generated"), Plan.Stage1LoadedShapes.Num() > 0);
	TestTrue(TEXT("Stage 2 Activated shapes generated"), Plan.Stage2ActivatedShapes.Num() > 0);
	const double MaxLoadedX = Plan.Stage1LoadedShapes.Last().Location.X;
	const double MaxActivatedX = Plan.Stage2ActivatedShapes.Last().Location.X;
	TestTrue(TEXT("Stage 1 Loaded reaches further ahead than Stage 2 Activated"), MaxLoadedX > MaxActivatedX);

	// Sharp turn widens into +/-45 deg fan.
	TestTrue(TEXT("Turning query valid"),
		UProject_JWorldStreamingComponent::BuildTwoStageVelocityShapes(FVector(2000, 0, -1000), true, 90.f, 5000.f, Profile, Plan));
	TestTrue(TEXT("Sharp turn triggers fan widening"), Plan.bTurnWidened);
	TestFalse(TEXT("Low altitude keeps Grid_Detail active"), Plan.bSuppressDetailGrid);
	TestTrue(TEXT("Turn fan retains the straight descent and extends both sides"), Plan.Stage2ActivatedShapes.Num() == 3
		&& Plan.Stage2ActivatedShapes[0].Location.Y == 0.0 && Plan.Stage2ActivatedShapes[0].Location.Z < 0.0
		&& Plan.Stage2ActivatedShapes[1].Location.Y < 0.0 && Plan.Stage2ActivatedShapes[2].Location.Y > 0.0);

	TestTrue(TEXT("Vertical flight engages both stages without a false heading turn"),
		UProject_JWorldStreamingComponent::BuildTwoStageVelocityShapes(FVector(0, 0, 3000), true, 90.f, 20000.f, Profile, Plan)
		&& Plan.bHighSpeedActive && !Plan.bTurnWidened && Plan.Stage1LoadedShapes.Num() > 0 && Plan.Stage2ActivatedShapes.Num() > 0
		&& Plan.Stage1LoadedShapes.Last().Location.Z > Plan.Stage2ActivatedShapes.Last().Location.Z);
	TestTrue(TEXT("Descent restores detail before the current clearance crosses the threshold"),
		UProject_JWorldStreamingComponent::BuildTwoStageVelocityShapes(FVector(0, 0, -2000), true, 0.f, 20000.f, Profile, Plan)
		&& Plan.bHighSpeedActive && !Plan.bSuppressDetailGrid && Plan.Stage2ActivatedShapes.Num() > 0 && Plan.Stage2ActivatedShapes.Last().Location.Z < 0.0);

	// Unload hysteresis and soft speed safety clamp.
	TestTrue(TEXT("Anchor held within hysteresis duration"),
		UProject_JWorldStreamingComponent::ShouldHoldHysteresisAnchor(FVector(50000, 0, 0), FVector::ZeroVector, 3.0, Profile));
	TestFalse(TEXT("Far anchor released after hysteresis duration"),
		UProject_JWorldStreamingComponent::ShouldHoldHysteresisAnchor(FVector(50000, 0, 0), FVector::ZeroVector, 10.0, Profile));
	TestFalse(TEXT("Flight releases an anchor far below it after hysteresis"),
		UProject_JWorldStreamingComponent::ShouldHoldHysteresisAnchor(FVector(0, 0, 50000), FVector::ZeroVector, 10.0, Profile, true));
	TestEqual(TEXT("Ready forward cells keep full speed"),
		UProject_JWorldStreamingComponent::ComputeSafeStreamingSpeedScale(3000.f, true, Profile), 1.0f);
	const float ClampedScale = UProject_JWorldStreamingComponent::ComputeSafeStreamingSpeedScale(3000.f, false, Profile);
	TestTrue(TEXT("Unready forward cells apply soft speed clamp"), ClampedScale >= Profile.UnreadyForwardSpeedClampMin && ClampedScale < 1.0f);
	auto* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT {
		if (World->GetBegunPlay()) World->EndPlay(EEndPlayReason::LevelTransition);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};
	auto* Mount = World->SpawnActor<AProject_JMountMoverPawn>(FVector(0, 0, 20000), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Flight fixture"), Mount)) return false;
	auto* Mover = Mount->GetMountMover();
	Mover->MotionProfile.bCanFly = true;
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	FMoverSyncState Flight;
	Flight.MovementMode = UProject_JMountMoverComponent::FlyingMountModeName;
	Flight.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>().SetTransforms_WorldSpace(
		Mount->GetActorLocation(), FRotator::ZeroRotator, FVector(3000, 0, 0), FVector::ZeroVector);
	FMoverAuxStateContext Aux;
	Mover->FinalizeFrame(&Flight, &Aux);
	auto* Source = NewObject<UProject_JWorldStreamingComponent>(Mount);
	TestTrue(TEXT("Missing surface retains detail even at high world Z"),
		Mount->IsFlying() && Source->BuildPawnVelocityPlan(*Mount, Profile, Plan) && Plan.bHighSpeedActive && !Plan.bSuppressDetailGrid);
	auto* Ridge = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Loaded static ridge"), Ridge)) return false;
	auto* RidgeCollision = NewObject<UBoxComponent>(Ridge);
	Ridge->SetRootComponent(RidgeCollision);
	RidgeCollision->InitBoxExtent(FVector(1000, 1000, 50));
	RidgeCollision->SetCollisionProfileName(TEXT("BlockAll"));
	RidgeCollision->RegisterComponent();
	Ridge->SetActorLocation(FVector(0, 0, 19000));
	++GFrameCounter;
	TestTrue(TEXT("High-elevation low flight retains detail when the ridge is loaded"),
		Source->BuildPawnVelocityPlan(*Mount, Profile, Plan) && !Plan.bSuppressDetailGrid);
	Ridge->SetActorLocation(FVector::ZeroVector);
	++GFrameCounter;
	TestTrue(TEXT("Confirmed clearance over a loaded surface permits detail suppression"),
		Source->BuildPawnVelocityPlan(*Mount, Profile, Plan) && Plan.bSuppressDetailGrid);
	auto* Ground = World->SpawnActor<ACharacter>(FVector(5000, 0, 0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Unrelated ground pawn"), Ground)) return false;
	Ground->GetCharacterMovement()->Velocity = FVector(0, 3000, 1000);
	Source->FlightReadinessPawn = Mount;
	Source->HysteresisAnchorTime = World->GetTimeSeconds();
	TestTrue(TEXT("Same-frame pawn swap uses the ground pawn's heading without the flight grid exclusion"),
		Source->BuildPawnVelocityPlan(*Ground, Profile, Plan) && Plan.bHighSpeedActive && !Plan.bSuppressDetailGrid && !Plan.bTurnWidened
		&& !Plan.Stage1LoadedShapes.IsEmpty() && Plan.Stage1LoadedShapes.Last().Location.Y > 0 && Plan.Stage1LoadedShapes.Last().Location.Z == 0);
	TestTrue(TEXT("The new pawn retires both old flight readiness and the trailing anchor"),
		!Source->FlightReadinessPawn.IsValid() && Source->HysteresisAnchorTime < 0);
	Source->FlightReadinessPawn = Ground;
	Source->HysteresisAnchorTime = World->GetTimeSeconds();
	TestTrue(TEXT("The same pawn can jump to another streaming region in the same frame"),
		Ground->SetActorLocation(FVector(100000, 0, 0), false, nullptr, ETeleportType::TeleportPhysics));
	Ground->GetCharacterMovement()->Velocity = FVector(3000, 0, 0);
	TestTrue(TEXT("The new region gets a fresh heading and no old residency hold"),
		Source->BuildPawnVelocityPlan(*Ground, Profile, Plan) && !Plan.bTurnWidened
		&& !Plan.Stage1LoadedShapes.IsEmpty() && Plan.Stage1LoadedShapes.Last().Location.X > 0
		&& Source->HysteresisAnchorTime < 0 && !Source->FlightReadinessPawn.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPhase1CheatManagerTest, "ProjectJ.Systems.Phase1.CheatManagerAuthorityGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPhase1CheatManagerTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	World->GetWorldSettings()->NotifyMatchStarted();

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APlayerController* PC = World->SpawnActor<APlayerController>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	AProject_JNPCCharacter* Pawn = World->SpawnActor<AProject_JNPCCharacter>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (TestNotNull(TEXT("PC"), PC) && TestNotNull(TEXT("Pawn"), Pawn))
	{
		PC->Possess(Pawn);
		UProject_JCheatManager* Cheats = NewObject<UProject_JCheatManager>(PC);
		TestTrue(TEXT("Authority controller can execute non-shipping cheat"), Cheats->CanExecuteAuthorityCheat());
		Cheats->PJ_Teleport(500.f, 250.f, 150.f);
		TestTrue(TEXT("Teleport moved pawn"), FVector::Dist(Pawn->GetActorLocation(), FVector(500.f, 250.f, 150.f)) < 5.f);
		Cheats->PJ_Teleport(NAN, 0.f, 0.f);
		TestFalse(TEXT("NaN teleport rejected safely"), Pawn->GetActorLocation().ContainsNaN());
	}

	World->EndPlay(EEndPlayReason::LevelTransition);
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}
#endif
