#include "Testing/Project_JEffectsCookedFixture.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "UnrealClient.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "PipelineStateCache.h"
#include "PSOPrecacheValidation.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "ProfilingDebugging/MiscTrace.h"

void UProject_JEffectsCookedFixture::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("ProjectJEffectsCookedFixture"))) { return; }
	Started = FPlatformTime::Seconds(); bPreload = FParse::Param(FCommandLine::Get(), TEXT("ProjectJEffectsPreload"));
	bMixedDistance = FParse::Param(FCommandLine::Get(), TEXT("ProjectJEffectsMixedDistance"));
	bDistanceCull = FParse::Param(FCommandLine::Get(), TEXT("ProjectJEffectsDistanceCull"));
	bFixedBounds = FParse::Param(FCommandLine::Get(), TEXT("ProjectJEffectsFixedBounds"));
	FParse::Value(FCommandLine::Get(), TEXT("ProjectJEOutput="), Output);
	if (Output.IsEmpty()) { Output = FPaths::ProjectSavedDir() / TEXT("Validation/EffectsCooked"); }
	IFileManager::Get().MakeDirectory(*Output, true);
	Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Tick));
#endif
}
bool UProject_JEffectsCookedFixture::Tick(float Delta)
{
	if (bFinished) { return false; }
	const double Now = FPlatformTime::Seconds();
	if (Now - Started > 90) { Finish(false, TEXT("Timed out")); return false; }
	auto* World = GetWorld();
	if (!World || !World->HasBegunPlay()) { return true; }
	if (World->GetNetMode() != NM_Standalone) { Finish(false, TEXT("Standalone fixture only")); return false; }
	if (Phase == 0)
	{
		auto* PC = World->GetFirstPlayerController(); if (!PC) { return true; }
		auto* Camera = World->SpawnActor<ACameraActor>(FVector(0, -1400, 700), FRotator(-24, 90, 0));
		PC->SetViewTarget(Camera); Phase = 1; PhaseStarted = Now;
		TRACE_BOOKMARK(TEXT("ProjectJ.EffectsCooked Begin Cooked=%d Preload=%d"), FPlatformProperties::RequiresCookedData(), bPreload);
	}
	if (!System && ((bPreload && Phase == 1) || Now - PhaseStarted >= 2))
	{
		const double LoadStarted = FPlatformTime::Seconds();
		const bool bAlreadyLoaded = FindObject<UNiagaraSystem>(nullptr, TEXT("/Game/SlashTrail_SoftTofu/Niagara/Basic/NS_SlashTrail_Basic.NS_SlashTrail_Basic")) != nullptr;
		System = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/SlashTrail_SoftTofu/Niagara/Basic/NS_SlashTrail_Basic.NS_SlashTrail_Basic"));
		FFileHelper::SaveStringToFile(FString::Printf(TEXT("load_ms=%.6f\ncooked=%d\npreload=%d\nasset_already_loaded=%d\n"), (FPlatformTime::Seconds() - LoadStarted) * 1000, FPlatformProperties::RequiresCookedData(), bPreload, bAlreadyLoaded), *(Output / TEXT("load.txt")));
		if (!System) { Finish(false, TEXT("Cooked player trail missing")); return false; }
		if (bDistanceCull)
		{
#if WITH_EDITOR
			// Isolated experiment only: restore in Finish; never dirty/save this asset.
			OriginalOverrides = System->GetScalabilityOverrides().Overrides;
			bOriginalOverrideEnabled = System->GetOverrideScalabilitySettings();
			FNiagaraSystemScalabilityOverride Override;
			Override.bOverrideDistanceSettings = true; Override.bCullByDistance = true; Override.MaxDistance = 2500;
			System->GetScalabilityOverrides().Overrides = {Override};
			System->SetOverrideScalabilitySettings(true);
			System->UpdateScalability();
#else
			Finish(false, TEXT("Temporary scalability authoring comparison requires Editor; cooked tests use authored settings")); return false;
#endif
		}
		const auto& Settings = System->GetScalabilitySettings();
		FFileHelper::SaveStringToFile(FString::Printf(TEXT("distance_cull=%d\nmax_distance=%.1f\nfixed_component_bounds=%d\nmixed_distance=%d\ncull_reaction=%d\n"), Settings.bCullByDistance, Settings.MaxDistance, bFixedBounds, bMixedDistance, System->GetEffectType() ? int32(System->GetEffectType()->CullReaction) : -1), *(Output / TEXT("scalability.txt")));
		FFileHelper::SaveStringToFile(System->GetExposedParameters().ToString(), *(Output / TEXT("parameters.txt")));
	}
	if ((Phase == 1 && Now - PhaseStarted >= 2) || (Phase == 2 && Now - PhaseStarted >= 5))
	{
		++Phase; PhaseStarted = FPlatformTime::Seconds(); bShot = false;
		const int32 Count = Phase == 2 ? 1 : 100;
		for (UNiagaraComponent* N : Effects) { if (IsValid(N)) { N->DestroyComponent(); } } Effects.Reset();
		for (int32 I = 0; I < Count; ++I)
		{
			const FVector InitialPosition((I % 10 - 4.5) * 100 + 80 + (bMixedDistance && I >= 20 ? 10000 : 0), (I / 10 - 4.5) * 100, 100);
			auto* N = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, System, InitialPosition, FRotator::ZeroRotator, FVector(1), false, true, ENCPoolMethod::None, bDistanceCull);
			if (!N && !(bDistanceCull && bMixedDistance && I >= 20)) { Finish(false, TEXT("Near trail spawn failed")); return false; }
			if (N && bFixedBounds) { N->SetSystemFixedBounds(FBox(FVector(-350), FVector(350))); }
			Effects.Add(N);
		}
		TRACE_BOOKMARK(TEXT("ProjectJ.EffectsCooked Cast Count=%d"), Count);
	}
	for (int32 I = 0; I < Effects.Num(); ++I)
	{
		const double A = (Now - PhaseStarted) * 5;
		const FVector Position((I % 10 - 4.5) * 100 + FMath::Cos(A) * 80 + (bMixedDistance && I >= 20 ? 10000 : 0), (I / 10 - 4.5) * 100 + FMath::Sin(A) * 80, 100);
		if (IsValid(Effects[I])) { Effects[I]->SetWorldLocation(Position); }
	}
	int32 Active = 0, Complete = 0;
	for (UNiagaraComponent* N : Effects) { if (IsValid(N)) { Active += N->IsActive(); Complete += N->IsComplete(); } }
	Rows += FString::Printf(TEXT("%d,%.6f,%.6f,%u,%d,%d,%d\n"), Phase, Now - Started, Delta * 1000, PipelineStateCache::NumActivePrecacheRequests(), Effects.Num(), Active, Complete);
	if (Phase >= 2 && !bShot && Now - PhaseStarted >= 0.15)
	{
		bShot = true; FScreenshotRequest::RequestScreenshot(Output / FString::Printf(TEXT("phase%d.png"), Phase), false, false);
	}
	if (Phase == 3 && Now - PhaseStarted >= 5) { Finish(true, TEXT("One and 100 trail components exercised; inspect captures before claiming visible pixels")); return false; }
	return true;
}
void UProject_JEffectsCookedFixture::Finish(bool bSuccess, const TCHAR* Reason)
{
	if (bFinished) { return; } bFinished = true;
#if PSO_PRECACHING_VALIDATE
	PSOCollectorStats::DumpPSOPrecacheValidationStats();
#endif
	FFileHelper::SaveStringToFile(Rows, *(Output / TEXT("frames.csv")));
	FFileHelper::SaveStringToFile(FString::Printf(TEXT("success=%d\ncooked=%d\nreason=%s\n"), bSuccess, FPlatformProperties::RequiresCookedData(), Reason), *(Output / TEXT("result.txt")));
	for (UNiagaraComponent* N : Effects) { if (IsValid(N)) { N->DestroyComponent(); } } Effects.Reset();
#if WITH_EDITOR
	if (System && bDistanceCull) { System->GetScalabilityOverrides().Overrides = OriginalOverrides; System->SetOverrideScalabilitySettings(bOriginalOverrideEnabled); System->UpdateScalability(); }
#endif
	FPlatformMisc::RequestExit(false);
}
void UProject_JEffectsCookedFixture::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(Handle); Super::Deinitialize();
}
