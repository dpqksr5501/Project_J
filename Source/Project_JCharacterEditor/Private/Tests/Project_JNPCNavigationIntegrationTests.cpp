#if WITH_DEV_AUTOMATION_TESTS
#include "DefaultMovementSet/NavMoverComponent.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "Misc/AutomationTest.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/CapsuleComponent.h"
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
#include "GameFramework/GameStateBase.h"
#include "System/Project_JNPCPathSubsystem.h"
#include "System/Project_JNPCDecisionSubsystem.h"
#include "Components/Project_JNPCActionComponent.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "HAL/PlatformTime.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "AnimPose.h"
#include "Engine/SkeletalMeshSocket.h"
#include "HAL/IConsoleManager.h"
#include "Animation/BlendSpace.h"
#include "AnimNodes/AnimNode_BlendSpacePlayer.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "Components/Project_JNPCActivationComponent.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Components/Project_JCombatPresentationComponent.h"
#include "Combat/Project_JCombatPresentationSet.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Animation/Project_JAnimNotifyState_MeleeHit.h"
#include "Project_JAbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "Abilities/GameplayAbility.h"
#include "Authoring/Project_JCanonicalMeleeAuthoring.h"
#include "Movement/Project_JMovementQueries.h"
#include "Movement/Project_JMoverPawn.h"
#include "Combat/Project_JMeleeTrajectory.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "MoverDataModelTypes.h"

namespace
{
	class FNativeNavCommand : public IAutomationLatentCommand
	{
		static constexpr double OwnerFollowDistance = 200.0;
	public:
		explicit FNativeNavCommand(FAutomationTestBase* InTest, bool bTestMovingTarget = false, bool bTestRebuild = false, bool bTestAutomaticDirty = false, bool bTestOwnerFollow = false, bool bTestNavDataRetirement = false, bool bTestPointNavigation = false, bool bTestNPCExecution = false, bool bTestMeleeWindowHitch = false, bool bTestCanonicalPath = false, bool bTestCanonicalStableBase = false)
			: Test(InTest), Started(FPlatformTime::Seconds()), bMovingTarget(bTestMovingTarget), bRebuild(bTestRebuild), bAutomaticDirty(bTestAutomaticDirty), bOwnerFollow(bTestOwnerFollow), bNavDataRetirement(bTestNavDataRetirement), bPointNavigation(bTestPointNavigation || bTestNPCExecution || bTestMeleeWindowHitch), bNPCExecution(bTestNPCExecution || bTestMeleeWindowHitch), bMeleeWindowHitch(bTestMeleeWindowHitch), bCanonicalPath(bTestCanonicalPath), bCanonicalStableBase(bTestCanonicalStableBase)
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
			CanonicalFloorActor = AddBox(FVector(0, 0, -50), FVector(40, 30, 1));
			Obstacle = AddBox(FVector(0, 0, 150), FVector(4, 5, 3)); // obstacle on the straight route
			if (bRebuild) { CastChecked<UStaticMeshComponent>(Obstacle->GetRootComponent())->SetMobility(EComponentMobility::Movable); }
			auto* Bounds = World->SpawnActor<ANavMeshBoundsVolume>();
			auto* Builder = NewObject<UCubeBuilder>();
			Builder->X = 3800; Builder->Y = 2800; Builder->Z = 1000;
			UActorFactory::CreateBrushForVolumeActor(Bounds, Builder);
			// Create the service first so the explicit nav-data actor can register,
			// then initialize the world with that data already available. Creating
			// the actor before the service leaves it without a registered generator.
			FNavigationSystem::AddNavigationSystemToWorld(*World, FNavigationSystemRunMode::GameMode, nullptr, false);
			Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			if(Nav)
			{
				// This fixture drives AAIController's standard path follower and
				// NavMover, not DetourCrowd agents. Do not create an unused crowd
				// service that searches for a replacement navmesh during teardown.
				// Only this transient instance is configured; project defaults stay intact.
				auto* CrowdClass=FindFProperty<FSoftClassProperty>(Nav->GetClass(),TEXT("CrowdManagerClass"));
				check(CrowdClass);
				CrowdClass->SetObjectPropertyValue_InContainer(Nav,nullptr);
			}
			auto* FixtureNavData=World->SpawnActor<ARecastNavMesh>();
			FixtureNavData->SetConfig(UNavigationSystemV1::GetDefaultSupportedAgent());
			// Static transient nav data must request its initial build before world
			// initialization decides whether to retain geometry in the nav octree.
			FixtureNavData->MarkRequiresInitialRebuild();
			if(bRebuild)
			{
				auto* Property=FindFProperty<FEnumProperty>(ANavigationData::StaticClass(),TEXT("RuntimeGeneration"));
				Property->GetUnderlyingProperty()->SetIntPropertyValue(Property->ContainerPtrToValuePtr<void>(FixtureNavData),int64(ERuntimeGenerationType::Dynamic));
			}
			if (Nav)
			{
				Nav->InitializeForWorld(*World,FNavigationSystemRunMode::GameMode);
				Nav->OnNavigationBoundsUpdated(Bounds);
				Nav->Build();
			}
			FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			UClass* NPCClass = bNPCExecution ? LoadClass<AProject_JNPCCharacter>(nullptr, TEXT("/Game/NPCs/BP_NPC_Test.BP_NPC_Test_C")) : AProject_JNPCCharacter::StaticClass();
			if (!Test->TestNotNull(TEXT("Selected real NPC class resolves"), NPCClass)) { bSetupFailed = true; return; }
			NPC = World->SpawnActor<AProject_JNPCCharacter>(NPCClass, FVector(-900, 0, 100), FRotator::ZeroRotator, P);
			if (!Test->TestNotNull(TEXT("Selected native/BP NPC spawns"), NPC)) { bSetupFailed = true; return; }
			if (!bPointNavigation)
			{
				AI = World->SpawnActor<AAIController>(); AI->Possess(NPC);
				Test->TestEqual(TEXT("This fixture exercises the standard path follower"),AI->GetPathFollowingComponent()->GetClass(),UPathFollowingComponent::StaticClass());
			}
			NPC->GetMesh()->SetCanEverAffectNavigation(false);
			NPC->GetMover()->FindSharedSettings_Mutable<UCommonLegacyMovementSettings>()->MaxSpeed = 300;
			NPC->GetAbilitySystemComponent()->InitAbilityActorInfo(NPC, NPC);
			NPC->GetAttributeSet()->InitHealth(100);
			World->InitializeActorsForPlay(FURL());
			World->GetWorldSettings()->NotifyBeginPlay();
			World->GetWorldSettings()->NotifyMatchStarted();
			Service = World->GetSubsystem<UProject_JNPCPathSubsystem>();
			if (bNPCExecution && !PrepareNPCExecution()) { bSetupFailed = true; return; }
			if (bCanonicalPath && !PrepareCanonicalExecution()) { bSetupFailed = true; return; }
			if (bMovingTarget)
			{
				// The target is scripted; only the follower is a Mover-driven NPC.
				// Moving a second live Mover by SetActorLocation would bypass its simulation.
				MovingTarget = World->SpawnActor<AActor>();
				if (!Test->TestNotNull(TEXT("Scripted moving target spawns"), MovingTarget)) { bSetupFailed = true; return; }
				auto* Root = NewObject<USceneComponent>(MovingTarget); MovingTarget->SetRootComponent(Root); Root->RegisterComponent();
				MovingTarget->SetActorLocation(FVector(900, 0, 90));
				Actions = NewObject<UProject_JNPCActionComponent>(NPC); NPC->AddInstanceComponent(Actions); Actions->RegisterComponent();
				Actions->RetryInterval = 0.1;
				if (bOwnerFollow)
				{
					auto* OwnerASC = NewObject<UAbilitySystemComponent>(MovingTarget);
					MovingTarget->AddInstanceComponent(OwnerASC); OwnerASC->RegisterComponent();
					auto* Attributes = NewObject<UProject_JAttributeSet>(OwnerASC);
					OwnerASC->AddAttributeSetSubobject(Attributes);
					OwnerASC->InitAbilityActorInfo(MovingTarget, MovingTarget);
					Attributes->InitMaxHealth(100); Attributes->InitHealth(100);
					Test->TestTrue(TEXT("Explicit follow consumes the same real navigation/Mover path"), Actions->StartFollowing(MovingTarget, OwnerASC, OwnerFollowDistance));
					Test->TestFalse(TEXT("Owner-follow cannot authorize an attack on its owner"), Actions->CanCommitToTarget(MovingTarget));
				}
				else
				{
					World->GetSubsystem<UProject_JNPCDecisionSubsystem>()->RegisterTarget(MovingTarget, 2);
					auto* Scoring = NewObject<UProject_JTargetScoringComponent>(NPC); NPC->AddInstanceComponent(Scoring); Scoring->RegisterComponent();
					Scoring->Range = 4000; Scoring->bUseUrgentNPCDecisionInterval = true; Scoring->StartBatchedNPCDecisions(1);
					Test->TestTrue(TEXT("Real moving-target consumer starts"), Actions->StartActions(Scoring, {}));
				}
				NPC->NavMover->GetNavMovementProperties()->bUseAccelerationForPaths = true;
			}
		}
		~FNativeNavCommand()
		{
			if (bNPCExecution) CleanupNPCExecution();
			World->EndPlay(EEndPlayReason::LevelTransition);
			World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
		}
		bool Update() override
		{
			if (bSetupFailed) return true;
			if (!Nav || !NPC || (!bPointNavigation && !AI) || !Service) { Test->AddError(TEXT("Native navigation fixture initialization failed")); return true; }
			if (FPlatformTime::Seconds() - Started > 30)
			{
				if (bNPCExecution) Test->AddInfo(FString::Printf(TEXT("AuthoredNPC timeout stage=%d finalizations=%d idle=%d blendChanged=%d movingPose=%d naturalEnd=%d cancelled=%d contacts=%d effects=%d violation=%d preparedController=%d"), ExecutionStage, ExecutionFinalizations, bExecutionIdleObserved, bExecutionBlendChanged, bExecutionMovingPose, bExecutionNaturalEnd, bExecutionCancelled, ExecutionContacts, ExecutionEffects, bExecutionViolation, bExecutionPreparedController));
				Test->AddError(FString::Printf(TEXT("Nav integration timed out: issued=%d completed=%d points=%d position=%s navbuilding=%d"),
					bIssued, bCompleted, PathPoints, *NPC->GetActorLocation().ToString(), Nav->IsNavigationBuildInProgress()));
				return true;
			}
			if (bCanonicalPath) return UpdateCanonicalExecution();
			if (bNPCExecution) return UpdateNPCExecution();
			if (bPointNavigation) return UpdatePointNavigation();
			if (bMovingTarget)
			{
				SimulatedSeconds += 0.05;
				MovingTarget->SetActorLocation(FVector(900, FMath::Min(SimulatedSeconds * 100.0, 800.0)+QueuedGoalOffsetY, 90));
				const auto PreviousMove = AI->GetPathFollowingComponent()->GetCurrentRequestId();
				const bool bWasFollowing = AI->GetPathFollowingComponent()->GetStatus() == EPathFollowingStatus::Moving;
				bool bRetiredThisFrame = false;
				if (bNavDataRetirement && !bRetiredNavData && bWasFollowing)
				{
					const auto LivePath = AI->GetPathFollowingComponent()->GetPath();
					auto* Data = LivePath.IsValid() ? LivePath->GetNavigationDataUsed() : nullptr;
					if (!Test->TestTrue(TEXT("Real consumed path has registered same-world NavData"), IsValid(Data) && Data->IsRegistered() && Data->GetWorld() == World)) return true;
					DispatchBeforeRetirement = Service->GetStats().Dispatched;
					Nav->UnregisterNavData(Data);
					bRetiredNavData = bRetiredThisFrame = true;
					Test->TestFalse(TEXT("Actual NavData unregisters"), Data->IsRegistered());
					Test->TestTrue(TEXT("Unregister alone leaves the actual path's flags ready"), LivePath->IsValid());
					Test->TestFalse(TEXT("Registered-owner check rejects the orphaned actual path"), Service->IsPathUsable(LivePath));
					Actions->UpdateAction();
					Test->TestTrue(TEXT("Actual follow consumer stops the retired NavData path"), AI->GetPathFollowingComponent()->GetStatus() != EPathFollowingStatus::Moving);
					RetiredNavData = Data;
					// Registration is queued through the system's public API. Let the
					// normal navigation tick process it, without calling protected hooks.
					Nav->RequestRegistrationDeferred(*Data);
					// The old owned move was stopped. Normal bounded action retry must
					// now obtain and consume another real async path to reach the target.
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
				// One existing real-follow case: refresh a newly queued replacement
				// before the service/world tick, while the old path remains in use.
				if (bOwnerFollow && !bNavDataRetirement && !bQueuedGoalRefreshed && bWasFollowing && Service->GetStats().Accepted>AcceptedBefore)
				{
					const uint64 PendingToken=Actions->GetPendingPathTokenForTest();
					const uint64 AcceptedNow=Service->GetStats().Accepted;
					QueuedRefreshAccepted=AcceptedNow; MoveBeforeQueuedRefresh=PreviousMove;
					QueuedGoalOffsetY=Actions->RepathDistance+50;
					MovingTarget->SetActorLocation(MovingTarget->GetActorLocation()+FVector(0,QueuedGoalOffsetY,0));
					RefreshedGoal=MovingTarget->GetActorLocation();
					Actions->UpdateAction(); Actions->UpdateAction();
					Test->TestTrue(TEXT("Real follow replacement has a live queued token"),PendingToken!=0);
					Test->TestEqual(TEXT("Repeated latest-goal updates preserve the actual token"),Actions->GetPendingPathTokenForTest(),PendingToken);
					Test->TestEqual(TEXT("Repeated updates never re-admit the queued path"),Service->GetStats().Accepted,AcceptedNow);
					Test->TestEqual(TEXT("Queued latest goal preserves the current move"),AI->GetPathFollowingComponent()->GetCurrentRequestId(),PreviousMove);
					bQueuedGoalRefreshed=true;
				}
				if (bWasFollowing && !bRetiredThisFrame && !(bRebuild && bRebuilt && Service->GetStats().Dispatched == DispatchBeforeRebuild) && Service->GetStats().Accepted > AcceptedBefore)
				{
					Test->TestEqual(TEXT("Repath admission preserves current movement ID"), AI->GetPathFollowingComponent()->GetCurrentRequestId(), PreviousMove);
					Test->TestEqual(TEXT("Repath admission preserves active path following"), AI->GetPathFollowingComponent()->GetStatus(), EPathFollowingStatus::Moving);
					++ContinuousRepaths;
				}
				World->Tick(LEVELTICK_All, 0.05f);
				if (bQueuedGoalRefreshed && !bRefreshedGoalConsumed)
				{
					const auto LivePath=AI->GetPathFollowingComponent()->GetPath();
					bRefreshedGoalConsumed=Service->GetStats().Accepted==QueuedRefreshAccepted &&
						Actions->GetPendingPathTokenForTest()==0 && AI->GetPathFollowingComponent()->GetCurrentRequestId()!=MoveBeforeQueuedRefresh &&
						LivePath.IsValid() && Service->IsPathUsable(LivePath) &&
						FVector::DistSquaredXY(LivePath->GetEndLocation(),RefreshedGoal)<FMath::Square(5.0);
				}
				if (SimulatedSeconds >= 8.0 && Actions->GetActionState() == EProjectJNPCActionState::InRange)
				{
					if(bOwnerFollow && !bNavDataRetirement)
					{
						Test->TestTrue(TEXT("Real queued follow goal was changed before engine dispatch"),bQueuedGoalRefreshed);
						Test->TestTrue(TEXT("Real Recast result and RequestMove consumed the latest queued goal"),bRefreshedGoalConsumed);
					}
					Test->TestTrue(TEXT("Moving target causes multiple asynchronous paths"), Service->GetStats().Dispatched > 1);
					Test->TestTrue(TEXT("Observed replacement requests while continuing a valid old path"), ContinuousRepaths > 0);
					const double ExpectedRange = bOwnerFollow ? OwnerFollowDistance : Actions->AttackRange;
					Test->TestTrue(TEXT("NPC reaches its configured distance from the moved target"), FVector::Dist(NPC->GetActorLocation(), MovingTarget->GetActorLocation()) <= ExpectedRange);
					if (!bRebuild && !bNavDataRetirement)
					{
						Test->TestTrue(TEXT("Real RequestMove replaces an owned moving path"), Actions->GetStats().ReplacedMoves > 0);
						Test->TestEqual(TEXT("Successful moving-target replacements never schedule failure backoff"), Actions->GetStats().RetryScheduled, uint64(0));
					}
					if (bNavDataRetirement)
					{
						Test->TestTrue(TEXT("Actual NavData retirement was exercised"), bRetiredNavData);
						Test->TestTrue(TEXT("NavData returned through normal deferred registration"), RetiredNavData.IsValid() && RetiredNavData->IsRegistered());
						Test->TestTrue(TEXT("Follow recovered through another real async path"), Service->GetStats().Dispatched > DispatchBeforeRetirement);
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

					FAIMoveRequest Move; Move.SetGoalLocation(Goal); Move.SetAcceptanceRadius(60);
					MoveId = AI->RequestMove(Move, Result.Path);
					Test->TestTrue(TEXT("Real path-following accepts completed path"), MoveId.IsValid());
				});
				Test->TestTrue(TEXT("Async service accepts native world request"), Token != 0);
			}
			if (bCompleted && FVector::DistSquared2D(NPC->GetActorLocation(), Goal) < FMath::Square(120.0))
			{

				Test->AddInfo(FString::Printf(TEXT("Real Recast + Mover: points=%d final=%s elapsed=%.3fs dispatches=%llu"),
					PathPoints, *NPC->GetActorLocation().ToString(), FPlatformTime::Seconds() - Started, Service->GetStats().Dispatched));
				return true;
			}
			return false;
		}
	private:
		// One selected authored NPC; all animation/simulation runs through normal WorldTick.
		bool PrepareNPCExecution()
		{
			ExecutionMesh.Reset(Cast<UProject_JBudgetedSkeletalMeshComponent>(NPC->GetMesh()));
			ExecutionAnim.Reset(ExecutionMesh ? ExecutionMesh->GetAnimInstance() : nullptr);
			ExecutionClass.Reset(LoadClass<UAnimInstance>(nullptr, TEXT("/Game/NPCs/Animation/ABP_NPC_Mover_Unarmed.ABP_NPC_Mover_Unarmed_C")));
			ExecutionBlend.Reset(LoadObject<UBlendSpace>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/BS_Idle_Walk_Run.BS_Idle_Walk_Run")));
			ExecutionDefinition.Reset(LoadObject<UProject_JNPCActivationDefinition>(nullptr, TEXT("/Game/DataAssetSets/NPC/DA_NPC_PrototypeCleaveActivation.DA_NPC_PrototypeCleaveActivation")));
			ExecutionAttack.Reset(LoadObject<UProject_JAttackDefinition>(nullptr, TEXT("/Game/DataAssetSets/AttackDefinition/NPC/DA_Attack_NPC_PrototypeCleave.DA_Attack_NPC_PrototypeCleave")));
			if (!Test->TestTrue(TEXT("Actual begun-play BP retains the authored non-MM candidate without an AnimClass setter"),
				NPC->HasActorBegunPlay() && ExecutionMesh && ExecutionAnim && ExecutionClass && ExecutionBlend && ExecutionDefinition && ExecutionAttack &&
				ExecutionMesh->GetAnimClass() == ExecutionClass.Get() && ExecutionAnim->GetClass() == ExecutionClass.Get() && ExecutionAttack->Montage && ExecutionAttack->DamageEffect)) return false;
			ExecutionAsset = ExecutionMesh->GetSkeletalMeshAsset(); ExecutionPost = ExecutionMesh->GetPostProcessInstance();
			ExecutionASC.Reset(Cast<UProject_JAbilitySystemComponent>(NPC->GetAbilitySystemComponent()));
			if (!Test->TestNotNull(TEXT("Actual NPC owns its native GAS recipient"), ExecutionASC.Get())) return false;
			ExecutionEpoch = ExecutionASC->GetActorInfoRevision();
			auto* Ability = ExecutionDefinition->AttackAbility ? ExecutionDefinition->AttackAbility->GetDefaultObject<UGameplayAbility>() : nullptr;
			const auto* AttackField = Ability ? FindFProperty<FObjectPropertyBase>(Ability->GetClass(), TEXT("AttackDefinition")) : nullptr;
			const auto* TagField = Ability ? FindFProperty<FStructProperty>(Ability->GetClass(), TEXT("HitEventTag")) : nullptr;
			if (!Test->TestTrue(TEXT("Existing authored GAS ability consumes the actual Cleave definition"), AttackField && TagField && AttackField->GetObjectPropertyValue_InContainer(Ability) == ExecutionAttack.Get())) return false;
			ExecutionTag = *TagField->ContainerPtrToValuePtr<FGameplayTag>(Ability);
			for (const auto& Notify : ExecutionAttack->Montage->Notifies)
				if (Notify.NotifyStateClass && Notify.NotifyStateClass->IsA<UProject_JAnimNotifyState_MeleeHit>())
				{ ++ExecutionMeleeNotifies; ExecutionFirstImpact = Notify.GetTriggerTime(); }
			if (!Test->TestTrue(TEXT("Actual montage has one finite natural melee notify boundary"), ExecutionTag.IsValid() && ExecutionMeleeNotifies == 1 && FMath::IsFinite(ExecutionFirstImpact) && ExecutionFirstImpact > .05f)) return false;
			if (bMeleeWindowHitch)
			{
				for (const auto& Notify : ExecutionAttack->Montage->Notifies)
					if (const auto* Melee = Cast<UProject_JAnimNotifyState_MeleeHit>(Notify.NotifyStateClass.Get()))
					{
						MeleeWindowEnd = Notify.GetEndTriggerTime();
						const auto* Socket = FindFProperty<FNameProperty>(Melee->GetClass(), TEXT("SocketName"));
						if (Socket) MeleeSocket = Socket->GetPropertyValue_InContainer(Melee);
					}
				if (!Test->TestTrue(TEXT("Hitch case uses the actual authored finite window/socket before montage completion"),
					FMath::IsFinite(MeleeWindowEnd) && MeleeWindowEnd > ExecutionFirstImpact && MeleeWindowEnd + .02f < ExecutionAttack->Montage->GetPlayLength() &&
					!MeleeSocket.IsNone() && ExecutionMesh->DoesSocketExist(MeleeSocket))) return false;
			}
			const auto* Interface = IAnimClassInterface::GetFromClass(ExecutionClass.Get());
			if (!Test->TestTrue(TEXT("Selected runtime class exposes a bounded BlendSpace player observation"), Interface && Interface->GetAnimNodeProperties().Num() <= 4096)) return false;
			for (auto* Property : Interface->GetAnimNodeProperties())
				if (Property && Property->Struct && Property->Struct->IsChildOf(FAnimNode_BlendSpacePlayerBase::StaticStruct()))
					if (Property->ContainerPtrToValuePtr<FAnimNode_BlendSpacePlayerBase>(ExecutionAnim.Get())->GetBlendSpace() == ExecutionBlend.Get()) { ExecutionBlendProperty = Property; break; }
			if (!Test->TestNotNull(TEXT("Authored runtime instance actually contains the expected BlendSpace player"), ExecutionBlendProperty)) return false;
			// Observe existing authored settings only. Production StartCombatActions owns missing-component assembly.
			ExecutionHit.Reset(NPC->FindComponentByClass<UProject_JCombatHitValidationComponent>());
			ExecutionPresentation.Reset(NPC->FindComponentByClass<UProject_JCombatPresentationComponent>());
			if (ExecutionHit) { ExecutionHitTags = ExecutionHit->ComponentTags; bExecutionHitCanTick = ExecutionHit->PrimaryComponentTick.bCanEverTick; }
			if (ExecutionPresentation)
			{
				ExecutionPresentationTags = ExecutionPresentation->ComponentTags;
				bExecutionPresentationCanTick = ExecutionPresentation->PrimaryComponentTick.bCanEverTick;
				ExecutionBaseSet.Reset(ExecutionPresentation->BasePresentationSet.Get());
			}
			PoseRequester.Reset(NewObject<USceneComponent>(NPC));
			SavedVisibility = ExecutionMesh->VisibilityBasedAnimTickOption; bSavedURO = ExecutionMesh->bEnableUpdateRateOptimizations; bSavedPoseDemand = ExecutionMesh->HasAnimationUpdateRequirement();
			ExecutionMesh->RequestAnimationUpdate(PoseRequester.Get(), EProject_JAnimationUpdateRequirement::GameplayPose);
			BoneHandle = ExecutionMesh->RegisterOnBoneTransformsFinalizedDelegate(FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateLambda([this] { ++ExecutionFinalizations; }));
			EventHandle = ExecutionASC->AddGameplayEventTagContainerDelegate(FGameplayTagContainer(ExecutionTag), FGameplayEventTagMulticastDelegate::FDelegate::CreateLambda(
				[this](FGameplayTag Tag, const FGameplayEventData* Event)
				{
					if (!Event || Tag != ExecutionTag || Event->Instigator.Get() != NPC || Event->Target.Get() != ExecutionVictim.Get()) return;
					// NotifyTick can report repeated contacts; authority deduplicates effects.
					++ExecutionContacts; if (ExecutionStage != 2) bExecutionViolation = true;
				}));
			EndHandle = ExecutionASC->OnAbilityEnded.AddLambda([this](const FAbilityEndedData& End)
			{
				if (!End.AbilityThatEnded || End.AbilityThatEnded->GetClass() != ExecutionDefinition->AttackAbility) return;
				Test->AddInfo(FString::Printf(TEXT("AuthoredNPC ability end stage=%d cancelled=%d contacts=%d effects=%d air=%d falling=%d position=%s target=%s"), ExecutionStage, End.bWasCancelled, ExecutionContacts, ExecutionEffects, NPC->GetMover()->IsAirborne(), NPC->GetMover()->IsFalling(), *NPC->GetActorLocation().ToString(), *GetNameSafe(ExecutionVictim.Get())));
				if (ExecutionStage == 2 && !End.bWasCancelled && !bExecutionNaturalEnd)
				{ bExecutionNaturalEnd = true; NPC->StopCombatActions(); }
				else if (ExecutionStage == 3 && bExecutionCancelling && End.bWasCancelled) bExecutionCancelled = true;
				else bExecutionViolation = true;
			});
			for (UObject* Object : TArray<UObject*>{NPC->GetClass(), ExecutionAsset.Get(), ExecutionClass.Get(), ExecutionBlend.Get(), ExecutionDefinition.Get(), Ability ? Ability->GetClass() : nullptr, ExecutionAttack.Get(), ExecutionAttack->Montage.Get()})
				if (Object) { ExecutionPins.Emplace(Object); ExecutionDirty.FindOrAdd(Object->GetOutermost(), Object->GetOutermost()->IsDirty()); }
			return true;
		}
		bool CheckCombatAssembly()
		{
			auto* Hit = NPC->FindComponentByClass<UProject_JCombatHitValidationComponent>();
			auto* Presentation = NPC->FindComponentByClass<UProject_JCombatPresentationComponent>();
			TInlineComponentArray<UProject_JCombatHitValidationComponent*> Hits(NPC);
			TInlineComponentArray<UProject_JCombatPresentationComponent*> Presentations(NPC);
			int32 HitCount = 0, PresentationCount = 0;
			for (auto* Entry : Hits) if (IsValid(Entry) && !Entry->IsBeingDestroyed()) ++HitCount;
			for (auto* Entry : Presentations) if (IsValid(Entry) && !Entry->IsBeingDestroyed()) ++PresentationCount;
			if (!Test->TestTrue(TEXT("Production helper owns one live begun replicated hit/presentation component without test backfill"),
				NPCExecutionCurrent() && HitCount == 1 && PresentationCount == 1 && IsValid(Hit) && Hit->IsRegistered() &&
				!Hit->IsBeingDestroyed() && Hit->HasBegunPlay() && Hit->GetOwner() == NPC && Hit->GetWorld() == World && Hit->GetIsReplicated() &&
				IsValid(Presentation) && Presentation->IsRegistered() && !Presentation->IsBeingDestroyed() && Presentation->HasBegunPlay() &&
				Presentation->GetOwner() == NPC && Presentation->GetWorld() == World && Presentation->GetIsReplicated())) return false;
			if (!Test->TestTrue(TEXT("Re-admission preserves exact components, authored tags/profile and tick capability"),
				(!ExecutionHit || ExecutionHit.Get() == Hit) && (!ExecutionPresentation || ExecutionPresentation.Get() == Presentation) &&
				Hit->ComponentTags == ExecutionHitTags && Presentation->ComponentTags == ExecutionPresentationTags &&
				Hit->PrimaryComponentTick.bCanEverTick == bExecutionHitCanTick &&
				Presentation->PrimaryComponentTick.bCanEverTick == bExecutionPresentationCanTick &&
				Presentation->BasePresentationSet.Get() == ExecutionBaseSet.Get())) return false;
			ExecutionHit.Reset(Hit); ExecutionPresentation.Reset(Presentation);
			return true;
		}
		bool NPCExecutionCurrent() const
		{
			return IsValid(NPC) && !NPC->IsActorBeingDestroyed() && NPC->HasAuthority() && NPC->GetWorld() == World &&
				ExecutionMesh && ExecutionMesh->IsRegistered() && !ExecutionMesh->IsBeingDestroyed() && ExecutionMesh->GetOwner() == NPC && ExecutionMesh->GetWorld() == World && NPC->GetMesh() == ExecutionMesh.Get() &&
				ExecutionAsset.IsValid() && ExecutionMesh->GetSkeletalMeshAsset() == ExecutionAsset.Get() && ExecutionMesh->GetAnimInstance() == ExecutionAnim.Get() &&
				ExecutionAnim && !ExecutionAnim->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) && ExecutionAnim->GetClass() == ExecutionClass.Get() && ExecutionMesh->GetAnimClass() == ExecutionClass.Get() &&
				(ExecutionPost.IsExplicitlyNull() ? ExecutionMesh->GetPostProcessInstance() == nullptr : ExecutionPost.IsValid() && ExecutionMesh->GetPostProcessInstance() == ExecutionPost.Get()) &&
				ExecutionASC && ExecutionASC->IsRegistered() && !ExecutionASC->IsBeingDestroyed() && ExecutionASC->GetOwner() == NPC && ExecutionASC->GetWorld() == World &&
				NPC->GetAbilitySystemComponent() == ExecutionASC.Get() && ExecutionASC->GetAvatarActor() == NPC && ExecutionASC->GetOwnerActor() == NPC && ExecutionASC->GetActorInfoRevision() == ExecutionEpoch &&
				(!AI || (NPC->GetController() == AI && AI->GetPawn() == NPC && !AI->IsActorBeingDestroyed()));
		}
		void ObserveNPCAnimation()
		{
			if (ExecutionMesh->IsRunningParallelEvaluation() || ExecutionFinalizations < 2) return; // Observe published state; never wait.
			const auto& Pose = ExecutionMesh->GetComponentSpaceTransforms();
			if (Pose.IsEmpty() || Pose.Num() > 512) return;
			const auto* Node = ExecutionBlendProperty->ContainerPtrToValuePtr<FAnimNode_BlendSpacePlayerBase>(ExecutionAnim.Get());
			if (Node->GetBlendSpace() != ExecutionBlend.Get()) { bExecutionViolation = true; return; }
			const FVector Position = Node->GetPosition();
			if (Position.ContainsNaN()) { bExecutionViolation = true; return; }
			ExecutionLastBlendPosition = Position;
			if (ExecutionStage == 0) { IdlePose = Pose; IdleBlendPosition = Position; bExecutionIdleObserved = true; }
			if (ExecutionStage == 1 && NPC->GetVelocity().Size2D() > 10)
			{
				bExecutionBlendChanged |= !Position.Equals(IdleBlendPosition, .01);
				if (Pose.Num() == IdlePose.Num()) for (int32 Index = 0; Index < Pose.Num(); ++Index)
					if (!Pose[Index].Equals(IdlePose[Index], 1.e-4)) { bExecutionMovingPose = true; break; }
			}
		}
		FVector ObserveMeleeWorldPoint() const
		{
			const FVector Tip = ExecutionAttack->HitSpec.bUseCanonicalBladeTipOffset ? ExecutionAttack->HitSpec.CanonicalBladeTipOffset : FVector::ZeroVector;
			return ExecutionMesh->GetSocketTransform(MeleeSocket, RTS_World).TransformPosition(Tip);
		}
		bool PrepareMeleeWindowHitch(float& TickDelta, bool& ThisTick)
		{
			ThisTick = false;
			if (ExecutionStage != 2 || bMeleeHitchIssued || ExecutionHit->GetActiveAttackDefinition() != ExecutionAttack.Get()) return true;
			const auto* Instance = ExecutionAnim->GetActiveInstanceForMontage(ExecutionAttack->Montage);
			if (!Instance) return true;
			const float Position = Instance->GetPosition();
			if (!Test->TestTrue(TEXT("Natural updates expose the first actual montage before its melee window"), FMath::IsFinite(Position) && Position < ExecutionFirstImpact)) return false;
			if (Position < ExecutionFirstImpact - .075f) return true;
			if (!Test->TestTrue(TEXT("Hitch starts from a completed live actual pose without an evaluation wait"), NPCExecutionCurrent() &&
				!ExecutionMesh->IsRunningParallelEvaluation() && Actions->GetIntentTarget() == ExecutionVictim.Get())) return false;
			const float Rate = Instance->GetPlayRate() * ExecutionAttack->Montage->RateScale;
			const float DesiredPosition = MeleeWindowEnd + .01f;
			if (!Test->TestTrue(TEXT("One finite positive real world tick can cross this window without ending the montage"),
				FMath::IsFinite(Rate) && Rate > 0 && DesiredPosition < ExecutionAttack->Montage->GetPlayLength())) return false;
			TickDelta = (DesiredPosition - Position) / Rate;
			if (!Test->TestTrue(TEXT("Crossing tick is finite and positive"), FMath::IsFinite(TickDelta) && TickDelta > 0)) return false;
			auto* Capsule = ExecutionVictim.IsValid() ? ExecutionVictim->FindComponentByClass<UCapsuleComponent>() : nullptr;
			if (!Test->TestTrue(TEXT("Crossing observes the actual registered same-world victim capsule, without changing it"),
				IsValid(Capsule) && Capsule->IsRegistered() && !Capsule->IsBeingDestroyed() && Capsule->GetOwner() == ExecutionVictim.Get() && Capsule->GetWorld() == World)) return false;
			MeleeVictimCapsule = Capsule; MeleeVictimBeforeTransform = Capsule->GetComponentTransform();
			MeleeVictimBeforeRadius = Capsule->GetScaledCapsuleRadius(); MeleeVictimBeforeHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
			MeleeVictimHealthBefore = ExecutionVictim->GetAttributeSet()->GetHealth();
			if (!Test->TestTrue(TEXT("The first hitch starts before any victim contact or damage"),
				ExecutionContacts == 0 && ExecutionEffects == 0 && FMath::IsFinite(MeleeVictimHealthBefore) && MeleeVictimHealthBefore > 0)) return false;
			MeleeMontageInstanceID = Instance->GetInstanceID();
			MeleeBeforePosition = Position; MeleeBeforeWorldTime = World->GetTimeSeconds(); MeleeBeforePoint = ObserveMeleeWorldPoint();
			if (!Test->TestFalse(TEXT("Independent pre-tick socket sample is finite"), MeleeBeforePoint.ContainsNaN())) return false;
			ThisTick = bMeleeHitchIssued = true;
			return true;
		}
		bool VerifyMeleeWindowHitch(float TickDelta)
		{
			const auto* Instance = ExecutionAnim->GetMontageInstanceForID(MeleeMontageInstanceID);
			const float Position = Instance ? Instance->GetPosition() : -1.f;
			const double WorldAdvance = World->GetTimeSeconds() - MeleeBeforeWorldTime;
			if (!Test->TestTrue(TEXT("One normal world tick crosses the exact authored melee window, not the whole montage"),
				Instance && Instance->Montage == ExecutionAttack->Montage && !bExecutionNaturalEnd &&
				ExecutionHit->GetActiveAttackDefinition() == ExecutionAttack.Get() && Actions->GetAttackAbilityHandle() == ExecutionGrant && ExecutionASC->FindAbilitySpecFromHandle(ExecutionGrant) &&
				MeleeBeforePosition < ExecutionFirstImpact && FMath::IsFinite(Position) && Position > MeleeWindowEnd && Position < ExecutionAttack->Montage->GetPlayLength() &&
				FMath::Abs(WorldAdvance - double(TickDelta)) < 1.e-5 && !ExecutionMesh->IsRunningParallelEvaluation())) return false;
			const FVector AfterPoint = ObserveMeleeWorldPoint();
			const double Span = double(Position) - MeleeBeforePosition;
			const FVector ExpectedStart = FMath::Lerp(MeleeBeforePoint, AfterPoint, (ExecutionFirstImpact - MeleeBeforePosition) / Span);
			const FVector ExpectedEnd = FMath::Lerp(MeleeBeforePoint, AfterPoint, (MeleeWindowEnd - MeleeBeforePosition) / Span);
			FVector TraceStart, TraceEnd; bool WindowOpen = false;
			const auto* GameState = World->GetGameState();
			const double QueryTime = GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
			const bool Found = ExecutionHit->FindAuthoritativeTraceAtTime(QueryTime, 0, ExecutionAttack->AttackTag, TraceStart, TraceEnd, WindowOpen);
			Test->AddInfo(FString::Printf(TEXT("AuthoredMelee singleWorldTick instance=%d delta=%.9g world_advance=%.9g montage_before=%.9g after=%.9g window=[%.9g,%.9g] history=%d open=%d actual_start=%s actual_end=%s expected_start=%s expected_end=%s"),
				MeleeMontageInstanceID, TickDelta, WorldAdvance, MeleeBeforePosition, Position, ExecutionFirstImpact, MeleeWindowEnd, Found, WindowOpen,
				*TraceStart.ToString(), *TraceEnd.ToString(), *ExpectedStart.ToString(), *ExpectedEnd.ToString()));
			if (!Test->TestTrue(TEXT("Public authoritative history contains a nonzero sweep clipped to the authored subinterval of the two actual world poses"),
				Found && !AfterPoint.ContainsNaN() && !TraceStart.ContainsNaN() && !TraceEnd.ContainsNaN() &&
				FVector::DistSquared(ExpectedStart, ExpectedEnd) > 1.e-6 && FVector::DistSquared(TraceStart, TraceEnd) > 1.e-6 &&
				TraceStart.Equals(ExpectedStart, 1.e-3) && TraceEnd.Equals(ExpectedEnd, 1.e-3))) return false;
			const auto* Capsule = MeleeVictimCapsule.Get();
			if (!Test->TestTrue(TEXT("The same passive victim capsule stayed stationary across the real crossing tick"),
				IsValid(Capsule) && Capsule->IsRegistered() && !Capsule->IsBeingDestroyed() && Capsule->GetWorld() == World && Capsule->GetOwner() == ExecutionVictim.Get() &&
				ExecutionVictim.IsValid() && ExecutionVictim->FindComponentByClass<UCapsuleComponent>() == Capsule &&
				Capsule->GetComponentTransform().Equals(MeleeVictimBeforeTransform, 1.e-3) && Capsule->GetScaledCapsuleRadius() == MeleeVictimBeforeRadius && Capsule->GetScaledCapsuleHalfHeight() == MeleeVictimBeforeHalfHeight)) return false;
			const double Radius = Capsule->GetScaledCapsuleRadius(), HalfHeight = Capsule->GetScaledCapsuleHalfHeight(), TraceRadius = ExecutionAttack->HitSpec.TraceRadius;
			const FVector Center = Capsule->GetComponentLocation(), Up = Capsule->GetUpVector();
			if (!Test->TestTrue(TEXT("Actual capsule and authored trace dimensions are finite"), !Center.ContainsNaN() && !Up.ContainsNaN() &&
				FMath::IsFinite(Radius) && Radius > 0 && FMath::IsFinite(HalfHeight) && HalfHeight >= Radius && FMath::IsFinite(TraceRadius) && TraceRadius > 0)) return false;
			const FVector Axis = Up * (HalfHeight - Radius);
			FVector SweepPoint, CapsulePoint;
			FMath::SegmentDistToSegmentSafe(TraceStart, TraceEnd, Center - Axis, Center + Axis, SweepPoint, CapsulePoint);
			const double Clearance = FVector::Distance(SweepPoint, CapsulePoint) - Radius - TraceRadius;
			Test->AddInfo(FString::Printf(TEXT("AuthoredMelee hitch capsule=%s center=%s halfheight=%.9g radius=%.9g trace_radius=%.9g clearance=%.9g contacts=%d effects=%d health_before=%.9g health_after=%.9g"),
				*Capsule->GetPathName(), *Center.ToString(), HalfHeight, Radius, TraceRadius, Clearance, ExecutionContacts, ExecutionEffects, MeleeVictimHealthBefore, ExecutionVictim->GetAttributeSet()->GetHealth()));
			if (!Test->TestTrue(TEXT("The independently clipped straight sweep does not overlap the real capsule and fabricates no contact or damage"),
				FMath::IsFinite(Clearance) && Clearance > 1.e-3 && ExecutionContacts == 0 && ExecutionEffects == 0 && ExecutionVictim->GetAttributeSet()->GetHealth() == MeleeVictimHealthBefore)) return false;
			bMeleeHitchVerified = true;
			return true;
		}

		bool PrepareCanonicalExecution()
		{
			FString Failure;
			if (!Test->TestTrue(TEXT("Actual RAW authoring producer builds a selected transient gameplay path"),
				Project_J::CanonicalMeleeAuthoring::BuildCandidate(*ExecutionAttack, *ExecutionMesh->GetSkeletalMeshAsset(), CanonicalPath, Failure)))
			{ Test->AddInfo(Failure); return false; }
			ExecutionAttack.Reset(DuplicateObject<UProject_JAttackDefinition>(ExecutionAttack.Get(), GetTransientPackage()));
			if (!ExecutionAttack) return false;
			ExecutionAttack->MeleeTrajectory = CanonicalPath;
			const FProject_JMeleeTrajectory OriginalPath = CanonicalPath;
			FProject_JMeleeTrajectory CorruptedPath = CanonicalPath;
			if (!Test->TestTrue(TEXT("The actual baked path contains a bounded nonempty sampling grid"), CorruptedPath.Keys.Num()>=2)) return false;
			CorruptedPath.Keys[1].MeshPosition.X += 1.;
			if (!Test->TestTrue(TEXT("A shape-valid altered baked key is rejected as a stale source/key stamp"),
				CorruptedPath.GetValidationFailure().IsEmpty() && !Project_J::CanonicalMeleeAuthoring::ValidateCurrentSource(*ExecutionAttack,*ExecutionMesh->GetSkeletalMeshAsset(),CorruptedPath,Failure) &&
				FProject_JMeleeTrajectory::StaticStruct()->CompareScriptStruct(&CanonicalPath,&OriginalPath,PPF_None))) return false;
			const TStrongObjectPtr<UProject_JAttackDefinition> Unsupported(DuplicateObject<UProject_JAttackDefinition>(ExecutionAttack.Get(),GetTransientPackage()));
			if (!Unsupported) return false;
			Unsupported->MovementPolicy=EProject_JAttackMovementPolicy::RootMotionWarped;
			FProject_JMeleeTrajectory Sentinel=CanonicalPath;
			if (!Test->TestTrue(TEXT("Unsupported actual authoring input rejects without replacing any output path field"),
				!Project_J::CanonicalMeleeAuthoring::BuildCandidate(*Unsupported,*ExecutionMesh->GetSkeletalMeshAsset(),Sentinel,Failure) &&
				FProject_JMeleeTrajectory::StaticStruct()->CompareScriptStruct(&Sentinel,&OriginalPath,PPF_None) &&
				FProject_JMeleeTrajectory::StaticStruct()->CompareScriptStruct(&ExecutionAttack->MeleeTrajectory,&OriginalPath,PPF_None))) return false;
			// The real direct GAS SourceObject contract is used; no helper-owned grant or ability CDO is rewritten.
			ExecutionASC->OnAbilityEnded.Remove(EndHandle);
			EndHandle = ExecutionASC->OnAbilityEnded.AddLambda([this](const FAbilityEndedData& End)
			{
				if (!End.AbilityThatEnded || End.AbilitySpecHandle != ExecutionGrant || End.AbilityThatEnded->GetClass() != ExecutionDefinition->AttackAbility) return;
				if (ExecutionStage == 2 && !End.bWasCancelled && !bExecutionNaturalEnd)
				{ bExecutionNaturalEnd = true; StopCanonicalExecution(); }
				else if (ExecutionStage == 3 && bExecutionCancelling && End.bWasCancelled) bExecutionCancelled = true;
				else bExecutionViolation = true;
			});
			return true;
		}
		bool StartCanonicalExecution()
		{
			CanonicalScoring.Reset(NPC->FindComponentByClass<UProject_JTargetScoringComponent>());
			if (!CanonicalScoring)
			{
				CanonicalScoring.Reset(NewObject<UProject_JTargetScoringComponent>(NPC));
				NPC->AddInstanceComponent(CanonicalScoring.Get()); CanonicalScoring->RegisterComponent();
			}
			if (!Test->TestTrue(TEXT("Same actual neutral actor/action admits normal shared scoring"), NPCExecutionCurrent() &&
				!NPC->HasCombatActions() && !CanonicalScoring->IsBatchedNPCDecisionRegistered() && CanonicalScoring->StartBatchedNPCDecisions(1))) return false;
			CanonicalScoringRevision = CanonicalScoring->GetBatchRegistrationRevision();
			ExecutionGrant = ExecutionASC->GiveAbility(FGameplayAbilitySpec(ExecutionDefinition->AttackAbility, ExecutionDefinition->AbilityLevel, INDEX_NONE, ExecutionAttack.Get()));
			CanonicalOwnedGrant = ExecutionGrant;
			if (!Test->TestTrue(TEXT("Public GAS grant uses the transient selected path from its original SourceObject"),
				ExecutionGrant.IsValid() && ExecutionASC->FindAbilitySpecFromHandle(ExecutionGrant) &&
				ExecutionASC->FindAbilitySpecFromHandle(ExecutionGrant)->SourceObject.Get() == ExecutionAttack.Get() &&
				Actions->StartActions(CanonicalScoring.Get(), ExecutionGrant))) { StopCanonicalExecution(); return false; }
			CanonicalActionToken = Actions->GetOwnershipToken();
			return Test->TestTrue(TEXT("The real action consumer owns the exact direct grant"), CanonicalActionToken != 0 && Actions->GetAttackAbilityHandle() == ExecutionGrant);
		}
		void StopCanonicalExecution()
		{
			const uint64 Token = CanonicalActionToken, Revision = CanonicalScoringRevision;
			const FGameplayAbilitySpecHandle Grant = CanonicalOwnedGrant;
			CanonicalActionToken = CanonicalScoringRevision = 0; CanonicalOwnedGrant = {};
			if (IsValid(Actions) && !Actions->IsBeingDestroyed()) Actions->StopActionsIfOwned(Token);
			if (ExecutionASC && !ExecutionASC->IsBeingDestroyed() && Grant.IsValid())
			{
				const auto* Spec = ExecutionASC->FindAbilitySpecFromHandle(Grant);
				if (Spec && Spec->SourceObject.Get() == ExecutionAttack.Get())
				{ ExecutionASC->CancelAbilityHandle(Grant); if (!ExecutionASC->IsBeingDestroyed()) ExecutionASC->ClearAbility(Grant); }
			}
			if (CanonicalScoring && !CanonicalScoring->IsBeingDestroyed() && Revision && CanonicalScoring->GetBatchRegistrationRevision() == Revision)
				CanonicalScoring->StopBatchedNPCDecisions();
		}
		bool ObserveCanonicalRoot(FTransform& Root, FTransform& MeshToRoot)
		{
			const auto* Updated = NPC->GetMover()->GetUpdatedComponent();
			const auto* Physical = NPC->GetMover()->GetSyncState().SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
			if (!Test->TestTrue(TEXT("Independent root observation is the actual committed native root"), Updated && Physical && (bCanonicalStableBase || !Physical->GetMovementBase()) &&
				Updated == NPC->GetRootComponent() && Updated->GetComponentLocation().Equals(Physical->GetLocation_WorldSpace(), UE_KINDA_SMALL_NUMBER) &&
				Updated->GetComponentQuat().Equals(Physical->GetOrientation_WorldSpace().Quaternion(), UE_KINDA_SMALL_NUMBER))) return false;
			if (bCanonicalStableBase)
			{
				if (!Test->TestTrue(TEXT("Natural native walking publishes the real unchanged movable floor as this NPC's base"), CanonicalFloor.IsValid() &&
					CanonicalFloor->IsRegistered() && !CanonicalFloor->IsBeingDestroyed() && CanonicalFloor->GetWorld()==World &&
					Physical->GetMovementBase()==CanonicalFloor.Get() && CanonicalFloor->Mobility==EComponentMobility::Movable &&
					CanonicalFloor->GetComponentTransform().Equals(CanonicalFloorTransform,0.))) return false;
				const FTransform PublishedBase(Physical->GetCapturedMovementBaseQuat(),Physical->GetCapturedMovementBasePos());
				if (!Test->TestTrue(TEXT("Observed published movement-base frame is finite and normalized"),
					!PublishedBase.ContainsNaN() && PublishedBase.GetRotation().IsNormalized())) return false;
				if (bCanonicalBaseObserved)
				{
					if (!Test->TestTrue(TEXT("Same actual floor, bone and exact published base frame survive the canonical hitch"),
						CanonicalBaseBone==Physical->GetMovementBaseBoneName() && CanonicalPublishedBase.Equals(PublishedBase,0.))) return false;
				}
				else
				{
					CanonicalPublishedBase=PublishedBase; CanonicalBaseBone=Physical->GetMovementBaseBoneName(); bCanonicalBaseObserved=true;
					Test->AddInfo(FString::Printf(TEXT("Canonical stable published base=%s bone=%s frame=%s"),*CanonicalFloor->GetPathName(),*CanonicalBaseBone.ToString(),*PublishedBase.ToString()));
				}
			}
			Root = Updated->GetComponentTransform(); MeshToRoot = ExecutionMesh->GetComponentTransform().GetRelativeTransform(Root);
			return Test->TestTrue(TEXT("Observed root and relative mesh mapping are finite normalized native values"),
				!Root.ContainsNaN() && !MeshToRoot.ContainsNaN() && Root.GetRotation().IsNormalized() && MeshToRoot.GetRotation().IsNormalized());
		}
		bool VerifyCanonicalHitch(float TickDelta)
		{
			const auto* Instance = ExecutionAnim->GetMontageInstanceForID(MeleeMontageInstanceID);
			const float After = Instance ? Instance->GetPosition() : -1.f;
			FTransform CurrentRoot, CurrentMeshToRoot;
			if (!Test->TestTrue(TEXT("One real canonical hitch crosses the entire original window, retaining the same actual GAS execution"),
				Instance && Instance->Montage == ExecutionAttack->Montage && !bExecutionNaturalEnd && After > MeleeWindowEnd &&
				After < ExecutionAttack->Montage->GetPlayLength() && MeleeBeforePosition < ExecutionFirstImpact &&
				ExecutionHit->GetActiveAttackDefinition() == ExecutionAttack.Get() && ExecutionASC->FindAbilitySpecFromHandle(ExecutionGrant) &&
				FMath::Abs(World->GetTimeSeconds()-MeleeBeforeWorldTime-double(TickDelta)) < 1.e-5 && !ExecutionMesh->IsRunningParallelEvaluation()) ||
				!ObserveCanonicalRoot(CurrentRoot, CurrentMeshToRoot)) return false;
			if (!Test->TestTrue(TEXT("The observed mesh-to-physical-root mapping is unchanged across the real interval"),
				CurrentMeshToRoot.Equals(CanonicalMeshToRoot, UE_KINDA_SMALL_NUMBER) && CurrentRoot.GetScale3D().Equals(CanonicalPreviousRoot.GetScale3D(), UE_KINDA_SMALL_NUMBER))) return false;
			const auto* GameState = World->GetGameState(); const double Now = GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
			FProject_JAuthoritativeSweepRecord Packet;
			// The first queued crossing delivers Begin/Tick. Observe its actual
			// publication now, then verify End on the normal following dispatch.
			const bool bFound=ExecutionHit->FindAuthoritativeTracePacketAtTime(Now,0,ExecutionAttack->AttackTag,Packet);
			Test->AddInfo(FString::Printf(TEXT("Canonical packet lookup now=%.9f found=%d atomic=%d open=%d stamp=%.9f pieces=%d expected=%d contacts=%d effects=%d"),
				Now,bFound,Packet.bAtomicPath,Packet.bHitWindowOpen,Packet.ServerTimestamp,Packet.Segments.Num(),CanonicalPath.Keys.Num()-1,ExecutionContacts,ExecutionEffects));
			if (!Test->TestTrue(TEXT("Public owning packet preserves every actual queried piece on the native crossing dispatch"),
				bFound && Packet.bAtomicPath && Packet.bHitWindowOpen &&
				FMath::Abs(Packet.ServerTimestamp-Now)<1.e-7 && Packet.Segments.Num() == CanonicalPath.Keys.Num()-1 && Packet.Segments.Num()>1)) return false;
			const auto* Capsule = MeleeVictimCapsule.Get();
			if (!Test->TestTrue(TEXT("Geometry reference is the unchanged real query-enabled victim capsule"), IsValid(Capsule) && Capsule->IsRegistered() &&
				!Capsule->IsBeingDestroyed() && Capsule->GetOwner()==ExecutionVictim.Get() && Capsule->GetWorld()==World && Capsule->IsQueryCollisionEnabled() &&
				Capsule->GetCollisionObjectType()==ECC_Pawn && Capsule->GetComponentTransform().Equals(MeleeVictimBeforeTransform,1.e-3) &&
				Capsule->GetScaledCapsuleRadius()==MeleeVictimBeforeRadius && Capsule->GetScaledCapsuleHalfHeight()==MeleeVictimBeforeHalfHeight)) return false;
			const double Radius = Capsule->GetScaledCapsuleRadius(), HalfHeight = Capsule->GetScaledCapsuleHalfHeight(), TraceRadius = ExecutionAttack->HitSpec.TraceRadius;
			const FVector Center = Capsule->GetComponentLocation(), Axis = Capsule->GetUpVector()*(HalfHeight-Radius);
			if (!Test->TestTrue(TEXT("Real capsule dimensions and selected trace radius are finite positive values"), FMath::IsFinite(Radius) && Radius>0 &&
				FMath::IsFinite(HalfHeight) && HalfHeight>=Radius && FMath::IsFinite(TraceRadius) && TraceRadius>0)) return false;
			TArray<FVector> Expected;
			for (const auto& Key : CanonicalPath.Keys)
			{
				if (!Test->TestTrue(TEXT("Every independently mapped key remains inside the actual consumed interval and authored window"),
					Key.MontageTime>=ExecutionFirstImpact && Key.MontageTime<=MeleeWindowEnd && Key.MontageTime>MeleeBeforePosition && Key.MontageTime<After)) return false;
				const double Alpha = (double(Key.MontageTime)-MeleeBeforePosition)/(double(After)-MeleeBeforePosition);
				const FQuat Rotation = FQuat::Slerp(CanonicalPreviousRoot.GetRotation(),CurrentRoot.GetRotation(),Alpha).GetNormalized();
				const FVector Translation = CanonicalPreviousRoot.GetLocation()*(1.-Alpha)+CurrentRoot.GetLocation()*Alpha;
				const FVector Local = CanonicalMeshToRoot.TransformPosition(Key.MeshPosition)*CanonicalPreviousRoot.GetScale3D();
				Expected.Add(Rotation.RotateVector(Local)+Translation);
			}
			double MinimumClearance = TNumericLimits<double>::Max(); bool bAnyOverlap = false, bClearHit = false, bAmbiguousContact = false;
			for (int32 I=0; I<Packet.Segments.Num(); ++I)
			{
				const auto& Piece = Packet.Segments[I];
				if (!Test->TestTrue(TEXT("Every public native piece equals the independent authored-key/root-endpoint mapping"),
					Piece.TraceStart.Equals(Expected[I],1.e-3) && Piece.TraceEnd.Equals(Expected[I+1],1.e-3))) return false;
				FVector SweepPoint, CapsulePoint;
				FMath::SegmentDistToSegmentSafe(Expected[I],Expected[I+1],Center-Axis,Center+Axis,SweepPoint,CapsulePoint);
				const double Clearance = FVector::Distance(SweepPoint,CapsulePoint)-Radius-TraceRadius;
				if (!FMath::IsFinite(Clearance)) return false;
				MinimumClearance = FMath::Min(MinimumClearance,Clearance);
				if (FMath::Abs(Clearance)<=1.e-3) bAmbiguousContact = true;
				if (Clearance < -1.e-3)
				{
					bAnyOverlap = true;
					FHitResult Block; FCollisionQueryParams Params(SCENE_QUERY_STAT(CanonicalMeleeReferenceLOS),false,NPC);
					const bool Blocked = World->LineTraceSingleByChannel(Block,Expected[I],Center,ECC_Visibility,Params);
					bClearHit |= !Blocked || Block.GetActor()==ExecutionVictim.Get();
				}
			}
			CanonicalExpectedEffects = bClearHit ? 1 : 0;
			if (!Test->TestTrue(TEXT("Reference proves a LOS-eligible contact, a strict complete miss or unambiguous blocked-only overlap"),
				bClearHit || (!bAmbiguousContact && (MinimumClearance>1.e-3 || bAnyOverlap)))) return false;
			Test->AddInfo(FString::Printf(TEXT("CanonicalMelee realTick=%.9g before=%.9g after=%.9g pieces=%d min_capsule_clearance_cm=%.9g expected_effects=%d actual_contacts=%d actual_effects=%d"),TickDelta,MeleeBeforePosition,After,Packet.Segments.Num(),MinimumClearance,CanonicalExpectedEffects,ExecutionContacts,ExecutionEffects));
			if (!Test->TestTrue(TEXT("Actual contact/effect agrees with independently proved geometry without changing placement or radius"),
				ExecutionEffects==CanonicalExpectedEffects && (bClearHit ? ExecutionContacts>0 : ExecutionContacts==0) &&
				(CanonicalExpectedEffects ? ExecutionVictim->GetAttributeSet()->GetHealth()<MeleeVictimHealthBefore :
				ExecutionVictim->GetAttributeSet()->GetHealth()==MeleeVictimHealthBefore))) return false;
			bMeleeHitchVerified = true; return true;
		}
		bool UpdateCanonicalExecution()
		{
			if (ExecutionStage<2) return UpdateNPCExecution(); // Original real Recast/NavMover/pose path.
			float Delta=.05f; bool Crossing=false;
			if (ExecutionStage==2 && !PrepareMeleeWindowHitch(Delta,Crossing)) return true;
			if (Crossing && !ObserveCanonicalRoot(CanonicalPreviousRoot,CanonicalMeshToRoot)) return true;
			World->Tick(LEVELTICK_All,Delta);
			if (!Test->TestTrue(TEXT("Canonical world tick preserves exact authored NPC/GAS/source recipient identity"), NPCExecutionCurrent() && !bExecutionViolation)) return true;
			if (Crossing && !VerifyCanonicalHitch(Delta)) return true;
			if (Crossing) ExecutionContactsAfter=ExecutionContacts;
			if (ExecutionStage==2 && bMeleeHitchVerified)
			{
				if (!Test->TestTrue(TEXT("Consumed path never replays contact or damage while native notify End is pending"),
					ExecutionContacts==ExecutionContactsAfter && ExecutionEffects==CanonicalExpectedEffects)) return true;
				if (!bCanonicalCloseObserved && ExecutionHit->GetActiveAttackDefinition()==ExecutionAttack.Get())
				{
					const auto* GameState=World->GetGameState(); const double Time=GameState?GameState->GetServerWorldTimeSeconds():World->GetTimeSeconds();
					FProject_JAuthoritativeSweepRecord Closed;
					if (ExecutionHit->FindAuthoritativeTracePacketAtTime(Time,0,ExecutionAttack->AttackTag,Closed) && !Closed.bHitWindowOpen)
					{
						if (!Test->TestTrue(TEXT("Normal native End publishes an atomic close with no invented or repeated geometry"),Closed.bAtomicPath && Closed.Segments.IsEmpty())) return true;
						bCanonicalCloseObserved=true;
						Test->AddInfo(FString::Printf(TEXT("Canonical natural notify close observed at world_time=%.9f contacts=%d effects=%d"),Time,ExecutionContacts,ExecutionEffects));
					}
				}
			}
			if (ExecutionStage==2)
			{
				if (!bExecutionNaturalEnd) return false;
				if (!Test->TestTrue(TEXT("Canonical actual montage naturally ends with all first-window pieces verified and its exact owned grant retired"),
					bMeleeHitchIssued && bMeleeHitchVerified && bCanonicalCloseObserved && ExecutionEffects==CanonicalExpectedEffects && !ExecutionASC->FindAbilitySpecFromHandle(ExecutionGrant) &&
					!ExecutionHit->GetActiveAttackDefinition() && !CanonicalActionToken && !CanonicalScoring->IsBatchedNPCDecisionRegistered())) return true;
				ExecutionHealthAfter=ExecutionVictim->GetAttributeSet()->GetHealth(); ExecutionContactsAfter=ExecutionContacts; ExecutionStage=3;
				if (!StartCanonicalExecution()) return true;
				return false;
			}
			if (ExecutionStage==3)
			{
				if (!ExecutionAnim->Montage_IsPlaying(ExecutionAttack->Montage) || ExecutionHit->GetActiveAttackDefinition()!=ExecutionAttack.Get()) return false;
				if (!Test->TestTrue(TEXT("Same actual canonical execution is observed before its next authored window"), ExecutionAnim->Montage_GetPosition(ExecutionAttack->Montage)<ExecutionFirstImpact)) return true;
				const uint64 Spatial=ExecutionHit->GetSpatialRevision(); ExecutionHit->ResetSpatialHistory();
				FProject_JAuthoritativeSweepRecord Retired;
				if (!Test->TestTrue(TEXT("Public spatial reset retires canonical geometry before cancellation without moving anything"), ExecutionHit->GetSpatialRevision()==Spatial+1 &&
					!ExecutionHit->FindAuthoritativeTracePacketAtTime(World->GetTimeSeconds(),0,ExecutionAttack->AttackTag,Retired))) return true;
				bExecutionCancelling=true; StopCanonicalExecution(); ExecutionStage=4; return false;
			}
			if (++CanonicalDrainTicks<2 || ExecutionAnim->IsAnyMontagePlaying()) return false;
			if (!Test->TestTrue(TEXT("Normal cancellation/spatial retirement produces no late canonical contact or GE"), bExecutionCancelled && !CanonicalActionToken &&
				!ExecutionASC->FindAbilitySpecFromHandle(ExecutionGrant) && !ExecutionHit->GetActiveAttackDefinition() && ExecutionContacts==ExecutionContactsAfter &&
				ExecutionEffects==CanonicalExpectedEffects && ExecutionVictim->GetAttributeSet()->GetHealth()==ExecutionHealthAfter)) return true;
			ExecutionMesh->ReleaseAnimationUpdate(PoseRequester.Get());
			if (!Test->TestTrue(TEXT("Canonical fixture releases its real gameplay pose request to the original policy"), ExecutionMesh->VisibilityBasedAnimTickOption==SavedVisibility &&
				bool(ExecutionMesh->bEnableUpdateRateOptimizations)==bSavedURO && ExecutionMesh->HasAnimationUpdateRequirement()==bSavedPoseDemand)) return true;
			if (!Test->TestTrue(TEXT("Same actual actor still admits a final owned neutral command for normal EndPlay"), NPC->StartNavigationTo(PointInitialFeet,60,[this](bool Arrived){++PointEndStops;Test->TestFalse(TEXT("Retirement is not arrival"),Arrived);}))) return true;
			World->EndPlay(EEndPlayReason::Quit);
			Test->TestTrue(TEXT("Canonical fixture retires all actual action/path/grant ownership normally"), PointEndStops==1 && Service->GetRequestCount()==0 &&
				World->GetSubsystem<UProject_JNPCDecisionSubsystem>()->GetAgentCount()+World->GetSubsystem<UProject_JNPCDecisionSubsystem>()->GetActionCount()==0 &&
				(!bExecutionPreparedController || AI->IsActorBeingDestroyed()));
			return true;
		}
		bool UpdateNPCExecution()
		{
			float TickDelta = .05f; bool MeleeCrossingThisTick = false;
			if (bMeleeWindowHitch && !PrepareMeleeWindowHitch(TickDelta, MeleeCrossingThisTick)) return true;
			World->Tick(LEVELTICK_All, TickDelta);
			if (!NPCExecutionCurrent() || bExecutionViolation)
				Test->AddInfo(FString::Printf(TEXT("AuthoredNPC recipient evidence stage=%d current=%d violation=%d contacts=%d effects=%d natural_end=%d cancelled=%d same_mesh=%d same_anim=%d same_asset=%d same_class=%d same_controller=%d asc_epoch=%llu expected_epoch=%llu"), ExecutionStage, NPCExecutionCurrent(), bExecutionViolation, ExecutionContacts, ExecutionEffects, bExecutionNaturalEnd, bExecutionCancelled, NPC->GetMesh()==ExecutionMesh.Get(), ExecutionMesh && ExecutionMesh->GetAnimInstance()==ExecutionAnim.Get(), ExecutionMesh && ExecutionMesh->GetSkeletalMeshAsset()==ExecutionAsset.Get(), ExecutionMesh && ExecutionMesh->GetAnimClass()==ExecutionClass.Get(), NPC->GetController()==AI, ExecutionASC ? ExecutionASC->GetActorInfoRevision():0, ExecutionEpoch));
			if (!Test->TestTrue(TEXT("Same authored animation/mesh/ASC/controller recipient survives natural world updates"), NPCExecutionCurrent() && !bExecutionViolation)) return true;
			if (MeleeCrossingThisTick && !VerifyMeleeWindowHitch(TickDelta)) return true;
			ExecutionMaxGroundSpeed = FMath::Max(ExecutionMaxGroundSpeed, NPC->GetVelocity().Size2D());
			ObserveNPCAnimation();
			if (bExecutionViolation) { Test->AddError(TEXT("Actual animation observation changed its expected BlendSpace or produced an invalid input")); return true; }
			if (ExecutionStage == 0)
			{
				FNavLocation Destination, Start;
				if (!bExecutionIdleObserved || Nav->IsNavigationBuildInProgress() || !Nav->ProjectPointToNavigation(FVector(900,0,0), Destination) || !Nav->ProjectPointToNavigation(NPC->GetNavAgentLocation(), Start)) return false;
				PointInitialFeet = NPC->GetNavAgentLocation(); Goal = Destination.Location;
				bExecutionPreparedController = NPC->GetController() == nullptr;
				if (!Test->TestTrue(TEXT("Authored NPC public helper admits the real obstacle route"), NPC->StartNavigationTo(Goal, 60, [this](bool Arrived) { ++PointArrivalStops; PointArrived = Arrived; }))) return true;
				AI = Cast<AAIController>(NPC->GetController()); Actions = NPC->FindComponentByClass<UProject_JNPCActionComponent>();
				if (!Test->TestTrue(TEXT("Helper supplies one ordinary native controller/action"), AI && Actions && AI->GetPawn() == NPC)) return true;
				ExecutionStage = 1; return false;
			}
			if (ExecutionStage == 1)
			{
				if (const auto Path = AI->GetPathFollowingComponent()->GetPath(); Path.IsValid()) PointPathPoints = FMath::Max(PointPathPoints, Path->GetPathPoints().Num());
				if (!PointArrivalStops) return false;
				Test->AddInfo(FString::Printf(TEXT("AuthoredNPC movement evidence arrived=%d arrival_calls=%d path_points=%d displacement_xy_cm=%.9g blend_changed=%d moving_pose=%d idle_blend=%s last_observed_blend=%s max_actual_ground_speed_cm_s=%.9g bone_finalizations=%d"), PointArrived, PointArrivalStops, PointPathPoints, FVector::Dist2D(PointInitialFeet, NPC->GetNavAgentLocation()), bExecutionBlendChanged, bExecutionMovingPose, *IdleBlendPosition.ToString(), *ExecutionLastBlendPosition.ToString(), ExecutionMaxGroundSpeed, ExecutionFinalizations));
				if (!Test->TestTrue(TEXT("Same authored NPC naturally moves, reaches and evaluates the non-MM BlendSpace pose"), PointArrived && PointArrivalStops == 1 && PointPathPoints > 2 && FVector::DistSquared2D(PointInitialFeet, NPC->GetNavAgentLocation()) > FMath::Square(1200.0) && bExecutionBlendChanged && bExecutionMovingPose)) return true;
				NPC->StopNavigation();
				if (bCanonicalStableBase)
				{
					CanonicalFloor = IsValid(CanonicalFloorActor) ? Cast<UStaticMeshComponent>(CanonicalFloorActor->GetRootComponent()) : nullptr;
					if (!Test->TestTrue(TEXT("Stable-base opt-in changes only the existing floor after the actual Recast route has completed"),
						CanonicalFloor.IsValid() && CanonicalFloor->IsRegistered() && CanonicalFloor->GetWorld()==World && CanonicalFloor->Mobility==EComponentMobility::Static)) return true;
					CanonicalFloorTransform=CanonicalFloor->GetComponentTransform();
					CanonicalFloor->SetMobility(EComponentMobility::Movable);
				}
				FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				ExecutionVictim = World->SpawnActor<AProject_JNPCCharacter>(AProject_JNPCCharacter::StaticClass(), NPC->GetActorLocation() + NPC->GetActorForwardVector()*120, FRotator::ZeroRotator, Params);
				if (!Test->TestNotNull(TEXT("Existing native passive ASC recipient spawns on the real floor"), ExecutionVictim.Get())) return true;
				ExecutionVictim->GetAbilitySystemComponent()->InitAbilityActorInfo(ExecutionVictim.Get(), ExecutionVictim.Get());
				ExecutionVictim->GetAttributeSet()->InitMaxHealth(1000); ExecutionVictim->GetAttributeSet()->InitHealth(1000);
				VictimEffectHandle = ExecutionVictim->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.AddLambda([this](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
				{
					if (Spec.Def != ExecutionAttack->DamageEffect->GetDefaultObject<UGameplayEffect>() || Spec.GetContext().GetOriginalInstigator() != NPC) return;
					++ExecutionEffects; if (ExecutionStage != 2 || ExecutionEffects != 1) bExecutionViolation = true;
				});
				if (!Test->TestTrue(TEXT("Real decision target registration succeeds"), World->GetSubsystem<UProject_JNPCDecisionSubsystem>()->RegisterTarget(ExecutionVictim.Get(), 2))) return true;
				ExecutionStage = 2;
				if (!Test->TestTrue(TEXT("Explicit neutral retirement admits same-NPC actual authored combat capability"), NPC->StartCombatActions(ExecutionDefinition.Get(), 1))) return true;
				if (!CheckCombatAssembly()) return true;
				if (bCanonicalPath)
				{
					// The helper assembles once; its normal Stop retains components for the existing direct-GAS source test.
					NPC->StopCombatActions();
					if (!Test->TestTrue(TEXT("Normal capability Stop retains the exact actor-lifetime combat assembly"), !NPC->HasCombatActions() && CheckCombatAssembly())) return true;
					return !StartCanonicalExecution();
				}
				ExecutionGrant = Actions->GetAttackAbilityHandle(); return false;
			}
			if (ExecutionStage == 2)
			{
				bExecutionMontage |= ExecutionAnim->Montage_IsPlaying(ExecutionAttack->Montage) && ExecutionHit->GetActiveAttackDefinition() == ExecutionAttack.Get() && Actions->GetIntentTarget() == ExecutionVictim.Get();
				if (!bExecutionNaturalEnd) return false;
				if (bMeleeWindowHitch && !Test->TestTrue(TEXT("The actual first attack exercised and verified the single crossing tick before natural completion"), bMeleeHitchIssued && bMeleeHitchVerified)) return true;
				if (bMeleeWindowHitch && !bMeleeRecoveryStarted)
				{
					if (!Test->TestTrue(TEXT("The linear-clip hitch ends naturally without an unsampled arc hit, damage or a retained grant"),
						ExecutionContacts == 0 && ExecutionEffects == 0 && ExecutionVictim->GetAttributeSet()->GetHealth() == MeleeVictimHealthBefore &&
						!NPC->HasCombatActions() && !ExecutionASC->FindAbilitySpecFromHandle(ExecutionGrant) && !ExecutionHit->GetActiveAttackDefinition())) return true;
					bMeleeRecoveryStarted = true; bExecutionNaturalEnd = false; bExecutionMontage = false;
					if (!Test->TestTrue(TEXT("Same actual NPC, controller and target explicitly re-admit one ordinary attack without another hitch"),
						NPC->StartCombatActions(ExecutionDefinition.Get(), 1))) return true;
					if (!CheckCombatAssembly()) return true;
					ExecutionGrant = Actions->GetAttackAbilityHandle();
					if (!Test->TestTrue(TEXT("Recovery owns its real new grant after the old one retired"), NPCExecutionCurrent() && ExecutionGrant.IsValid() && ExecutionASC->FindAbilitySpecFromHandle(ExecutionGrant))) return true;
					return false;
				}
				if (!Test->TestTrue(TEXT("Natural decision/ability/montage/notify applies exactly one actual health effect"), bExecutionMontage && ExecutionContacts >= 1 && ExecutionEffects == 1 && ExecutionVictim->GetAttributeSet()->GetHealth() < 1000 && !NPC->HasCombatActions() && !ExecutionASC->FindAbilitySpecFromHandle(ExecutionGrant) && !ExecutionHit->GetActiveAttackDefinition())) return true;
				ExecutionHealthAfter = ExecutionVictim->GetAttributeSet()->GetHealth(); ExecutionContactsAfter = ExecutionContacts; ExecutionStage = 3;
				if (!Test->TestTrue(TEXT("Same NPC/controller can explicitly re-admit combat for cancellation"), NPC->StartCombatActions(ExecutionDefinition.Get(), 1))) return true;
				if (!CheckCombatAssembly()) return true;
				ExecutionGrant = Actions->GetAttackAbilityHandle(); return false;
			}
			if (ExecutionStage == 3)
			{
				if (!ExecutionAnim->Montage_IsPlaying(ExecutionAttack->Montage) || ExecutionHit->GetActiveAttackDefinition() != ExecutionAttack.Get()) return false;
				if (!Test->TestTrue(TEXT("Observe the actual second attack before its authored impact"), ExecutionAnim->Montage_GetPosition(ExecutionAttack->Montage) < ExecutionFirstImpact && ExecutionEffects == 1 && ExecutionContacts == ExecutionContactsAfter)) return true;
				bExecutionCancelling = true; NPC->StopCombatActions(); ExecutionStage = 4; return false;
			}
			if (ExecutionAnim->IsAnyMontagePlaying()) return false; // Natural animation drain, no forced evaluation.
			if (!CheckCombatAssembly()) return true;
			if (!Test->TestTrue(TEXT("Cancellation retires montage/hit/grant without a late effect"), bExecutionCancelled && !NPC->HasCombatActions() && !ExecutionASC->FindAbilitySpecFromHandle(ExecutionGrant) && !ExecutionHit->GetActiveAttackDefinition() && ExecutionContacts == ExecutionContactsAfter && ExecutionEffects == 1 && ExecutionVictim->GetAttributeSet()->GetHealth() == ExecutionHealthAfter)) return true;
			ExecutionMesh->ReleaseAnimationUpdate(PoseRequester.Get());
			if (!Test->TestTrue(TEXT("GameplayPose demand composes and restores the original mesh policy after montage drain"), ExecutionMesh->VisibilityBasedAnimTickOption == SavedVisibility && bool(ExecutionMesh->bEnableUpdateRateOptimizations) == bSavedURO && ExecutionMesh->HasAnimationUpdateRequirement() == bSavedPoseDemand)) return true;
			if (!Test->TestTrue(TEXT("Final live neutral command is owned by the same actor before normal world retirement"), NPC->StartNavigationTo(PointInitialFeet, 60, [this](bool Arrived) { ++PointEndStops; Test->TestFalse(TEXT("World retirement does not fabricate arrival"), Arrived); }))) return true;
			World->EndPlay(EEndPlayReason::Quit);
			Test->TestEqual(TEXT("World retirement cancels the last exact neutral command once"), PointEndStops, 1);
			Test->TestTrue(TEXT("World retirement releases capability/grant and only requires destruction for a helper-prepared controller"), !NPC->HasCombatActions() && !ExecutionASC->FindAbilitySpecFromHandle(ExecutionGrant) && (!bExecutionPreparedController || AI->IsActorBeingDestroyed()));
			Test->TestEqual(TEXT("Normal retirement releases actual agent/action registrations"), World->GetSubsystem<UProject_JNPCDecisionSubsystem>()->GetAgentCount() + World->GetSubsystem<UProject_JNPCDecisionSubsystem>()->GetActionCount(), 0);
			Test->TestEqual(TEXT("Normal retirement releases shared path ownership"), Service->GetRequestCount(), 0);
			Test->AddInfo(FString::Printf(TEXT("One authored non-MM NPC: natural Recast/NavMover reach, poseFinalizations=%d BlendSpaceChanged=%d movingPose=%d contacts=%d effects=%d natural montage end, cancellation and EndPlay. GameplayPose offscreen proof only, not rendered-client quality or64-NPC load."), ExecutionFinalizations, bExecutionBlendChanged, bExecutionMovingPose, ExecutionContacts, ExecutionEffects));
			return true;
		}
		void CleanupNPCExecution()
		{
			// Detach observations before external stop/EndPlay and before command fields retire.
			if (ExecutionASC) { ExecutionASC->RemoveGameplayEventTagContainerDelegate(FGameplayTagContainer(ExecutionTag), EventHandle); ExecutionASC->OnAbilityEnded.Remove(EndHandle); }
			if (ExecutionVictim.IsValid()) ExecutionVictim->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.Remove(VictimEffectHandle);
			if (bCanonicalPath) StopCanonicalExecution();
			if (ExecutionMesh) { ExecutionMesh->UnregisterOnBoneTransformsFinalizedDelegate(BoneHandle); if (PoseRequester) ExecutionMesh->ReleaseAnimationUpdate(PoseRequester.Get()); }
			if (IsValid(NPC) && !NPC->IsActorBeingDestroyed()) { NPC->StopNavigation(); NPC->StopCombatActions(); }
			for (const auto& Entry : ExecutionDirty) Test->TestEqual(TEXT("The integration never changes authored package dirty state: ") + Entry.Key->GetName(), Entry.Key->IsDirty(), Entry.Value);
		}
		bool UpdatePointNavigation()
		{
			World->Tick(LEVELTICK_All, 0.05f);
			if (PointPhase == 0)
			{
				FNavLocation PointGoal, PointStart, PointOldGoal;
				if (Nav->IsNavigationBuildInProgress() || !Nav->ProjectPointToNavigation(FVector(900, 0, 0), PointGoal) ||
					!Nav->ProjectPointToNavigation(NPC->GetNavAgentLocation(), PointStart) ||
					!Nav->ProjectPointToNavigation(FVector(-600, -700, 0), PointOldGoal)) return false;
				PointInitialFeet = NPC->GetNavAgentLocation(); Goal = PointGoal.Location;
				if (!Test->TestTrue(TEXT("Native NPC helper admits a neutral destination without a target Actor"),
					NPC->StartNavigationTo(PointOldGoal.Location, 60, [this](bool Arrived) { ++PointOldStops; Test->TestFalse(TEXT("Explicit retirement reports cancellation"), Arrived); }))) return true;
				Actions = NPC->FindComponentByClass<UProject_JNPCActionComponent>(); AI = Cast<AAIController>(NPC->GetController());
				if (!Test->TestTrue(TEXT("Public helper supplies the actual controller and action"), Actions && AI && AI->GetPawn() == NPC)) return true;
				Test->TestEqual(TEXT("Neutral helper uses standard native path following"), AI->GetPathFollowingComponent()->GetClass(), UPathFollowingComponent::StaticClass());
				NPC->NavMover->GetNavMovementProperties()->bUseAccelerationForPaths = true;
				const uint64 PointOldOwner = Actions->GetOwnershipToken();
				Actions->UpdateAction(); Service->Tick(0);
				if (!Test->TestTrue(TEXT("The superseded intent reached actual asynchronous native navigation"), Service->GetStats().Dispatched > 0)) return true;
				NPC->StopNavigation();
				Test->TestEqual(TEXT("Old navigation notifies once before a new intent"), PointOldStops, 1);
				if (!Test->TestTrue(TEXT("Same native controller/action accept the replacement point"), NPC->StartNavigationTo(Goal, 60,
					[this](bool Arrived) { ++PointArrivalStops; PointArrived = Arrived; }))) return true;
				Test->TestTrue(TEXT("Retired owner cannot stop the replacement"), !Actions->StopActionsIfOwned(PointOldOwner) && Actions->GetOwnershipToken() != PointOldOwner);
				Test->TestNull(TEXT("Point movement creates no target Actor intent"), Actions->GetIntentTarget());
				Test->TestNull(TEXT("Point movement creates no hostile scoring source"), Actions->GetScoringSource());
				Test->TestFalse(TEXT("Point movement grants no attack"), Actions->GetAttackAbilityHandle().IsValid());
				PointPhase = 1; return false;
			}
			if (PointPhase == 1)
			{
				if (const auto PointPath = AI->GetPathFollowingComponent()->GetPath(); PointPath.IsValid())
					PointPathPoints = FMath::Max(PointPathPoints, PointPath->GetPathPoints().Num());
				if (!PointArrivalStops) return false;
				Test->TestTrue(TEXT("One actual reach produces one successful stop"), PointArrived && PointArrivalStops == 1);
				Test->TestTrue(TEXT("Real Recast route goes around the obstacle"), PointPathPoints > 2);
				Test->TestTrue(TEXT("Only actual Mover simulation displaced the NPC"), FVector::DistSquared2D(PointInitialFeet, NPC->GetNavAgentLocation()) > FMath::Square(1200.0));
				Test->TestTrue(TEXT("The actual native path follower reached the nav-feet destination"), AI->GetPathFollowingComponent()->HasReached(Goal, EPathFollowingReachMode::ExactLocation, 60));
				Test->TestTrue(TEXT("Arrival retires its action ownership before notification"), Actions->GetOwnershipToken() == 0 && Actions->GetActionState() == EProjectJNPCActionState::Disabled);
				Test->TestEqual(TEXT("Late old path cannot produce a second old stop"), PointOldStops, 1);
				Test->TestEqual(TEXT("Neutral arrival leaves no hostile agent"), World->GetSubsystem<UProject_JNPCDecisionSubsystem>()->GetAgentCount(), 0);
				const uint64 PointForeign = Service->Submit(NPC, NPC, PointInitialFeet, 130, [this](const FProjectJNPCPathCompletion& Result)
				{
					PointForeignReady = true;
					Test->TestEqual(TEXT("Foreign movement also receives a real native path"), Result.Status, EProjectJNPCPathStatus::Success);
					PointForeignPath = Result.Path;
				});
				if (!Test->TestTrue(TEXT("An independent consumer can request the return path"), PointForeign != 0)) return true;
				PointPhase = 2; return false;
			}
			if (!PointForeignReady) return false;
			if (!Test->TestTrue(TEXT("Actual return path is usable"), PointForeignPath.IsValid() && Service->IsPathUsable(PointForeignPath))) return true;
			FAIMoveRequest PointForeignMove; PointForeignMove.SetGoalLocation(PointInitialFeet); PointForeignMove.SetAcceptanceRadius(60);
			const auto PointForeignId = AI->RequestMove(PointForeignMove, PointForeignPath);
			if (!Test->TestTrue(TEXT("Another native consumer owns actual movement"), PointForeignId.IsValid() && AI->GetPathFollowingComponent()->GetStatus() == EPathFollowingStatus::Moving)) return true;
			Test->TestFalse(TEXT("Neutral helper cannot replace a foreign move"), NPC->StartNavigationTo(Goal, 60));
			NPC->StopNavigation();
			Test->TestEqual(TEXT("An unowned neutral stop cannot abort the foreign request"), AI->GetPathFollowingComponent()->GetCurrentRequestId(), PointForeignId);
			AI->StopMovement(); // This fixture owns that independent movement request.
			if (!Test->TestTrue(TEXT("Explicit cancellation starts from another real neutral lease"), NPC->StartNavigationTo(PointInitialFeet, 60,
				[this](bool Arrived) { ++PointCancelledStops; Test->TestFalse(TEXT("Cancelled navigation never claims arrival"), Arrived); }))) return true;
			Actions->UpdateAction(); NPC->StopNavigation();
			Test->TestEqual(TEXT("Explicit stop callback is delivered once"), PointCancelledStops, 1);
			if (!Test->TestTrue(TEXT("A new neutral lease remains reusable until world retirement"), NPC->StartNavigationTo(PointInitialFeet, 60,
				[this](bool Arrived) { ++PointEndStops; Test->TestFalse(TEXT("World retirement never claims arrival"), Arrived); }))) return true;
			World->EndPlay(EEndPlayReason::Quit);
			Test->TestEqual(TEXT("World EndPlay retires the exact active neutral lease once"), PointEndStops, 1);
			Test->TestEqual(TEXT("Neutral world retirement releases all action slots"), World->GetSubsystem<UProject_JNPCDecisionSubsystem>()->GetActionCount(), 0);
			Test->TestEqual(TEXT("Neutral world retirement releases all path requests"), Service->GetRequestCount(), 0);
			Test->AddInfo(TEXT("Public NPC helper -> actual Recast/standard path follower/NavMover displacement and reach; old dispatched intent cancelled before replacement, foreign move preserved, explicit cancel/EndPlay. No target Actor/scoring/attack authority or NPC teleport."));
			return true;
		}
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
		bool bAutomaticDirty = false, bOwnerFollow = false, bSetupFailed = false;



		AActor* Obstacle = nullptr;
		uint64 DispatchBeforeRebuild = 0, DispatchBeforeRetirement = 0;
		bool bNavDataRetirement = false, bRetiredNavData = false;
		TWeakObjectPtr<ANavigationData> RetiredNavData;
		AActor* MovingTarget = nullptr;
		UProject_JNPCActionComponent* Actions = nullptr;
		double SimulatedSeconds = 0, QueuedGoalOffsetY = 0;
		FVector RefreshedGoal=FVector::ZeroVector;
		uint64 QueuedRefreshAccepted=0;
		FAIRequestID MoveBeforeQueuedRefresh;
		bool bQueuedGoalRefreshed=false, bRefreshedGoalConsumed=false;
		int32 ContinuousRepaths = 0;
		bool bPointNavigation = false, PointArrived = false, PointForeignReady = false;
		int32 PointPhase = 0, PointOldStops = 0, PointArrivalStops = 0, PointCancelledStops = 0, PointEndStops = 0, PointPathPoints = 0;
		FVector PointInitialFeet = FVector::ZeroVector;
		FNavPathSharedPtr PointForeignPath;
		bool bNPCExecution = false, bExecutionIdleObserved = false, bExecutionBlendChanged = false, bExecutionMovingPose = false;
		bool bMeleeWindowHitch = false, bMeleeHitchIssued = false, bMeleeHitchVerified = false, bMeleeRecoveryStarted = false;
		bool bCanonicalPath = false;
		bool bCanonicalStableBase=false, bCanonicalBaseObserved=false;
		AActor* CanonicalFloorActor=nullptr;
		TWeakObjectPtr<UStaticMeshComponent> CanonicalFloor;
		FTransform CanonicalFloorTransform, CanonicalPublishedBase;
		FName CanonicalBaseBone;
		FProject_JMeleeTrajectory CanonicalPath;
		FTransform CanonicalPreviousRoot, CanonicalMeshToRoot;
		TStrongObjectPtr<UProject_JTargetScoringComponent> CanonicalScoring;
		uint64 CanonicalActionToken=0, CanonicalScoringRevision=0;
		FGameplayAbilitySpecHandle CanonicalOwnedGrant;
		int32 CanonicalExpectedEffects=0, CanonicalDrainTicks=0;
		bool bCanonicalCloseObserved=false;
		TWeakObjectPtr<UCapsuleComponent> MeleeVictimCapsule;
		FTransform MeleeVictimBeforeTransform;
		float MeleeVictimBeforeRadius = 0, MeleeVictimBeforeHalfHeight = 0, MeleeVictimHealthBefore = 0;
		int32 MeleeMontageInstanceID = INDEX_NONE;
		float MeleeWindowEnd = 0, MeleeBeforePosition = 0;
		double MeleeBeforeWorldTime = 0;
		FName MeleeSocket;
		FVector MeleeBeforePoint = FVector::ZeroVector;
		bool bExecutionViolation = false, bExecutionNaturalEnd = false, bExecutionMontage = false, bExecutionCancelling = false, bExecutionCancelled = false;
		bool bSavedURO = false, bSavedPoseDemand = false, bExecutionPreparedController = false;
		int32 ExecutionStage = 0, ExecutionFinalizations = 0, ExecutionMeleeNotifies = 0, ExecutionContacts = 0, ExecutionContactsAfter = 0, ExecutionEffects = 0;
		float ExecutionFirstImpact = 0, ExecutionHealthAfter = 0;
		uint64 ExecutionEpoch = 0;
		EVisibilityBasedAnimTickOption SavedVisibility = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
		TStrongObjectPtr<UProject_JBudgetedSkeletalMeshComponent> ExecutionMesh;
		TStrongObjectPtr<UAnimInstance> ExecutionAnim;
		TStrongObjectPtr<UClass> ExecutionClass;
		TStrongObjectPtr<UBlendSpace> ExecutionBlend;
		TStrongObjectPtr<UProject_JNPCActivationDefinition> ExecutionDefinition;
		TStrongObjectPtr<UProject_JAttackDefinition> ExecutionAttack;
		TStrongObjectPtr<UProject_JCombatHitValidationComponent> ExecutionHit;
		TStrongObjectPtr<UProject_JCombatPresentationComponent> ExecutionPresentation;
		TStrongObjectPtr<UProject_JCombatPresentationSet> ExecutionBaseSet;
		TArray<FName> ExecutionHitTags, ExecutionPresentationTags;
		bool bExecutionHitCanTick = false, bExecutionPresentationCanTick = false;
		TStrongObjectPtr<UProject_JAbilitySystemComponent> ExecutionASC;
		TStrongObjectPtr<UObject> PoseRequester;
		TWeakObjectPtr<USkeletalMesh> ExecutionAsset;
		TWeakObjectPtr<UAnimInstance> ExecutionPost;
		TWeakObjectPtr<AProject_JNPCCharacter> ExecutionVictim;
		TArray<TStrongObjectPtr<UObject>> ExecutionPins;
		TMap<UPackage*, bool> ExecutionDirty;
		TArray<FTransform> IdlePose;
		FVector IdleBlendPosition = FVector::ZeroVector, ExecutionLastBlendPosition = FVector::ZeroVector;
		double ExecutionMaxGroundSpeed = 0;
		FStructProperty* ExecutionBlendProperty = nullptr;
		FGameplayTag ExecutionTag;
		FGameplayAbilitySpecHandle ExecutionGrant;
		FDelegateHandle BoneHandle, EventHandle, EndHandle, VictimEffectHandle;

	};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNativeNavigationTest, "ProjectJ.Integrated.NativeNavMovement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNativeNavigationTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FNativeNavCommand(this));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCAuthoredExecutionTest, "ProjectJ.NPCGameplay.AuthoredNavigationCombatLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCAuthoredExecutionTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FNativeNavCommand(this, false, false, false, false, false, false, true));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCMeleeWindowHitchTest, "ProjectJ.NPCGameplay.AuthoredMeleeWindowHitch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCMeleeWindowHitchTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FNativeNavCommand(this, false, false, false, false, false, false, true, true));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCanonicalMeleeNativeHitchTest, "ProjectJ.NPCGameplay.CanonicalMeleeWindowHitch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCanonicalMeleeNativeHitchTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FNativeNavCommand(this, false, false, false, false, false, false, true, true, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCanonicalMeleeStableBaseHitchTest, "ProjectJ.NPCGameplay.CanonicalMeleeStableBaseHitch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCanonicalMeleeStableBaseHitchTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FNativeNavCommand(this, false, false, false, false, false, false, true, true, true, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNeutralPointNavigationTest, "ProjectJ.NPCAction.NativePointNavigation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNeutralPointNavigationTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FNativeNavCommand(this, false, false, false, false, false, true));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCompanionFollowNavigationTest, "ProjectJ.Companion.NativeFollowMovement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCompanionFollowNavigationTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FNativeNavCommand(this, true, false, false, true));
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
	// FNativeNavCommand creates explicit Recast data and disables its unused
	// crowd service before initialization. Its real dirty/repath assertions are
	// the contract; the former missing-data/crowd warnings must no longer occur.
	ADD_LATENT_AUTOMATION_COMMAND(FNativeNavCommand(this, true, true, true));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNavDataRetirementTest, "ProjectJ.Companion.NativeNavDataRetirement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNavDataRetirementTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FNativeNavCommand(this, true, false, false, true, true));
	return true;
}

// The actual player mesh has a declared compatible skeleton distinct from the
// source sequence/montage. Keep the same RAW producer and never edit that asset.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCanonicalMeleeCompatibleSkeletonTest, "ProjectJ.NPCGameplay.CanonicalMeleeCompatibleSkeleton",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCanonicalMeleeCompatibleSkeletonTest::RunTest(const FString&)
{
    const TStrongObjectPtr<UProject_JAttackDefinition> SavedNPC(LoadObject<UProject_JAttackDefinition>(nullptr,
        TEXT("/Game/DataAssetSets/AttackDefinition/NPC/DA_Attack_NPC_PrototypeCleave_Canonical.DA_Attack_NPC_PrototypeCleave_Canonical")));
    if (!TestNotNull(TEXT("Existing saved NPC canonical attack exists"),SavedNPC.Get()) || !TestTrue(TEXT("Existing NPC canonical path is enabled"),SavedNPC->MeleeTrajectory.bEnabled)) return false;
    const TStrongObjectPtr<USkeletalMesh> NPCMesh(SavedNPC->MeleeTrajectory.SourceMesh.LoadSynchronous());
    if (!TestNotNull(TEXT("Existing NPC canonical source mesh exists"),NPCMesh.Get()) || !TestNotNull(TEXT("Existing NPC montage exists"),SavedNPC->Montage.Get())) return false;
    TestTrue(TEXT("NPC preservation check exercises the same-skeleton fast path"),NPCMesh->GetSkeleton()==SavedNPC->Montage->GetSkeleton());
    FProject_JMeleeTrajectory NPCRebuilt;FString NPCFailure;
    const bool NPCBuilt=Project_J::CanonicalMeleeAuthoring::BuildCandidate(*SavedNPC,*NPCMesh,NPCRebuilt,NPCFailure);
    if (!TestTrue(FString::Printf(TEXT("Same-skeleton NPC producer still succeeds: %s"),*NPCFailure),NPCBuilt)) return false;
    TestTrue(TEXT("Same-skeleton NPC keys and complete source stamp stay identical to the existing saved path"),
        FProject_JMeleeTrajectory::StaticStruct()->CompareScriptStruct(&NPCRebuilt,&SavedNPC->MeleeTrajectory,PPF_None));
    const TStrongObjectPtr<UProject_JAttackDefinition> SelectedAttack(LoadObject<UProject_JAttackDefinition>(nullptr,
        TEXT("/Game/DataAssetSets/AttackDefinition/Spellblade/DA_Attack_Spellblade_LMB1.DA_Attack_Spellblade_LMB1")));
    const TStrongObjectPtr<USkeletalMesh> SelectedMesh(LoadObject<USkeletalMesh>(nullptr,
        TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete.SK_Spellblade_Complete")));
    if (!TestNotNull(TEXT("Actual player attack exists"),SelectedAttack.Get()) || !TestNotNull(TEXT("Actual player main mesh exists"),SelectedMesh.Get())) return false;
    USkeleton* const OriginalTarget=SelectedMesh->GetSkeleton();
    if (!TestNotNull(TEXT("Actual player target skeleton exists"),OriginalTarget) || !TestNotNull(TEXT("Actual player montage exists"),SelectedAttack->Montage.Get())) return false;
    TestTrue(TEXT("This actually exercises distinct source and target skeletons"),OriginalTarget!=SelectedAttack->Montage->GetSkeleton());
    const bool AttackDirty=SelectedAttack->GetOutermost()->IsDirty(),MeshDirty=SelectedMesh->GetOutermost()->IsDirty(),TargetDirty=OriginalTarget->GetOutermost()->IsDirty();
    const bool OriginalUseSourceModes=OriginalTarget->GetUseRetargetModesFromCompatibleSkeleton();
    const FProject_JMeleeTrajectory OriginalPath=SelectedAttack->MeleeTrajectory;
    const auto OriginalSocketIdentities=SelectedMesh->GetActiveSocketList();
    // Public selected-asset/CDO diagnostic only. No source/mesh/socket override,
    // transient actor, setter or template mutation can make this fixture pass.
    const auto LogSocket=[this](const TCHAR* Scope,const USkeletalMeshSocket* Socket)
    {
        if (Socket) AddInfo(FString::Printf(TEXT("CanonicalSocket scope=%s name=%s parent=%s local=%s object=%s"),
            Scope,*Socket->SocketName.ToString(),*Socket->BoneName.ToString(),*Socket->GetSocketLocalTransform().ToString(),*Socket->GetPathName()));
    };
    const auto LogMeshSockets=[&](const TCHAR* Scope,USkeletalMesh* Asset)
    {
        if (!Asset) {AddInfo(FString::Printf(TEXT("CanonicalSocket scope=%s mesh=missing"),Scope));return;}
        const auto Active=Asset->GetActiveSocketList();
        AddInfo(FString::Printf(TEXT("CanonicalSocket scope=%s mesh=%s skeleton=%s active_sockets=%d"),Scope,*Asset->GetPathName(),*GetPathNameSafe(Asset->GetSkeleton()),Active.Num()));
        for (const auto* Socket:Active) LogSocket(Scope,Socket);
    };
    LogMeshSockets(TEXT("selected_player_target"),SelectedMesh.Get());
    LogMeshSockets(TEXT("existing_npc_source_mesh"),NPCMesh.Get());
    if (USkeleton* MontageSkeleton=SelectedAttack->Montage->GetSkeleton())
    {
        AddInfo(FString::Printf(TEXT("CanonicalSocket scope=montage_source_skeleton skeleton=%s sockets=%d"),*MontageSkeleton->GetPathName(),MontageSkeleton->Sockets.Num()));
        for (const auto& Socket:MontageSkeleton->Sockets) LogSocket(TEXT("montage_source_skeleton"),Socket.Get());
    }
    for (int32 NotifyIndex=0;NotifyIndex<SelectedAttack->Montage->Notifies.Num();++NotifyIndex)
    {
        const auto& Event=SelectedAttack->Montage->Notifies[NotifyIndex];
        if (const auto* Melee=Cast<UProject_JAnimNotifyState_MeleeHit>(Event.NotifyStateClass.Get()))
        {
            const FName Trace=Melee->GetTraceSocketName();
            AddInfo(FString::Printf(TEXT("CanonicalSocket notify=%d class=%s trace=%s start=%.9g end=%.9g target_mesh_bone=%d target_skeleton_bone=%d"),
                NotifyIndex,*Melee->GetClass()->GetPathName(),*Trace.ToString(),Event.GetTriggerTime(),Event.GetEndTriggerTime(),
                SelectedMesh->GetRefSkeleton().FindBoneIndex(Trace),OriginalTarget->GetReferenceSkeleton().FindBoneIndex(Trace)));
        }
    }
    const TStrongObjectPtr<UClass> PlayerClass(LoadClass<APawn>(nullptr,TEXT("/Game/Character_BPs/Spellblade/BP_Spellblade.BP_Spellblade_C")));
    const TStrongObjectPtr<APawn> PlayerDefaults(PlayerClass.IsValid()?PlayerClass->GetDefaultObject<APawn>():nullptr);
    if (PlayerDefaults.IsValid())
    {
        const auto* MoverDefaults=Cast<AProject_JMoverPawn>(PlayerDefaults.Get());
        const auto* NativeMesh=MoverDefaults?MoverDefaults->GetMesh():nullptr;
        const auto* PoseSource=Project_J::Movement::FindPoseSource(PlayerDefaults.Get());
        AddInfo(FString::Printf(TEXT("CanonicalSocket CDO=%s native_mesh=%s native_asset=%s pose_source=%s pose_asset=%s native_equals_pose=%d pose_asset_equals_selected=%d; template metadata, not spawned runtime proof"),
            *PlayerDefaults->GetPathName(),*GetPathNameSafe(NativeMesh),*GetPathNameSafe(NativeMesh?NativeMesh->GetSkeletalMeshAsset():nullptr),
            *GetPathNameSafe(PoseSource),*GetPathNameSafe(PoseSource?PoseSource->GetSkeletalMeshAsset():nullptr),NativeMesh && NativeMesh==PoseSource,
            PoseSource && PoseSource->GetSkeletalMeshAsset()==SelectedMesh.Get()));
        for (const auto* Component:PlayerDefaults->GetComponents())
        {
            const auto* MeshComponent=Cast<USkeletalMeshComponent>(Component);
            if (!MeshComponent) continue;
            FString Tags;
            for (const FName Tag:MeshComponent->ComponentTags) {if (!Tags.IsEmpty()) Tags+=TEXT(",");Tags+=Tag.ToString();}
            AddInfo(FString::Printf(TEXT("CanonicalSocket component=%s class=%s asset=%s parent=%s tags=%s native_pose=%d"),
                *MeshComponent->GetPathName(),*MeshComponent->GetClass()->GetPathName(),*GetPathNameSafe(MeshComponent->GetSkeletalMeshAsset()),
                *GetPathNameSafe(MeshComponent->GetAttachParent()),*Tags,MeshComponent==PoseSource));
        }
    }
    else AddInfo(TEXT("CanonicalSocket selected player CDO unavailable; source identity metadata is unqualified."));
    FProject_JMeleeTrajectory ActualPath;FString Failure;
    const bool Built=Project_J::CanonicalMeleeAuthoring::BuildCandidate(*SelectedAttack,*SelectedMesh,ActualPath,Failure);
    if (!TestTrue(FString::Printf(TEXT("Actual compatible player RAW producer succeeds: %s"),*Failure),Built)) return false;
    TestTrue(TEXT("Actual compatible result passes native shape"),ActualPath.GetValidationFailure().IsEmpty());
    TestTrue(TEXT("Actual compatible source stamp validates without pose rebake"),
        Project_J::CanonicalMeleeAuthoring::ValidateCurrentSource(*SelectedAttack,*SelectedMesh,ActualPath,Failure));

    // Independent native compressed playback is diagnostic input only, never
    // a producer/fallback or a zero-difference/arbitrary quality acceptance test.
    const FAnimSegment& ReferenceSegment=SelectedAttack->Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0];
    const TStrongObjectPtr<UAnimSequence> ReferenceSequence(Cast<UAnimSequence>(ReferenceSegment.GetAnimReference().Get()));
    const IConsoleVariable* ForceRaw=IConsoleManager::Get().FindConsoleVariable(TEXT("a.ForceEvalRawData"));
    const bool ReferenceAvailable=ReferenceSequence.IsValid() && ReferenceSequence->GetSkeleton() && ReferenceSequence->IsCompressedDataValid() && ForceRaw && ForceRaw->GetInt()==0 &&
        ReferenceSequence->GetSkeletonVirtualBoneGuid()==ReferenceSequence->GetSkeleton()->GetVirtualBoneGuid();
    int32 ReferenceSamples=0;double MaximumDelta=0;float WorstTime=0;FVector WorstRaw=FVector::ZeroVector,WorstCompressed=FVector::ZeroVector;
    if (ReferenceAvailable)
    {
        FAnimPoseEvaluationOptions ReferenceOptions;ReferenceOptions.EvaluationType=EAnimDataEvalType::Compressed;
        ReferenceOptions.bShouldRetarget=true;ReferenceOptions.bExtractRootMotion=true;ReferenceOptions.bIncorporateRootMotionIntoPose=false;
        ReferenceOptions.OptionalSkeletalMesh=SelectedMesh.Get();ReferenceOptions.bEvaluateCurves=true;
        const USkeletalMeshSocket* ReferenceSocket=SelectedMesh->FindSocket(ActualPath.SocketName);
        const bool HasSocket=ReferenceSocket!=nullptr;
        for (const auto& Key:ActualPath.Keys)
        {
            FAnimPose ReferencePose;
            UAnimPoseExtensions::GetAnimPoseAtTime(ReferenceSequence.Get(),ReferenceSegment.ConvertTrackPosToAnimPos(Key.MontageTime),ReferenceOptions,ReferencePose);
            if (!ReferencePose.IsValid()) break;
            TArray<FName> ReferenceNames;
            if (HasSocket) UAnimPoseExtensions::GetSocketNames(ReferencePose,ReferenceNames);
            else UAnimPoseExtensions::GetBoneNames(ReferencePose,ReferenceNames);
            if (!ReferenceNames.Contains(ActualPath.SocketName)) break;
            if (HasSocket)
            {TArray<FName> ReferenceBones;UAnimPoseExtensions::GetBoneNames(ReferencePose,ReferenceBones);if (!ReferenceBones.Contains(ReferenceSocket->BoneName)) break;}
            const FTransform ReferenceTrace=HasSocket?UAnimPoseExtensions::GetSocketPose(ReferencePose,ActualPath.SocketName,EAnimPoseSpaces::World):
                UAnimPoseExtensions::GetBonePose(ReferencePose,ActualPath.SocketName,EAnimPoseSpaces::World);
            const FVector ReferencePosition=ReferenceTrace.TransformPosition(ActualPath.BladeTipOffset);
            const double Delta=FVector::Distance(Key.MeshPosition,ReferencePosition);
            if (ReferenceTrace.ContainsNaN() || !FMath::IsFinite(Delta)) break;
            ++ReferenceSamples;
            if (Delta>=MaximumDelta) {MaximumDelta=Delta;WorstTime=Key.MontageTime;WorstRaw=Key.MeshPosition;WorstCompressed=ReferencePosition;}
        }
    }
    AddInfo(FString::Printf(TEXT("CanonicalCompatible RAW_vs_native_compressed observed=%d samples=%d requested=%d max_delta_cm=%.9g worst_montage_time=%.9g raw=%s compressed=%s; inputs differ, diagnostic only, no tolerance/geometry acceptance"),
        ReferenceAvailable && ReferenceSamples==ActualPath.Keys.Num(),ReferenceSamples,ActualPath.Keys.Num(),MaximumDelta,WorstTime,*WorstRaw.ToString(),*WorstCompressed.ToString()));
    if (!ReferenceAvailable || ReferenceSamples!=ActualPath.Keys.Num())
        AddInfo(TEXT("Native compressed comparison is unqualified: payload/virtual-bone identity/ordinary RAW-force policy or complete native pose samples are unavailable; no forced completion or RAW fallback is counted."));

    const TStrongObjectPtr<UPackage> Scratch(CreatePackage(*FString::Printf(TEXT("/Temp/ProjectJCanonicalTarget_%s"),*FGuid::NewGuid().ToString(EGuidFormats::Digits))));
    Scratch->SetFlags(RF_Transient);
    const TStrongObjectPtr<USkeleton> TargetCopy(DuplicateObject<USkeleton>(OriginalTarget,Scratch.Get(),TEXT("TargetCopy")));
    const TStrongObjectPtr<USkeletalMesh> MeshCopy(DuplicateObject<USkeletalMesh>(SelectedMesh.Get(),Scratch.Get(),TEXT("MeshCopy")));
    if (!TestNotNull(TEXT("Owned transient target skeleton copy exists"),TargetCopy.Get()) || !TestNotNull(TEXT("Owned transient mesh copy exists"),MeshCopy.Get())) return false;
    MeshCopy->SetSkeleton(TargetCopy.Get());
    const TStrongObjectPtr<USkeletalMeshSocket> SocketAlias(MeshCopy->FindSocket(ActualPath.SocketName));
    if (!TestNotNull(TEXT("Owned target trace socket exists"),SocketAlias.Get()) || !TestTrue(TEXT("Fixture socket belongs to the owned mesh copy"),SocketAlias->GetOuter()==MeshCopy.Get())) return false;
    MeshCopy->GetMeshOnlySocketList().Add(SocketAlias.Get());
    TestTrue(TEXT("Native active socket list really repeats the same object"),MeshCopy->GetActiveSocketList().FilterByPredicate(
        [&](const USkeletalMeshSocket* Socket){return Socket==SocketAlias.Get();}).Num()>1);
    FProject_JMeleeTrajectory CopyPath;
    const bool CopyBuilt=Project_J::CanonicalMeleeAuthoring::BuildCandidate(*SelectedAttack,*MeshCopy,CopyPath,Failure);
    if (!TestTrue(FString::Printf(TEXT("Repeated same socket identity is accepted by the actual producer: %s"),*Failure),CopyBuilt)) return false;
    const TStrongObjectPtr<USkeletalMeshSocket> Conflict(NewObject<USkeletalMeshSocket>(MeshCopy.Get(),NAME_None,RF_Transient));
    Conflict->SocketName=SocketAlias->SocketName;Conflict->BoneName=SocketAlias->BoneName;
    Conflict->RelativeLocation=SocketAlias->RelativeLocation;Conflict->RelativeRotation=SocketAlias->RelativeRotation;Conflict->RelativeScale=SocketAlias->RelativeScale;
    MeshCopy->GetMeshOnlySocketList().Add(Conflict.Get());
    FProject_JMeleeTrajectory AmbiguousOutput=CopyPath;
    TestTrue(TEXT("Distinct same-name sockets are rejected even with identical geometry, preserving the complete output"),
        !Project_J::CanonicalMeleeAuthoring::BuildCandidate(*SelectedAttack,*MeshCopy,AmbiguousOutput,Failure) &&
        Failure.Contains(TEXT("multiple distinct")) &&
        FProject_JMeleeTrajectory::StaticStruct()->CompareScriptStruct(&AmbiguousOutput,&CopyPath,PPF_None));
    MeshCopy->GetMeshOnlySocketList().Remove(Conflict.Get());
    TestTrue(TEXT("Removing only the owned conflicting socket restores the accepted source stamp"),
        Project_J::CanonicalMeleeAuthoring::ValidateCurrentSource(*SelectedAttack,*MeshCopy,CopyPath,Failure));
    TargetCopy->SetUseRetargetModesFromCompatibleSkeleton(!OriginalUseSourceModes);
    TestTrue(TEXT("Target retarget-mode policy change invalidates the stored stamp without a shape error"),
        CopyPath.GetValidationFailure().IsEmpty() &&
        !Project_J::CanonicalMeleeAuthoring::ValidateCurrentSource(*SelectedAttack,*MeshCopy,CopyPath,Failure) && Failure.Contains(TEXT("changed")));
    TargetCopy->SetUseRetargetModesFromCompatibleSkeleton(OriginalUseSourceModes);
    TestTrue(TEXT("Restoring the transient target policy restores stamp validity"),
        Project_J::CanonicalMeleeAuthoring::ValidateCurrentSource(*SelectedAttack,*MeshCopy,CopyPath,Failure));

    const TStrongObjectPtr<USkeleton> Unrelated(NewObject<USkeleton>(Scratch.Get(),TEXT("Unrelated"),RF_Transient));
    MeshCopy->SetSkeleton(Unrelated.Get());
    FProject_JMeleeTrajectory Sentinel=CopyPath;
    TestTrue(TEXT("Unrelated target is rejected by compatibility, preserving the entire output"),
        !Project_J::CanonicalMeleeAuthoring::BuildCandidate(*SelectedAttack,*MeshCopy,Sentinel,Failure) &&
        Failure.Contains(TEXT("declared native compatible skeleton")) &&
        FProject_JMeleeTrajectory::StaticStruct()->CompareScriptStruct(&Sentinel,&CopyPath,PPF_None));
    TestTrue(TEXT("Original target active socket identities and ordering are unchanged"),SelectedMesh->GetActiveSocketList()==OriginalSocketIdentities);
    TestTrue(TEXT("Original player target and policy are unchanged"),SelectedMesh->GetSkeleton()==OriginalTarget && OriginalTarget->GetUseRetargetModesFromCompatibleSkeleton()==OriginalUseSourceModes);
    TestTrue(TEXT("Original player trajectory is unchanged"),FProject_JMeleeTrajectory::StaticStruct()->CompareScriptStruct(&SelectedAttack->MeleeTrajectory,&OriginalPath,PPF_None));
    TestTrue(TEXT("Original player asset dirty flags are unchanged"),SelectedAttack->GetOutermost()->IsDirty()==AttackDirty && SelectedMesh->GetOutermost()->IsDirty()==MeshDirty && OriginalTarget->GetOutermost()->IsDirty()==TargetDirty);
    return true;
}
#endif
