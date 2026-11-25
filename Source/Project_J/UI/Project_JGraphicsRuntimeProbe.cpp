#include "UI/Project_JPlayerUIComponent.h"

#if !UE_BUILD_SHIPPING
#include "UI/Project_JGraphicsSettingsWidget.h"
#include "UI/Project_JPlayerHUDWidget.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CheckBox.h"
#include "Containers/Ticker.h"
#include "DynamicRHI.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "RenderTimer.h"
#include "UnrealClient.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

namespace
{
constexpr int32 ProbePresets[] = {0, 3, 3, 0};
constexpr uint64 ProbeMemoryReserve = 4ull * 1024 * 1024 * 1024;

struct FGraphicsWorldProbe
{
	FString Path;
	FString Rows = TEXT("pass,quality,frame_ms,game_ms,draw_ms,gpu_ms\n");
	int32 Pass = 0;
	int32 Samples = 0;
	double Started = FPlatformTime::Seconds();
	double AppliedAt = 0.;
	double PreviousFrame = 0.;
	double NextMemoryCheck = 0.;
	uint64 MinPhysical = MAX_uint64;
	uint64 MinCommit = MAX_uint64;
	bool bAwaitingCapture = false;

	bool Finish(const TCHAR* Reason = nullptr) const
	{
		const bool bWritten = !Path.IsEmpty() && FFileHelper::SaveStringToFile(Rows, *(Path / TEXT("frames.csv")));
		const bool bSuccess = !Reason && bWritten;
		UE_LOG(LogTemp, Display, TEXT("PROJECT_J_GRAPHICS_WORLD_PROBE success=%d passes=%d min_physical_mib=%llu min_commit_mib=%llu reason=%s"),
			bSuccess, Pass, MinPhysical == MAX_uint64 ? 0 : MinPhysical / (1024 * 1024),
			MinCommit == MAX_uint64 ? 0 : MinCommit / (1024 * 1024), Reason ? Reason : TEXT("none"));
		FPlatformMisc::RequestExitWithStatus(false, bSuccess ? 0 : 1);
		return false;
	}
};

float ReadGraphicsProbeCVar(const TCHAR* Name)
{
	const auto* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
	return Variable ? Variable->GetFloat() : -999.f;
}
}
#endif

void UProject_JPlayerUIComponent::StartGraphicsWorldProbe(const FString& OutputDirectory)
{
#if !UE_BUILD_SHIPPING
	const auto Run = MakeShared<FGraphicsWorldProbe>();
	auto* PC = Cast<APlayerController>(GetOwner());
	FString IsolatedUserDir;
	if (!PC || !PC->GetPawn() || !PC->PlayerCameraManager || !Screen || !GetWorld()
		|| !FParse::Value(FCommandLine::Get(), TEXT("UserDir="), IsolatedUserDir) || IsolatedUserDir.IsEmpty()
		|| !IFileManager::Get().DirectoryExists(*OutputDirectory)
		|| IFileManager::Get().FileExists(*(OutputDirectory / TEXT("frames.csv"))))
	{
		Run->Finish(TEXT("Missing isolated user directory, camera, HUD or fresh evidence directory"));
		return;
	}
	Run->Path = OutputDirectory;
	auto View = PC->PlayerCameraManager->GetCameraCacheView();
	FString ExplicitView;
	if (FParse::Value(FCommandLine::Get(), TEXT("ProjectJGraphicsWorldView="), ExplicitView, false))
	{
		TArray<FString> Components;
		ExplicitView.ParseIntoArray(Components, TEXT(","), false);
		double Values[5];
		bool bValid = Components.Num() == UE_ARRAY_COUNT(Values);
		for (int32 Index = 0; bValid && Index < UE_ARRAY_COUNT(Values); ++Index)
			bValid = Components[Index].IsNumeric() && LexTryParseString(Values[Index], *Components[Index]) && FMath::IsFinite(Values[Index]);
		if (!bValid)
		{
			Run->Finish(TEXT("Explicit view must be X,Y,Z,Pitch,Yaw"));
			return;
		}
		View.Location = FVector(Values[0], Values[1], Values[2]);
		View.Rotation = FRotator(Values[3], Values[4], 0.);
	}
	auto* Camera = GetWorld()->SpawnActor<ACameraActor>(View.Location, View.Rotation);
	if (!Camera)
	{
		Run->Finish(TEXT("Could not freeze the starting camera"));
		return;
	}
	Camera->GetCameraComponent()->SetFieldOfView(View.FOV);
	PC->SetViewTarget(Camera);
	UE_LOG(LogTemp, Display, TEXT("PROJECT_J_GRAPHICS_WORLD_VIEW map=%s location=%s rotation=%s fov=%.2f"),
		*GetWorld()->GetMapName(), *View.Location.ToCompactString(), *View.Rotation.ToCompactString(), View.FOV);

	FTSTicker::GetCoreTicker().AddTicker(TEXT("ProjectJGraphicsWorldProbe"), 0.f, [Owner = TWeakObjectPtr<UProject_JPlayerUIComponent>(this), Run](float)
	{
		auto* UI = Owner.Get();
		auto* World = UI ? UI->GetWorld() : nullptr;
		if (!World || !UI->Screen || World->bIsTearingDown) return Run->Finish(TEXT("Test world or HUD retired"));
		const double Now = FPlatformTime::Seconds();
		if (Now - Run->Started > 120.) return Run->Finish(TEXT("World probe exceeded the time limit"));
		if (Now >= Run->NextMemoryCheck)
		{
			Run->NextMemoryCheck = Now + .5;
			const auto Memory = FPlatformMemory::GetStats();
			Run->MinPhysical = FMath::Min(Run->MinPhysical, Memory.AvailablePhysical);
			Run->MinCommit = FMath::Min(Run->MinCommit, Memory.AvailableVirtual);
			if (FMath::Min(Run->MinPhysical, Run->MinCommit) < ProbeMemoryReserve) return Run->Finish(TEXT("Memory reserve reached"));
		}
		const FString Capture = Run->Path / FString::Printf(TEXT("Pass%d.png"), Run->Pass + 1);
		if (Run->bAwaitingCapture)
		{
			if (FScreenshotRequest::IsScreenshotRequested()) return true;
			if (!IFileManager::Get().FileExists(*Capture)) return Run->Finish(TEXT("Frame capture missing"));
			Run->bAwaitingCapture = false;
			Run->AppliedAt = 0.;
			Run->Samples = 0;
			if (++Run->Pass == UE_ARRAY_COUNT(ProbePresets)) return Run->Finish();
			return true;
		}
		const int32 Quality = ProbePresets[Run->Pass];
		if (!Run->AppliedAt)
		{
			auto* Native = UGameUserSettings::GetGameUserSettings();
			if (!Native) return Run->Finish(TEXT("Game user settings missing"));
			Native->SetDynamicResolutionEnabled(false);
			UI->Screen->ExecuteCommand(TEXT("Settings"));
			UI->Screen->ExecuteCommand(TEXT("Graphics"));
			auto* Widget = UI->Screen->GraphicsSettings.Get();
			if (!Widget || !UI->Screen->WindowStack.Contains(TEXT("Graphics"))) return Run->Finish(TEXT("Graphics window did not open"));
			Widget->TakeWidget();
			Widget->Preset->SetSelectedIndex(Quality);
			Widget->SelectPreset({}, ESelectInfo::OnMouseClick);
			Widget->RenderScale->SetSelectedIndex(Widget->RenderFactors.IndexOfByKey(100.f));
			Widget->FrameLimit->SetSelectedIndex(Widget->FrameLimits.IndexOfByKey(0.f));
			Widget->VSync->SetIsChecked(false);
			Widget->Apply();
			UI->SetMenuOpen(false);
			Run->AppliedAt = Now;
			return true;
		}
		if (Now - Run->AppliedAt < 4.) return true;
		if (auto* Partition = World->GetSubsystem<UWorldPartitionSubsystem>(); Partition && !Partition->IsAllStreamingCompleted())
		{
			if (Run->Samples) return Run->Finish(TEXT("World Partition streaming changed during sampling"));
			return true;
		}
		if (Run->Samples == 0)
		{
			const float Scale = ReadGraphicsProbeCVar(TEXT("sg.ResolutionQuality"));
			const float Shadow = ReadGraphicsProbeCVar(TEXT("r.ShadowQuality"));
			const float GI = ReadGraphicsProbeCVar(TEXT("r.Lumen.DiffuseIndirect.Allow"));
			const float Reflection = ReadGraphicsProbeCVar(TEXT("r.Lumen.Reflections.Allow"));
			UE_LOG(LogTemp, Display, TEXT("PROJECT_J_GRAPHICS_WORLD_VALUES pass=%d quality=%d scale=%.1f screen=%.1f shadow=%.0f pages=%.0f gi=%.0f reflection=%.0f vsync=%.0f cap=%.0f"),
				Run->Pass + 1, Quality, Scale, ReadGraphicsProbeCVar(TEXT("r.ScreenPercentage")), Shadow,
				ReadGraphicsProbeCVar(TEXT("r.Shadow.Virtual.MaxPhysicalPages")), GI, Reflection,
				ReadGraphicsProbeCVar(TEXT("r.VSync")), ReadGraphicsProbeCVar(TEXT("t.MaxFPS")));
			if (Scale != 100.f || Shadow != (Quality ? 5.f : 0.f) || GI != (Quality ? 1.f : 0.f) || Reflection != GI
				|| ReadGraphicsProbeCVar(TEXT("t.MaxFPS")) != 0.f || ReadGraphicsProbeCVar(TEXT("r.VSync")) != 0.f)
				return Run->Finish(TEXT("Effective renderer settings did not match the HUD request"));
			Run->PreviousFrame = Now;
			++Run->Samples;
			return true;
		}
		Run->Rows += FString::Printf(TEXT("%d,%d,%.5f,%.5f,%.5f,%.5f\n"), Run->Pass + 1, Quality,
			(Now - Run->PreviousFrame) * 1000., FPlatformTime::ToMilliseconds(GGameThreadTime),
			FPlatformTime::ToMilliseconds(GRenderThreadTime), FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
		Run->PreviousFrame = Now;
		if (++Run->Samples == 181)
		{
			Run->bAwaitingCapture = true;
			FScreenshotRequest::RequestScreenshot(Capture, false, false);
		}
		return true;
	});
#endif
}
