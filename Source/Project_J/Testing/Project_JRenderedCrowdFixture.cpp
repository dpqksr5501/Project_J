#include "Testing/Project_JRenderedCrowdFixture.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Movement/Project_JMovementQueries.h"
#include "Movement/Project_JMoverComponent.h"
#include "Movement/Project_JMoverLifecycle.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "DefaultMovementSet/Modes/SmoothWalkingMode.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/CollisionProfile.h"
#include "Net/Iris/ReplicationSystem/ReplicationSystemUtil.h"
#include "Net/Iris/ReplicationSystem/EngineReplicationBridge.h"
#include "Iris/ReplicationSystem/NetRefHandle.h"
#include "Testing/Project_JAuthoredAnimationFixture.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Equipment/Project_JWeaponPresentationProfile.h"
#include "Animation/Project_JHandContact.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JPlayerAnimInstance.h"
#include "Components/Project_JAnimationUpdateCoordinatorComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "DynamicRHI.h"
#include "RenderTimer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "UnrealClient.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Components/Project_JModularMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "ClothingSimulationInstance.h"
#include "ClothingSimulationInteractor.h"
#include "SkeletalRenderPublic.h"
#if !WITH_EDITORONLY_DATA
#include "Rendering/SkeletalMeshRenderData.h"
#endif
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Equipment/Project_JWeaponPresentationActor.h"
#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "System/Project_JVisualAssetSubsystem.h"
#include "System/Project_JPresentationBudgetSubsystem.h"
#include "System/Project_JNPCDecisionSubsystem.h"
#include "System/Project_JTargetScoringSubsystem.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#if WITH_EDITOR
#include "Chooser.h"
#include "ObjectChooser_Asset.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimClassInterface.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/Chooser/PoseSearchChooserColumn.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#endif
#include "UObject/StrongObjectPtr.h"

void UProject_JRenderedCrowdFixture::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if !UE_BUILD_SHIPPING
	bNetworkObserver = FParse::Param(FCommandLine::Get(), TEXT("ProjectJRenderedClientObserver"));
    const bool RequestedLeaderBoundsProof=FParse::Param(FCommandLine::Get(),TEXT("ProjectJRenderedCrowdLeaderBoundsProof"));
    bSharedPhysicsBoundsProof=FParse::Param(FCommandLine::Get(),TEXT("ProjectJRenderedCrowdSharedPhysicsBoundsProof"));
    bLeaderBoundsProof=RequestedLeaderBoundsProof || bSharedPhysicsBoundsProof;
    if (!bNetworkObserver && !bLeaderBoundsProof && !FParse::Param(FCommandLine::Get(), TEXT("ProjectJRenderedCrowdFixture"))) { return; }
	Started = FPlatformTime::Seconds();
	bWithLocalPlayer = FParse::Param(FCommandLine::Get(), TEXT("ProjectJRenderedCrowdLocalPlayer"));
	bSimulateMovement = FParse::Param(FCommandLine::Get(), TEXT("ProjectJRenderedCrowdMovement"));
 bTrajectoryMetrics = FParse::Param(FCommandLine::Get(), TEXT("ProjectJRenderedCrowdTrajectoryMetrics"));
	bNPCMetrics = FParse::Param(FCommandLine::Get(), TEXT("ProjectJRenderedCrowdNPCMetrics"));
	bRequirePlayerPoseReadiness=FParse::Param(FCommandLine::Get(),TEXT("ProjectJRenderedCrowdPlayerPoseReadiness"));
	FParse::Value(FCommandLine::Get(),TEXT("ProjectJRenderedCrowdNPCCombatCount="),NPCCombatCount);
	bNPCAuthoredPath=FParse::Param(FCommandLine::Get(),TEXT("ProjectJRenderedCrowdNPCAuthoredPath"));
	bNPCMetrics |= NPCCombatCount>0;
	FXResourceCount=FParse::Param(FCommandLine::Get(),TEXT("ProjectJRenderedCrowdMixedFX"))?132:0;
	if (bNPCMetrics)
	{
		NPCDecisionRows = TEXT("phase,frame_id,elapsed_s,phase_game_s,world_time_s,agents,targets,actions,outstanding,core_pending,launched_root_tasks,")
			TEXT("submitted_batches,submitted_decisions,applied_decisions,discarded_decisions,rejected_batches,capacity_deferrals,")
			TEXT("npc_full_tick_ms,npc_collect_apply_ms,npc_shared_capture_ms,npc_action_ms,oldest_ready_capture_age_ms,")
			TEXT("agent_visits,candidate_visits,snapshot_values,result_visits,target_reads,snapshot_builds,cells,linear_fallbacks,observers,observer_candidate_visits,importance_updates,promotions,incomplete_observers,action_visits,action_updates,")
			TEXT("completed_batches,completed_compute_us,completed_core_delivery_ms\n");
	}
	bClothExecutionExperiment=FParse::Param(FCommandLine::Get(),TEXT("ProjectJRenderedCrowdClothExecutionExperiment"));
	bCloseView = FParse::Param(FCommandLine::Get(), TEXT("ProjectJRenderedCrowdCloseView"));
	bSecondaryMotionBudgetExperiment = FParse::Param(FCommandLine::Get(), TEXT("ProjectJRenderedCrowdSecondaryMotionBudgetExperiment"));
	bCanonicalProof = FParse::Param(FCommandLine::Get(), TEXT("ProjectJRenderedCrowdCanonicalProof"));
	FParse::Value(FCommandLine::Get(), TEXT("ProjectJRenderedCrowdCount="), CrowdCount);
	CrowdCount = FMath::Clamp(CrowdCount, 1, 200);
	if (bNetworkObserver) { CrowdCount = 200; bWithLocalPlayer = true; bSimulateMovement = true; }
	FParse::Value(FCommandLine::Get(), TEXT("ProjectJRenderedCrowdNanite="), ExpectedNaniteMode);
	FParse::Value(FCommandLine::Get(), TEXT("ProjectJRenderedCrowdWeaponVisibility="), WeaponVisibility);
	FParse::Value(FCommandLine::Get(), TEXT("ProjectJRenderedCrowdWeaponMesh="), ExpectedWeaponMesh);
	FParse::Value(FCommandLine::Get(), TEXT("ProjectJRenderedCrowdOutput="), Output);
	if (Output.IsEmpty()) { Output = FPaths::ProjectSavedDir() / TEXT("Validation/RenderedCrowd"); }
	IFileManager::Get().MakeDirectory(*Output, true);
	if (bRequirePlayerPoseReadiness)
	{
#if !WITH_EDITOR
		Finish(false,TEXT("Player PoseSearch readiness requires an Editor host; cooked Game mode is unsupported")); return;
#else
		if (bNetworkObserver) { Finish(false,TEXT("Player pose readiness opt-in does not alter the remote observer fixture")); return; }
		if (IFileManager::Get().FileExists(*(Output/TEXT("player-pose-readiness.json")))) { bFinished=true; UE_LOG(LogTemp,Error,TEXT("Preserve existing player-pose-readiness.json; use a fresh output")); FPlatformMisc::RequestExit(false); return; }
#endif
	}
    if (bLeaderBoundsProof)
    {
        if(RequestedLeaderBoundsProof && bSharedPhysicsBoundsProof)
        { Finish(false,TEXT("Leader and shared-physics bounds proof modes are mutually exclusive")); return; }
#if !WITH_EDITOR
        Finish(false,TEXT("Leader bounds proof requires an Editor host")); return;
#else
        if (bNetworkObserver || CrowdCount!=1 || !bSimulateMovement || !bCanonicalProof || !bRequirePlayerPoseReadiness ||
            bSecondaryMotionBudgetExperiment || bClothExecutionExperiment ||
            !FParse::Param(FCommandLine::Get(),TEXT("ProjectJRenderedCrowdFixture")))
        { Finish(false,TEXT("Leader bounds proof requires standalone Count1 Movement CanonicalProof PlayerPoseReadiness without quality experiments")); return; }
#endif
    }
	if (bNPCAuthoredPath && !NPCCombatCount) { Finish(false,TEXT("Authored NPC path requires the actual NPC combat cohort")); return; }
	if (NPCCombatCount<0 || NPCCombatCount>32 || (NPCCombatCount>0 && (!bSimulateMovement || bNetworkObserver || bLeaderBoundsProof || bCloseView || bSecondaryMotionBudgetExperiment || bClothExecutionExperiment)))
	{ Finish(false,TEXT("NPC combat cohort requires standalone Movement, Count0..32, without incompatible quality/proof/view modes")); return; }
	if (FXResourceCount && (CrowdCount!=200 || NPCCombatCount!=32 || !bWithLocalPlayer || !bSimulateMovement || bNetworkObserver || bLeaderBoundsProof || bCloseView || bCanonicalProof || bSecondaryMotionBudgetExperiment || bClothExecutionExperiment || ExpectedNaniteMode>=0 || WeaponVisibility>=0 || !ExpectedWeaponMesh.IsEmpty()))
	{ Finish(false,TEXT("Mixed FX requires the unchanged standalone 200-player/32-NPC local movement workload and ordinary quality")); return; }
	if(bCloseView && CrowdCount!=1){Finish(false,TEXT("Close-view quality review requires exactly one actor"));return;}
	if (ExpectedNaniteMode < -1 || ExpectedNaniteMode > 1)
	{
		Finish(false, TEXT("Nanite expectation must be -1, 0 or 1")); return;
	}
	if (WeaponVisibility < -1 || WeaponVisibility > 1)
	{
		Finish(false, TEXT("Weapon visibility must be -1, 0 or 1")); return;
	}
    if (bLeaderBoundsProof) StartLeaderBoundsProof();
	Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Tick));
#endif
}
bool UProject_JRenderedCrowdFixture::Setup()
{
	auto* World = GetWorld();
	if (GUsingNullRHI || World->GetNetMode() != NM_Standalone) { return false; }
	FActorSpawnParameters Params; Params.ObjectFlags |= RF_Transient;
	if (bSimulateMovement)
	{
		auto* Floor=World->SpawnActor<AStaticMeshActor>(FVector(1710,900,-50),FRotator::ZeroRotator,Params);
		auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
		if(!Floor || !Cube) return false;
		Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Floor->GetStaticMeshComponent()->SetStaticMesh(Cube);Floor->SetActorScale3D(FVector(150,150,1));
		Floor->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
	for (int32 I = 0; I < CrowdCount; ++I)
	{
		auto* Controller = World->SpawnActor<AAIController>(Params);
		auto* State = World->SpawnActor<AProject_JPlayerState>(Params);
		if (!Controller || !State) { return false; }
		Controller->SetPlayerState(State); State->SetOwner(Controller);
		auto* Player = ProjectJAuthoredAnimationFixture::Spawn(World, Controller, State, FVector((I % 20) * 180, (I / 20) * 200, 100), bSimulateMovement);
		if (!Player) { return false; }
		if (!bSimulateMovement) { Controller->UnPossess(); Player->SetPlayerState(State); }
		Player->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
		if (I == 0 && bWithLocalPlayer)
		{
			// Keep the existing controlled-kinematics fixture, but exercise actual
			// local ownership in the same 200-body scene rather than faking a role.
			auto* Local = World->GetFirstPlayerController();
			if (!Local) return false;
			Controller->SetPlayerState(nullptr);
			State->SetOwner(Local); Local->SetPlayerState(State); Local->Possess(Player);
		}
		CastChecked<UProject_JAbilitySystemComponent>(Player->GetAbilitySystemComponent())->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
		Players.Add(Player);
		RegisterObservedPlayer(Player, I);
	}
	World->SpawnActor<ADirectionalLight>(FVector(0, 0, 500), FRotator(-40, -45, 0));
	const FVector Center=bCloseView?Players[0]->GetActorLocation():FVector(1710,900,100);
	const FVector Position=bCloseView?Center+FVector(450,-450,220):FVector(1710,-3000,3200);
	auto* Camera = World->SpawnActor<ACameraActor>(Position, (Center - Position).Rotation());
	if (!Camera) { return false; }
	Camera->GetCameraComponent()->SetFieldOfView(65);
	World->GetFirstPlayerController()->SetViewTarget(Camera);
	if(bCloseView) ReviewCamera=Camera;
	FString NPCFailure;
	if (!PrepareAsyncScreenshots(NPCFailure)) { Finish(false,*NPCFailure); return false; }
	if (!SetupFXResourceCohort(NPCFailure)) { Finish(false,*NPCFailure); return false; }
	if (!SetupNPCCombatCohort(NPCFailure)) { Finish(false,*NPCFailure); return false; }
	return true;
}
void UProject_JRenderedCrowdFixture::RestoreClothExecutionOverrides()
{
	const auto Previous=MoveTemp(ClothExecutionOriginals);
	ClothExecutionOriginals.Reset();
	if(Previous.IsEmpty()) return;
	int32 Restored=0,Missing=0,Mismatches=0;
	for(const auto& Original:Previous)
	{
		if(auto* Part=Original.Mesh.Get();IsValid(Part))
		{
			Part->SetClothExecutionBudgetAllowed(Original.bAllowed);
			++Restored;
			Mismatches+=Part->IsClothExecutionBudgetAllowed()!=Original.bAllowed ||
				Part->ClothBlendWeight!=Original.Blend || Part->IsClothingSimulationSuspended()!=Original.bSuspended;
		}
		else ++Missing;
	}
	FFileHelper::SaveStringToFile(FString::Printf(TEXT("{\"parts\":%d,\"restored\":%d,\"missing\":%d,\"mismatches\":%d}"),
		Previous.Num(),Restored,Missing,Mismatches),*(Output/TEXT("cloth-execution-restore.json")));
}
void UProject_JRenderedCrowdFixture::RegisterObservedPlayer(AProject_JPlayerCharacter* Player, int32 Index)
{
	if(bClothExecutionExperiment)
	{
		TInlineComponentArray<UProject_JModularMeshComponent*> Parts(Player);
		for(auto* Part:Parts) if(Part->GetSkeletalMeshAsset() && Part->GetSkeletalMeshAsset()->HasActiveClothingAssets())
		{
			FClothExecutionOriginal Original;
			Original.Mesh=Part; Original.Blend=Part->ClothBlendWeight;
			Original.bAllowed=Part->IsClothExecutionBudgetAllowed();
			Original.bSuspended=Part->IsClothingSimulationSuspended(); Original.bLocal=bWithLocalPlayer && Index==0;
			ClothExecutionOriginals.Add(Original);
			Part->SetClothExecutionBudgetAllowed(true); // Transient fixture opt-in in both OFF/ON arms.
		}
	}
	// A separate canonical proof cohort fixes source pose admission, so a
	// faster cosmetic solver cannot alter adaptive ABA scheduling between runs.
	if (bCanonicalProof) if (auto* Source = Cast<UProject_JBudgetedSkeletalMeshComponent>(Player->GetMesh()))
		Source->RequestAnimationUpdate(this, EProject_JAnimationUpdateRequirement::GameplayPose);
	// Record the actual pose writers and Leader Pose consumers in both routes.
	// An AnimInstance class name alone cannot identify a direct body post process.
	TInlineComponentArray<USkeletalMeshComponent*> PoseMeshes(Player);
	for (const auto* Mesh : PoseMeshes)
	{
		const auto* Asset=Mesh->GetSkeletalMeshAsset();
		const auto* Main=Mesh->GetAnimInstance();
		const auto* Post=Mesh->GetPostProcessInstance();
		const bool bModular=Mesh->IsA<UProject_JModularMeshComponent>();
		ExpectedModularParts+=bModular;
        const auto* Leader=Mesh->LeaderPoseComponent.Get();
        // Registration-time metadata only. Keep the original eight route fields;
        // UObject names/asset paths use the existing unquoted, comma-free CSV contract.
        // HasActiveClothingAssets covers any bound LOD, not current-LOD execution.
        PoseRoutes+=FString::Printf(TEXT("%d,%s,%s,%s,%s,%s,%d,%d,%s,%.9g,%d,%d,%d,%s,%s,%s,%s,%.9g,%d,%d,%d\n"),Index,*Mesh->GetName(),
            *GetPathNameSafe(Asset),*GetPathNameSafe(Main?Main->GetClass():nullptr),
            *GetPathNameSafe(Post?Post->GetClass():nullptr),*GetNameSafe(Mesh->LeaderPoseComponent.Get()),
            Asset?Asset->GetRefSkeleton().GetNum():0,bModular,
            *GetPathNameSafe(Mesh->GetClass()),Mesh->BoundsScale,
            Mesh->bUseBoundsFromLeaderPoseComponent?1:0,Mesh->bUseAttachParentBound?1:0,Mesh->bComponentUseFixedSkelBounds?1:0,
            *GetPathNameSafe(Mesh->GetPhysicsAsset()),*GetPathNameSafe(Leader?Leader->GetSkinnedAsset():nullptr),
            *GetPathNameSafe(Leader?Leader->GetPhysicsAsset():nullptr),*GetPathNameSafe(Leader?Leader->GetClass():nullptr),
            Leader?Leader->BoundsScale:0.f,Leader && Leader->bComponentUseFixedSkelBounds?1:0,
            Mesh->GetRelativeTransform().Equals(FTransform::Identity)?1:0,Asset && Asset->HasActiveClothingAssets()?1:0);
	}
	PreviousPositions.Add(Player->GetActorLocation());TravelDistances.Add(0.);Reversals.Add(0);Landings.Add(0);WasInAir.Add(false);LastDirection.Add(0.f);
}

bool UProject_JRenderedCrowdFixture::Tick(float Delta)
{
	if (bFinished) { return false; }
	const double Now = FPlatformTime::Seconds();
	if (Now - Started > (ExpectedNaniteMode >= 0 ? 600 : 180)) { Finish(false, TEXT("Bounded rendered fixture timeout")); return false; }
	auto* World = GetWorld();
	if (!World || !World->HasBegunPlay() || !World->GetFirstPlayerController()) { return true; }
	bool ScreenshotReady=false; FString ScreenshotError;
	if (!PollAsyncScreenshot(ScreenshotReady,ScreenshotError)) { Finish(false,*ScreenshotError); return false; }
	if (bNetworkObserver)
	{
		if (RequestedNetworkPhase == 0) return true;
		if (!NetworkObserver.IsValid() || NetworkObserver->GetWorld() != World || World->GetNetMode() != NM_Client)
		{ Finish(false, TEXT("Rendered network observer retired or changed world")); return false; }
		// Camera exists before appearance admission. No sample/pose/cloth counter
		// is collected until the exact real replicated 200-actor roster is bound.
		if (Players.Num() != CrowdCount || Phase == 0 || CompletedNetworkPhase == Phase) return true;
	}
	if (Phase == 0)
	{
		if (Now - Started < 2) { return true; }
		if (!Setup()) { Finish(false, TEXT("Real RHI/authored 200 body setup failed")); return false; }
		// Mover and montages advance on world time. Wall-time phases would finish
		// too early when the engine clamps game delta under a heavy crowd load.
		Phase = 1; PhaseAt = World->GetTimeSeconds();
		TRACE_BOOKMARK(TEXT("ProjectJ.RenderedCrowd Warmup Actors=%d"), Players.Num());
		return true;
	}
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(ProjectJRenderedCrowdObserver);
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_RenderedCrowd_Observer);
	if(bCloseView && IsValid(ReviewCamera) && Players.Num()==1 && IsValid(Players[0]))
		ReviewCamera->SetActorLocation(Players[0]->GetActorLocation()+FVector(450,-450,220));
	int32 Visible = 0, Montages = 0, Prediction30=0;
 uint64 TrajectoryCalls = 0, TrajectoryCycles = 0, LocalTrajectoryCalls = 0, LocalTrajectoryCycles = 0;
 uint64 OldestTrajectoryUpdate = MAX_uint64, NewestTrajectoryUpdate = 0;
	int32 BodyLODs[3]={}, LocalLOD=INDEX_NONE;
	int32 NaniteCandidates=0, NaniteParts=0, ClothParts=0, ClothTickParts=0, ModularParts=0;
	int32 RetargetBodies=0, DirectBodies=0;
	int32 GameplayPoseDemands=0, SecondaryOptIns=0, SecondaryEnabled=0, SecondaryDisabled=0, LocalSecondaryDisabled=0;
	int32 ManagedCloths=0,ReducedCloths=0,LocalReducedCloths=0,RenderLODs[3]={},LocalRenderMax=INDEX_NONE;
	int64 ClothVertexCost=0;
	int32 UnknownClothCostParts=0,QualifiedClothNativeFrames=0;
	FProjectJClothExecutionStats ClothExecutionStats;
	int32 ClothExecutionOptedIn=0,ClothExecutionSuspended=0,LocalClothAuthoringMismatches=0;
	if(bClothExecutionExperiment) for(const auto& Original:ClothExecutionOriginals)
	{
		const auto* Part=Original.Mesh.Get();
		if(!IsValid(Part)) { Finish(false,TEXT("Experimental cloth part retired during observation")); return false; }
		ClothExecutionOptedIn+=Part->IsClothExecutionBudgetAllowed();
		ClothExecutionSuspended+=Part->IsClothingSimulationSuspended();
		if(Original.bLocal && (!FMath::IsNearlyEqual(Part->ClothBlendWeight,Original.Blend) ||
			Part->IsClothingSimulationSuspended()!=Original.bSuspended)) ++LocalClothAuthoringMismatches;
	}
	if(LocalClothAuthoringMismatches) { Finish(false,TEXT("Local cloth execution changed authored blend/suspension")); return false; }
	int32 ClothSimulations=0,ClothSubsteps1=0,ClothSubsteps3=0,RemoteMaxIterations=0,LocalClothSubsteps=0,LocalClothIterations=0;
	int32 WeaponMeshes=0, VisibleWeapons=0, RenderedWeapons=0, AutoLODWeapons=0, MatchingWeapons=0;
	const double PhaseAge = World->GetTimeSeconds() - PhaseAt;
	FString NPCFailure;
	if (!ObserveNPCCombatCohort(NPCFailure)) { Finish(false,*NPCFailure); return false; }
	if (!ObserveFXResourceCohort(NPCFailure)) { Finish(false,*NPCFailure); return false; }
	const int32 CanonicalStep = FMath::RoundToInt(PhaseAge * 60.0);
	const int64 CanonicalKey = int64(Phase) * 100000 + CanonicalStep;
	const bool bCaptureCanonical = bCanonicalProof && Phase > 1 && PhaseAge >= 2 && PhaseAge <= 7 &&
		(Phase == 5 || CanonicalStep % 6 == 0) && CanonicalKey != LastCanonicalKey;
	if (bCaptureCanonical) LastCanonicalKey = CanonicalKey;
	const bool bJumpWitness = !bNetworkObserver && bSimulateMovement && Phase == 4 && PhaseAge <= 1.2 && JumpWitnessRows < 32;
	int32 JumpAirCount = 0;
	FVector JumpPosition = FVector::ZeroVector, JumpVelocity = FVector::ZeroVector;
	const auto LogJumpWitness = [&](const TCHAR* Kind, int32 AirCount, const FVector& Position, const FVector& Velocity)
	{
		if (!bJumpWitness || JumpWitnessRows >= 32) { return; }
		const auto* Mover = Players[0]->GetMover();
		if (!Mover) { return; }
		const auto& Step = Mover->GetLastTimeStep(); // Public last physical timestep; not world time renamed as sim time.
		const auto* Life = Mover->GetSyncState().SyncStateCollection.FindDataByType<FProject_JMoverLifecycle>();
		const auto* Settings = Mover->FindSharedSettings<UCommonLegacyMovementSettings>();
		const FVector Gravity = Mover->GetGravityAcceleration();
		++JumpWitnessRows;
		UE_LOG(LogTemp, Display, TEXT("PROJECT_J_JUMP_WITNESS kind=%s row=%d cap=32 actor=0 actors=%d frame=%llu phase_game_s=%.9f world_s=%.9f wall_elapsed_s=%.9f last_sim_base_ms=%.9f last_sim_frame=%d last_sim_step_ms=%.9f mode=%s backend_async=%d air_count=%d z=%.9f vz=%.9f lifecycle_valid=%d jump_sequence=%u landing_sequence=%llu air_sequence=%llu settings_valid=%d jump_speed=%.9f gravity_x=%.9f gravity_y=%.9f gravity_z=%.9f"),
			Kind, JumpWitnessRows, Players.Num(), GFrameCounter, World->GetTimeSeconds() - PhaseAt, World->GetTimeSeconds(), FPlatformTime::Seconds() - Started,
			Step.BaseSimTimeMs, Step.ServerFrame, double(Step.StepMs), *Mover->GetMovementModeName().ToString(), Mover->IsBackendAsync(), AirCount,
			Position.Z, Velocity.Z, Life != nullptr, Life ? Life->JumpSequence : 0, Life ? Life->LandingSequence : 0, Life ? Life->AirSequence : 0,
			Settings != nullptr, Settings ? double(Settings->JumpUpwardsSpeed) : 0., Gravity.X, Gravity.Y, Gravity.Z);
	};
	for (int32 Index=0;Index<Players.Num();++Index)
	{
		auto* Player=Players[Index].Get();
		if (!IsValid(Player)) { Finish(false, TEXT("Authored actor disappeared")); return false; }
		if (bNetworkObserver && (Player->HasAuthority() || Player->GetWorld() != World ||
			Player->GetLocalRole() != (Index == 0 ? ROLE_AutonomousProxy : ROLE_SimulatedProxy) ||
			Player->IsLocallyControlled() != (Index == 0) || !Player->GetPlayerState() ||
			!Player->GetMesh()->GetAnimInstance() || !Player->GetCurrentCombatStyle() || !Player->GetCurrentWeaponPresentationProfile()))
		{ Finish(false, TEXT("Real network actor role, owner or representation changed")); return false; }
		if(const auto* Coordinator=Player->FindComponentByClass<UProject_JAnimationUpdateCoordinatorComponent>())
		{
			ManagedCloths+=Coordinator->GetManagedClothCount();ReducedCloths+=Coordinator->GetReducedClothCount();
			int32 UnknownParts=0;
			ClothVertexCost+=Coordinator->GetManagedClothVertexCost(UnknownParts);
			UnknownClothCostParts+=UnknownParts;
			QualifiedClothNativeFrames+=Coordinator->GetQualifiedClothNativeFrameCount(GFrameCounter);
			if(bClothExecutionExperiment)
			{
				const auto Stats=Coordinator->GetClothExecutionStats();
				ClothExecutionStats.Eligible+=Stats.Eligible; ClothExecutionStats.Reserved+=Stats.Reserved; ClothExecutionStats.Protected+=Stats.Protected;
				ClothExecutionStats.Unknown+=Stats.Unknown; ClothExecutionStats.Active+=Stats.Active; ClothExecutionStats.FadeOut+=Stats.FadeOut;
				ClothExecutionStats.Parked+=Stats.Parked; ClothExecutionStats.Warming+=Stats.Warming; ClothExecutionStats.FadeIn+=Stats.FadeIn;
				ClothExecutionStats.Unmanaged+=Stats.Unmanaged; ClothExecutionStats.Draining+=Stats.Draining;
				ClothExecutionStats.Desired+=Stats.Desired; ClothExecutionStats.MayResume+=Stats.MayResume;
			}
			if(bWithLocalPlayer && Index==0) LocalReducedCloths=Coordinator->GetReducedClothCount();
		}
		if (WeaponVisibility >= 0 || bNetworkObserver)
		{
			const auto* Presentation=Player->FindComponentByClass<UProject_JWeaponPresentationComponent>();
			auto* Weapon=Presentation ? Cast<AProject_JWeaponPresentationActor>(Presentation->GetSpawnedWeapon()) : nullptr;
			const auto Visuals=Weapon ? Weapon->GetVisualMeshes() : TArray<UMeshComponent*>();
			auto* Mesh=Visuals.IsEmpty() ? nullptr : Visuals[0];
			if (Mesh)
			{
				const bool bShow=WeaponVisibility!=0;
				if (!bNetworkObserver) for (auto* Visual:Visuals) if (Visual->IsVisible()!=bShow) Visual->SetVisibility(bShow);
				++WeaponMeshes; VisibleWeapons+=Mesh->IsVisible(); RenderedWeapons+=Mesh->WasRecentlyRendered(.2f);
				const auto* Static=Cast<UStaticMeshComponent>(Mesh);
				const auto* Skinned=Cast<USkeletalMeshComponent>(Mesh);
				AutoLODWeapons+=Static ? Static->GetForcedLodModel()==0 : Skinned && Skinned->GetForcedLOD()==0;
				const UObject* Asset=Static ? static_cast<const UObject*>(Static->GetStaticMesh()) : Skinned ? Skinned->GetSkeletalMeshAsset() : nullptr;
				MatchingWeapons+=ExpectedWeaponMesh.IsEmpty() || (Asset && Asset->GetPathName()==ExpectedWeaponMesh);
			}
		}
		if (bSimulateMovement)
		{
			const double Age=PhaseAge;
			const float Direction=Phase==3 || Phase==4 ? (FMath::Fmod(Age,2.)<1.?1.f:-1.f):0.f;
			const FVector Local=FRotator(0,Player->GetControlRotation().Yaw,0).UnrotateVector(FVector(Direction,0,0));
			if (!bNetworkObserver) // Network input belongs to each real owner/server AI, never this observer.
			{
				Player->GetLocomotionAnimStateComponent()->SetMoveInput(FVector2D(Local.Y,Local.X));
				if(Phase==4 && !bJumpRequested) Player->Jump();
				else if(Phase!=4 || Player->GetMover()->IsAirborne()) Player->StopJumping();
			}
			const FVector Position=Player->GetActorLocation();
			TravelDistances[Index]+=FVector::Dist2D(Position,PreviousPositions[Index]);PreviousPositions[Index]=Position;
			const FVector PhysicalVelocity=Player->GetVelocity();
			if (bNetworkObserver) ObserveNetworkMotion(Phase,Index,Player,Position,PhysicalVelocity,PhaseAge);
			const float Velocity=PhysicalVelocity.X;
			if(FMath::Abs(Velocity)>5.f) { const float Sign=FMath::Sign(Velocity);if(LastDirection[Index]!=0 && LastDirection[Index]!=Sign) ++Reversals[Index];LastDirection[Index]=Sign; }
			const bool bInAir=Project_J::Movement::IsInAir(Player);if(WasInAir[Index] && !bInAir) ++Landings[Index];WasInAir[Index]=bInAir;
			if (bNetworkObserver && Phase==4) NetworkSawJump[Index]=NetworkSawJump[Index] || bInAir;
			if (bJumpWitness)
			{
				JumpAirCount += bInAir;
				if (Index == 0)
				{
					JumpPosition = Position; JumpVelocity = PhysicalVelocity;
					// Jump() pressed the real input above; lifecycle sequence is separately accepted physical state.
					if (!bJumpRequested) { LogJumpWitness(TEXT("input_requested"), INDEX_NONE, Position, PhysicalVelocity); }
				}
			}
		}
		TInlineComponentArray<USkeletalMeshComponent*> Meshes(Player);
		const USkeletalMeshComponent* Body=nullptr;
		for (const auto* Mesh:Meshes)
		{
			if (!Project_J::Animation::FindBodyContactController(Mesh)) continue;
			if (Body) { Finish(false,TEXT("Ambiguous body contact pose producers"));return false; }
			Body=Mesh;
		}
		if (!Body) { Finish(false,TEXT("No body contact pose producer"));return false; }
		if (const auto* Source = Cast<UProject_JBudgetedSkeletalMeshComponent>(Player->GetMesh())) GameplayPoseDemands += Source->IsCombatCritical();
		auto* Contact = Cast<UProject_JRetargetAnimInstance>(Body->GetPostProcessInstance());
		if (!Contact) Contact = Cast<UProject_JRetargetAnimInstance>(Body->GetAnimInstance());
		if (Contact)
		{
			// Diagnostic clones only, after external static proof. This does not save
			// or opt in the production body asset. Leave the first warmup frames out.
			if (bSecondaryMotionBudgetExperiment) Contact->bAllowSecondaryMotionBudget = true;
			SecondaryOptIns += Contact->bAllowSecondaryMotionBudget;
			SecondaryEnabled += Contact->bEnableSecondaryMotion;
			SecondaryDisabled += !Contact->bEnableSecondaryMotion;
			if (bWithLocalPlayer && Index == 0) LocalSecondaryDisabled += !Contact->bEnableSecondaryMotion;
		}
		// Optional paired fixed-step quality evidence, not part of a timing run.
		// Eight spatially distributed probes stay inside the actual 200-body workload.
		if (bCaptureCanonical && (Index < 2 || Index == CrowdCount / 4 || Index == CrowdCount / 2 ||
			Index == CrowdCount * 3 / 4 || Index >= CrowdCount - 3))
		{
			const auto* Source = Player->GetMesh();
			const auto* Anim = Source->GetAnimInstance();
			const auto* Montage = Anim ? Anim->GetCurrentActiveMontage() : nullptr;
			const float MontageTime = Montage ? Anim->Montage_GetPosition(Montage) : -1.f;
			const auto Append = [&](FName Kind, bool bValid, const FTransform& Transform)
			{
				const FVector P = Transform.GetLocation(); const FQuat Q = Transform.GetRotation();
				CanonicalRows += FString::Printf(TEXT("%d,%d,%d,%.9f,%s,%d,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f\n"),
					Phase, CanonicalStep, Index, double(MontageTime), *Kind.ToString(), bValid ? 1 : 0, P.X, P.Y, P.Z, Q.X, Q.Y, Q.Z, Q.W);
				++CanonicalRowsCount;
			};
            const auto* Hit = Player->FindComponentByClass<UProject_JCombatHitValidationComponent>();
            FProject_JAuthoritativeSweepRecord TracePacket;
            const bool bPacket=Hit && Hit->FindAuthoritativeTracePacketAtTime(World->GetTimeSeconds(),0,FGameplayTag(),TracePacket);
            const int32 ExtraPieces=bPacket && TracePacket.bAtomicPath?TracePacket.Segments.Num():0;
			if (CanonicalRowsCount + 9 + 2*ExtraPieces > 65536) { Finish(false, TEXT("Bounded canonical quality probe capacity exceeded")); return false; }
			for (const FName Bone : {FName(TEXT("pelvis")), FName(TEXT("spine_03")), FName(TEXT("hand_l")), FName(TEXT("hand_r")), FName(TEXT("foot_l")), FName(TEXT("foot_r"))})
				Append(Bone, Source->DoesSocketExist(Bone), Source->GetSocketTransform(Bone, RTS_Component));
			const auto* Profile = Player->GetCurrentWeaponPresentationProfile();
			const FName Socket = Profile ? Profile->DrawnSocketName : FName(TEXT("WeaponSocket_R"));
			Append(TEXT("weapon_socket"), Source->DoesSocketExist(Socket), Source->GetSocketTransform(Socket, RTS_Component));
			FVector Start = FVector::ZeroVector, End = FVector::ZeroVector;
            const bool bTrace=bPacket && !TracePacket.bAtomicPath && TracePacket.Segments.Num()==1;
            if (bTrace) { Start=TracePacket.Segments[0].TraceStart; End=TracePacket.Segments[0].TraceEnd; }
			const FTransform SourceWorld = Source->GetComponentTransform();
			Append(TEXT("sweep_start"), bTrace, FTransform(FQuat::Identity, bTrace ? SourceWorld.InverseTransformPosition(Start) : FVector::ZeroVector));
			Append(TEXT("sweep_end"), bTrace, FTransform(FQuat::Identity, bTrace ? SourceWorld.InverseTransformPosition(End) : FVector::ZeroVector));
            // A path has no authoritative first-to-last chord. Keep the old
            // projection unavailable and expose every actual piece explicitly.
            for (int32 Piece=0;Piece<ExtraPieces;++Piece)
            {
                const auto& Segment=TracePacket.Segments[Piece];
                Append(FName(*FString::Printf(TEXT("sweep_piece_%d_start"),Piece)),true,FTransform(FQuat::Identity,SourceWorld.InverseTransformPosition(Segment.TraceStart)));
                Append(FName(*FString::Printf(TEXT("sweep_piece_%d_end"),Piece)),true,FTransform(FQuat::Identity,SourceWorld.InverseTransformPosition(Segment.TraceEnd)));
            }

		}
		RetargetBodies+=Cast<UProject_JRetargetAnimInstance>(Body->GetAnimInstance())!=nullptr;
		DirectBodies+=Cast<UProject_JPlayerAnimInstance>(Body->GetAnimInstance())!=nullptr &&
			Project_J::Animation::FindBodyContactController(Body)==Body->GetPostProcessInstance();
		const int32 BodyLOD=Body->GetPredictedLODLevel();
		if(BodyLOD>=0 && BodyLOD<3) ++BodyLODs[BodyLOD];
		if(bWithLocalPlayer && Player==Players[0]) LocalLOD=BodyLOD;
		bool bVisible = false;
		for (auto* Mesh : Meshes)
		{
			if(Mesh->GetNumLODs()>1 && Mesh->GetVisibleFlag() && !Mesh->bHiddenInGame)
			{
				const int32 LOD=Mesh->GetPredictedLODLevel();
				if(LOD>=0 && LOD<3) ++RenderLODs[LOD];
				if(bWithLocalPlayer && Index==0) LocalRenderMax=FMath::Max(LocalRenderMax,LOD);
			}
			// Workload counters are useful even when no render-path override is requested.
			{
				ModularParts += Mesh->IsA<UProject_JModularMeshComponent>();
				if (const auto* Asset = Mesh->GetSkeletalMeshAsset())
				{
#if WITH_EDITORONLY_DATA
					NaniteCandidates += Asset->GetNaniteSettings().bEnabled;
#else
					// Cooked assets have render data, not editor authoring settings.
					const auto* RenderData=Asset->GetResourceForRendering();
					NaniteCandidates += RenderData && RenderData->HasValidNaniteData();
#endif
					NaniteParts += Mesh->GetMeshObject() && Mesh->GetMeshObject()->IsNaniteMesh();
					if (Asset->HasActiveClothingAssets())
					{
						++ClothParts;
						ClothTickParts += Mesh->IsComponentTickEnabled() && !Mesh->bDisableClothSimulation && !Mesh->IsClothingSimulationSuspended();
						for(const auto& Simulation:Mesh->GetClothingSimulationInstances())
							if(const auto* Interactor=Simulation.GetClothingSimulationInteractor();Interactor && Interactor->GetNumCloths()>0)
							{
								++ClothSimulations;
								const int32 Substeps=Interactor->GetNumSubsteps(),Iterations=Interactor->GetNumIterations();
								ClothSubsteps1+=Substeps==1;ClothSubsteps3+=Substeps==3;
								if(bWithLocalPlayer && Index==0){LocalClothSubsteps=Substeps;LocalClothIterations=Iterations;}
								else RemoteMaxIterations=FMath::Max(RemoteMaxIterations,Iterations);
							}
					}
				}
			}
			if (Mesh==Body || Mesh->LeaderPoseComponent.Get()==Body)
			{
				bVisible |= Mesh->IsVisible() && !Mesh->bHiddenInGame && Mesh->WasRecentlyRendered(.2f);
			}
		}
		Visible += bVisible;
		Montages += Player->GetMesh()->GetAnimInstance()->IsAnyMontagePlaying();
		if(const auto* Anim=Cast<UProject_JPlayerAnimInstance>(Player->GetMesh()->GetAnimInstance()))
		{
			Prediction30+=Anim->GetTrajectoryPredictionRate()==30;
#if !UE_BUILD_SHIPPING
   if (bTrajectoryMetrics)
   {
    const auto Counters = Anim->GetTrajectoryDiagnosticCounters();
    TrajectoryCalls += Counters.Calls; TrajectoryCycles += Counters.Cycles;
    if (Counters.Calls)
    {
     OldestTrajectoryUpdate = FMath::Min(OldestTrajectoryUpdate, Counters.LastUpdateFrame);
     NewestTrajectoryUpdate = FMath::Max(NewestTrajectoryUpdate, Counters.LastUpdateFrame);
    }
    if (bWithLocalPlayer && Index == 0)
    { LocalTrajectoryCalls = Counters.Calls; LocalTrajectoryCycles = Counters.Cycles; }
   }
#endif
			const auto& Policy=Anim->GetCurrentOptimizationPolicy();
			if(Anim->IsAnyMontagePlaying() && (!Policy.bEnableHandIK || !Policy.bEnableFootIK || !Policy.bEnableRetargetIK || !Policy.bEnableFollowerRetarget))
			{ Finish(false,TEXT("An active montage lost source IK/retarget quality"));return false; }
		}
	}
	if (bJumpWitness && JumpAirCount != LastJumpWitnessAirCount)
	{
		LastJumpWitnessAirCount = JumpAirCount;
		LogJumpWitness(TEXT("air_count_transition"), JumpAirCount, JumpPosition, JumpVelocity);
	}
 // Core ticker and mesh updates do not share a promised frame boundary. Report
 // exact cumulative-counter differences between ticker samples, with frame IDs;
 // never subtract these values from the delayed GGameThreadTime measurement.
 const uint64 SampleFrame = GFrameCounter;
 if (bTrajectoryMetrics && (TrajectoryCalls < PreviousTrajectoryCalls || TrajectoryCycles < PreviousTrajectoryCycles ||
  LocalTrajectoryCalls < PreviousLocalTrajectoryCalls || LocalTrajectoryCycles < PreviousLocalTrajectoryCycles))
 { Finish(false, TEXT("Trajectory diagnostic owner/counters changed during the workload")); return false; }
 const uint64 IntervalCalls = bTrajectoryMetrics && bHasTrajectoryCounters ? TrajectoryCalls - PreviousTrajectoryCalls : 0;
 const uint64 IntervalCycles = bTrajectoryMetrics && bHasTrajectoryCounters ? TrajectoryCycles - PreviousTrajectoryCycles : 0;
 const uint64 LocalIntervalCalls = bTrajectoryMetrics && bHasTrajectoryCounters ? LocalTrajectoryCalls - PreviousLocalTrajectoryCalls : 0;
 const uint64 LocalIntervalCycles = bTrajectoryMetrics && bHasTrajectoryCounters ? LocalTrajectoryCycles - PreviousLocalTrajectoryCycles : 0;
	const bool bCapture = Phase > 1 && !bShot && PhaseAge > 1;
	Rows += FString::Printf(TEXT("%d,%.5f,%.6f,%.6f,%d,%d,%d,%d,%d,%d,%d,%d,%.6f,%.6f,%.6f,%d,%d,%d,%d,%d,%d,%.6f,%d,%d,%d,%d,%d,%d,%d\n"), Phase, Now - Started, Delta * 1000,
		FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()), Players.Num(), Visible, Montages, bCapture,BodyLODs[0],BodyLODs[1],BodyLODs[2],LocalLOD,
		FPlatformTime::ToMilliseconds(GGameThreadTime),FPlatformTime::ToMilliseconds(GRenderThreadTime),FPlatformTime::ToMilliseconds(GRHIThreadTime),Prediction30,
		NaniteCandidates,NaniteParts,ClothParts,ClothTickParts,ModularParts,PhaseAge,
		WeaponMeshes,VisibleWeapons,RenderedWeapons,AutoLODWeapons,MatchingWeapons,DirectBodies,RetargetBodies);
	Rows.RemoveFromEnd(TEXT("\n"));
	Rows+=FString::Printf(TEXT(",%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n"),ManagedCloths,ReducedCloths,LocalReducedCloths,
		RenderLODs[0],RenderLODs[1],RenderLODs[2],LocalRenderMax,ClothSimulations,ClothSubsteps1,ClothSubsteps3,RemoteMaxIterations,LocalClothSubsteps,LocalClothIterations);
	Rows.RemoveFromEnd(TEXT("\n"));
	Rows += FString::Printf(TEXT(",%d,%d,%d,%d,%d\n"), GameplayPoseDemands, SecondaryOptIns, SecondaryEnabled, SecondaryDisabled, LocalSecondaryDisabled);
 Rows.RemoveFromEnd(TEXT("\n"));
 Rows += FString::Printf(TEXT(",%d,%llu,%llu,%llu,%.6f,%llu,%.6f,%llu,%llu\n"), bTrajectoryMetrics,
  bHasTrajectoryCounters ? PreviousTrajectorySampleFrame : SampleFrame, SampleFrame, IntervalCalls,
  FPlatformTime::ToMilliseconds64(IntervalCycles), LocalIntervalCalls, FPlatformTime::ToMilliseconds64(LocalIntervalCycles),
  OldestTrajectoryUpdate == MAX_uint64 ? 0 : OldestTrajectoryUpdate, NewestTrajectoryUpdate);
 if (bNetworkObserver)
 {
  Rows.RemoveFromEnd(TEXT("\n"));
  Rows += TEXT(",1,1,199,0,1\n"); // Every actor's actual role was checked above this same sample.
 }
 else { Rows.RemoveFromEnd(TEXT("\n")); Rows += TEXT(",0,0,0,0,0\n"); }
 Rows.RemoveFromEnd(TEXT("\n"));
 Rows+=FString::Printf(TEXT(",%lld,%d,%d\n"),ClothVertexCost,UnknownClothCostParts,QualifiedClothNativeFrames);
 Rows.RemoveFromEnd(TEXT("\n"));
 Rows+=FString::Printf(TEXT(",%d,%d,%d,%lld,%lld,%lld,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n"),
  ClothExecutionOptedIn,ClothExecutionSuspended,LocalClothAuthoringMismatches,
  ClothExecutionStats.Eligible,ClothExecutionStats.Reserved,ClothExecutionStats.Protected,ClothExecutionStats.Unknown,
  ClothExecutionStats.Active,ClothExecutionStats.FadeOut,ClothExecutionStats.Parked,ClothExecutionStats.Warming,ClothExecutionStats.FadeIn,
  ClothExecutionStats.Unmanaged,ClothExecutionStats.Draining,ClothExecutionStats.Desired,ClothExecutionStats.MayResume);
	if (bNPCMetrics)
	{
		const auto* Decisions = World->GetSubsystem<UProject_JNPCDecisionSubsystem>();
		const auto* Scoring = World->GetSubsystem<UProject_JTargetScoringSubsystem>();
		if (!Decisions || !Scoring) { Finish(false, TEXT("Opt-in NPC metrics subsystems unavailable")); return false; }
		const auto& NPC = Decisions->GetStats();
		// Latest completed NPC Tick at this sampler point, not an aligned per-frame CPU contribution.
		// Cumulative completion differences preserve real counts without repeating a last batch sample.
		NPCDecisionRows += FString::Printf(
			TEXT("%d,%llu,%.5f,%.5f,%.5f,%d,%d,%d,%d,%d,%llu,")
			TEXT("%llu,%llu,%llu,%llu,%llu,%llu,%.6f,%.6f,%.6f,%.6f,%.6f,")
			TEXT("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%llu,%.6f,%.6f\n"),
			Phase, SampleFrame, Now - Started, PhaseAge, World->GetTimeSeconds(), Decisions->GetAgentCount(), Decisions->GetTargetCount(),
			Decisions->GetActionCount(), Decisions->GetOutstandingCount(), Scoring->GetPendingCount(), Scoring->GetLaunchedTaskCount(),
			NPC.SubmittedBatches, NPC.SubmittedDecisions, NPC.AppliedDecisions, NPC.DiscardedDecisions, NPC.RejectedBatches, NPC.BatchCapacityDeferrals,
			NPC.LastFullTickMilliseconds, NPC.LastTickGameThreadMilliseconds, NPC.LastSharedSnapshotMilliseconds, NPC.LastTickActionMilliseconds, NPC.OldestReadyResultMilliseconds,
			NPC.LastTickAgentVisits, NPC.LastTickCandidateVisits, NPC.LastTickSnapshotValues, NPC.LastTickResults, NPC.LastTickTargetPositionReads,
			NPC.LastTickSnapshotBuilds, NPC.LastTickSpatialCells, NPC.LastTickLinearFallbacks, NPC.LastTickObservers, NPC.LastTickObserverCandidateVisits,
			NPC.LastTickImportanceUpdates, NPC.LastTickPromotions, NPC.bObserverCoverageIncomplete ? 1 : 0, NPC.LastTickActionVisits, NPC.LastTickActionUpdates,
			NPC.CompletedBatches, NPC.CompletedComputeMicroseconds, NPC.CompletedCoreDeliveryMilliseconds);
	}
 PreviousTrajectoryCalls = TrajectoryCalls; PreviousTrajectoryCycles = TrajectoryCycles;
 PreviousLocalTrajectoryCalls = LocalTrajectoryCalls; PreviousLocalTrajectoryCycles = LocalTrajectoryCycles;
 PreviousTrajectorySampleFrame = SampleFrame; bHasTrajectoryCounters = bTrajectoryMetrics;
	if ((WeaponVisibility >= 0 || bNetworkObserver) && Phase > 1 && PhaseAge > 2 &&
		(WeaponMeshes!=CrowdCount || VisibleWeapons!=((bNetworkObserver || WeaponVisibility) ? CrowdCount : 0) || MatchingWeapons!=CrowdCount))
	{ Finish(false,TEXT("Weapon comparison lost the equipped mesh or requested visibility"));return false; }
	if (ExpectedNaniteMode >= 0 && Phase > 1 && PhaseAge > 2)
	{
		if (NaniteCandidates == 0 || NaniteParts != (ExpectedNaniteMode ? NaniteCandidates : 0) ||
			ExpectedModularParts <= 0 || ModularParts != ExpectedModularParts || ClothParts != CrowdCount || ClothTickParts != ClothParts)
		{ Finish(false, TEXT("Actual Nanite path or authored modular/cloth workload did not match")); return false; }
	}
	if (bSimulateMovement && Phase==4) bJumpRequested=true;
	if (Phase > 1) { ++Samples; VisibleSamples += Visible == CrowdCount; }
	if (Phase > 1 && bWithLocalPlayer)
	{
		const auto* Local = Players[0].Get();
		const auto* Anim = Cast<UProject_JCharacterAnimInstance>(Local->GetMesh()->GetAnimInstance());
		const auto* Coordinator = Local->FindComponentByClass<UProject_JAnimationUpdateCoordinatorComponent>();
		if (LocalLOD!=0 || LocalRenderMax>0 || LocalReducedCloths!=0 || !Local->IsPlayerControlled() || !Local->IsLocallyControlled() || !Anim ||
			Anim->GetCurrentOptimizationPolicy().Tier != EProject_JAnimBudgetTier::Local ||
			!Anim->GetCurrentOptimizationPolicy().bEnableHandIK || !Anim->GetCurrentOptimizationPolicy().bEnableFootIK ||
			!Coordinator || Coordinator->GetVisualEvaluationInterval() != 0.f)
		{ Finish(false, TEXT("Local source/visual quality was reduced in the 200-body scene")); return false; }
		if(const auto* PlayerAnim=Cast<UProject_JPlayerAnimInstance>(Anim); !PlayerAnim || PlayerAnim->GetTrajectoryPredictionRate()!=60)
		{ Finish(false,TEXT("Local trajectory precision was reduced"));return false; }
		++LocalQualitySamples;
	}
	if (bCapture)
	{
        // One bounded witness at the existing screenshot boundary, outside the measured/screenshot-excluded window.
        // Function-owner CLASS_Native selects the actual generated dispatch branch; no mode/tuning is changed.
        int32 Walking = 0, Native = 0, Blueprint = 0, Missing = 0;
        FString FirstClass, FirstOwner;
        int32 ModeAvailable = 0, SameFirstClass = 0, SameFirstGenerateOwner = 0;
        int32 NativeGenerate = 0, BlueprintGenerate = 0, MissingGenerate = 0;
        const UClass* FirstModeClass = nullptr;
        const UClass* FirstGenerateOwner = nullptr;
        FName FirstModeName;
        for (const auto& Player : Players)
        {
            const auto* Mover = Player ? Player->GetMover() : nullptr;
            const auto* Mode = Mover ? Mover->GetMovementMode() : nullptr;
            if (Mode)
            {
                ++ModeAvailable;
                const UClass* ModeClass = Mode->GetClass();
                const auto* GenerateFunction = Mode->FindFunction(TEXT("GenerateMove"));
                const UClass* GenerateOwner = GenerateFunction ? GenerateFunction->GetOwnerClass() : nullptr;
                if (!FirstModeClass)
                {
                    FirstModeClass = ModeClass;
                    FirstGenerateOwner = GenerateOwner;
                    FirstModeName = Mover->GetMovementModeName();
                }
                SameFirstClass += ModeClass == FirstModeClass;
                SameFirstGenerateOwner += GenerateOwner == FirstGenerateOwner;
                if (!GenerateOwner) ++MissingGenerate;
                else if (GenerateOwner->HasAnyClassFlags(CLASS_Native)) ++NativeGenerate;
                else ++BlueprintGenerate;
            }
            else ++MissingGenerate;
            if (!Mode || !Mode->IsA<USmoothWalkingMode>()) { ++Missing; continue; }
            ++Walking;
            const auto* Function = Mode->FindFunction(TEXT("GenerateWalkMove"));
            if (!Function) { ++Missing; continue; }
            const auto* OwnerClass = Function->GetOwnerClass();
            if (OwnerClass->HasAnyClassFlags(CLASS_Native)) ++Native; else ++Blueprint;
            if (FirstClass.IsEmpty()) { FirstClass = Mode->GetClass()->GetPathName(); FirstOwner = OwnerClass->GetPathName(); }
        }
        const IConsoleVariable* WarpedMontage = IConsoleManager::Get().FindConsoleVariable(TEXT("a.PoseSearchTrajectory.UseWarpedMontage"));
        const IConsoleVariable* FlyingParamQueries = IConsoleManager::Get().FindConsoleVariable(TEXT("mover.Prediction.NativeFlyingParamsQueries"));
        UE_LOG(LogTemp, Display, TEXT("PROJECT_J_MOVEMENT_DISPATCH phase=%d actors=%d mode_available=%d same_first_class=%d same_first_generate_owner=%d native_generate=%d blueprint_generate=%d missing_generate=%d first_mode=%s first_class=%s first_generate_owner=%s warped_montage=%d native_flying_param_queries=%d"),
            Phase, Players.Num(), ModeAvailable, SameFirstClass, SameFirstGenerateOwner, NativeGenerate, BlueprintGenerate, MissingGenerate,
            *FirstModeName.ToString(), *GetPathNameSafe(FirstModeClass), *GetPathNameSafe(FirstGenerateOwner), WarpedMontage ? WarpedMontage->GetInt() : -1, FlyingParamQueries ? FlyingParamQueries->GetInt() : -1);
        UE_LOG(LogTemp, Display, TEXT("PROJECT_J_WALKING_DISPATCH phase=%d actors=%d walking=%d native=%d blueprint=%d missing=%d first_class=%s first_owner=%s"),
            Phase, Players.Num(), Walking, Native, Blueprint, Missing, *FirstClass, *FirstOwner);
		if (!RequestFixtureScreenshot(Output / FString::Printf(TEXT("phase%d.png"), Phase),ScreenshotError))
		{ Finish(false,*ScreenshotError); return false; }
		bShot = true;
	}
	if (PhaseAge >= 8)
	{
#if WITH_EDITOR
		if (bRequirePlayerPoseReadiness)
		{
			bool Ready=false; FString Failure;
			if (!AdvancePlayerPoseReadiness(Ready,Failure)) { Finish(false,*Failure); return false; }
			if (!Ready) { return true; }
		}
#endif
		// Class loading and bounded weapon application may span a few long frames.
		// Keep preparation outside measured phases until the unchanged workload is ready.
		if (Phase == 1 && WeaponVisibility >= 0)
		{
			if (WeaponMeshes != CrowdCount || VisibleWeapons != (WeaponVisibility ? CrowdCount : 0) || MatchingWeapons != CrowdCount) return true;
			UE_LOG(LogTemp, Display, TEXT("PROJECT_J_RENDERED_CROWD_READY actors=%d weapons=%d visible=%d matching=%d"), Players.Num(), WeaponMeshes, VisibleWeapons, MatchingWeapons);
		}
		if (Phase > 1 && (!Samples || VisibleSamples < Samples / 2))
		{ Finish(false, TEXT("All authored bodies were not observed rendered for half the measurement")); return false; }
		if (bNetworkObserver)
		{
			WriteNetworkMotionWitness(Phase,Output/FString::Printf(TEXT("motion-observer-phase%d.csv"),Phase));
            // Opt-in observation only. The existing phase-completion guard makes
            // selection once, after phase3. The same capture closes on phase5 entry.
            if (Phase==3 && FParse::Param(FCommandLine::Get(),TEXT("ProjectJRenderedDecodedEndpoints")))
            {
                const TStrongObjectPtr<UProject_JRenderedCrowdFixture> KeepSelf(this);
                const TStrongObjectPtr<UWorld> KeepWorld(World);
                const TStrongObjectPtr<APlayerController> KeepObserver(NetworkObserver.Get());
                const APawn* ObserverPawn=KeepObserver.IsValid() ? KeepObserver->GetPawn():nullptr;
                const auto* ObserverBridge=ObserverPawn ? UE::Net::FReplicationSystemUtil::GetActorReplicationBridge(ObserverPawn):nullptr;
                const uint64 ObserverId=ObserverBridge ? ObserverBridge->GetReplicatedRefHandle(ObserverPawn).GetId():0;
                if (!IsValid(ObserverPawn) || ObserverPawn->IsActorBeingDestroyed() || ObserverPawn->GetWorld()!=World
                    || ObserverPawn->GetLocalRole()!=ROLE_AutonomousProxy || Players.IsEmpty() || Players[0].Get()!=ObserverPawn || !ObserverId)
                { Finish(false,TEXT("Decoded endpoint observer identity unavailable")); return false; }
                FString Command;
                {
                    TArray<uint64,TInlineAllocator<3>> Selected;
                    const bool bFixed=FParse::Param(FCommandLine::Get(),TEXT("ProjectJRenderedFixedEndpointSelection"));
                    FString Selection=FString::Printf(TEXT("{\"selection_phase\":3,\"observation_phase\":4,\"observer_pawn_actor_id\":\"%llu\",\"selector\":\"%s\",\"selected\":["),ObserverId,
                        bFixed?TEXT("fixed_npc_avatar_indices_2_3_4"):TEXT("negative_actor_dx_without_negative_vx_then_other_sp"));
                    if (NetworkMotionWitness.Num()==400)
                    for (int32 Pass=0;Pass<(bFixed?1:2);++Pass)
                    for (int32 I=bFixed?2:1;I<Players.Num() && (!bFixed || I<=4) && Selected.Num()<(bFixed?3:Pass==0?2:3);++I)
                    {
                        const auto& W=NetworkMotionWitness[I];
                        const auto* Candidate=Players[I].Get();
                        const bool bMismatch=W.NegativeDX>0 && W.NegativeVx==0 && W.PositiveVx>0;
                        if ((!bFixed && bMismatch!=(Pass==0)) || !W.bIdentityStable || W.InvalidSamples || !W.Samples ||
                            W.Role!=ROLE_SimulatedProxy || !W.ActorId || !IsValid(Candidate) || Candidate->IsActorBeingDestroyed() ||
                            Candidate->GetWorld()!=World || Candidate->GetLocalRole()!=ROLE_SimulatedProxy) continue;
                        const auto* Bridge=UE::Net::FReplicationSystemUtil::GetActorReplicationBridge(Candidate);
                        if (!Bridge || Bridge->GetReplicatedRefHandle(Candidate).GetId()!=W.ActorId) continue;
                        const auto* State=Candidate->GetPlayerState<AProject_JPlayerState>();
                        if(bFixed && (!State || State->GetPlayerId()!=10000+I)) continue;
                        Selection+=FString::Printf(TEXT("%s{\"actor_id\":\"%llu\",\"sample_index\":%d,\"canonical_player_id\":%d,\"kind\":\"%s\",\"negative_dx_samples\":%d,\"negative_vx_samples\":%d}"),
                            Selected.IsEmpty()?TEXT(""):TEXT(","),W.ActorId,I,State?State->GetPlayerId():-1,
                            bFixed?TEXT("fixed_npc_avatar"):bMismatch?TEXT("phase3_mismatch"):TEXT("other_sp"),W.NegativeDX,W.NegativeVx);
                        Selected.Add(W.ActorId);
                    }
                    Selection+=TEXT("],\"raw_basis_is_world_proven\":false,\"serial_gaps_are_unobserved_intermediate_codec_events\":true}");
                    if (Selected.IsEmpty() || (bFixed && Selected.Num()!=3) || !FFileHelper::SaveStringToFile(Selection,*(Output/TEXT("decoded-endpoint-selection.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
                    { Finish(false,TEXT("Bounded decoded endpoint selection unavailable")); return false; }
                    Command=TEXT("ProjectJ.CaptureMoverBuffers 120 RawReceive ActorIds=");
                    for (int32 I=0;I<Selected.Num();++I) Command+=FString::Printf(TEXT("%s%llu"),I?TEXT(","):TEXT(""),Selected[I]);
                }
                if (!GEngine || !GEngine->Exec(World,*Command))
                { Finish(false,TEXT("Existing bounded decoded endpoint command unavailable")); return false; }
                if (bFinished || bNetworkViewRetired || !bNetworkObserver || GetWorld()!=KeepWorld.Get() || KeepWorld->bIsTearingDown ||
                    !IsValid(KeepObserver.Get()) || KeepObserver->IsActorBeingDestroyed() || NetworkObserver.Get()!=KeepObserver.Get() || KeepObserver->GetWorld()!=KeepWorld.Get()) return false;
            }
			if (Phase == 6)
			{
				Finish(true, TEXT("200 real replicated avatars over two actual client connections"));
				// Publish only after Finish stores any async capture completion.
				CompletedNetworkPhase = Phase; return false;
			}
			CompletedNetworkPhase = Phase;
			return true; // Existing server stage/owner RPC advances the next phase.
		}
		if (Phase == (bSimulateMovement?6:3))
		{
			bool FXReady=false;
			if (!AdvanceFXResourceRetirement(FXReady,NPCFailure)) { Finish(false,*NPCFailure); return false; }
			if (!FXReady) return true;
			bool NPCReady=false;
			if (!AdvanceNPCCombatRetirement(NPCReady,NPCFailure)) { Finish(false,*NPCFailure); return false; }
			if (!NPCReady) return true; // Normal engine ticks drain owned work; never block/flush.
			if (!PollAsyncScreenshot(ScreenshotReady,ScreenshotError)) { Finish(false,*ScreenshotError); return false; }
			if (!ScreenshotReady) return true;
			Finish(true, TEXT("Actual authored bodies; real viewport; no remote connections")); return false;
		}
#if WITH_EDITOR
		if (bRequirePlayerPoseReadiness && Phase==1) { bPlayerPoseMeasurementAdmitted=true; PlayerPoseAdmissionFrame=GFrameCounter; PlayerPoseAdmissionAt=FPlatformTime::Seconds(); }
#endif
		++Phase; PhaseAt = World->GetTimeSeconds(); Samples = VisibleSamples = 0; bShot = false;
		TRACE_BOOKMARK(TEXT("ProjectJ.RenderedCrowd Phase=%d Actors=%d"), Phase, Players.Num());
		if (Phase==5 && !BeginNPCCombatCohort(NPCFailure)) { Finish(false,*NPCFailure); return false; }
		if (Phase == (bSimulateMovement?5:3)) for (AProject_JPlayerCharacter* Player : Players)
		{
			Player->HandleSkillInputTagPressed(FProject_JGameplayTags::Get().InputTag_Skill_R);
			Player->HandleSkillInputTagReleased(FProject_JGameplayTags::Get().InputTag_Skill_R);
		}
	}
	return true;
}
void UProject_JRenderedCrowdFixture::Finish(bool bSuccess, const TCHAR* Reason)
{
	if (bFinished) return;
	bFinished = true;
	if (bSuccess && NPCCombatCount && (bAwaitingScreenshotPixels || ScreenshotWrite.IsValid() ||
		ScreenshotWritesStarted!=5 || ScreenshotWritesCompleted!=5 || ScreenshotCompletedMask!=0x7cu || !ScreenshotFailure.IsEmpty()))
	{ bSuccess=false; Reason=TEXT("Asynchronous screenshot writes incomplete or failed"); }
	ReleaseAsyncScreenshotCapture();
	CleanupFXResourceCohort();
	CleanupNPCCombatCohort();
	RestoreClothExecutionOverrides();
	ReleaseCanonicalProofDemands();
	FString WorkloadFailureDetails;
	FFileHelper::SaveStringToFile(PoseRoutes,*(Output/TEXT("pose_routes.csv")));
	if (bSimulateMovement)
	{
		FString Work=TEXT("actor,travel_cm,reversals,landings\n");
		int32 FailedTravel=0, FailedReversals=0, FailedLandings=0;
		for(int32 Index=0;Index<Players.Num();++Index)
		{
			Work+=FString::Printf(TEXT("%d,%.3f,%d,%d\n"),Index,TravelDistances[Index],Reversals[Index],Landings[Index]);
			bSuccess &= TravelDistances[Index]>100.f && Reversals[Index]>0 && Landings[Index]>0;
			FailedTravel+=!(TravelDistances[Index]>100.f); FailedReversals+=Reversals[Index]<=0; FailedLandings+=Landings[Index]<=0;
			if (bNetworkObserver && (!NetworkSawJump.IsValidIndex(Index) || !NetworkSawJump[Index] || WasInAir[Index]))
			{ bSuccess=false; Reason=TEXT("A real replicated Avatar did not exhibit phase4 air and final landing"); }
		}
		FFileHelper::SaveStringToFile(Work,*(Output/TEXT("workload.csv")));
		if (bNetworkObserver && (FailedTravel || FailedReversals || FailedLandings))
			WorkloadFailureDetails=FString::Printf(TEXT("; travel_failed=%d reversal_failed=%d landing_failed=%d"),FailedTravel,FailedReversals,FailedLandings);
	}
	FFileHelper::SaveStringToFile(Rows, *(Output / TEXT("frames.csv")));
	if (bNPCMetrics && !FFileHelper::SaveStringToFile(NPCDecisionRows, *(Output / TEXT("npc_decisions.csv"))))
	{ bSuccess = false; Reason = TEXT("NPC decision metrics evidence write failed"); }
	if (bCanonicalProof && !FFileHelper::SaveStringToFile(CanonicalRows, *(Output / TEXT("canonical_pose.csv"))))
	{ bSuccess = false; Reason = TEXT("Canonical quality evidence write failed"); }
    if (bLeaderBoundsProof && !EndLeaderBoundsProof(bSuccess,Reason))
    { bSuccess=false; Reason=TEXT("Native bounds qualification incomplete or failed; see bounds_native_proof.json"); }
	if (bRequirePlayerPoseReadiness && !WritePlayerPoseReadiness(bSuccess,FString(Reason)+WorkloadFailureDetails))
	{ bSuccess=false; Reason=TEXT("Player pose readiness receipt write failed"); }
	if (!WriteFXResourceCohort(bSuccess,FString(Reason)+WorkloadFailureDetails))
	{ bSuccess=false; Reason=TEXT("Mixed FX resource evidence incomplete or failed; see fx-resource receipt"); }
	if (!WriteNPCCombatCohort(bSuccess,FString(Reason)+WorkloadFailureDetails))
	{ bSuccess=false; Reason=TEXT("NPC combat cohort evidence incomplete or failed; see npc-combat receipt"); }
	const FString DetailedReason=FString(Reason)+WorkloadFailureDetails;
	FFileHelper::SaveStringToFile(FString::Printf(TEXT("mode=%s\nstarted=%d\ncompleted=%d\ncompleted_phase_mask=%u\npending_write=%d\nfailure=%s\n"),
		NPCCombatCount?TEXT("ImageWriteQueue_single_inflight"):TEXT("Engine_default"),ScreenshotWritesStarted,ScreenshotWritesCompleted,
		ScreenshotCompletedMask,ScreenshotWrite.IsValid(),*ScreenshotFailure),*(Output/TEXT("screenshots.txt")));
	FFileHelper::SaveStringToFile(FString::Printf(TEXT("success=%d\ncooked=%d\nactors=%d\nlocal_player=%d\nlocal_quality_samples=%d\nreason=%s\n"), bSuccess,
		FPlatformProperties::RequiresCookedData(), Players.Num(), bWithLocalPlayer, LocalQualitySamples, *DetailedReason), *(Output / TEXT("result.txt")));
	if (bNetworkObserver)
	{
		bNetworkFailed = !bSuccess;
#if CSV_PROFILER
		if (FParse::Param(FCommandLine::Get(), TEXT("ProjectJRenderedClientCSV")))
		{
			// EndCapture can broadcast native delegates. Finish is already
			// one-shot; pin this owner only during that synchronous call.
			const TStrongObjectPtr<UProject_JRenderedCrowdFixture> KeepSelf(this);
			NetworkCSVCompletion = FCsvProfiler::Get()->EndCapture();
		}
#endif
	}
	else FPlatformMisc::RequestExit(false); // The network server owns normal process completion.
}
void UProject_JRenderedCrowdFixture::ReleaseCanonicalProofDemands()
{
	if (!bCanonicalProof) return;
	for (const auto& Player : Players) if (IsValid(Player.Get()))
		if (auto* Source = Cast<UProject_JBudgetedSkeletalMeshComponent>(Player->GetMesh())) Source->ReleaseAnimationUpdate(this);
}
void UProject_JRenderedCrowdFixture::Deinitialize()
{
	ReleaseAsyncScreenshotCapture();
    if (bLeaderBoundsProof) EndLeaderBoundsProof(false,TEXT("Fixture deinitialized"));
	if (bRequirePlayerPoseReadiness && !bFinished) { WritePlayerPoseReadiness(false,TEXT("Fixture deinitialized before readiness/run completion")); }
	PlayerPoseReadinessPins.Reset();
	CleanupFXResourceCohort();
	CleanupNPCCombatCohort();
	RestoreClothExecutionOverrides();
	StopNetworkObservation();
	ReleaseCanonicalProofDemands();
	FTSTicker::GetCoreTicker().RemoveTicker(Handle);
	Super::Deinitialize();
}

#if WITH_EDITOR
bool UProject_JRenderedCrowdFixture::CollectPlayerPoseInputs(FString& Signature,FString& SourceIdentity,TArray<FObjectKey>& Keys,TArray<UObject*>& Pins,TArray<UPoseSearchDatabase*>& Databases,FString& Failure) const
{
	check(IsInGameThread());
	const auto Pin=[&](UObject* Object) { if (Object) { Pins.AddUnique(Object); } };
	const auto Key=[&](const UObject* Object) { Keys.Add(Object ? FObjectKey(Object) : FObjectKey()); };
	TSharedRef<FJsonObject> Scope=MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Consumers,Membership,Classes,MeshInputs,SourceConsumers,PrimaryMeshes; TSet<UClass*> SeenClasses;
	const auto Bad=[&](const FString& Reason)
	{
		Failure=Reason; Scope->SetArrayField(TEXT("consumers"),Consumers); Scope->SetArrayField(TEXT("classes"),Classes); Scope->SetArrayField(TEXT("meshes"),MeshInputs); Scope->SetArrayField(TEXT("candidate_membership"),Membership);
		Scope->SetStringField(TEXT("partial_failure"),Reason); FJsonSerializer::Serialize(Scope,TJsonWriterFactory<>::Create(&Signature)); return false;
	};
	TArray<UChooserTable*> Roots,Tables; TSet<UChooserTable*> Visiting;
	int32 ChooserCanonicalRowCount=0,PoseRowVisits=0;
	TFunction<bool(UChooserTable*)> VisitTable;
	TFunction<bool(const FInstancedStruct&,const FString&)> VisitResult;
	const auto AddDatabase=[&](UPoseSearchDatabase* Database,const FString& Site)
	{
		if (!Database) { return true; } // Native PoseMatch rows/fallback may have no database.
		if (!IsValid(Database) || !IsValid(Database->Schema)) { return Bad(TEXT("Invalid PoseSearch database/schema at ")+Site+TEXT(" database=")+GetPathNameSafe(Database)); }
		if (!Databases.Contains(Database))
		{
			if (Databases.Num()>=128) { return Bad(TEXT("Player PoseSearch closure exceeds128 databases")); }
			Databases.Add(Database); Pin(Database); Pin(Database->Schema->GetOutermost()); Key(Database); Key(Database->Schema.Get());
		}
		return true;
	};
	VisitResult=[&](const FInstancedStruct& Result,const FString& Site)
	{
		if (!Result.IsValid()) { return true; }
		UObject* Asset=nullptr;
		if (Result.GetScriptStruct()==FEvaluateChooser::StaticStruct()) { Asset=Result.Get<FEvaluateChooser>().Chooser; }
		else if (Result.GetScriptStruct()==FNestedChooser::StaticStruct()) { Asset=Result.Get<FNestedChooser>().Chooser; }
		else if (Result.GetScriptStruct()==FAssetChooser::StaticStruct()) { Asset=Result.Get<FAssetChooser>().Asset; }
		else if (Result.GetScriptStruct()==FSoftAssetChooser::StaticStruct())
		{
			const auto& Soft=Result.Get<FSoftAssetChooser>().Asset; Asset=Soft.Get();
			if (!Soft.IsNull() && !IsValid(Asset)) { return Bad(TEXT("Unloaded soft chooser result (no synchronous load allowed) at ")+Site); }
		}
		else { return Bad(TEXT("Unsupported dynamic chooser result at ")+Site+TEXT(": ")+Result.GetScriptStruct()->GetPathName()); }
		Key(Asset); Pin(Asset);
		auto Edge=MakeShared<FJsonObject>(); Edge->SetStringField(TEXT("site"),Site); Edge->SetStringField(TEXT("result_type"),Result.GetScriptStruct()->GetPathName()); Edge->SetStringField(TEXT("result"),GetPathNameSafe(Asset)); Edge->SetStringField(TEXT("result_package"),Asset ? Asset->GetOutermost()->GetName() : TEXT("None")); Membership.Add(MakeShared<FJsonValueObject>(Edge));
		if (!Asset) { return true; }
		if (auto* Table=Cast<UChooserTable>(Asset)) { return VisitTable(Table); }
		if (auto* Database=Cast<UPoseSearchDatabase>(Asset)) { return AddDatabase(Database,Site); }
		return Asset->IsA<UAnimationAsset>() || Bad(TEXT("Unsupported chooser result asset at ")+Site+TEXT(": ")+Asset->GetPathName());
	};
	VisitTable=[&](UChooserTable* Table)
	{
		if (!IsValid(Table)) { return Bad(TEXT("Invalid player chooser table")); }
		if (Visiting.Contains(Table)) { return Bad(TEXT("Cyclic player chooser table: ")+Table->GetPathName()); }
		if (Tables.Contains(Table)) { return true; }
		if (Tables.Num()>=64) { return Bad(TEXT("Player chooser closure exceeds64 tables")); }
		Tables.Add(Table); Visiting.Add(Table); Pin(Table); Pin(Table->GetRootChooser()); Key(Table); Key(Table->GetRootChooser());
		const auto& Results=Table->IsCookedData() ? Table->CookedResults : Table->ResultsStructs;
		ChooserCanonicalRowCount+=Results.Num()+1; if (ChooserCanonicalRowCount>4096) { return Bad(TEXT("Player chooser closure exceeds4096 canonical rows/fallbacks")); }
		if (Table->ColumnsStructs.Num()>128) { return Bad(TEXT("Player chooser table exceeds128 columns: ")+Table->GetPathName()); }
		for (int32 ColumnIndex=0;ColumnIndex<Table->ColumnsStructs.Num();++ColumnIndex)
		{
			const auto* Column=Table->ColumnsStructs[ColumnIndex].GetPtr<FPoseSearchColumn>(); if (!Column) { continue; }
			if (Column->GetChooser()!=Table || Column->GetNumRows()!=Results.Num()) { return Bad(TEXT("PoseMatch table/row ownership mismatch: ")+Table->GetPathName()); }
			for (int32 Row=0;Row<=Results.Num();++Row)
			{
				if (++PoseRowVisits>8192) { return Bad(TEXT("Player PoseMatch closure exceeds8192 row/fallback visits")); }
				const bool Fallback=Row==Results.Num(); const int32 NativeRow=Fallback ? ChooserColumn_SpecialIndex_Fallback : Row;
				auto* Database=Column->GetDatabase(NativeRow); Key(Database);
				const FString Site=FString::Printf(TEXT("%s column%d %s"),*Table->GetPathName(),ColumnIndex,Fallback ? TEXT("fallback") : *FString::FromInt(Row));
				if (!AddDatabase(Database,Site)) { return false; }
				auto Member=MakeShared<FJsonObject>(); Member->SetStringField(TEXT("site"),Site); Member->SetStringField(TEXT("database"),GetPathNameSafe(Database)); Member->SetStringField(TEXT("database_outer"),Database ? GetPathNameSafe(Database->GetOuter()) : TEXT("None")); Member->SetStringField(TEXT("database_package"),Database ? Database->GetOutermost()->GetName() : TEXT("None")); Member->SetStringField(TEXT("chooser_root"),GetPathNameSafe(Table->GetRootChooser())); Member->SetStringField(TEXT("schema"),Database ? GetPathNameSafe(Database->Schema.Get()) : TEXT("None")); Membership.Add(MakeShared<FJsonValueObject>(Member));
			}
		}
		for (int32 Row=0;Row<Results.Num();++Row) if (!VisitResult(Results[Row],FString::Printf(TEXT("%s row%d"),*Table->GetPathName(),Row))) { return false; }
		if (!VisitResult(Table->FallbackResult,Table->GetPathName()+TEXT(" fallback"))) { return false; }
		Visiting.Remove(Table); return true;
	};
	if (!GetWorld() || GetWorld()->bIsTearingDown || GetWorld()->GetNetMode()!=NM_Standalone || Players.Num()!=CrowdCount) { return Bad(TEXT("Player pose readiness requires its current standalone roster")); }
	for (int32 ActorIndex=0;ActorIndex<Players.Num();++ActorIndex)
	{
		auto* Player=Players[ActorIndex].Get();
		if (!IsValid(Player) || Player->IsActorBeingDestroyed() || Player->GetWorld()!=GetWorld() || !IsValid(Player->GetMesh())) { return Bad(TEXT("Player/source mesh retired during readiness")); }
		Key(Player); Key(Player->GetMesh()); Key(Player->GetMesh()->GetSkeletalMeshAsset()); Key(Player->GetMesh()->LeaderPoseComponent.Get()); Pin(Player->GetMesh()->GetSkeletalMeshAsset()); bool FoundPlayerChooserConsumer=false;
		auto Primary=MakeShared<FJsonObject>(); Primary->SetNumberField(TEXT("actor"),ActorIndex); Primary->SetStringField(TEXT("player"),Player->GetPathName()); Primary->SetStringField(TEXT("component"),Player->GetMesh()->GetPathName());
		Primary->SetStringField(TEXT("asset"),GetPathNameSafe(Player->GetMesh()->GetSkeletalMeshAsset())); Primary->SetStringField(TEXT("leader"),GetPathNameSafe(Player->GetMesh()->LeaderPoseComponent.Get())); PrimaryMeshes.Add(MakeShared<FJsonValueObject>(Primary));
		TInlineComponentArray<USkeletalMeshComponent*> Meshes(Player);
		for (auto* Mesh:Meshes)
		{
			if (!IsValid(Mesh) || !Mesh->IsRegistered()) { return Bad(TEXT("Player skeletal component is not registered")); }
			auto MeshInput=MakeShared<FJsonObject>(); MeshInput->SetNumberField(TEXT("actor"),ActorIndex); MeshInput->SetStringField(TEXT("component"),Mesh->GetPathName());
			MeshInput->SetStringField(TEXT("asset"),GetPathNameSafe(Mesh->GetSkeletalMeshAsset())); MeshInput->SetStringField(TEXT("leader"),GetPathNameSafe(Mesh->LeaderPoseComponent.Get())); MeshInput->SetBoolField(TEXT("source_mesh"),Mesh==Player->GetMesh()); MeshInputs.Add(MakeShared<FJsonValueObject>(MeshInput));
			TArray<UAnimInstance*> Instances; if (auto* Anim=Mesh->GetAnimInstance()) Instances.Add(Anim); if (auto* Post=Mesh->GetPostProcessInstance()) Instances.AddUnique(Post);
			for (auto* Linked:static_cast<const USkeletalMeshComponent*>(Mesh)->GetLinkedAnimInstances()) if (Linked) Instances.AddUnique(Linked);
			for (auto* Instance:Instances)
			{
				if (!IsValid(Instance)) { return Bad(TEXT("Player anim instance retired during readiness")); }
				auto Consumer=MakeShared<FJsonObject>(); Consumer->SetNumberField(TEXT("actor"),ActorIndex); Consumer->SetStringField(TEXT("mesh"),Mesh->GetPathName()); Consumer->SetStringField(TEXT("instance"),Instance->GetPathName()); Consumer->SetStringField(TEXT("class"),Instance->GetClass()->GetPathName()); Consumer->SetStringField(TEXT("binding"),Instance==Mesh->GetAnimInstance() ? TEXT("main") : Instance==Mesh->GetPostProcessInstance() ? TEXT("post") : TEXT("linked"));
				if (!SeenClasses.Contains(Instance->GetClass()))
				{
					SeenClasses.Add(Instance->GetClass()); TArray<TSharedPtr<FJsonValue>> Nodes; bool HasMotionMatch=false;
					if (const auto* Interface=IAnimClassInterface::GetFromClass(Instance->GetClass())) for (const auto* Property:Interface->GetAnimNodeProperties())
					{
						if (!Property || !Property->Struct) { return Bad(TEXT("Invalid anim-node class metadata")); }
						Nodes.Add(MakeShared<FJsonValueString>(Property->Struct->GetPathName())); HasMotionMatch |= Property->Struct->IsChildOf(FAnimNode_MotionMatching::StaticStruct());
					}
					auto Class=MakeShared<FJsonObject>(); Class->SetStringField(TEXT("path"),Instance->GetClass()->GetPathName()); Class->SetArrayField(TEXT("node_types"),Nodes); Classes.Add(MakeShared<FJsonValueObject>(Class));
					if (HasMotionMatch) { Consumers.Add(MakeShared<FJsonValueObject>(Consumer)); return Bad(TEXT("Unsupported live MotionMatching-node consumer (no public candidate getter): ")+Instance->GetClass()->GetPathName()); }
				}
				if (auto* Native=Cast<UProject_JPlayerAnimInstance>(Instance))
				{
					if (!IsValid(Native->GroundAnimationChooser) || !IsValid(Native->AirAnimationChooser)) { return Bad(TEXT("Player Ground/Air chooser roots are missing")); }
					FoundPlayerChooserConsumer=true;
					Key(Mesh); Key(Mesh->GetSkeletalMeshAsset()); Key(Mesh->LeaderPoseComponent.Get()); Key(Instance); Key(Instance->GetClass()); Pin(Mesh->GetSkeletalMeshAsset()); Pin(Instance->GetClass());
					Consumer->SetStringField(TEXT("asset"),GetPathNameSafe(Mesh->GetSkeletalMeshAsset())); Consumer->SetStringField(TEXT("leader"),GetPathNameSafe(Mesh->LeaderPoseComponent.Get()));
					SourceConsumers.Add(MakeShared<FJsonValueObject>(Consumer));
					Roots.AddUnique(Native->GroundAnimationChooser); Roots.AddUnique(Native->AirAnimationChooser); Key(Native->GroundAnimationChooser); Key(Native->AirAnimationChooser);
					Consumer->SetStringField(TEXT("ground_root"),Native->GroundAnimationChooser->GetPathName()); Consumer->SetStringField(TEXT("air_root"),Native->AirAnimationChooser->GetPathName()); Consumer->SetStringField(TEXT("scope"),TEXT("source_player_chooser_input_candidates"));
				}
				else
				{
					// Known PP/contact/physics routes are observed, not certified as free of arbitrary script calls.
					Consumer->SetStringField(TEXT("scope"),TEXT("auxiliary_class_node_metadata_only; function_based_PoseSearch_not_certified"));
					for (TFieldIterator<FObjectPropertyBase> It(Instance->GetClass());It;++It) if (It->PropertyClass && It->PropertyClass->IsChildOf(UChooserTable::StaticClass()))
					{ return Bad(TEXT("Unsupported declared chooser on a non-player animation consumer: ")+Instance->GetClass()->GetPathName()); }
				}
				Consumers.Add(MakeShared<FJsonValueObject>(Consumer));
			}
		}
		if (!FoundPlayerChooserConsumer) { return Bad(FString::Printf(TEXT("Player%d has no actual PlayerAnimInstance chooser consumer"),ActorIndex)); }
	}
	for (auto* Root:Roots) if (!VisitTable(Root)) { return false; }
	if (Roots.IsEmpty() || Databases.IsEmpty()) { return Bad(TEXT("Expected player PoseSearch input closure is empty/unknown")); }
	Scope->SetArrayField(TEXT("consumers"),Consumers); Scope->SetArrayField(TEXT("classes"),Classes); Scope->SetArrayField(TEXT("meshes"),MeshInputs); Scope->SetArrayField(TEXT("candidate_membership"),Membership); Scope->SetNumberField(TEXT("table_count"),Tables.Num()); Scope->SetNumberField(TEXT("canonical_rows"),ChooserCanonicalRowCount); Scope->SetNumberField(TEXT("pose_match_row_visits"),PoseRowVisits);
	// Auxiliary PP/linked metadata may grow naturally. Only actual source consumers and their closed candidate inputs define readiness identity.
	auto Identity=MakeShared<FJsonObject>(); Identity->SetArrayField(TEXT("primary_meshes"),PrimaryMeshes); Identity->SetArrayField(TEXT("source_consumers"),SourceConsumers); Identity->SetArrayField(TEXT("candidate_membership"),Membership);
	if (!FJsonSerializer::Serialize(Identity,TJsonWriterFactory<>::Create(&SourceIdentity))) { return Bad(TEXT("Source candidate identity serialization failed")); }
	return FJsonSerializer::Serialize(Scope,TJsonWriterFactory<>::Create(&Signature));
}

bool UProject_JRenderedCrowdFixture::AdvancePlayerPoseReadiness(bool& Ready,FString& Failure)
{
	check(IsInGameThread()); Ready=false;
	FString Signature,SourceIdentity; TArray<FObjectKey> Keys; TArray<UObject*> Pins; TArray<UPoseSearchDatabase*> Databases;
	if (!CollectPlayerPoseInputs(Signature,SourceIdentity,Keys,Pins,Databases,Failure)) { PlayerPoseLatestScope=Signature; PlayerPoseFailureScope=Signature; bPlayerPoseMembershipUnchanged=false; return false; }
	PlayerPoseLatestScope=Signature;
	if (!bPlayerPosePreparationStarted)
	{
		bPlayerPosePreparationStarted=true; PlayerPosePreparationAt=FPlatformTime::Seconds(); PlayerPoseScope=Signature; PlayerPoseSourceIdentity=SourceIdentity; PlayerPoseScopeKeys=MoveTemp(Keys);
		for (auto* Object:Pins) { PlayerPoseReadinessPins.Add(Object); PlayerPoseOriginalDirty.Add(Object->GetOutermost()->IsDirty()); }
		for (auto* Database:Databases) { auto& Entry=PlayerPoseDatabases.AddDefaulted_GetRef(); Entry.Database=Database; }
	}
	else if (SourceIdentity!=PlayerPoseSourceIdentity || Keys!=PlayerPoseScopeKeys) { PlayerPoseFailureScope=Signature; bPlayerPoseMembershipUnchanged=false; Failure=TEXT("Player pose input/class/root/closure membership changed"); return false; }
	for (int32 Index=0;Index<PlayerPoseReadinessPins.Num();++Index)
		if (!IsValid(PlayerPoseReadinessPins[Index]) || PlayerPoseReadinessPins[Index]->GetOutermost()->IsDirty()!=PlayerPoseOriginalDirty[Index]) { Failure=TEXT("Player pose input package dirty state changed"); return false; }
	if (bPlayerPoseMeasurementAdmitted) { Ready=bPlayerPoseReadinessSuccess; return Ready; } // No build requests in measured phases.
	if (PlayerPoseLastPollFrame==GFrameCounter) { return true; } PlayerPoseLastPollFrame=GFrameCounter;
	const TStrongObjectPtr<UProject_JRenderedCrowdFixture> KeepSelf(this); const TStrongObjectPtr<UWorld> KeepWorld(GetWorld());
	bool AllSuccess=true, IssuedNew=false;
	for (int32 Index=0;Index<PlayerPoseDatabases.Num();++Index)
	{
		const TStrongObjectPtr<UPoseSearchDatabase> Database(PlayerPoseDatabases[Index].Database.Get()); if (!Database.IsValid()) { Failure=TEXT("Player PoseSearch DB retired"); return false; }
		const bool New=!PlayerPoseDatabases[Index].bRequested; PlayerPoseDatabases[Index].bRequested=true;
		if (New) { ++PlayerPoseDatabases[Index].NewRequests; IssuedNew=true; } else { ++PlayerPoseDatabases[Index].Continues; }
		const auto Status=UE::PoseSearch::FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database.Get(),New ? UE::PoseSearch::ERequestAsyncBuildFlag::NewRequest : UE::PoseSearch::ERequestAsyncBuildFlag::ContinueRequest);
		if (!IsValid(this) || bFinished || !KeepWorld.IsValid() || KeepWorld->bIsTearingDown || GetWorld()!=KeepWorld.Get() || !PlayerPoseDatabases.IsValidIndex(Index) || PlayerPoseDatabases[Index].Database.Get()!=Database.Get()) { Failure=TEXT("Readiness scope retired during async index request"); return false; }
		auto& Entry=PlayerPoseDatabases[Index];
		if (New || Entry.Status!=int32(Status)) Entry.Transitions.Add(FString::Printf(TEXT("%s:%s@%llu"),New ? TEXT("NewRequest") : TEXT("Continue"),Status==UE::PoseSearch::EAsyncBuildIndexResult::Success ? TEXT("Success") : Status==UE::PoseSearch::EAsyncBuildIndexResult::Failed ? TEXT("Failed") : TEXT("InProgress"),uint64(GFrameCounter)));
		Entry.Status=int32(Status);
		if (Status==UE::PoseSearch::EAsyncBuildIndexResult::Failed) { Failure=TEXT("Player PoseSearch build Failed: ")+Database->GetPathName(); return false; }
		AllSuccess &= Status==UE::PoseSearch::EAsyncBuildIndexResult::Success;
	}
	FString CurrentSignature,CurrentIdentity; TArray<FObjectKey> CurrentKeys; TArray<UObject*> CurrentPins; TArray<UPoseSearchDatabase*> CurrentDatabases;
	const bool CurrentCollected=CollectPlayerPoseInputs(CurrentSignature,CurrentIdentity,CurrentKeys,CurrentPins,CurrentDatabases,Failure);
	PlayerPoseLatestScope=CurrentSignature;
	if (!CurrentCollected || CurrentIdentity!=PlayerPoseSourceIdentity || CurrentKeys!=PlayerPoseScopeKeys)
	{ PlayerPoseFailureScope=CurrentSignature; bPlayerPoseMembershipUnchanged=false; if (Failure.IsEmpty()) Failure=TEXT("Player pose inputs changed during index requests"); return false; }
	for (int32 Index=0;Index<PlayerPoseReadinessPins.Num();++Index)
		if (!IsValid(PlayerPoseReadinessPins[Index]) || PlayerPoseReadinessPins[Index]->GetOutermost()->IsDirty()!=PlayerPoseOriginalDirty[Index]) { Failure=TEXT("Player pose input dirty state changed during index requests"); return false; }
	if (IssuedNew) { return true; } // A subsequent natural engine frame must confirm Continue/Success.
	if (AllSuccess) { PlayerPoseScope=CurrentSignature; } // Full metadata at admission; later observations are recorded separately.
	bPlayerPoseReadinessSuccess=AllSuccess; Ready=AllSuccess; return true;
}
#endif

bool UProject_JRenderedCrowdFixture::WritePlayerPoseReadiness(bool RunSuccess,const FString& Reason)
{
	if (!bRequirePlayerPoseReadiness || bPlayerPoseReceiptWritten) { return true; }
	bPlayerPoseReceiptWritten=true;
	auto Receipt=MakeShared<FJsonObject>(); Receipt->SetNumberField(TEXT("schema"),1); Receipt->SetStringField(TEXT("scope"),TEXT("source_player_chooser_input_candidates"));
	Receipt->SetBoolField(TEXT("run_success"),RunSuccess); Receipt->SetStringField(TEXT("reason"),Reason); Receipt->SetNumberField(TEXT("player_count"),Players.Num());
	Receipt->SetBoolField(TEXT("all_candidate_disk_hashes_known"),false); Receipt->SetBoolField(TEXT("actual_selection_proven"),false); Receipt->SetBoolField(TEXT("private_effective_build_key_known"),false); Receipt->SetBoolField(TEXT("all_animation_consumers_ready"),false);
	TArray<TSharedPtr<FJsonValue>> Databases;
#if WITH_EDITOR
	bool DirtyUnchanged=PlayerPoseReadinessPins.Num()==PlayerPoseOriginalDirty.Num();
	for (int32 Index=0;Index<PlayerPoseReadinessPins.Num();++Index) DirtyUnchanged &= IsValid(PlayerPoseReadinessPins[Index]) && PlayerPoseReadinessPins[Index]->GetOutermost()->IsDirty()==PlayerPoseOriginalDirty[Index];
	Receipt->SetBoolField(TEXT("dirty_unchanged"),DirtyUnchanged); Receipt->SetBoolField(TEXT("input_membership_unchanged"),bPlayerPoseMembershipUnchanged);
	Receipt->SetStringField(TEXT("input_identity_scope"),TEXT("Primary/source PlayerAnimInstance, mesh asset/leader, class, roots and static row/fallback/DB/schema candidates; auxiliary observations are separate"));
	Receipt->SetBoolField(TEXT("observed_metadata_changed_since_admission"),PlayerPoseLatestScope!=PlayerPoseScope);
	Receipt->SetBoolField(TEXT("readiness_success"),bPlayerPoseReadinessSuccess && bPlayerPoseMembershipUnchanged && DirtyUnchanged); Receipt->SetBoolField(TEXT("measurement_admitted"),bPlayerPoseMeasurementAdmitted);
	Receipt->SetStringField(TEXT("admission_frame"),LexToString(PlayerPoseAdmissionFrame)); Receipt->SetNumberField(TEXT("admission_phase"),bPlayerPoseMeasurementAdmitted ? 2 : 0);
	Receipt->SetNumberField(TEXT("preparation_wall_seconds"),bPlayerPosePreparationStarted ? (bPlayerPoseMeasurementAdmitted ? PlayerPoseAdmissionAt : FPlatformTime::Seconds())-PlayerPosePreparationAt : 0);
	TSharedPtr<FJsonObject> Scope; if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(PlayerPoseScope),Scope) && Scope.IsValid()) Receipt->SetObjectField(TEXT("configured_inputs"),Scope);
	TSharedPtr<FJsonObject> LatestScope; if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(PlayerPoseLatestScope),LatestScope) && LatestScope.IsValid()) Receipt->SetObjectField(TEXT("latest_observed_inputs"),LatestScope);
	TSharedPtr<FJsonObject> SourceIdentity; if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(PlayerPoseSourceIdentity),SourceIdentity) && SourceIdentity.IsValid()) Receipt->SetObjectField(TEXT("source_candidate_identity"),SourceIdentity);
	TSharedPtr<FJsonObject> FailureScope; if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(PlayerPoseFailureScope),FailureScope) && FailureScope.IsValid()) Receipt->SetObjectField(TEXT("failure_inputs"),FailureScope);
	for (const auto& Entry:PlayerPoseDatabases)
	{
		auto Database=MakeShared<FJsonObject>(); Database->SetStringField(TEXT("path"),GetPathNameSafe(Entry.Database.Get())); Database->SetStringField(TEXT("status"),Entry.Status==int32(UE::PoseSearch::EAsyncBuildIndexResult::Success) ? TEXT("Success") : Entry.Status==int32(UE::PoseSearch::EAsyncBuildIndexResult::Failed) ? TEXT("Failed") : Entry.bRequested ? TEXT("InProgress") : TEXT("NotRequested"));
		Database->SetNumberField(TEXT("new_requests"),Entry.NewRequests); Database->SetNumberField(TEXT("continues"),Entry.Continues); TArray<TSharedPtr<FJsonValue>> Transitions; for (const auto& Transition:Entry.Transitions) Transitions.Add(MakeShared<FJsonValueString>(Transition)); Database->SetArrayField(TEXT("transitions"),Transitions); Databases.Add(MakeShared<FJsonValueObject>(Database));
	}
#else
	Receipt->SetBoolField(TEXT("readiness_success"),false); Receipt->SetBoolField(TEXT("measurement_admitted"),false); Receipt->SetBoolField(TEXT("input_membership_unchanged"),false); Receipt->SetBoolField(TEXT("dirty_unchanged"),false);
#endif
	Receipt->SetNumberField(TEXT("database_count"),Databases.Num()); Receipt->SetArrayField(TEXT("databases"),Databases); FString Json;
	return FJsonSerializer::Serialize(Receipt,TJsonWriterFactory<>::Create(&Json)) && FFileHelper::SaveStringToFile(Json,*(Output/TEXT("player-pose-readiness.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_NoReplaceExisting);
}

bool UProject_JRenderedCrowdFixture::PrepareNetworkView(APlayerController* Observer)
{
    if (!bNetworkObserver || bFinished || bNetworkViewRetired || GUsingNullRHI || !IsValid(Observer) ||
        Observer->GetWorld() != GetWorld() || GetWorld()->bIsTearingDown || Observer->IsActorBeingDestroyed() ||
        GetWorld()->GetNetMode() != NM_Client || !Observer->IsLocalController()) return false;
    if (NetworkObserver.IsValid()) return NetworkObserver == Observer && IsValid(ReviewCamera);
    const TStrongObjectPtr<UProject_JRenderedCrowdFixture> KeepSelf(this);
    const TStrongObjectPtr<APlayerController> KeepObserver(Observer);
    const TStrongObjectPtr<UWorld> World(GetWorld());
    NetworkObserver = Observer; NetworkWaitStarted = FPlatformTime::Seconds();
    PreviousViewTarget = Observer->GetViewTarget();
    const auto Current=[&]
    {
        return IsValid(this) && !bFinished && !bNetworkViewRetired && World.IsValid() && !World->bIsTearingDown &&
            GetWorld()==World.Get() && KeepObserver.IsValid() && !Observer->IsActorBeingDestroyed() &&
            Observer->GetWorld()==World.Get() && NetworkObserver==Observer;
    };
    FActorSpawnParameters Params; Params.ObjectFlags |= RF_Transient;
    const TStrongObjectPtr<AStaticMeshActor> Floor(World->SpawnActor<AStaticMeshActor>(FVector(1710,900,-50), FRotator::ZeroRotator, Params));
    if (!Floor.IsValid() || !Current())
    { if (IsValid(Floor.Get())) Floor->Destroy(); StopNetworkObservation(); return false; }
    NetworkFloor = Floor.Get();
    const TStrongObjectPtr<UStaticMesh> Cube(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    if (!Cube.IsValid() || !Current() || !IsValid(Floor.Get())) { StopNetworkObservation(); return false; }
    // Disable collision before assigning the mesh; physical floor already exists.
    Floor->GetStaticMeshComponent()->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
    if (!Current() || !IsValid(Floor.Get())) { StopNetworkObservation(); return false; }
    Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
    if (!Current() || !IsValid(Floor.Get())) { StopNetworkObservation(); return false; }
    Floor->GetStaticMeshComponent()->SetStaticMesh(Cube.Get());
    if (!Current() || !IsValid(Floor.Get())) { StopNetworkObservation(); return false; }
    Floor->SetActorScale3D(FVector(150,150,1));
    if (!Current() || !IsValid(Floor.Get())) { StopNetworkObservation(); return false; }
    const auto* FloorMesh=Floor->GetStaticMeshComponent();
    UE_LOG(LogTemp,Display,TEXT("PROJECT_J_NETWORK_VISUAL_FLOOR profile=%s enabled=%d default_collision=%d registered=%d"),
        *FloorMesh->GetCollisionProfileName().ToString(),int32(FloorMesh->GetCollisionEnabled()),FloorMesh->bUseDefaultCollision!=0,FloorMesh->IsRegistered());
    if (!FloorMesh->IsRegistered() || FloorMesh->GetCollisionProfileName()!=UCollisionProfile::NoCollision_ProfileName ||
        FloorMesh->GetCollisionEnabled()!=ECollisionEnabled::NoCollision || FloorMesh->bUseDefaultCollision)
    { Finish(false,TEXT("Network visual floor did not preserve NoCollision profile")); StopNetworkObservation(); return false; }
    const TStrongObjectPtr<ADirectionalLight> Light(World->SpawnActor<ADirectionalLight>(FVector(0,0,500), FRotator(-40,-45,0), Params));
    if (!Light.IsValid() || !Current())
    { if (IsValid(Light.Get())) Light->Destroy(); StopNetworkObservation(); return false; }
    NetworkLight = Light.Get();
    const FVector Position(1710,-3000,3200), Center(1710,900,100);
    const TStrongObjectPtr<ACameraActor> Camera(World->SpawnActor<ACameraActor>(Position, (Center-Position).Rotation(), Params));
    if (!Camera.IsValid() || !Current())
    { if (IsValid(Camera.Get())) Camera->Destroy(); StopNetworkObservation(); return false; }
    ReviewCamera = Camera.Get();
    ReviewCamera->GetCameraComponent()->SetFieldOfView(65);
    if (!Current() || !IsValid(Camera.Get())) { StopNetworkObservation(); return false; }
    Observer->SetViewTarget(ReviewCamera);
    if (!Current() || Observer->GetViewTarget()!=Camera.Get()) { StopNetworkObservation(); return false; }
    return true;
}

bool UProject_JRenderedCrowdFixture::BindNetworkPlayers(const TArray<AProject_JPlayerCharacter*>& Actors)
{
    if (!bNetworkObserver || bFinished || !Players.IsEmpty() || Actors.Num() != 200 || !NetworkObserver.IsValid() || !ReviewCamera) return false;
    TSet<AProject_JPlayerCharacter*> Unique;
    for (int32 I=0; I<Actors.Num(); ++I)
    {
        auto* Player=Actors[I];
        const auto* ASC=IsValid(Player) ? Player->GetAbilitySystemComponent() : nullptr;
        const auto* Runtime=IsValid(Player) ? Player->FindComponentByClass<UProject_JEquipmentRuntimeComponent>() : nullptr;
        const auto* Presentation=IsValid(Player) ? Player->FindComponentByClass<UProject_JWeaponPresentationComponent>() : nullptr;
        const auto* Weapon=Presentation ? Cast<AProject_JWeaponPresentationActor>(Presentation->GetSpawnedWeapon()) : nullptr;
        const auto Visuals=Weapon ? Weapon->GetVisualMeshes() : TArray<UMeshComponent*>();
        if (!IsValid(Player) || Unique.Contains(Player) || Player->GetWorld()!=GetWorld() || Player->HasAuthority() ||
            Player->GetLocalRole()!=(I==0 ? ROLE_AutonomousProxy : ROLE_SimulatedProxy) || Player->IsLocallyControlled()!=(I==0) ||
            (I==0 && Player!=NetworkObserver->GetPawn()) || !Player->GetPlayerState() || !ASC ||
            ASC->GetOwnerActor()!=Player->GetPlayerState() || ASC->GetAvatarActor()!=Player ||
            !Runtime || !Runtime->GetPresentedItemInSlot(EProject_JEquipmentSlot::Weapon) || !Player->GetCurrentCombatStyle() ||
            !Player->GetCurrentWeaponPresentationProfile() || !Player->GetMesh()->GetAnimInstance() ||
            Visuals.IsEmpty()) return false;
        for (const auto* Visual:Visuals) if (!Visual || !Visual->IsVisible()) return false;
        Unique.Add(Player);
        int32 Bodies=0;
        TInlineComponentArray<USkeletalMeshComponent*> Meshes(Player);
        for (const auto* Mesh:Meshes)
            Bodies += Mesh->GetSkeletalMeshAsset() && Project_J::Animation::FindBodyContactController(Mesh)!=nullptr;
        if (Bodies!=1) return false;
    }
    for (int32 I=0; I<Actors.Num(); ++I) { Players.Add(Actors[I]); RegisterObservedPlayer(Actors[I], I); NetworkSawJump.Add(false); }
    const auto* Visual=GetWorld()->GetSubsystem<UProject_JVisualAssetSubsystem>();
    const auto* Presentation=GetWorld()->GetSubsystem<UProject_JPresentationBudgetSubsystem>();
    const auto V=Visual ? Visual->GetStats() : FProjectJVisualAssetStats{};
    const auto P=Presentation ? Presentation->GetStats() : FProjectJPresentationBudgetStats{};
    const auto* FlyingReuse=IConsoleManager::Get().FindConsoleVariable(TEXT("mover.Prediction.ReuseFlyingParams"));
    const FString Preparation=FString::Printf(TEXT("{\"actors\":200,\"autonomous\":1,\"simulated\":199,\"authority\":0,\"owner\":1,\"sample_index_zero_class_id\":\"%s\",\"flying_reuse_level\":%d,\"wait_wall_s\":%.9f,\"visual_leases\":%d,\"visual_groups\":%d,\"visual_in_flight\":%d,\"visual_peak_in_flight\":%d,\"visual_rejected\":%llu,\"presentation_pending\":%d,\"presentation_rejected\":%llu,\"sample_before_ready\":false}"),
        *Actors[0]->GetPlayerState<AProject_JPlayerState>()->GetPublicClassId().ToString(),FlyingReuse ? FlyingReuse->GetInt():-1,FPlatformTime::Seconds()-NetworkWaitStarted, Visual ? Visual->GetLeaseCount():0, Visual ? Visual->GetGroupCount():0,
        V.InFlight, V.PeakInFlight, V.Rejected, P.Pending, P.Rejected);
    if (!FFileHelper::SaveStringToFile(Preparation, *(Output/TEXT("network-preparation.json"))))
    { Finish(false,TEXT("Network appearance readiness evidence write failed")); return false; }
    Phase=RequestedNetworkPhase; PhaseAt=GetWorld()->GetTimeSeconds();
    return Phase==1;
}

void UProject_JRenderedCrowdFixture::SetNetworkPhase(int32 InPhase)
{
    if (!bNetworkObserver || bFinished || InPhase<1 || InPhase>6) return;
    if (RequestedNetworkPhase==InPhase) return;
    if(InPhase>=3 && InPhase<=5 && FParse::Param(FCommandLine::Get(),TEXT("ProjectJRenderedDecodedEndpoints")))
    {
        const auto* Controller=NetworkObserver.Get();
        const APawn* Pawn=Controller ? Controller->GetPawn():nullptr;
        const auto* Camera=Controller ? Controller->PlayerCameraManager.Get():nullptr;
        if(Controller && Pawn && Camera)
        {
            FVector Location; FRotator Rotation;
            Controller->GetPlayerViewPoint(Location,Rotation);
            const auto View=MakeShared<FJsonObject>();
            View->SetNumberField(TEXT("phase"),InPhase);
            View->SetNumberField(TEXT("world_seconds"),GetWorld()->GetTimeSeconds());
            View->SetNumberField(TEXT("x"),Location.X); View->SetNumberField(TEXT("y"),Location.Y); View->SetNumberField(TEXT("z"),Location.Z);
            View->SetNumberField(TEXT("pitch"),Rotation.Pitch); View->SetNumberField(TEXT("yaw"),Rotation.Yaw);
            View->SetNumberField(TEXT("fov"),Camera->GetFOVAngle());
            View->SetStringField(TEXT("legacy_movement_component"),GetNameSafe(Pawn->GetMovementComponent()));
            View->SetBoolField(TEXT("mover_present"),Project_J::Movement::FindMover(Pawn)!=nullptr);
            View->SetBoolField(TEXT("use_client_side_camera_updates"),Camera->bUseClientSideCameraUpdates);
            View->SetBoolField(TEXT("should_send_client_side_camera_update"),Camera->bShouldSendClientSideCameraUpdate);
            FString Json;
            if(!FJsonSerializer::Serialize(View,TJsonWriterFactory<>::Create(&Json)) ||
                !FFileHelper::SaveStringToFile(Json,*(Output/FString::Printf(TEXT("client-view-phase%d.json"),InPhase))))
            { Finish(false,TEXT("Client view observation write failed")); return; }
        }
    }
    if (InPhase!=RequestedNetworkPhase+1 || (InPhase>1 && CompletedNetworkPhase!=InPhase-1))
    { Finish(false,TEXT("Network render phase skipped or advanced before measurement completion")); return; }
    if (InPhase==5 && FParse::Param(FCommandLine::Get(),TEXT("ProjectJRenderedDecodedEndpoints")))
    {
        // Phase4 is eight GAME seconds; the existing capture has its separate
        // 120s WALL cap. Never use an8s wall timeout to claim full phase4 evidence.
        const TStrongObjectPtr<UProject_JRenderedCrowdFixture> KeepSelf(this);
        const TStrongObjectPtr<UWorld> KeepWorld(GetWorld());
        const TStrongObjectPtr<APlayerController> KeepObserver(NetworkObserver.Get());
        const int32 ExpectedPhase=RequestedNetworkPhase;
        if (!KeepWorld.IsValid() || KeepWorld->bIsTearingDown || !GEngine ||
            !GEngine->Exec(KeepWorld.Get(),TEXT("ProjectJ.CaptureMoverBuffers Stop")))
        { Finish(false,TEXT("Existing decoded endpoint capture stop unavailable")); return; }
        if (bFinished || bNetworkViewRetired || !bNetworkObserver || GetWorld()!=KeepWorld.Get() || KeepWorld->bIsTearingDown ||
            !IsValid(KeepObserver.Get()) || KeepObserver->IsActorBeingDestroyed() || NetworkObserver.Get()!=KeepObserver.Get() ||
            KeepObserver->GetWorld()!=KeepWorld.Get() || RequestedNetworkPhase!=ExpectedPhase || CompletedNetworkPhase!=4) return;
    }
    RequestedNetworkPhase=InPhase;
    if (Players.Num()==CrowdCount)
    {
        Phase=InPhase; PhaseAt=GetWorld()->GetTimeSeconds(); Samples=VisibleSamples=0; bShot=false; bJumpRequested=false;
    }
}

void UProject_JRenderedCrowdFixture::StopNetworkObservation()
{
    if (!bNetworkObserver) return;
    const TStrongObjectPtr<UProject_JRenderedCrowdFixture> KeepSelf(this);
	RestoreClothExecutionOverrides();
    bNetworkViewRetired=true; // One fixture view lease; cleanup callbacks cannot recreate it.
    const TStrongObjectPtr<ACameraActor> Camera(ReviewCamera.Get());
    const TStrongObjectPtr<AActor> Floor(NetworkFloor.Get()), Light(NetworkLight.Get());
    const TStrongObjectPtr<APlayerController> Observer(NetworkObserver.Get());
    const TStrongObjectPtr<AActor> Previous(PreviousViewTarget.Get());
    ReviewCamera=nullptr; NetworkFloor.Reset(); NetworkLight.Reset(); NetworkObserver.Reset();
    PreviousViewTarget.Reset();
    if (Observer.IsValid() && !Observer->IsActorBeingDestroyed() && Observer->GetViewTarget()==Camera.Get() && IsValid(Previous.Get()))
        Observer->SetViewTarget(Previous.Get());
    if (IsValid(Camera.Get())) Camera->Destroy();
    if (IsValid(Light.Get())) Light->Destroy();
    if (IsValid(Floor.Get())) Floor->Destroy();
    // Replicated Avatars belong to the server/real controllers, never this sampler.
}

void UProject_JRenderedCrowdFixture::ObserveNetworkMotion(int32 InPhase,int32 ActorIndex,
    const AProject_JPlayerCharacter* ObservedAvatar,const FVector& Position,const FVector& Velocity,double PhaseAge)
{
    static const bool bRequested=FParse::Param(FCommandLine::Get(),TEXT("ProjectJRenderedClientFixture"));
    const UWorld* ObservationWorld=GetWorld();
    if (!bRequested || InPhase<3 || InPhase>4 || ActorIndex<0 || ActorIndex>=200 || !IsValid(ObservedAvatar) ||
        !IsValid(ObservationWorld) || ObservedAvatar->GetWorld()!=ObservationWorld || ObservationWorld->bIsTearingDown) return;
    if (NetworkMotionWitness.IsEmpty()) NetworkMotionWitness.SetNum(400);
    auto& Witness=NetworkMotionWitness[(InPhase-3)*200+ActorIndex];
    if (!FMath::IsFinite(Position.X) || !FMath::IsFinite(Velocity.X) || !FMath::IsFinite(PhaseAge)) { ++Witness.InvalidSamples; return; }
    const auto* Mover=ObservedAvatar->GetMover();
    const uint64 QPC=FPlatformTime::Cycles64();
    if (Witness.Samples==0)
    {
        Witness.ObjectKey=FObjectKey(ObservedAvatar); Witness.Role=int32(ObservedAvatar->GetLocalRole());
        const auto* Bridge=UE::Net::FReplicationSystemUtil::GetActorReplicationBridge(ObservedAvatar);
        Witness.ActorId=Bridge ? Bridge->GetReplicatedRefHandle(ObservedAvatar).GetId():0;
        Witness.FirstQPC=QPC; Witness.FirstPhaseAge=PhaseAge; Witness.FirstWorldTime=ObservationWorld->GetTimeSeconds();
        Witness.FirstX=Position.X; Witness.MinVx=Witness.MaxVx=Velocity.X;
        if (Mover) { Witness.FirstSimFrame=Mover->GetLastTimeStep().ServerFrame; Witness.FirstSimMs=Mover->GetLastTimeStep().BaseSimTimeMs; Witness.FirstMode=Mover->GetMovementModeName(); }
    }
    else
    {
        if (Witness.ObjectKey!=FObjectKey(ObservedAvatar) || Witness.Role!=int32(ObservedAvatar->GetLocalRole())) { Witness.bIdentityStable=false; return; }
        const double DX=Position.X-Witness.LastX;
        Witness.MinDX=FMath::Min(Witness.MinDX,DX); Witness.MaxDX=FMath::Max(Witness.MaxDX,DX);
        Witness.PositiveDX+=DX>0.; Witness.NegativeDX+=DX<0.;
        if (DX>0.) Witness.SumPositiveDX+=DX; else Witness.SumNegativeDX+=DX;
    }
    ++Witness.Samples;
    // Exact existing reversal threshold, recorded only; this does not decide a pass.
    Witness.PositiveVx+=Velocity.X>5.; Witness.NegativeVx+=Velocity.X< -5.;
    Witness.MinVx=FMath::Min(Witness.MinVx,Velocity.X); Witness.MaxVx=FMath::Max(Witness.MaxVx,Velocity.X);
    Witness.LastX=Position.X; Witness.LastPhaseAge=PhaseAge; Witness.LastWorldTime=ObservationWorld->GetTimeSeconds(); Witness.LastQPC=QPC;
    if (Mover) { Witness.LastSimFrame=Mover->GetLastTimeStep().ServerFrame; Witness.LastSimMs=Mover->GetLastTimeStep().BaseSimTimeMs; Witness.LastMode=Mover->GetMovementModeName(); }
}

void UProject_JRenderedCrowdFixture::WriteNetworkMotionWitness(int32 InPhase,const FString& Filename) const
{
    if (InPhase<3 || InPhase>4 || NetworkMotionWitness.Num()!=400) return;
    FString Data=TEXT("phase,actor_id,sample_index,role,identity_stable,samples,invalid_samples,min_vx,max_vx,positive_vx_over5,negative_vx_below_minus5,min_actor_dx,max_actor_dx,positive_dx_samples,negative_dx_samples,sum_positive_dx_cm,sum_negative_dx_cm,first_actor_x,last_actor_x,first_phase_age,last_phase_age,first_world_s,last_world_s,first_qpc,last_qpc,first_sim_frame,last_sim_frame,first_sim_base_ms,last_sim_base_ms,first_mode,last_mode\n");
    for (int32 I=0; I<200; ++I)
    {
        const auto& W=NetworkMotionWitness[(InPhase-3)*200+I];
        if (W.Samples==0 && W.InvalidSamples==0) continue;
        Data+=FString::Printf(TEXT("%d,%llu,%d,%d,%d,%d,%d,%.9f,%.9f,%d,%d,%.9f,%.9f,%d,%d,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%llu,%llu,%d,%d,%.9f,%.9f,%s,%s\n"),
            InPhase,W.ActorId,I,W.Role,W.bIdentityStable,W.Samples,W.InvalidSamples,W.MinVx,W.MaxVx,W.PositiveVx,W.NegativeVx,
            W.MinDX,W.MaxDX,W.PositiveDX,W.NegativeDX,W.SumPositiveDX,W.SumNegativeDX,W.FirstX,W.LastX,
            W.FirstPhaseAge,W.LastPhaseAge,W.FirstWorldTime,W.LastWorldTime,W.FirstQPC,W.LastQPC,W.FirstSimFrame,W.LastSimFrame,
            W.FirstSimMs,W.LastSimMs,*W.FirstMode.ToString(),*W.LastMode.ToString());
    }
    if (!FFileHelper::SaveStringToFile(Data,*Filename)) UE_LOG(LogTemp,Error,TEXT("Network motion witness write failed: %s"),*Filename);
}
