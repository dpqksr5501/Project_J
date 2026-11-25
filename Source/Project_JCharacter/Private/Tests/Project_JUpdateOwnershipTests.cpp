#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/Project_JAnimationQualityPolicy.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "Combat/Project_JServerSideRewindComponent.h"
#include "Components/Project_JAnimationUpdateCoordinatorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/Project_JModularMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerState.h"
#include "Misc/ScopeExit.h"
#include "Mount/Project_JMountMoverComponent.h"
#include "Project_JSpellbladeCharacter.h"
#include "System/Project_JCharacterAnimationBudgetSubsystem.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"
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

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJRemoteMontageTimingTest, "ProjectJ.Animation.RemoteMontageTiming", UpdateTestFlags)
bool FProjectJRemoteMontageTimingTest::RunTest(const FString&)
{
	auto* World = UWorld::CreateWorld(EWorldType::PIE, false);
	GEngine->CreateNewWorldContext(EWorldType::PIE).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::LevelTransition); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* PlayerClass = LoadClass<AProject_JPlayerCharacter>(nullptr,
		TEXT("/Game/Character_BPs/Spellblade/BP_Spellblade.BP_Spellblade_C"));
	auto* Attack = LoadObject<UAnimMontage>(nullptr, TEXT("/Game/Anim_Assets/Spellblade/Animations/Sword/Montage/CombatKit/CombatRevision/AM_SpellbladeKit_BasicCut01.AM_SpellbladeKit_BasicCut01"));
	if (!TestNotNull(TEXT("Saved Spellblade loads"), PlayerClass) || !TestNotNull(TEXT("Saved attack montage loads"), Attack)) return false;
	auto* Player = World->SpawnActor<AProject_JPlayerCharacter>(PlayerClass);
	if (!TestNotNull(TEXT("Spellblade fixture spawns"), Player)) return false;
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	World->GetWorldSettings()->NotifyMatchStarted();
	auto* Mesh = CastChecked<UProject_JBudgetedSkeletalMeshComponent>(Player->GetMesh());
	auto* Anim = Mesh->GetAnimInstance();
	auto* Coordinator = Player->FindComponentByClass<UProject_JAnimationUpdateCoordinatorComponent>();
	auto* Budget = World->GetSubsystem<UProject_JCharacterAnimationBudgetSubsystem>();
	auto* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Project_J.Anim.RemoteProxyMontageTimingOnly"));
	if (!TestNotNull(TEXT("Authored master animation instance"), Anim) ||
		!TestNotNull(TEXT("Animation coordinator"), Coordinator) || !TestNotNull(TEXT("Animation budget subsystem"), Budget) ||
		!TestNotNull(TEXT("Montage timing switch"), CVar)) return false;
	const int32 PreviousCVar = CVar->GetInt();
	ON_SCOPE_EXIT { CVar->Set(PreviousCVar, ECVF_SetByConsole); };
	CVar->Set(1, ECVF_SetByConsole);
	using enum EVisibilityBasedAnimTickOption;
	Budget->SetEnabledOverride(false);
	AddInfo(FString::Printf(TEXT("Saved Spellblade source policy=%d visible=%d hidden=%d"), int32(Mesh->VisibilityBasedAnimTickOption),
		Mesh->IsVisible(), Mesh->bHiddenInGame));
	// Model an authored visible source independently of the actual Blueprint's
	// render-part visibility and full-pose policy.
	Mesh->SetVisibility(true);
	Mesh->SetHiddenInGame(false);
	Mesh->bAllowProjectBudget = true;
	Mesh->VisibilityBasedAnimTickOption = OnlyTickPoseWhenRendered;
	if (auto* LocalController = Player->GetController()) LocalController->UnPossess();
	World->SetPlayInEditorInitialNetMode(NM_Client);
	Player->SetRole(ROLE_SimulatedProxy);
	Mesh->SetLastRenderTime(-100.f);
	TestEqual(TEXT("PIE runs as a client"), Player->GetNetMode(), NM_Client);
	TestFalse(TEXT("Client simulated player is not locally controlled"), Player->IsLocallyControlled());
	if (!TestTrue(TEXT("A real compatible attack montage starts"), Anim->Montage_Play(Attack) > 0.f)) return false;
	TestEqual(TEXT("A simulated client player on a visibility baseline leases montage timing"),
		Mesh->VisibilityBasedAnimTickOption, OnlyTickMontagesWhenNotRendered);
	Coordinator->RequestUrgentRemoteAnimationUpdate(.5f);
	TestEqual(TEXT("Urgent presentation composes with montage timing"), Mesh->VisibilityBasedAnimTickOption, OnlyTickMontagesWhenNotRendered);

	const auto Step = [&]()
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 1.f / 60.f);
	};
	const float Start = Anim->Montage_GetPosition(Attack);
	Step();
	TestTrue(TEXT("An off-screen attack advances through the engine montage tick"), Anim->Montage_GetPosition(Attack) > Start);
	Mesh->SetLastRenderTime(World->GetTimeSeconds());
	Step();
	TestTrue(TEXT("A rendered source resumes skinned transform updates"), Mesh->ShouldUpdateTransform(false));
	Mesh->SetHiddenInGame(true);
	Budget->Refresh();
	TestEqual(TEXT("A hidden source that could drive a visible follower keeps full pose"), Mesh->VisibilityBasedAnimTickOption, AlwaysTickPoseAndRefreshBones);
	Mesh->SetHiddenInGame(false);
	Budget->Refresh();
	TestEqual(TEXT("Returning to direct visibility restores montage timing"), Mesh->VisibilityBasedAnimTickOption, OnlyTickMontagesWhenNotRendered);
	CVar->Set(0, ECVF_SetByConsole);
	Budget->Refresh();
	TestEqual(TEXT("Live switch disable restores full protected pose"), Mesh->VisibilityBasedAnimTickOption, AlwaysTickPoseAndRefreshBones);
	CVar->Set(1, ECVF_SetByConsole);

	Player->SetRole(ROLE_AutonomousProxy);
	Budget->Refresh();
	TestEqual(TEXT("An autonomous player keeps full pose"), Mesh->VisibilityBasedAnimTickOption, AlwaysTickPoseAndRefreshBones);
	Player->SetRole(ROLE_SimulatedProxy);
	World->SetPlayInEditorInitialNetMode(NM_ListenServer);
	Budget->Refresh();
	TestEqual(TEXT("The listen server keeps full pose"), Mesh->VisibilityBasedAnimTickOption, AlwaysTickPoseAndRefreshBones);
	World->SetPlayInEditorInitialNetMode(NM_Client);
	Budget->Refresh();
	Anim->Montage_Stop(0.f, Attack);
	for (int32 Frame = 0; Frame < 4; ++Frame) Step();
	Budget->Refresh();
	TestEqual(TEXT("Ending the last montage restores the exact visibility option"),
		Mesh->VisibilityBasedAnimTickOption, OnlyTickPoseWhenRendered);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJHiddenLeaderMontageTimingTest, "ProjectJ.Animation.HiddenLeaderMontageTiming", UpdateTestFlags)
bool FProjectJHiddenLeaderMontageTimingTest::RunTest(const FString&)
{
	auto* World = UWorld::CreateWorld(EWorldType::PIE, false);
	GEngine->CreateNewWorldContext(EWorldType::PIE).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::LevelTransition); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* PlayerClass = LoadClass<AProject_JPlayerCharacter>(nullptr,
		TEXT("/Game/Character_BPs/Spellblade/BP_Spellblade.BP_Spellblade_C"));
	auto* Attack = LoadObject<UAnimMontage>(nullptr, TEXT("/Game/Anim_Assets/Spellblade/Animations/Sword/Montage/CombatKit/CombatRevision/AM_SpellbladeKit_BasicCut01.AM_SpellbladeKit_BasicCut01"));
	if (!TestNotNull(TEXT("Saved Spellblade"), PlayerClass) || !TestNotNull(TEXT("Saved montage"), Attack)) return false;
	auto* Player = World->SpawnActor<AProject_JPlayerCharacter>(PlayerClass);
	if (!TestNotNull(TEXT("Spellblade instance"), Player)) return false;
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	World->GetWorldSettings()->NotifyMatchStarted();
	if (auto* Controller = Player->GetController()) Controller->UnPossess();
	World->SetPlayInEditorInitialNetMode(NM_Client);
	Player->SetRole(ROLE_SimulatedProxy);
	auto* Mesh = CastChecked<UProject_JBudgetedSkeletalMeshComponent>(Player->GetMesh());
	auto* Anim = Mesh->GetAnimInstance();
	auto* Budget = World->GetSubsystem<UProject_JCharacterAnimationBudgetSubsystem>();
	auto* Coordinator = Player->FindComponentByClass<UProject_JAnimationUpdateCoordinatorComponent>();
	auto* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Project_J.Anim.RemoteProxyMontageTimingOnly"));
	if (!TestNotNull(TEXT("Animation instance"), Anim) || !TestNotNull(TEXT("Budget subsystem"), Budget)
		|| !TestNotNull(TEXT("Coordinator"), Coordinator) || !TestNotNull(TEXT("Feature switch"), CVar)) return false;
	const int32 PreviousSwitch = CVar->GetInt();
	ON_SCOPE_EXIT { CVar->Set(PreviousSwitch, ECVF_SetByConsole); };
	CVar->Set(1, ECVF_SetByConsole);
	Budget->SetEnabledOverride(false);
	using enum EVisibilityBasedAnimTickOption;
	TestTrue(TEXT("The actual saved leader is hidden"), Mesh->bHiddenInGame && !Mesh->IsVisible());
	TestEqual(TEXT("The actual saved source drives full pose by default"), Mesh->VisibilityBasedAnimTickOption, AlwaysTickPoseAndRefreshBones);
	const ETickingGroup InitialTickGroup = Mesh->PrimaryComponentTick.TickGroup.GetValue();
	UProject_JModularMeshComponent* Follower = nullptr;
	for (auto* Component : Player->GetComponents())
	{
		auto* Part = Cast<UProject_JModularMeshComponent>(Component);
		if (Part && Part->LeaderPoseComponent.Get() == Mesh && Part->IsVisible() && !Part->bHiddenInGame && Part->GetSkinnedAsset())
		{
			Follower = Part;
			break;
		}
	}
	if (!TestNotNull(TEXT("An authored visible modular part follows the hidden leader"), Follower)
		|| !TestTrue(TEXT("A real attack starts"), Anim->Montage_Play(Attack) > 0.f)) return false;
	TestEqual(TEXT("Remote hidden leader observes the post-camera tick"), Mesh->PrimaryComponentTick.TickGroup.GetValue(), TG_PostUpdateWork);

	const auto Step = [&](bool bInView, bool bProvideView = true)
	{
		++GFrameCounter;
		Budget->MontageViewsFrame = GFrameCounter;
		Budget->MontageViews.Reset();
		if (bProvideView)
		{
			auto& Volume = Budget->MontageViews.AddDefaulted_GetRef();
			// A deterministic viewport substitute for the late game tick. This
			// does not modify the Blueprint's render, tick or LeaderPose flags.
			Volume.Planes.Add(FPlane(1., 0., 0., bInView ? 1.e8 : -1.e8));
			Volume.Init();
		}
		World->Tick(LEVELTICK_All, 1.f / 60.f);
	};

	Step(false, false);
	TestEqual(TEXT("Missing view keeps full pose"), Mesh->VisibilityBasedAnimTickOption, AlwaysTickPoseAndRefreshBones);
	const float Before = Anim->Montage_GetPosition(Attack);
	Step(false);
	TestEqual(TEXT("All authored visible parts offscreen allow montage timing"), Mesh->VisibilityBasedAnimTickOption, OnlyTickMontagesWhenNotRendered);
	TestTrue(TEXT("Native montage progresses with the leader hidden"), Anim->Montage_GetPosition(Attack) > Before);
	TestFalse(TEXT("The hidden leader skips bone transforms offscreen"), Mesh->ShouldUpdateTransform(false));
	Step(true);
	TestEqual(TEXT("View entry restores full pose in that mesh tick"), Mesh->VisibilityBasedAnimTickOption, AlwaysTickPoseAndRefreshBones);
	TestTrue(TEXT("Transforms recompute on view entry"), Mesh->ShouldUpdateTransform(false));
	Follower->SetLastRenderTime(World->GetTimeSeconds());
	Step(false);
	TestEqual(TEXT("One recently drawn follower keeps full pose"), Mesh->VisibilityBasedAnimTickOption, AlwaysTickPoseAndRefreshBones);
	Follower->SetLastRenderTime(-100.f);
	Step(false);
	Coordinator->RequestUrgentRemoteAnimationUpdate(.5f);
	TestEqual(TEXT("Urgent publication immediately restores full pose"), Mesh->VisibilityBasedAnimTickOption, AlwaysTickPoseAndRefreshBones);
	Anim->Montage_Stop(0.f, Attack);
	for (int32 Frame = 0; Frame < 4; ++Frame) Step(false);
	TestEqual(TEXT("Ending the montage restores the saved bone policy"), Mesh->VisibilityBasedAnimTickOption, AlwaysTickPoseAndRefreshBones);
	TestEqual(TEXT("Ending the montage restores the original tick group"), Mesh->PrimaryComponentTick.TickGroup.GetValue(), InitialTickGroup);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAuditMountedClothTest, "ProjectJ.AuditFollowup.RemoteMoverRiderCloth", UpdateTestFlags)
bool FProjectJAuditMountedClothTest::RunTest(const FString&)
{
	auto* World = UWorld::CreateWorld(EWorldType::PIE, false);
	GEngine->CreateNewWorldContext(EWorldType::PIE).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::LevelTransition); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Mount = World->SpawnActor<AProject_JMountMoverPawn>(FVector(250, 0, 100), FRotator::ZeroRotator);
	auto* Rider = World->SpawnActor<AProject_JSpellbladeCharacter>(FVector(0, 0, 100), FRotator::ZeroRotator);
	auto* State = World->SpawnActor<APlayerState>();
	auto* ReplicatedRider = FindFProperty<FObjectPropertyBase>(AProject_JMountMoverPawn::StaticClass(), TEXT("Rider"));
	if (!Mount || !Rider || !State || !ReplicatedRider) return false;
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	World->SetPlayInEditorInitialNetMode(NM_Client);
	Mount->SetRole(ROLE_SimulatedProxy);
	Rider->SetRole(ROLE_SimulatedProxy);
	Mount->SetPlayerState(State);
	ReplicatedRider->SetObjectPropertyValue_InContainer(Mount, Rider);
	TestTrue(TEXT("Rider attaches to the Mover mount"), Rider->AttachToActor(Mount, FAttachmentTransformRules::KeepWorldTransform));
	TestEqual(TEXT("PIE fixture models a simulated remote client"), Rider->GetNetMode(), NM_Client);
	TestFalse(TEXT("A settled remote Mover rider is eligible for cloth budgeting"), ProjectJ::Animation::IsClothExecutionProtected(Rider));
	ReplicatedRider->SetObjectPropertyValue_InContainer(Mount, nullptr);
	TestTrue(TEXT("An unsettled rider still receives the ownership protection"), ProjectJ::Animation::IsClothExecutionProtected(Rider));
	return !HasAnyErrors();
}
#endif

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
	auto* Owner = Scope.World->SpawnActor<AProject_JSpellbladeCharacter>();
	if (!TestNotNull(TEXT("Current player pawn spawned"), Owner)) return false;
	auto* Coordinator = Owner->FindComponentByClass<UProject_JAnimationUpdateCoordinatorComponent>();
	auto* Mesh = Cast<UProject_JBudgetedSkeletalMeshComponent>(Owner->GetMesh());
	if (!TestNotNull(TEXT("Current pawn owns the coordinator"), Coordinator) ||
		!TestNotNull(TEXT("Current pawn uses the budgeted skeletal mesh"), Mesh)) return false;
	Mesh->bAllowProjectBudget = false;
	Scope.BeginPlay();
	Owner->SetRole(ROLE_SimulatedProxy);
	TestEqual(TEXT("Urgent-window fixture models a simulated proxy"), Owner->GetLocalRole(), ROLE_SimulatedProxy);
	Mesh->bEnableUpdateRateOptimizations = true;
	Coordinator->RequestUrgentRemoteAnimationUpdate(0.5f);
	TestFalse(TEXT("Urgent presentation temporarily bypasses URO"), Mesh->bEnableUpdateRateOptimizations);
	Scope.Step(0.1f);
	Coordinator->RequestUrgentRemoteAnimationUpdate(0.01f);
	Coordinator->RequestUrgentRemoteAnimationUpdate(0.0f);
	Coordinator->RequestUrgentRemoteAnimationUpdate(-1.0f);
	Coordinator->RequestUrgentRemoteAnimationUpdate(std::numeric_limits<float>::quiet_NaN());
	Coordinator->RequestUrgentRemoteAnimationUpdate(std::numeric_limits<float>::infinity());
	TestTrue(TEXT("Short and invalid requests cannot shorten the active window"),
		Scope.World->GetTimerManager().GetTimerRemaining(Coordinator->RestoreAnimationUpdateRateTimer) >= 0.39f);
	for (int32 Frame = 0; Frame < 8; ++Frame) { Scope.Step(0.1f); }
	TestTrue(TEXT("The original mesh policy is restored on expiry"), Mesh->bEnableUpdateRateOptimizations);
	Mesh->bEnableUpdateRateOptimizations = false;
	Coordinator->RequestUrgentRemoteAnimationUpdate(0.1f);
	for (int32 Frame = 0; Frame < 3; ++Frame) { Scope.Step(0.1f); }
	TestFalse(TEXT("An originally disabled URO policy stays disabled"), Mesh->bEnableUpdateRateOptimizations);
	Mesh->bEnableUpdateRateOptimizations = true;
	Coordinator->RequestUrgentRemoteAnimationUpdate(1.0f);
	Coordinator->DestroyComponent();
	TestTrue(TEXT("Component teardown releases its override immediately"), Mesh->bEnableUpdateRateOptimizations);
	return !HasAnyErrors();
}
#endif
