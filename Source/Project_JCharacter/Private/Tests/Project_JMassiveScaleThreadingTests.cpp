#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Combat/Project_JCombatSpatialHistorySubsystem.h"
#include "Combat/Project_JMultiPartHitComponent.h"
#include "Combat/Project_JProjectileManagerSubsystem.h"
#include "Combat/Project_JServerSideRewindComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Mass/Project_JMassMovementProcessor.h"
#include "MassEntityManager.h"
#include "MassEntityManagerStorage.h"
#include "MassExecutor.h"
#include "MassProcessingContext.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "Movement/Project_JPlayerMoverPrioritizerConfig.h"
#include "Network/Project_JNetObjectPrioritizer_Combat.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Project_JNPCCharacter.h"
#include "Project_JSpellbladeCharacter.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "Components/Project_JAnimationUpdateCoordinatorComponent.h"
#include "AI/Project_JThreatComponent.h"
#include "UObject/Package.h"
#if UE_WITH_IRIS
#include "Engine/NetDriver.h"
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Iris/ReplicationSystem/Prioritization/NetObjectPrioritizer.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#endif

namespace
{
struct FMassiveScaleTestWorld
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);

	FMassiveScaleTestWorld()
	{
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	}

	void BeginPlay()
	{
		World->InitializeActorsForPlay(FURL());
		World->GetWorldSettings()->NotifyBeginPlay();
		World->GetWorldSettings()->NotifyMatchStarted();
	}

	void Step(float DeltaSeconds)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, DeltaSeconds);
	}

	~FMassiveScaleTestWorld()
	{
		if (World->GetBegunPlay())
		{
			World->EndPlay(EEndPlayReason::LevelTransition);
		}
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
};

TSharedRef<FMassEntityManager> CreateMassiveScaleMassManager()
{
	auto Manager = MakeShared<FMassEntityManager>();
	FMassEntityManagerStorageInitParams Params;
#if WITH_MASS_CONCURRENT_RESERVE
	Params.Emplace<FMassEntityManager_InitParams_Concurrent>(FMassEntityManager_InitParams_Concurrent{1024, 256});
#else
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	Params.Emplace<FMassEntityManager_InitParams_SingleThreaded>();
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif
	Manager->Initialize(Params);
	return Manager;
}

constexpr auto MassiveScaleTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

struct FTestLatePoseTick : FTickFunction
{
	AActor* Actor = nullptr;
	virtual FString DiagnosticMessage() override { return TEXT("SpatialHistoryTestLatePose"); }
	virtual void ExecuteTick(float, ELevelTick, ENamedThreads::Type, const FGraphEventRef&) override
	{
		Actor->SetActorLocation(FVector(1000, 2000, 100));
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSpatialHistoryAggregationTest, "ProjectJ.Combat.SpatialHistoryAggregation", MassiveScaleTestFlags)
bool FProjectJSpatialHistoryAggregationTest::RunTest(const FString&)
{
	FMassiveScaleTestWorld Scope;
	auto* Subsystem = Scope.World->GetSubsystem<UProject_JCombatSpatialHistorySubsystem>();
	if (!TestNotNull(TEXT("CombatSpatialHistorySubsystem exists for game worlds"), Subsystem))
	{
		return false;
	}

	TArray<ACharacter*> AuthActors;
	TArray<UProject_JServerSideRewindComponent*> AuthRewinds;
	for (int32 i = 0; i < 6; ++i)
	{
		ACharacter* Actor = Scope.World->SpawnActor<ACharacter>(FVector(i * 200.0f, 0.0f, 100.0f), FRotator::ZeroRotator);
		Actor->GetCharacterMovement()->SetComponentTickEnabled(false);
		auto* Rewind = NewObject<UProject_JServerSideRewindComponent>(Actor);
		Rewind->RecordRateHz = 30.0f;
		Rewind->RegisterComponent();
		AuthActors.Add(Actor);
		AuthRewinds.Add(Rewind);
	}

	ACharacter* Boss = Scope.World->SpawnActor<ACharacter>(FVector(1000.0f, 500.0f, 100.0f), FRotator::ZeroRotator);
	Boss->GetCharacterMovement()->SetComponentTickEnabled(false);
	auto* MultiPart = NewObject<UProject_JMultiPartHitComponent>(Boss);
	FProject_JBossHitPartSpec HeadSpec;
	HeadSpec.PartName = TEXT("Head");
	HeadSpec.SocketOrBoneName = TEXT("root");
	HeadSpec.LocalOffset = FVector(0.0f, 0.0f, 120.0f);
	HeadSpec.SphereRadius = 100.0f;
	HeadSpec.DamageMultiplier = 1.5f;
	HeadSpec.bIsWeakpoint = true;
	MultiPart->HitParts.Add(HeadSpec);
	MultiPart->RegisterComponent();
	FTestLatePoseTick LatePose;
	LatePose.Actor = Boss;
	LatePose.bCanEverTick = true;
	LatePose.TickGroup = TG_PostPhysics;
	LatePose.EndTickGroup = TG_PostPhysics;
	LatePose.RegisterTickFunction(Scope.World->PersistentLevel);
	Boss->GetMesh()->PrimaryComponentTick.TickGroup = TG_PostPhysics;
	Boss->GetMesh()->PrimaryComponentTick.EndTickGroup = TG_PostPhysics;
	Boss->GetMesh()->PrimaryComponentTick.AddPrerequisite(Boss, LatePose);
	ON_SCOPE_EXIT { LatePose.UnRegisterTickFunction(); };

	ACharacter* RemoteProxy = Scope.World->SpawnActor<ACharacter>(FVector(-500.0f, 0.0f, 100.0f), FRotator::ZeroRotator);
	RemoteProxy->SetReplicates(true);
	RemoteProxy->SwapRoles();
	auto* RemoteRewind = NewObject<UProject_JServerSideRewindComponent>(RemoteProxy);
	RemoteRewind->RegisterComponent();

	Scope.BeginPlay();

	TestEqual(TEXT("All 6 authority SSR components registered with aggregator"), Subsystem->GetRegisteredSSRCount(), 6);
	TestEqual(TEXT("Authority MultiPart boss registered with aggregator"), Subsystem->GetRegisteredMultiPartCount(), 1);
	TestFalse(TEXT("MultiPart boss component tick is disabled when aggregated"), MultiPart->IsComponentTickEnabled());
	TestFalse(TEXT("Remote proxy SSR component tick stays disabled"), RemoteRewind->IsComponentTickEnabled());
	for (UProject_JServerSideRewindComponent* Rewind : AuthRewinds)
	{
		TestFalse(TEXT("Authority SSR component tick is disabled when aggregated"), Rewind->IsComponentTickEnabled());
	}

	Scope.Step(1.0f / 60.0f);
	const FVector FirstHead = FVector(1000, 2000, 100) + HeadSpec.LocalOffset;
	Boss->SetActorLocation(FVector(1000, 3000, 100)); // A live-pose fallback cannot pass the history check.
	FName FirstPart;
	float FirstMultiplier = 0.f;
	FVector FirstCenter;
	TestTrue(TEXT("First sample waits for the same-group pose prerequisite"),
		MultiPart->VerifyRewoundSegmentHit(Scope.World->GetTimeSeconds(),
			FirstHead - FVector(120, 0, 0), FirstHead + FVector(120, 0, 0), 10.f,
			FirstPart, FirstMultiplier, FirstCenter) && FirstCenter.Equals(FirstHead));

	for (int32 Frame = 0; Frame < 12; ++Frame)
	{
		for (int32 i = 0; i < AuthActors.Num(); ++i)
		{
			AuthActors[i]->SetActorLocation(FVector(i * 200.0f + Frame * 10.0f, 0.0f, 100.0f));
		}
		Scope.Step(1.0f / 60.0f);
	}

	for (int32 i = 0; i < AuthRewinds.Num(); ++i)
	{
		double Oldest = 0.0, Newest = 0.0;
		TestTrue(TEXT("Aggregated authority SSR captured pose history bounds"), AuthRewinds[i]->TryGetHistoryTimeBounds(Oldest, Newest));
		TestTrue(TEXT("Aggregated authority SSR history spans multiple 30Hz intervals"), (Newest - Oldest) >= 0.12);
		const FVector LatestLoc = AuthActors[i]->GetActorLocation();
		TestTrue(TEXT("Aggregated authority SSR verifies hit against rewound pose"),
			AuthRewinds[i]->ServerVerifyHit(Newest, LatestLoc - FVector(150.0f, 0.0f, 0.0f), LatestLoc + FVector(150.0f, 0.0f, 0.0f), 25.0f));
	}
	double RemoteOldest = 0.0, RemoteNewest = 0.0;
	TestFalse(TEXT("Remote proxy recorded zero poses"), RemoteRewind->TryGetHistoryTimeBounds(RemoteOldest, RemoteNewest));

	FName HitPartName = NAME_None;
	float HitMult = 0.0f;
	FVector HitCenter = FVector::ZeroVector;
	const FVector HeadWorld = Boss->GetActorLocation() + HeadSpec.LocalOffset;
	TestTrue(TEXT("Aggregated MultiPart boss captured rewound weakpoint history"),
		MultiPart->VerifyRewoundSegmentHit(
			Scope.World->GetTimeSeconds(),
			HeadWorld - FVector(200.0f, 0.0f, 0.0f),
			HeadWorld + FVector(200.0f, 0.0f, 0.0f),
			20.0f,
			HitPartName,
			HitMult,
			HitCenter));
	TestEqual(TEXT("Weakpoint part name matches"), HitPartName, FName(TEXT("Head")));
	TestTrue(TEXT("Weakpoint multiplier matches"), FMath::IsNearlyEqual(HitMult, 1.5f));
	TestTrue(TEXT("PostPhysics history observes the late pose prerequisite"), HitCenter.Equals(HeadWorld));

	AuthActors.Last()->SetActorLocation(FVector(3000, 0, 100));
	AuthRewinds.Last()->ResetHistory();
	Scope.Step(1.0f / 240.0f);
	double Oldest = 0.0, Newest = 0.0;
	TestTrue(TEXT("Teleport records on the first eligible tick inside its former cadence"),
		AuthRewinds.Last()->TryGetHistoryTimeBounds(Oldest, Newest) && Newest == Scope.World->GetTimeSeconds());

	auto* Rewind = AuthRewinds.Last();
	Rewind->SetComponentTickEnabled(false);
	Scope.Step(1.0f / 60.0f);
	TestFalse(TEXT("An external pause stops centralized samples"), Rewind->TryGetHistoryTimeBounds(Oldest, Newest));
	Rewind->SetComponentTickEnabled(true);
	Scope.Step(1.0f / 60.0f);
	TestTrue(TEXT("Resume uses the centralized writer"), Rewind->TryGetHistoryTimeBounds(Oldest, Newest));
	TestFalse(TEXT("Resume never enables a second tick writer"), Rewind->IsComponentTickEnabled());
	Rewind->UnregisterComponent();
	MultiPart->UnregisterComponent();
	Scope.Step(1.0f / 60.0f);
	TestEqual(TEXT("Unregister detaches SSR before the next pass"), Subsystem->GetRegisteredSSRCount(), 5);
	TestEqual(TEXT("Unregister removes the MultiPart slot and its prerequisite"), Subsystem->GetRegisteredMultiPartCount(), 0);
	Rewind->RegisterComponent();
	MultiPart->RegisterComponent();
	Scope.Step(1.0f / 60.0f);
	TestEqual(TEXT("Reregister restores SSR after BeginPlay without duplication"), Subsystem->GetRegisteredSSRCount(), 6);
	TestEqual(TEXT("Reregister restores the socket-dependent MultiPart slot"), Subsystem->GetRegisteredMultiPartCount(), 1);
	TestTrue(TEXT("Reregister captures a new pose"), Rewind->TryGetHistoryTimeBounds(Oldest, Newest));

	auto* RewindOwner = AuthActors.Last();
	RewindOwner->SetRole(ROLE_SimulatedProxy);
	Scope.Step(1.0f / 60.0f);
	TestFalse(TEXT("Losing authority invalidates previous samples"), Rewind->TryGetHistoryTimeBounds(Oldest, Newest));
	RewindOwner->SetRole(ROLE_Authority);
	Scope.Step(1.0f / 60.0f);
	TestTrue(TEXT("Restored authority resumes sampling"), Rewind->TryGetHistoryTimeBounds(Oldest, Newest));

	for (int32 i = 0; i < 3; ++i)
	{
		AuthActors[i]->Destroy();
	}
	Boss->Destroy();

	TestEqual(TEXT("Destroyed authority SSR actors auto-unregister cleanly"), Subsystem->GetRegisteredSSRCount(), 3);
	TestEqual(TEXT("Destroyed MultiPart boss auto-unregisters cleanly"), Subsystem->GetRegisteredMultiPartCount(), 0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJProjectileParallelAndPruneTest, "ProjectJ.Combat.ProjectileParallelAndPrune", MassiveScaleTestFlags)
bool FProjectJProjectileParallelAndPruneTest::RunTest(const FString&)
{
	FMassiveScaleTestWorld Scope;
	Scope.BeginPlay();

	auto* ProjectileSubsystem = Scope.World->GetSubsystem<UProject_JProjectileManagerSubsystem>();
	if (!TestNotNull(TEXT("ProjectileManagerSubsystem exists"), ProjectileSubsystem))
	{
		return false;
	}

	AActor* Instigator = Scope.World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	constexpr int32 SpawnCount = 80; // Exceeds default ProjectJ.Projectile.ParallelThreshold (64)
	{
		FMassiveScaleTestWorld Other;
		AActor* ForeignSource = Other.World->SpawnActor<AActor>();
		TestEqual(TEXT("An actor from another world cannot own a projectile here"),
			ProjectileSubsystem->SpawnProjectile(ForeignSource, FVector::ZeroVector, FVector::ForwardVector, 8.f, 1.f, 0, 0.f, {}), uint64(0));
		TestFalse(TEXT("Another world's target cannot occupy a local cooldown channel"),
			ProjectileSubsystem->AdmitMultiHitTarget(42, ForeignSource, 10.0, 0.5f));
	}
	TArray<uint64> Ids;
	Ids.Reserve(SpawnCount);
	for (int32 i = 0; i < SpawnCount; ++i)
	{
		const uint64 Id = ProjectileSubsystem->SpawnProjectile(
			Instigator,
			FVector(0.0f, i * 100.0f, 5000.0f),
			FVector(1200.0f, 0.0f, 0.0f),
			16.0f,
			2.0f,
			0,
			0.5f,
			FProjectJOnProjectileHit());
		TestTrue(TEXT("Pooled projectile spawned cleanly"), Id != 0);
		Ids.Add(Id);
	}
	TestEqual(TEXT("All 80 projectiles active before parallel tick"), ProjectileSubsystem->GetActiveProjectileCount(), SpawnCount);

	for (int32 Frame = 0; Frame < 5; ++Frame)
	{
		Scope.Step(1.0f / 30.0f);
	}
	TestEqual(TEXT("All 80 unobstructed projectiles survive parallel kinematics ticks"), ProjectileSubsystem->GetActiveProjectileCount(), SpawnCount);

	for (int32 i = 0; i < SpawnCount / 2; ++i)
	{
		TestTrue(TEXT("CancelProjectile removes active projectile"), ProjectileSubsystem->CancelProjectile(Ids[i]));
	}
	TestEqual(TEXT("Half of projectiles remain after cancel"), ProjectileSubsystem->GetActiveProjectileCount(), SpawnCount / 2);

	AActor* TempTarget = Scope.World->SpawnActor<AActor>(FVector(1000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	const uint64 ChannelId = 42;
	TestTrue(TEXT("First multi-hit admission succeeds"), ProjectileSubsystem->AdmitMultiHitTarget(ChannelId, TempTarget, 10.0, 0.5f));
	TestFalse(TEXT("Duplicate multi-hit within cooldown rejected"), ProjectileSubsystem->AdmitMultiHitTarget(ChannelId, TempTarget, 10.1, 0.5f));
	TempTarget->Destroy();
	// Force weak pointer invalidation before prune check.
	Scope.Step(1.0f / 30.0f);
	const int32 Pruned = ProjectileSubsystem->PruneStaleMultiHitCooldowns();
	TestTrue(TEXT("Stale multi-hit cooldown entry pruned after target destruction"), Pruned >= 1);

	for (int32 i = SpawnCount / 2; i < SpawnCount; ++i)
	{
		ProjectileSubsystem->CancelProjectile(Ids[i]);
	}
	for (uint64 Channel = 1; Channel <= 200; ++Channel)
	{
		ProjectileSubsystem->AdmitMultiHitTarget(Channel, Instigator, Scope.World->GetTimeSeconds(), 0.1f);
	}
	TestTrue(TEXT("Cooldown maintenance runs with no active projectiles"), ProjectileSubsystem->IsTickable());
	for (int32 Frame = 0; Frame < 65; ++Frame)
	{
		Scope.Step(1.0f / 30.0f);
	}
	TestTrue(TEXT("Target remains alive"), IsValid(Instigator));
	TestFalse(TEXT("Expired channels release all idle maintenance"), ProjectileSubsystem->IsTickable());

	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJProjectileAsyncSweepTest, "ProjectJ.Combat.ProjectileAsyncSweep", MassiveScaleTestFlags)
bool FProjectJProjectileAsyncSweepTest::RunTest(const FString&)
{
	auto* Async = IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Projectile.UseAsyncSweep"));
	auto* Threshold = IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Projectile.AsyncSweepThreshold"));
	if (!Async || !Threshold) return false;
	const int32 PreviousAsync = Async->GetInt();
	const int32 PreviousThreshold = Threshold->GetInt();
	ON_SCOPE_EXIT { Async->Set(PreviousAsync, ECVF_SetByCode); Threshold->Set(PreviousThreshold, ECVF_SetByCode); };
	Threshold->Set(1, ECVF_SetByCode);

	for (const int32 Mode : {0, 1})
	{
		Async->Set(Mode, ECVF_SetByCode);
		FMassiveScaleTestWorld Scope;
		Scope.BeginPlay();
		auto* Projectiles = Scope.World->GetSubsystem<UProject_JProjectileManagerSubsystem>();
		AActor* Source = Scope.World->SpawnActor<AActor>();
		AActor* Wall = Scope.World->SpawnActor<AActor>();
		auto* Box = NewObject<UBoxComponent>(Wall);
		Wall->SetRootComponent(Box);
		Box->SetBoxExtent(FVector(5, 40, 40));
		Box->SetWorldLocation(FVector(250, 0, 100));
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->RegisterComponent();
		TArray<TPair<uint64, AActor*>> Hits;
		const auto Launch = [&](float Lane)
		{
			return Projectiles->SpawnProjectile(Source, FVector(0, Lane, 100), FVector(1000, 0, 0), 8.f, 0.35f, 2, 0.f,
				FProjectJOnProjectileHit::CreateLambda([&Hits](uint64 Id, AActor* Target, const FHitResult&) { Hits.Emplace(Id, Target); }));
		};
		const uint64 Cancelled = Launch(0.f);
		const uint64 Continuing = Launch(0.f);
		Scope.Step(0.1f);
		TestTrue(TEXT("Cancellation removes a projectile with an outstanding trace"), Projectiles->CancelProjectile(Cancelled));
		Scope.Step(0.1f);
		Scope.Step(0.1f);
		TestEqual(TEXT("Async delivers on the next world tick, sync delivers immediately"), Hits.Num(), Mode ? 0 : 1);
		Scope.Step(0.1f);
		TestTrue(TEXT("The last valid segment hits the wall even at expiry after a swapped removal"),
			Hits.Num() == 1 && Hits[0].Key == Continuing && Hits[0].Value == Wall);
		TestEqual(TEXT("The wall terminates a piercing projectile"), Projectiles->GetActiveProjectileCount(), 0);

		const uint64 WithSource = Launch(0.f);
		TestTrue(TEXT("Second projectile starts"), WithSource != 0);
		Scope.Step(0.1f);
		Source->Destroy();
		Scope.Step(0.1f);
		TestEqual(TEXT("Destroying the source retires any pending projectile"), Projectiles->GetActiveProjectileCount(), 0);
		TestEqual(TEXT("Retired projectile did not dispatch a hit"), Hits.Num(), 1);
		// Both hits are collected in one tick. Teardown from the first callback retires
		// the rest of that world's delivery batch in both collision modes.
		int32 TeardownHits = 0;
		for (int32 Index = 0; Index < 2; ++Index)
		{
			Projectiles->SpawnProjectile(nullptr, FVector(200, 0, 100), FVector(1000, 0, 0), 8.f, 1.f, 0, 0.f,
				FProjectJOnProjectileHit::CreateLambda([&](uint64, AActor*, const FHitResult&)
				{
					++TeardownHits;
					Scope.World->BeginTearingDown();
				}));
		}
		// Issue the asynchronous traces on a normal world tick. Dispatch the terminal
		// callback outside UWorld::Tick so engine tick functions can retire safely.
		if (Mode)
		{
			Scope.Step(0.1f);
			++GFrameCounter;
		}
		Projectiles->Tick(0.1f);
		TestEqual(TEXT("Only the callback that begins teardown is delivered"), TeardownHits, 1);
		TestEqual(TEXT("World teardown closes admission even for ownerless projectiles"),
			Projectiles->SpawnProjectile(nullptr, FVector::ZeroVector, FVector::ForwardVector, 8.f, 1.f, 0, 0.f, {}), uint64(0));
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMassZeroAllocCrowdTest, "ProjectJ.Mass.ZeroAllocSteadyStateCrowd", MassiveScaleTestFlags)
bool FProjectJMassZeroAllocCrowdTest::RunTest(const FString&)
{
	constexpr int32 EntityCount = 128;
	TArray<FVector> SerialPositions;
	for (const bool bParallel : {false, true})
	{
		auto Manager = CreateMassiveScaleMassManager();
		const UScriptStruct* Fragments[] = {FProjectJMassMovementFragment::StaticStruct()};
		const auto Archetype = Manager->CreateArchetype(Fragments);
		TArray<FMassEntityHandle> Handles;
		Handles.Reserve(EntityCount);
		for (int32 i = 0; i < EntityCount; ++i)
		{
			const auto Handle = Manager->CreateEntity(Archetype);
			Handles.Add(Handle);
			auto& Frag = Manager->GetFragmentDataChecked<FProjectJMassMovementFragment>(Handle);
			Frag.StableId = i + 1;
			Frag.Position = FVector((i % 16) * 140.0f, (i / 16) * 120.0f, 0.0f);
			Frag.Speed = 300.0f + (i % 10) * 10.0f;
			Frag.RouteCount = 2;
			Frag.Route[0] = Frag.Position;
			Frag.Route[1] = Frag.Position + FVector(10000.0f, 0.0f, 0.0f);
			Frag.NextPoint = 1;
			Frag.bMassOwnsMovement = true;
			Frag.bRouteValid = true;
		}

		auto* Processor = NewObject<UProject_JMassMovementProcessor>();
		Processor->AddToRoot();
		Processor->bParallel = bParallel;
		Processor->bUseCrowdSpacing = true;
		Processor->CallInitialize(GetTransientPackage(), Manager);

		for (int32 StepIdx = 0; StepIdx < 12; ++StepIdx)
		{
			UE::Mass::FProcessingContext Context(Manager, 0.05f);
			UE::Mass::Executor::Run(*Processor, Context);
		}

		for (int32 i = 0; i < EntityCount; ++i)
		{
			const auto& Frag = Manager->GetFragmentDataChecked<FProjectJMassMovementFragment>(Handles[i]);
			TestTrue(TEXT("Entity progressed along corridor across reusable scratch ticks"), Frag.Position.X > (i % 16) * 140.0f);
			if (!bParallel)
			{
				SerialPositions.Add(Frag.Position);
			}
			else
			{
				TestTrue(TEXT("Parallel zero-alloc crowd spacing matches serial deterministically"),
					Frag.Position.Equals(SerialPositions[i], 1e-4));
			}
		}

		for (const auto Handle : Handles)
		{
			Manager->DestroyEntity(Handle);
		}
		Processor->RemoveFromRoot();
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJIrisCombatPrioritizerPolicyTest, "ProjectJ.Net.IrisCombatPrioritizerPolicy", MassiveScaleTestFlags)
bool FProjectJIrisCombatPrioritizerPolicyTest::RunTest(const FString&)
{
	const auto* DefaultCfg = GetDefault<UProject_JPlayerMoverPrioritizerConfig>();
	const auto* ActiveCfg = GetDefault<UProject_JCombatActivePrioritizerConfig>();
	const auto* ModeCfg = GetDefault<UProject_JCombatModePrioritizerConfig>();
	const auto* DeadCfg = GetDefault<UProject_JCombatDeadPrioritizerConfig>();
	const auto* BossCfg = GetDefault<UProject_JCombatBossPrioritizerConfig>();
	if (!TestNotNull(TEXT("Default mover prioritizer config exists"), DefaultCfg)
		|| !TestNotNull(TEXT("Active combat prioritizer config exists"), ActiveCfg)
		|| !TestNotNull(TEXT("Combat mode prioritizer config exists"), ModeCfg)
		|| !TestNotNull(TEXT("Dead prioritizer config exists"), DeadCfg)
		|| !TestNotNull(TEXT("Boss prioritizer config exists"), BossCfg))
	{
		return false;
	}

	TestTrue(TEXT("Active combat tier outranks combat mode tier"), ActiveCfg->InnerPriority > ModeCfg->InnerPriority);
	TestTrue(TEXT("Combat mode tier outranks default mover tier"), ModeCfg->InnerPriority > DefaultCfg->InnerPriority);
	TestTrue(TEXT("Default mover tier outranks dead tier"), DefaultCfg->InnerPriority > DeadCfg->InnerPriority);
	TestTrue(TEXT("Boss tier protects outer arena priority"), BossCfg->OuterPriority >= ActiveCfg->OuterPriority && BossCfg->OuterRadius >= ActiveCfg->OuterRadius);
	TestTrue(TEXT("All spatial prioritizers retain owner boost"), DefaultCfg->OwnerPriorityBoost >= 1.0f && ActiveCfg->OwnerPriorityBoost >= 2.0f);

	FMassiveScaleTestWorld Scope;
	AProject_JNPCCharacter* NPC = Scope.World->SpawnActor<AProject_JNPCCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	Scope.BeginPlay();
	if (!TestNotNull(TEXT("NPC spawned"), NPC)) { return false; }

	UAbilitySystemComponent* ASC = NPC->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("NPC ASC initialized"), ASC)) { return false; }
	const FProject_JGameplayTags& Tags = FProject_JGameplayTags::Get();

	TestEqual(TEXT("Idle actor maps to default mover Iris tier"),
		UProject_JNetObjectPrioritizer_Combat::ResolveIrisPrioritizerName(NPC),
		UProject_JNetObjectPrioritizer_Combat::PrioritizerName_DefaultMover);

	ASC->AddLooseGameplayTag(Tags.State_CombatMode);
	TestEqual(TEXT("CombatMode tag maps to CombatMode Iris tier"),
		UProject_JNetObjectPrioritizer_Combat::ResolveIrisPrioritizerName(NPC),
		UProject_JNetObjectPrioritizer_Combat::PrioritizerName_CombatMode);

	ASC->AddLooseGameplayTag(Tags.State_Attacking);
	TestEqual(TEXT("Attacking tag maps to CombatActive Iris tier"),
		UProject_JNetObjectPrioritizer_Combat::ResolveIrisPrioritizerName(NPC),
		UProject_JNetObjectPrioritizer_Combat::PrioritizerName_CombatActive);

	ASC->RemoveLooseGameplayTag(Tags.State_Attacking);
	ASC->AddLooseGameplayTag(Tags.State_Dodging);
	TestEqual(TEXT("Dodging tag maps to CombatActive Iris tier"),
		UProject_JNetObjectPrioritizer_Combat::ResolveIrisPrioritizerName(NPC),
		UProject_JNetObjectPrioritizer_Combat::PrioritizerName_CombatActive);

	auto* BossBrain = NewObject<UProject_JNPCBrainDefinition>(GetTransientPackage());
	BossBrain->bIsGiantBoss = true;
	NPC->BrainDefinition = BossBrain;
	TestEqual(TEXT("Giant boss maps to CombatBoss Iris tier"),
		UProject_JNetObjectPrioritizer_Combat::ResolveIrisPrioritizerName(NPC),
		UProject_JNetObjectPrioritizer_Combat::PrioritizerName_CombatBoss);

	ASC->AddLooseGameplayTag(Tags.State_Dead);
	TestEqual(TEXT("Dead tag overrides to CombatDead Iris tier"),
		UProject_JNetObjectPrioritizer_Combat::ResolveIrisPrioritizerName(NPC),
		UProject_JNetObjectPrioritizer_Combat::PrioritizerName_CombatDead);

	return !HasAnyErrors();
}

#if UE_WITH_IRIS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJIrisCombatPrioritizerRuntimeTest, "ProjectJ.Net.IrisCombatPrioritizerRuntime", MassiveScaleTestFlags)
bool FProjectJIrisCombatPrioritizerRuntimeTest::RunTest(const FString&)
{
	using FPolicy = UProject_JNetObjectPrioritizer_Combat;
	auto& Console = IConsoleManager::Get();
	auto* Enabled = Console.FindConsoleVariable(TEXT("ProjectJ.Net.CombatIrisPrioritizer"));
	if (!TestNotNull(TEXT("Combat Iris CVar exists"), Enabled)) return false;
	const int32 Before = Enabled->GetInt();
	const auto SetEnabled = [&](int32 Value)
	{
		Enabled->Set(Value, ECVF_SetByConsole);
		Console.CallAllConsoleVariableSinks();
	};
	ON_SCOPE_EXIT { SetEnabled(Before); };
	SetEnabled(1);
	FMassiveScaleTestWorld Scope;
	Scope.BeginPlay();
	FURL URL(nullptr, TEXT("/Engine/Maps/Entry?listen"), TRAVEL_Absolute);
	URL.Port = 0;
	if (!TestTrue(TEXT("A native game net driver can listen"), Scope.World->Listen(URL))) return false;
	auto* Driver = Scope.World->GetNetDriver();
	auto* System = Driver ? Driver->GetReplicationSystem() : nullptr;
	if (!TestTrue(TEXT("A real authoritative Iris replication system exists"), System && System->IsServer())) return false;
	auto* Bridge = Cast<UObjectReplicationBridge>(System->GetReplicationBridge());
	if (!TestNotNull(TEXT("The driver provides the Iris object bridge"), Bridge)) return false;
	TestEqual(TEXT("The engine spatial prioritizer still occupies handle zero"),
		System->GetPrioritizerHandle(TEXT("DefaultPrioritizer")), UE::Net::DefaultSpatialNetObjectPrioritizerHandle);
	TSet<uint32> Handles;
	for (const FName Tier : {FPolicy::PrioritizerName_DefaultMover, FPolicy::PrioritizerName_CombatMode,
		FPolicy::PrioritizerName_CombatActive, FPolicy::PrioritizerName_CombatDead, FPolicy::PrioritizerName_CombatBoss})
	{
		TestNotNull(FString::Printf(TEXT("%s was instantiated in the real system"), *Tier.ToString()), System->GetPrioritizer(Tier));
		Handles.Add(static_cast<uint32>(System->GetPrioritizerHandle(Tier)));
	}
	TestEqual(TEXT("All five registered tiers have distinct handles"), Handles.Num(), 5);

	auto* NPC = Scope.World->SpawnActor<AProject_JNPCCharacter>();
	if (!TestNotNull(TEXT("An authority NPC exists"), NPC)) return false;
	NPC->SetReplicates(false);
	for (int32 Frame = 0; Frame < 8; ++Frame) Scope.Step(1.f / 60.f);
	TestEqual(TEXT("Stopping replication clears the applied tier"), NPC->GetAppliedIrisPrioritizerName(), NAME_None);
	NPC->SetReplicates(true);
	TestTrue(TEXT("Late replication starts a real net-ref handle"), Bridge->GetReplicatedRefHandle(NPC).IsCompleteHandle());
	TestEqual(TEXT("The late registration applied the mover tier"), NPC->GetAppliedIrisPrioritizerName(), FPolicy::PrioritizerName_DefaultMover);
	auto* ASC = NPC->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("NPC ASC initialized"), ASC)) return false;
	const auto& Tags = FProject_JGameplayTags::Get();
	ASC->AddLooseGameplayTag(Tags.State_CombatMode);
	TestEqual(TEXT("Iris accepted the CombatMode tier"), NPC->GetAppliedIrisPrioritizerName(), FPolicy::PrioritizerName_CombatMode);
	ASC->AddLooseGameplayTag(Tags.State_Attacking);
	TestEqual(TEXT("Iris accepted the CombatActive tier"), NPC->GetAppliedIrisPrioritizerName(), FPolicy::PrioritizerName_CombatActive);
	SetEnabled(0);
	TestEqual(TEXT("Disabling immediately returns a live actor to the mover tier"), NPC->GetAppliedIrisPrioritizerName(), FPolicy::PrioritizerName_DefaultMover);
	SetEnabled(1);
	TestEqual(TEXT("Re-enabling reapplies the current combat state"), NPC->GetAppliedIrisPrioritizerName(), FPolicy::PrioritizerName_CombatActive);

	auto* MultiPart = NewObject<UProject_JMultiPartHitComponent>(NPC);
	MultiPart->RegisterComponent();
	TestEqual(TEXT("Iris accepted the Boss tier after dynamic registration"), NPC->GetAppliedIrisPrioritizerName(), FPolicy::PrioritizerName_CombatBoss);
	ASC->AddLooseGameplayTag(Tags.State_Dead);
	TestEqual(TEXT("Iris accepted the Dead tier"), NPC->GetAppliedIrisPrioritizerName(), FPolicy::PrioritizerName_CombatDead);
	ASC->RemoveLooseGameplayTag(Tags.State_Dead);
	MultiPart->UnregisterComponent();
	TestEqual(TEXT("Removing the last registered boss hit component restores combat tier"), NPC->GetAppliedIrisPrioritizerName(), FPolicy::PrioritizerName_CombatActive);
	return !HasAnyErrors();
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCoordinatorCacheTopologyTest, "ProjectJ.Animation.CoordinatorCacheTopology", MassiveScaleTestFlags)
bool FProjectJCoordinatorCacheTopologyTest::RunTest(const FString&)
{
	FMassiveScaleTestWorld Scope;
	auto* NPC = Scope.World->SpawnActor<AProject_JNPCCharacter>();
	auto* Mesh = NPC ? Cast<UProject_JBudgetedSkeletalMeshComponent>(NPC->GetMesh()) : nullptr;
	if (!TestNotNull(TEXT("NPC mesh can cache the coordinator"), Mesh)) return false;
	Scope.BeginPlay();
	TestNull(TEXT("NPC has no authored coordinator"), Mesh->GetCachedAnimationUpdateCoordinator());
	TestNull(TEXT("The negative cache stays empty"), Mesh->GetCachedAnimationUpdateCoordinator());
	auto* Coordinator = NewObject<UProject_JAnimationUpdateCoordinatorComponent>(NPC);
	Coordinator->RegisterComponent();
	TestTrue(TEXT("Registration invalidates a previous miss"), Mesh->GetCachedAnimationUpdateCoordinator() == Coordinator);
	Coordinator->UnregisterComponent();
	TestNull(TEXT("Unregistration invalidates a previous hit"), Mesh->GetCachedAnimationUpdateCoordinator());
	Coordinator->RegisterComponent();
	TestTrue(TEXT("Registration revives a cached miss again"), Mesh->GetCachedAnimationUpdateCoordinator() == Coordinator);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMultiPartBinarySearchRewindTest, "ProjectJ.Combat.MultiPartBinarySearchRewind", MassiveScaleTestFlags)
bool FProjectJMultiPartBinarySearchRewindTest::RunTest(const FString&)
{
	FMassiveScaleTestWorld Scope;
	ACharacter* Boss = Scope.World->SpawnActor<ACharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	Boss->GetCharacterMovement()->SetComponentTickEnabled(false);
	auto* MultiPart = NewObject<UProject_JMultiPartHitComponent>(Boss);
	FProject_JBossHitPartSpec CoreSpec;
	CoreSpec.PartName = TEXT("Core");
	CoreSpec.LocalOffset = FVector(0.0f, 0.0f, 100.0f);
	CoreSpec.SphereRadius = 50.0f;
	CoreSpec.DamageMultiplier = 2.0f;
	CoreSpec.bIsWeakpoint = true;
	MultiPart->HitParts.Add(CoreSpec);
	MultiPart->RegisterComponent();
	Scope.BeginPlay();

	// Record 24 frames (exceeds MaxHistoryFrames=18 so ring buffer wraps cleanly).
	for (int32 i = 0; i < 24; ++i)
	{
		const double Timestamp = 1.0 + i * 0.05;
		Boss->SetActorLocation(FVector(i * 100.0f, 0.0f, 0.0f));
		MultiPart->RecordSample(Timestamp);
	}

	// Ring retains frames 6..23 (Timestamps 1.30..2.15, X=600..2300). Query midpoint between frame 12 (1.60, X=1200) and frame 13 (1.65, X=1300).
	FName OutPart = NAME_None;
	float OutMult = 0.0f;
	FVector OutCenter = FVector::ZeroVector;
	TestTrue(TEXT("Binary search resolves interpolated midpoint on wrapped ring buffer"),
		MultiPart->VerifyRewoundSegmentHit(1.625, FVector(1250.0f, -100.0f, 100.0f), FVector(1250.0f, 100.0f, 100.0f), 10.0f, OutPart, OutMult, OutCenter));
	TestEqual(TEXT("Part name matches Core"), OutPart, FName(TEXT("Core")));
	TestTrue(TEXT("Multiplier matches 2.0"), FMath::IsNearlyEqual(OutMult, 2.0f));
	TestTrue(TEXT("Interpolated X center matches 1250cm"), FMath::IsNearlyEqual(OutCenter.X, 1250.0, 2.0));
	return !HasAnyErrors();
}
#endif
