#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JAnimationUpdateSchedule.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEffectType.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCrowdAnimationTest, "ProjectJ.Crowd.AnimationPhases",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCrowdAnimationTest::RunTest(const FString&)
{
	TArray<FProjectJAnimationUpdateSchedule> Schedules; Schedules.SetNum(512);
	FString CSV = TEXT("frame,updates\n");
	int32 Peak = 0, Total = 0;
	for (int32 Frame = 0; Frame < 121; ++Frame)
	{
		int32 Updates = 0;
		for (int32 I = 0; I < Schedules.Num(); ++I)
		{
			Updates += Schedules[I].Advance(1.f / 60, .1f, 10000 + I, false) ? 1 : 0;
		}
		if (Frame == 0) { TestEqual(TEXT("Initial pose context is immediate"), Updates, 512); }
		else { Peak = FMath::Max(Peak, Updates); Total += Updates; }
		CSV += FString::Printf(TEXT("%d,%d\n"), Frame, Updates);
	}
	TestTrue(TEXT("Periodic database selection is spread across frames"), Peak < 100);
	TestTrue(TEXT("Phasing preserves the requested average cadence"), FMath::Abs(Total - 10240) <= 512);
	for (int32 I = 0; I < Schedules.Num(); ++I)
	{
		TestTrue(TEXT("Critical transition bypasses periodic delay"), Schedules[I].Advance(0, .1f, 10000 + I, true));
		TestFalse(TEXT("Repeated paused frame does not repeat periodic work"), Schedules[I].Advance(0, .1f, 10000 + I, false));
		TestTrue(TEXT("Interval changes refresh immediately"), Schedules[I].Advance(0, .2f, 10000 + I, false));
		TestTrue(TEXT("Owner replacement refreshes immediately"), Schedules[I].Advance(0, .2f, 20000 + I, false));
		TestTrue(TEXT("Full-rate policy always updates"), Schedules[I].Advance(0, 0, 20000 + I, false));
	}
	AddInfo(FString::Printf(TEXT("512 synchronized arrivals; periodic GT database-selection peak=%d per frame, updates over 120 frames=%d. This is a schedule contract, not measured PoseSearch CPU or FPS."), Peak, Total));
	FString Output; FParse::Value(FCommandLine::Get(), TEXT("ProjectJCrowdOutput="), Output);
	if (!Output.IsEmpty()) { IFileManager::Get().MakeDirectory(*Output, true); TestTrue(TEXT("Schedule CSV saved"), FFileHelper::SaveStringToFile(CSV, *(Output / TEXT("animation-phases.csv")))); }
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJEffectsAssetAudit, "ProjectJ.Crowd.EffectsAssetAudit",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJEffectsAssetAudit::RunTest(const FString&)
{
	// Read only: inspect the existing player trail, never modify or save authored content.
	auto* System = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/SlashTrail_SoftTofu/Niagara/Basic/NS_SlashTrail_Basic.NS_SlashTrail_Basic"));
	if (!TestNotNull(TEXT("Existing player trail resolves"), System)) { return false; }
	AddInfo(FString::Printf(TEXT("EffectsAudit System=%s EffectType=%s FixedBounds=%d Bounds=%s"), *System->GetPathName(), *GetPathNameSafe(System->GetEffectType()), System->bFixedBounds, *System->GetFixedBounds().ToString()));
	const auto& S = System->GetScalabilitySettings();
	AddInfo(FString::Printf(TEXT("EffectsAudit EffectiveCullDistance=%d Distance=%.1f CullEffectTypeCount=%d MaxInstances=%d CullSystemCount=%d MaxSystemInstances=%d"), S.bCullByDistance, S.MaxDistance, S.bCullMaxInstanceCount, S.MaxInstances, S.bCullPerSystemMaxInstanceCount, S.MaxSystemInstances));
	for (const auto& Handle : System->GetEmitterHandles())
	{
		if (const auto* Data = Handle.GetEmitterData())
		{
			AddInfo(FString::Printf(TEXT("EffectsAudit Emitter=%s Enabled=%d SimTarget=%s"), *Handle.GetName().ToString(), Handle.GetIsEnabled(), Data->SimTarget == ENiagaraSimTarget::GPUComputeSim ? TEXT("GPU") : TEXT("CPU")));
		}
	}
	return true;
}
#endif
