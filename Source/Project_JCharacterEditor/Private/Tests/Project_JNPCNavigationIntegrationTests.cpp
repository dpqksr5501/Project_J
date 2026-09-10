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
#include "Mass/Project_JMassRepresentationSubsystem.h"
#include "Components/Project_JNPCActionComponent.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "HAL/PlatformTime.h"
#include "UObject/UnrealType.h"

namespace
{
	class FNativeNavCommand : public IAutomationLatentCommand
	{
	public:
		explicit FNativeNavCommand(FAutomationTestBase* InTest, bool bTestMovingTarget = false, bool bTestRebuild = false, bool bTestAutomaticDirty = false, bool bTestMassRoute = false)
			: Test(InTest), Started(FPlatformTime::Seconds()), bMovingTarget(bTestMovingTarget), bRebuild(bTestRebuild), bAutomaticDirty(bTestAutomaticDirty), bMassRoute(bTestMassRoute)
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
				return Actor;
			};
			AddBox(FVector(0, 0, -50), FVector(40, 30, 1));
			Obstacle = AddBox(FVector(0, 0, 150), FVector(4, 5, 3)); // obstacle on the straight route
			if (bRebuild) { CastChecked<UStaticMeshComponent>(Obstacle->GetRootComponent())->SetMobility(EComponentMobility::Movable); }
			auto* Bounds = World->SpawnActor<ANavMeshBoundsVolume>();
			auto* Builder = NewObject<UCubeBuilder>();
			Builder->X = 3800; Builder->Y = 2800; Builder->Z = 1000;
			UActorFactory::CreateBrushForVolumeActor(Bounds, Builder);
			FNavigationSystem::AddNavigationSystemToWorld(*World, FNavigationSystemRunMode::GameMode);
			Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			if (Nav)
			{
				Nav->OnNavigationBoundsUpdated(Bounds);
				if (auto* Data = Nav->GetDefaultNavDataInstance(FNavigationSystem::Create))
				{
					if (bRebuild)
					{
						auto* Property = FindFProperty<FEnumProperty>(ANavigationData::StaticClass(), TEXT("RuntimeGeneration"));
						Property->GetUnderlyingProperty()->SetIntPropertyValue(Property->ContainerPtrToValuePtr<void>(Data), int64(ERuntimeGenerationType::Dynamic));
					}
					Data->MarkRequiresInitialRebuild();
				}
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
				if (bMassRoute && bWasFollowing && !MassToken.IsValid())
				{
					auto* Bridge = World->GetSubsystem<UProject_JMassRepresentationSubsystem>();
					MassPath = AI->GetPathFollowingComponent()->GetPath();
					MassToken = Bridge->RegisterNPC(NPC);
					Test->TestTrue(TEXT("Owned Character path can be lent to Mass"), Bridge->SetRoute(MassToken, MassPath));
					Bridge->SetObserversForTest({FVector(20000, 0, 0)});
					Bridge->Tick(0);
					Test->TestTrue(TEXT("Active pursuit hands movement to Mass"), Bridge->IsMassOwned(MassToken.Id));
					Test->TestEqual(TEXT("Old action consumer stopped before Mass moves"), Actions->GetActionState(), EProjectJNPCActionState::Disabled);
					Test->TestEqual(TEXT("Owned path following released"), AI->GetPathFollowingComponent()->GetStatus(), EPathFollowingStatus::Idle);
					const FVector Before = NPC->GetActorLocation();
					World->Tick(LEVELTICK_All, 0.05f);
					Test->TestTrue(TEXT("Mass advances the retained Character after action shutdown"), !NPC->GetActorLocation().Equals(Before));
					Bridge->SetObserversForTest({NPC->GetActorLocation()}); Bridge->Tick(0);
					Test->TestFalse(TEXT("Near promotion releases Mass ownership"), Bridge->IsMassOwned(MassToken.Id));
					Test->TestTrue(TEXT("Scoring/action start contract restored"), Actions->GetScoringSource() && Actions->GetActionState() != EProjectJNPCActionState::Disabled);
					Test->TestTrue(TEXT("Movement component restored for resumed pursuit"), NPC->GetCharacterMovement()->IsComponentTickEnabled());
					Bridge->UnregisterNPC(Bridge->GetToken(MassToken.Id));
					return false; // Continue the real moving-target scenario until resumed pursuit arrives.
				}
				if (bRebuild && !bRebuilt && bWasFollowing)
				{
					bRebuilt = true; DispatchBeforeRebuild = Service->GetStats().Dispatched;
					PathBeforeDirty = AI->GetPathFollowingComponent()->GetPath();
					Obstacle->SetActorLocation(FVector(0, 500, 150));
					if (!bAutomaticDirty)
					{
						// Legacy GroupA checks explicit invalidation/build. GroupC leaves both to the engine.
						if (auto Path = AI->GetPathFollowingComponent()->GetPath()) { Path->Invalidate(); }
						Nav->Build();
					}
				}
				const uint64 AcceptedBefore = Service->GetStats().Accepted;
				Actions->UpdateAction();
				if (bWasFollowing && !(bRebuild && bRebuilt && Service->GetStats().Dispatched == DispatchBeforeRebuild) && Service->GetStats().Accepted > AcceptedBefore)
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
					if (!bRebuild)
					{
						Test->TestTrue(TEXT("Real RequestMove replaces an owned moving path"), Actions->GetStats().ReplacedMoves > 0);
						Test->TestEqual(TEXT("Successful moving-target replacements never schedule failure backoff"), Actions->GetStats().RetryScheduled, uint64(0));
					}
					if (bRebuild)
					{
						Test->TestTrue(TEXT("Dynamic obstacle rebuild exercised"), bRebuilt);
						Test->TestTrue(TEXT("Async service recovers after path invalidation and rebuild"), Service->GetStats().Dispatched > DispatchBeforeRebuild);
						if (bAutomaticDirty)
						{
							FNavLocation Position;
							Test->TestTrue(TEXT("Automatic dirty update opens the old obstacle center"),
								Nav->ProjectPointToNavigation(FVector(0, 0, 0), Position, FVector(30, 30, 100)));
							Test->TestTrue(TEXT("Engine invalidated the consumed path without explicit invalidate"),
								PathBeforeDirty.IsValid() && !PathBeforeDirty->IsUpToDate());
						}
					}
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
					if (bMassRoute)
					{
						auto* Bridge = World->GetSubsystem<UProject_JMassRepresentationSubsystem>();
						Bridge->SetObserversForTest({FVector(20000, 0, 0)});
						MassToken = Bridge->RegisterNPC(NPC); MassPath = Result.Path;
						Test->TestTrue(TEXT("C service path handed to Mass as route values"), Bridge->SetRoute(MassToken, Result.Path));
						return;
					}
					FAIMoveRequest Move; Move.SetGoalLocation(Goal); Move.SetAcceptanceRadius(60);
					MoveId = AI->RequestMove(Move, Result.Path);
					Test->TestTrue(TEXT("Real path-following accepts completed path"), MoveId.IsValid());
				});
				Test->TestTrue(TEXT("Async service accepts native world request"), Token != 0);
			}
			if (bCompleted && FVector::DistSquared2D(NPC->GetActorLocation(), Goal) < FMath::Square(120.0))
			{
				if (bMassRoute)
				{
					auto* Bridge = World->GetSubsystem<UProject_JMassRepresentationSubsystem>();
					Test->TestTrue(TEXT("Mass owns actual Recast route traversal"), Bridge->IsMassOwned(MassToken.Id));
					Test->TestFalse(TEXT("No simultaneous CharacterMovement writer"), NPC->GetCharacterMovement()->IsComponentTickEnabled());
					MassPath->Invalidate(); const FVector Position = NPC->GetActorLocation(); Bridge->Tick(0.05f);
					Test->TestFalse(TEXT("Invalidated Recast route returns movement authority"), Bridge->IsMassOwned(MassToken.Id));
					Test->TestTrue(TEXT("Invalidation cannot advance old route"), NPC->GetActorLocation().Equals(Position));
					Test->TestTrue(TEXT("CMC restored after real route handoff"), NPC->GetCharacterMovement()->IsComponentTickEnabled());
					Bridge->UnregisterNPC(Bridge->GetToken(MassToken.Id));
				}
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
		FNavPathSharedPtr PathBeforeDirty;
		double Started;
		int32 PathPoints = 0;
		bool bIssued = false, bCompleted = false;
		bool bMovingTarget = false;
		bool bRebuild = false, bRebuilt = false;
		bool bAutomaticDirty = false;
		bool bMassRoute = false;
		FProjectJMassAgentToken MassToken;
		FNavPathSharedPtr MassPath;
		AActor* Obstacle = nullptr;
		uint64 DispatchBeforeRebuild = 0;
		AActor* MovingTarget = nullptr;
		UProject_JNPCActionComponent* Actions = nullptr;
		double SimulatedSeconds = 0;
		int32 ContinuousRepaths = 0;
	};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMassNativeNavigationTest, "ProjectJ.GroupD.MassNativeNavigation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMassNativeNavigationTest::RunTest(const FString& Parameters)
{
	AddExpectedMessage(TEXT("SpawnMissingNavigationData: No saved navigation data found"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
	AddExpectedMessage(TEXT("Unable to find RecastNavMesh instance while trying to create UCrowdManager instance"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
	ADD_LATENT_AUTOMATION_COMMAND(FNativeNavCommand(this, false, false, false, true));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMassActionHandoffTest, "ProjectJ.GroupD.MassActionHandoff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMassActionHandoffTest::RunTest(const FString& Parameters)
{
	AddExpectedMessage(TEXT("SpawnMissingNavigationData: No saved navigation data found"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
	AddExpectedMessage(TEXT("Unable to find RecastNavMesh instance while trying to create UCrowdManager instance"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
	ADD_LATENT_AUTOMATION_COMMAND(FNativeNavCommand(this, true, false, false, true));
	return true;
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJDynamicNavRecoveryTest, "ProjectJ.GroupA.DynamicNavRecovery",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJDynamicNavRecoveryTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FNativeNavCommand(this, true, true));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAutomaticDirtyNavRecoveryTest, "ProjectJ.GroupC.AutomaticDirtyRecovery",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAutomaticDirtyNavRecoveryTest::RunTest(const FString& Parameters)
{
	AddExpectedMessage(TEXT("SpawnMissingNavigationData: No saved navigation data found"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
	AddExpectedMessage(TEXT("Unable to find RecastNavMesh instance while trying to create UCrowdManager instance"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
	ADD_LATENT_AUTOMATION_COMMAND(FNativeNavCommand(this, true, true, true));
	return true;
}
#endif
