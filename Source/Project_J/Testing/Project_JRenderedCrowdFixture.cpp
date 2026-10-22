#include "Testing/Project_JRenderedCrowdFixture.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Movement/Project_JMovementQueries.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Testing/Project_JAuthoredAnimationFixture.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JPlayerAnimInstance.h"
#include "Components/Project_JAnimationUpdateCoordinatorComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "DynamicRHI.h"
#include "RenderTimer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "ProfilingDebugging/MiscTrace.h"

void UProject_JRenderedCrowdFixture::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("ProjectJRenderedCrowdFixture"))) { return; }
	Started = FPlatformTime::Seconds();
	bWithLocalPlayer = FParse::Param(FCommandLine::Get(), TEXT("ProjectJRenderedCrowdLocalPlayer"));
	bSimulateMovement = FParse::Param(FCommandLine::Get(), TEXT("ProjectJRenderedCrowdMovement"));
	FParse::Value(FCommandLine::Get(), TEXT("ProjectJRenderedCrowdCount="), CrowdCount);
	CrowdCount = FMath::Clamp(CrowdCount, 1, 200);
	FParse::Value(FCommandLine::Get(), TEXT("ProjectJRenderedCrowdOutput="), Output);
	if (Output.IsEmpty()) { Output = FPaths::ProjectSavedDir() / TEXT("Validation/RenderedCrowd"); }
	IFileManager::Get().MakeDirectory(*Output, true);
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
		PreviousPositions.Add(Player->GetActorLocation());TravelDistances.Add(0.);Reversals.Add(0);Landings.Add(0);WasInAir.Add(false);LastDirection.Add(0.f);
	}
	World->SpawnActor<ADirectionalLight>(FVector(0, 0, 500), FRotator(-40, -45, 0));
	const FVector Center(1710, 900, 100), Position(1710, -3000, 3200);
	auto* Camera = World->SpawnActor<ACameraActor>(Position, (Center - Position).Rotation());
	if (!Camera) { return false; }
	Camera->GetCameraComponent()->SetFieldOfView(65);
	World->GetFirstPlayerController()->SetViewTarget(Camera);
	return true;
}
bool UProject_JRenderedCrowdFixture::Tick(float Delta)
{
	if (bFinished) { return false; }
	const double Now = FPlatformTime::Seconds();
	if (Now - Started > 180) { Finish(false, TEXT("Bounded rendered fixture timeout")); return false; }
	auto* World = GetWorld();
	if (!World || !World->HasBegunPlay() || !World->GetFirstPlayerController()) { return true; }
	if (Phase == 0)
	{
		if (Now - Started < 2) { return true; }
		if (!Setup()) { Finish(false, TEXT("Real RHI/authored 200 body setup failed")); return false; }
		Phase = 1; PhaseAt = FPlatformTime::Seconds();
		TRACE_BOOKMARK(TEXT("ProjectJ.RenderedCrowd Warmup Actors=200"));
		return true;
	}
	int32 Visible = 0, Montages = 0, Prediction30=0;
	int32 BodyLODs[3]={}, LocalLOD=INDEX_NONE;
	for (int32 Index=0;Index<Players.Num();++Index)
	{
		auto* Player=Players[Index].Get();
		if (!IsValid(Player)) { Finish(false, TEXT("Authored actor disappeared")); return false; }
		if (bSimulateMovement)
		{
			const double Age=Now-PhaseAt;
			const float Direction=Phase==3 || Phase==4 ? (FMath::Fmod(Age,2.)<1.?1.f:-1.f):0.f;
			const FVector Local=FRotator(0,Player->GetControlRotation().Yaw,0).UnrotateVector(FVector(Direction,0,0));
			Player->GetLocomotionAnimStateComponent()->SetMoveInput(FVector2D(Local.Y,Local.X));
			if(Phase==4 && !bJumpRequested) Player->Jump();
			else if(Phase!=4 || Player->GetMover()->IsAirborne()) Player->StopJumping();
			const FVector Position=Player->GetActorLocation();
			TravelDistances[Index]+=FVector::Dist2D(Position,PreviousPositions[Index]);PreviousPositions[Index]=Position;
			const float Velocity=Player->GetVelocity().X;
			if(FMath::Abs(Velocity)>5.f) { const float Sign=FMath::Sign(Velocity);if(LastDirection[Index]!=0 && LastDirection[Index]!=Sign) ++Reversals[Index];LastDirection[Index]=Sign; }
			const bool bInAir=Project_J::Movement::IsInAir(Player);if(WasInAir[Index] && !bInAir) ++Landings[Index];WasInAir[Index]=bInAir;
		}
		TInlineComponentArray<USkeletalMeshComponent*> Meshes(Player);
		bool bVisible = false;
		for (auto* Mesh : Meshes)
			if (Cast<UProject_JRetargetAnimInstance>(Mesh->GetAnimInstance()))
			{
				bVisible |= Mesh->WasRecentlyRendered(.2f);
				const int32 LOD=Mesh->GetPredictedLODLevel();
				if(LOD>=0 && LOD<3) ++BodyLODs[LOD];
				if(bWithLocalPlayer && Player==Players[0]) LocalLOD=LOD;
			}
		Visible += bVisible;
		Montages += Player->GetMesh()->GetAnimInstance()->IsAnyMontagePlaying();
		if(const auto* Anim=Cast<UProject_JPlayerAnimInstance>(Player->GetMesh()->GetAnimInstance()))
		{
			Prediction30+=Anim->GetTrajectoryPredictionRate()==30;
			const auto& Policy=Anim->GetCurrentOptimizationPolicy();
			if(Anim->IsAnyMontagePlaying() && (!Policy.bEnableHandIK || !Policy.bEnableFootIK || !Policy.bEnableRetargetIK || !Policy.bEnableFollowerRetarget))
			{ Finish(false,TEXT("An active montage lost source IK/retarget quality"));return false; }
		}
	}
	const bool bCapture = Phase > 1 && !bShot && Now - PhaseAt > 1;
	Rows += FString::Printf(TEXT("%d,%.5f,%.6f,%.6f,%d,%d,%d,%d,%d,%d,%d,%d,%.6f,%.6f,%.6f,%d\n"), Phase, Now - Started, Delta * 1000,
		FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()), Players.Num(), Visible, Montages, bCapture,BodyLODs[0],BodyLODs[1],BodyLODs[2],LocalLOD,
		FPlatformTime::ToMilliseconds(GGameThreadTime),FPlatformTime::ToMilliseconds(GRenderThreadTime),FPlatformTime::ToMilliseconds(GRHIThreadTime),Prediction30);
	if (bSimulateMovement && Phase==4) bJumpRequested=true;
	if (Phase > 1) { ++Samples; VisibleSamples += Visible == CrowdCount; }
	if (Phase > 1 && bWithLocalPlayer)
	{
		const auto* Local = Players[0].Get();
		const auto* Anim = Cast<UProject_JCharacterAnimInstance>(Local->GetMesh()->GetAnimInstance());
		const auto* Coordinator = Local->FindComponentByClass<UProject_JAnimationUpdateCoordinatorComponent>();
		if (LocalLOD!=0 || !Local->IsPlayerControlled() || !Local->IsLocallyControlled() || !Anim ||
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
		bShot = true; FScreenshotRequest::RequestScreenshot(Output / FString::Printf(TEXT("phase%d.png"), Phase), false, false);
	}
	if (Now - PhaseAt >= 8)
	{
		if (Phase > 1 && (!Samples || VisibleSamples < Samples / 2))
		{ Finish(false, TEXT("All 200 retarget bodies were not observed rendered for half the measurement")); return false; }
		if (Phase == (bSimulateMovement?6:3)) { Finish(true, TEXT("Actual authored retarget bodies; real viewport; no remote connections")); return false; }
		++Phase; PhaseAt = Now; Samples = VisibleSamples = 0; bShot = false;
		TRACE_BOOKMARK(TEXT("ProjectJ.RenderedCrowd Phase=%d Actors=200"), Phase);
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
	bFinished = true;
	if (bSimulateMovement)
	{
		FString Work=TEXT("actor,travel_cm,reversals,landings\n");
		for(int32 Index=0;Index<Players.Num();++Index)
		{
			Work+=FString::Printf(TEXT("%d,%.3f,%d,%d\n"),Index,TravelDistances[Index],Reversals[Index],Landings[Index]);
			bSuccess &= TravelDistances[Index]>100.f && Reversals[Index]>0 && Landings[Index]>0;
		}
		FFileHelper::SaveStringToFile(Work,*(Output/TEXT("workload.csv")));
	}
	FFileHelper::SaveStringToFile(Rows, *(Output / TEXT("frames.csv")));
	FFileHelper::SaveStringToFile(FString::Printf(TEXT("success=%d\ncooked=%d\nactors=%d\nlocal_player=%d\nlocal_quality_samples=%d\nreason=%s\n"), bSuccess,
		FPlatformProperties::RequiresCookedData(), Players.Num(), bWithLocalPlayer, LocalQualitySamples, Reason), *(Output / TEXT("result.txt")));
	FPlatformMisc::RequestExit(false);
}
void UProject_JRenderedCrowdFixture::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(Handle);
	Super::Deinitialize();
}
