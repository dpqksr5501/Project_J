#include "Testing/Project_JRenderedCrowdFixture.h"
#include "Components/SkeletalMeshComponent.h"
#include "Testing/Project_JAuthoredAnimationFixture.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "DynamicRHI.h"
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
	for (int32 I = 0; I < 200; ++I)
	{
		auto* Controller = World->SpawnActor<AAIController>(Params);
		auto* State = World->SpawnActor<AProject_JPlayerState>(Params);
		if (!Controller || !State) { return false; }
		Controller->SetPlayerState(State); State->SetOwner(Controller);
		auto* Player = ProjectJAuthoredAnimationFixture::Spawn(World, Controller, State, FVector((I % 20) * 180, (I / 20) * 200, 100));
		if (!Player) { return false; }
		Controller->UnPossess(); Player->SetPlayerState(State);
		CastChecked<UProject_JAbilitySystemComponent>(Player->GetAbilitySystemComponent())->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
		Players.Add(Player);
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
	int32 Visible = 0, Montages = 0;
	for (AProject_JPlayerCharacter* Player : Players)
	{
		if (!IsValid(Player)) { Finish(false, TEXT("Authored actor disappeared")); return false; }
		TInlineComponentArray<USkeletalMeshComponent*> Meshes(Player);
		bool bVisible = false;
		for (auto* Mesh : Meshes)
			if (Cast<UProject_JRetargetAnimInstance>(Mesh->GetAnimInstance())) bVisible |= Mesh->WasRecentlyRendered(.2f);
		Visible += bVisible;
		Montages += Player->GetMesh()->GetAnimInstance()->IsAnyMontagePlaying();
	}
	const bool bCapture = Phase > 1 && !bShot && Now - PhaseAt > 1;
	Rows += FString::Printf(TEXT("%d,%.5f,%.6f,%.6f,%d,%d,%d,%d\n"), Phase, Now - Started, Delta * 1000,
		FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()), Players.Num(), Visible, Montages, bCapture);
	if (Phase > 1) { ++Samples; VisibleSamples += Visible == 200; }
	if (bCapture)
	{
		bShot = true; FScreenshotRequest::RequestScreenshot(Output / FString::Printf(TEXT("phase%d.png"), Phase), false, false);
	}
	if (Now - PhaseAt >= 8)
	{
		if (Phase > 1 && (!Samples || VisibleSamples < Samples / 2))
		{ Finish(false, TEXT("All 200 retarget bodies were not observed rendered for half the measurement")); return false; }
		if (Phase == 3) { Finish(true, TEXT("200 actual authored retarget bodies; real viewport idle/attack; no remote connections")); return false; }
		++Phase; PhaseAt = Now; Samples = VisibleSamples = 0; bShot = false;
		TRACE_BOOKMARK(TEXT("ProjectJ.RenderedCrowd Phase=%d Actors=200"), Phase);
		if (Phase == 3) for (AProject_JPlayerCharacter* Player : Players)
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
	FFileHelper::SaveStringToFile(Rows, *(Output / TEXT("frames.csv")));
	FFileHelper::SaveStringToFile(FString::Printf(TEXT("success=%d\ncooked=%d\nactors=%d\nreason=%s\n"), bSuccess,
		FPlatformProperties::RequiresCookedData(), Players.Num(), Reason), *(Output / TEXT("result.txt")));
	FPlatformMisc::RequestExit(false);
}
void UProject_JRenderedCrowdFixture::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(Handle);
	Super::Deinitialize();
}
