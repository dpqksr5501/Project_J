#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BrushComponent.h"
#include "Builders/CubeBuilder.h"
#include "ActorFactories/ActorFactory.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "Project_JNPCCharacter.h"
#include "Project_JAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "System/Project_JNPCPathSubsystem.h"
#include "System/Project_JNPCDecisionSubsystem.h"
#include "Components/Project_JNPCActionComponent.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "HAL/PlatformTime.h"

namespace
{
	class FNativeNavCommand : public IAutomationLatentCommand
	{
	public:
		explicit FNativeNavCommand(FAutomationTestBase* InTest, bool bTestMovingTarget = false)
			: Test(InTest), Started(FPlatformTime::Seconds()), bMovingTarget(bTestMovingTarget)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			auto AddBox = [&](FVector Location, FVector Scale)
			{
				auto* Actor = World->SpawnActor<AActor>();
				auto* Mesh = NewObject<UStaticMeshComponent>(Actor);
				Actor->SetRootComponent(Mesh); Mesh->SetStaticMesh(Cube);
				Mesh->SetCollisionProfileName(TEXT("BlockAll")); Mesh->SetMobility(EComponentMobility::Static);
				Mesh->RegisterComponent(); Actor->SetActorLocation(Location); Actor->SetActorScale3D(Scale);
			};
			AddBox(FVector(0, 0, -50), FVector(40, 30, 1));
			AddBox(FVector(0, 0, 150), FVector(4, 5, 3)); // obstacle on the straight route
			auto* Bounds = World->SpawnActor<ANavMeshBoundsVolume>();
			auto* Builder = NewObject<UCubeBuilder>();
			Builder->X = 3800; Builder->Y = 2800; Builder->Z = 1000;
			UActorFactory::CreateBrushForVolumeActor(Bounds, Builder);
			FNavigationSystem::AddNavigationSystemToWorld(*World, FNavigationSystemRunMode::GameMode);
			Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			if (Nav)
			{
				Nav->OnNavigationBoundsUpdated(Bounds);
				if (auto* Data = Nav->GetDefaultNavDataInstance(FNavigationSystem::Create)) { Data->MarkRequiresInitialRebuild(); }
				Nav->Build();
			}
			FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			NPC = World->SpawnActor<AProject_JNPCCharacter>(AProject_JNPCCharacter::StaticClass(), FVector(-900, 0, 100), FRotator::ZeroRotator, P);
			AI = World->SpawnActor<AAIController>(); AI->Possess(NPC);
			NPC->GetMesh()->SetCanEverAffectNavigation(false);
			NPC->GetCharacterMovement()->MaxWalkSpeed = 300;
			NPC->GetAbilitySystemComponent()->InitAbilityActorInfo(NPC, NPC);
			NPC->GetAttributeSet()->InitHealth(100);
			World->InitializeActorsForPlay(FURL());
			World->GetWorldSettings()->NotifyBeginPlay();
			World->GetWorldSettings()->NotifyMatchStarted();
			Service = World->GetSubsystem<UProject_JNPCPathSubsystem>();
			if (bMovingTarget)
			{
				MovingTarget = World->SpawnActor<AActor>();
				auto* Root = NewObject<USceneComponent>(MovingTarget); MovingTarget->SetRootComponent(Root); Root->RegisterComponent();
				MovingTarget->SetActorLocation(FVector(900, 0, 90));
				World->GetSubsystem<UProject_JNPCDecisionSubsystem>()->RegisterTarget(MovingTarget, 2);
				auto* Scoring = NewObject<UProject_JTargetScoringComponent>(NPC); NPC->AddInstanceComponent(Scoring); Scoring->RegisterComponent();
				Scoring->Range = 4000; Scoring->bUseUrgentNPCDecisionInterval = true;
				Scoring->StartBatchedNPCDecisions(1);
				Actions = NewObject<UProject_JNPCActionComponent>(NPC); NPC->AddInstanceComponent(Actions); Actions->RegisterComponent();
				Actions->RetryInterval = 0.1;
				Test->TestTrue(TEXT("Real moving-target consumer starts"), Actions->StartActions(Scoring, {}));
				NPC->GetCharacterMovement()->GetNavMovementProperties()->bUseAccelerationForPaths = true;
			}
		}
		~FNativeNavCommand()
		{
			World->EndPlay(EEndPlayReason::LevelTransition);
			World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
		}
		bool Update() override
		{
			if (!Nav || !NPC || !AI || !Service) { Test->AddError(TEXT("Native navigation fixture initialization failed")); return true; }
			if (FPlatformTime::Seconds() - Started > 30)
			{
				Test->AddError(FString::Printf(TEXT("Nav integration timed out: issued=%d completed=%d points=%d position=%s navbuilding=%d"),
					bIssued, bCompleted, PathPoints, *NPC->GetActorLocation().ToString(), Nav->IsNavigationBuildInProgress()));
				return true;
			}
			if (bMovingTarget)
			{
				SimulatedSeconds += 0.05;
				MovingTarget->SetActorLocation(FVector(900, FMath::Min(SimulatedSeconds * 100.0, 800.0), 90));
				const auto PreviousMove = AI->GetPathFollowingComponent()->GetCurrentRequestId();
				const bool bWasFollowing = AI->GetPathFollowingComponent()->GetStatus() == EPathFollowingStatus::Moving;
				const uint64 AcceptedBefore = Service->GetStats().Accepted;
				Actions->UpdateAction();
				if (bWasFollowing && Service->GetStats().Accepted > AcceptedBefore)
				{
					Test->TestEqual(TEXT("Repath admission preserves current movement ID"), AI->GetPathFollowingComponent()->GetCurrentRequestId(), PreviousMove);
					Test->TestEqual(TEXT("Repath admission preserves active path following"), AI->GetPathFollowingComponent()->GetStatus(), EPathFollowingStatus::Moving);
					++ContinuousRepaths;
				}
				World->Tick(LEVELTICK_All, 0.05f);
				if (SimulatedSeconds >= 8.0 && Actions->GetActionState() == EProjectJNPCActionState::InRange)
				{
					Test->TestTrue(TEXT("Moving target causes multiple asynchronous paths"), Service->GetStats().Dispatched > 1);
					Test->TestTrue(TEXT("Observed replacement requests while continuing a valid old path"), ContinuousRepaths > 0);
					Test->TestTrue(TEXT("NPC reaches the moved target"), FVector::Dist(NPC->GetActorLocation(), MovingTarget->GetActorLocation()) <= Actions->AttackRange);
					Test->AddInfo(FString::Printf(TEXT("Moving target: dispatches=%llu continuous_repaths=%d final=%s"),
						Service->GetStats().Dispatched, ContinuousRepaths, *NPC->GetActorLocation().ToString()));
					return true;
				}
				return false;
			}
			World->Tick(LEVELTICK_All, 0.05f);
			if (!bIssued)
			{
				FNavLocation GoalOnNav, StartOnNav;
				if (!Nav->ProjectPointToNavigation(FVector(900, 0, 0), GoalOnNav)
					|| !Nav->ProjectPointToNavigation(NPC->GetNavAgentLocation(), StartOnNav)) { return false; }
				bIssued = true; Goal = GoalOnNav.Location;
				const auto Token = Service->Submit(NPC, NPC, Goal, 1, [this](const FProjectJNPCPathCompletion& Result)
				{
					bCompleted = true;
					Test->TestEqual(TEXT("Real async Recast result"), Result.Status, EProjectJNPCPathStatus::Success);
					if (!Result.Path) { return; }
					PathPoints = Result.Path->GetPathPoints().Num();
					Test->TestTrue(TEXT("Route avoids obstacle rather than a straight-line mock"), PathPoints > 2);
					FAIMoveRequest Move; Move.SetGoalLocation(Goal); Move.SetAcceptanceRadius(60);
					MoveId = AI->RequestMove(Move, Result.Path);
					Test->TestTrue(TEXT("Real path-following accepts completed path"), MoveId.IsValid());
				});
				Test->TestTrue(TEXT("Async service accepts native world request"), Token != 0);
			}
			if (bCompleted && FVector::DistSquared2D(NPC->GetActorLocation(), Goal) < FMath::Square(120.0))
			{
				Test->AddInfo(FString::Printf(TEXT("Real Recast + CharacterMovement: points=%d final=%s elapsed=%.3fs dispatches=%llu"),
					PathPoints, *NPC->GetActorLocation().ToString(), FPlatformTime::Seconds() - Started, Service->GetStats().Dispatched));
				return true;
			}
			return false;
		}
	private:
		FAutomationTestBase* Test;
		UWorld* World = nullptr;
		UNavigationSystemV1* Nav = nullptr;
		AProject_JNPCCharacter* NPC = nullptr;
		AAIController* AI = nullptr;
		UProject_JNPCPathSubsystem* Service = nullptr;
		FVector Goal = FVector::ZeroVector;
		FAIRequestID MoveId;
		double Started;
		int32 PathPoints = 0;
		bool bIssued = false, bCompleted = false;
		bool bMovingTarget = false;
		AActor* MovingTarget = nullptr;
		UProject_JNPCActionComponent* Actions = nullptr;
		double SimulatedSeconds = 0;
		int32 ContinuousRepaths = 0;
	};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNativeNavigationTest, "ProjectJ.Integrated.NativeNavMovement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNativeNavigationTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FNativeNavCommand(this));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMovingTargetNavigationTest, "ProjectJ.Integrated.MovingTargetPursuit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMovingTargetNavigationTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FNativeNavCommand(this, true));
	return true;
}
#endif
