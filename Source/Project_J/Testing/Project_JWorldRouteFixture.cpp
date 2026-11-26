#include "Testing/Project_JWorldRouteFixture.h"

#include "Camera/CameraActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Mount/Project_JMountMoverComponent.h"
#include "Project_JPlayerCharacter.h"
#include "World/Project_JWorldStreamingComponent.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "UnrealClient.h"

AProject_JWorldRouteFixture::AProject_JWorldRouteFixture()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
}

void AProject_JWorldRouteFixture::BeginPlay()
{
	Super::BeginPlay();
#if !UE_BUILD_SHIPPING
	if (GetNetMode() == NM_Standalone && FParse::Param(FCommandLine::Get(), TEXT("ProjectJVerifyWorldRoute")))
	{
		FParse::Value(FCommandLine::Get(), TEXT("ProjectJRouteCapture="), CaptureDirectory);
		if (!CaptureDirectory.IsEmpty() && !IFileManager::Get().DirectoryExists(*CaptureDirectory))
		{
			Finish(false, TEXT("capture directory does not exist"));
			return;
		}
		StartTime = FPlatformTime::Seconds();
		SetActorTickEnabled(true);
	}
#endif
}

void AProject_JWorldRouteFixture::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
	auto* World = GetWorld();
	auto* PC = World->GetFirstPlayerController();
	const double Now = FPlatformTime::Seconds();
	if (Now - StartTime > 100.0) { Finish(false, TEXT("timeout")); return; }
	if (!PC || World->GetTimeSeconds() < 3.0) return;
	if (bEntry)
	{
		if (Stage == EStage::Spawn)
		{
			auto* Player = Cast<AProject_JPlayerCharacter>(PC->GetPawn());
			if (!Player) return;
			Stage = EStage::Outbound;
			UE_LOG(LogTemp, Display, TEXT("PROJECTJ_WORLD_ROUTE portal_input map=%s"), *World->GetPathName());
			Player->TryInteract();
		}
		return;
	}

	if (Stage == EStage::Spawn)
	{
		auto* Rider = Cast<AProject_JPlayerCharacter>(PC->GetPawn());
		if (!Rider || !MountClass) return;
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
		Mount = World->SpawnActor<AProject_JMountMoverPawn>(MountClass, Rider->GetActorLocation(), FRotator::ZeroRotator, Spawn);
		if (!Mount.IsValid() || !Mount->TryMountRider(Rider)) { Finish(false, TEXT("mount rejected")); return; }
		CenterY = Mount->GetActorLocation().Y;
		Stage = EStage::Outbound;
		UE_LOG(LogTemp, Display, TEXT("PROJECTJ_WORLD_ROUTE mounted map=%s mount=%s"), *World->GetPathName(), *Mount->GetClass()->GetPathName());
	}
	if (Stage <= EStage::Inbound)
	{
		auto* Pawn = Mount.Get();
		if (!Pawn || PC->GetPawn() != Pawn || Pawn->IsFlying()) { Finish(false, TEXT("lost ground mount")); return; }
		const FVector Position = Pawn->GetActorLocation();
		FHitResult Ground;
		++RoadFrames;
		if (!World->LineTraceSingleByObjectType(Ground, Position, Position - FVector(0, 0, 220),
			FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionQueryParams(SCENE_QUERY_STAT(ProjectJRouteFloor), false, Pawn)))
			++MissingRoadFrames;
		const double Yaw = Pawn->GetActorRotation().Yaw;
		if (Stage == EStage::Outbound && Position.X >= TurnaroundX) Stage = EStage::Turn;
		if (Stage == EStage::Turn && FMath::Abs(Yaw) >= 178.0) Stage = EStage::Inbound;
		if (Stage == EStage::Turn)
			Pawn->SetMountControlInput(FVector2D(0, 1));
		else
		{
			const double Ahead = Stage == EStage::Inbound ? -3000.0 : 3000.0;
			const double Desired = FMath::RadiansToDegrees(FMath::Atan2(CenterY - Position.Y, Ahead));
			const double Steering = FMath::Clamp(FMath::FindDeltaAngleDegrees(Yaw, Desired) / 25.0, -1.0, 1.0);
			Pawn->SetMountControlInput(FVector2D(1, Steering), 0, true);
		}
		if (Stage != EStage::Inbound || Position.X > BridgeStartX) return;
		Pawn->SetMountControlInput(FVector2D::ZeroVector);
		if (MissingRoadFrames > 0 || RoadFrames < 40) { Finish(false, TEXT("ground coverage")); return; }
		UE_LOG(LogTemp, Display, TEXT("PROJECTJ_WORLD_ROUTE bridge_pass frames=%d missing=%d"), RoadFrames, MissingRoadFrames);
		if (!Pawn->DismountRider(true)) { Finish(false, TEXT("dismount rejected")); return; }
		auto* Rider = Cast<AProject_JPlayerCharacter>(PC->GetPawn());
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
		if (!Rider || !FlightMountClass) { Finish(false, TEXT("missing flight mount class")); return; }
		Mount = World->SpawnActor<AProject_JMountMoverPawn>(FlightMountClass, Rider->GetActorLocation(), FRotator::ZeroRotator, Spawn);
		if (!Mount.IsValid() || !Mount->TryMountRider(Rider)) { Finish(false, TEXT("flight mount rejected")); return; }
		Mount->GetMountMover()->MotionProfile.bCanFly = true;
		Stage = EStage::Takeoff;
		StageTime = World->GetTimeSeconds();
		return;
	}
	if (Stage == EStage::Takeoff || Stage == EStage::Flight)
	{
		auto* Pawn = Mount.Get();
		if (!Pawn || PC->GetPawn() != Pawn) { Finish(false, TEXT("lost flight mount")); return; }
		const double Simulated = World->GetTimeSeconds();
		double Elapsed = Simulated - StageTime;
		if (Stage == EStage::Takeoff)
		{
			if (!Pawn->IsFlying())
			{
				if (Elapsed > 3.0) { Finish(false, TEXT("takeoff rejected")); return; }
				if (Elapsed > 0.25) Pawn->GetMountMover()->TryEnterFlightMode();
				return;
			}
			Stage = EStage::Flight;
			StageTime = Simulated;
			Elapsed = 0.0;
			FlightYaw = Pawn->GetActorRotation().Yaw;
		}
		if (!Pawn->IsFlying()) { Finish(false, TEXT("premature landing")); return; }
		const double Yaw = Pawn->GetActorRotation().Yaw;
		TurnDegrees += FMath::Abs(FMath::FindDeltaAngleDegrees(FlightYaw, Yaw));
		FlightYaw = Yaw;
		const double Vertical = Pawn->GetVelocity().Z;
		if (Elapsed < 8.0) ClimbPeak = FMath::Max(ClimbPeak, Vertical);
		if (Elapsed >= 11.0) DivePeak = FMath::Min(DivePeak, Vertical);
		if (Elapsed < 13.0)
		{
			const float Turn = Elapsed >= 8.0 && Elapsed < 11.0 ? 1.0f : 0.0f;
			const float Pitch = Elapsed < 8.0 ? 0.62f : (Elapsed >= 11.0 ? -0.80f : 0.0f);
			Pawn->SetMountControlInput(FVector2D(1, Turn), Pitch);
			return;
		}
		Pawn->SetMountControlInput(FVector2D::ZeroVector);
		if (ClimbPeak < 200.0 || DivePeak > -200.0 || TurnDegrees < 45.0)
		{ Finish(false, TEXT("flight motion coverage")); return; }
		UE_LOG(LogTemp, Display, TEXT("PROJECTJ_WORLD_ROUTE flight_pass climb=%.2f dive=%.2f turn=%.2f"), ClimbPeak, DivePeak, TurnDegrees);
		if (auto* Preparation = PC->FindComponentByClass<UProject_JWorldStreamingComponent>()) Preparation->DisableStreamingSource();
		if (auto* Preload = PC->FindComponentByClass<UProject_JWorldPreloadStreamingComponent>()) Preload->DisableStreamingSource();
		Camera = World->SpawnActor<ACameraActor>(FVector(26000, 2900, 1100), FRotator(-4, 180, 0));
		if (!Camera.IsValid()) { Finish(false, TEXT("camera spawn")); return; }
		PC->SetViewTarget(Camera.Get());
		Stage = EStage::Near;
		StageTime = Now;
		return;
	}

	if (!Camera.IsValid()) { Finish(false, TEXT("lost camera")); return; }
	struct FRepresentation
	{
		int32 Actors = 0;
		int32 VisibleRenderStates = 0;
		bool bRecentlyRendered = false;
		void Add(const AActor& Actor)
		{
			++Actors;
			for (const auto* Mesh : TInlineComponentArray<UStaticMeshComponent*>(&Actor))
			{
				if (!Mesh->IsVisible()) continue;
				VisibleRenderStates += Mesh->IsRenderStateCreated();
				bRecentlyRendered |= Mesh->WasRecentlyRendered(0.15f);
			}
		}
	};
	FRepresentation Original, Proxy;
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(OriginalCanopyTag)) Original.Add(**It);
	}
	for (TActorIterator<AWorldPartitionHLOD> It(World); It; ++It) Proxy.Add(**It);

	if (Stage == EStage::Near && Original.bRecentlyRendered && Now - StageTime >= 1.0)
	{
		CaptureRepresentation(TEXT("Near"));
		Stage = EStage::Away;
		StageTime = Now;
	}
	if (Stage == EStage::Away || Stage == EStage::Return)
	{
		++HLODFrames;
		if (!Original.bRecentlyRendered && !Proxy.bRecentlyRendered && ++NeitherVisibleFrames <= 8)
		{
			const TCHAR* Direction = Stage == EStage::Away ? TEXT("away") : TEXT("return");
			UE_LOG(LogTemp, Display, TEXT("PROJECTJ_WORLD_ROUTE visibility_sample frame=%llu phase=%s camera_x=%.1f original_actors=%d original_render_states=%d proxy_actors=%d proxy_render_states=%d"),
				GFrameCounter, Direction, Camera->GetActorLocation().X, Original.Actors, Original.VisibleRenderStates, Proxy.Actors, Proxy.VisibleRenderStates);
			CaptureRepresentation(FString::Printf(TEXT("Gap_%s_%d"), Direction, NeitherVisibleFrames));
		}
		const double Alpha = FMath::Clamp((Now - StageTime) / 8.0, 0.0, 1.0);
		const double X = FMath::Lerp(26000.0, 60000.0, Stage == EStage::Away ? Alpha : 1.0 - Alpha);
		Camera->SetActorLocation(FVector(X, 2900, 1100));
		if (Alpha >= 1.0) Stage = Stage == EStage::Away ? EStage::Far : EStage::Reentered;
	}
	else if (Stage == EStage::Far && Original.Actors == 0 && Proxy.bRecentlyRendered)
	{
		UE_LOG(LogTemp, Display, TEXT("PROJECTJ_WORLD_ROUTE proxy_pass original_loaded=0 proxy_rendered=1"));
		CaptureRepresentation(TEXT("Far"));
		Stage = EStage::Return;
		StageTime = Now;
	}
	else if (Stage == EStage::Reentered && Original.bRecentlyRendered)
	{
		if (!CaptureDirectory.IsEmpty() && !bReturnCaptureRequested)
		{
			bReturnCaptureRequested = true;
			CaptureRepresentation(TEXT("Returned"));
			return;
		}
		if (bReturnCaptureRequested && FScreenshotRequest::IsScreenshotRequested()) return;
		Finish(true, TEXT("returned to original canopy"));
	}
#endif
}

void AProject_JWorldRouteFixture::CaptureRepresentation(const FString& Name) const
{
#if !UE_BUILD_SHIPPING
	if (!CaptureDirectory.IsEmpty() && !FScreenshotRequest::IsScreenshotRequested())
		FScreenshotRequest::RequestScreenshot(CaptureDirectory / (Name + TEXT(".png")), false, false);
#endif
}

void AProject_JWorldRouteFixture::Finish(bool bPassed, const TCHAR* Reason)
{
	SetActorTickEnabled(false);
	UE_LOG(LogTemp, Display, TEXT("PROJECTJ_WORLD_ROUTE passed=%d reason=%s road_frames=%d missing_road=%d transition_frames=%d neither_recently_rendered=%d"),
		bPassed, Reason, RoadFrames, MissingRoadFrames, HLODFrames, NeitherVisibleFrames);
	FPlatformMisc::RequestExitWithStatus(false, bPassed ? 0 : 1);
}
