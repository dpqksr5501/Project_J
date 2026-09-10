#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Components/Project_JCombatPresentationComponent.h"
#include "Combat/Project_JCombatPresentationSet.h"
#include "Project_JNPCCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "UObject/ObjectKey.h"
#include "ProfilingDebugging/MiscTrace.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(CrowdAttackTag, "ProjectJ.Tests.Crowd.Attack");
UE_DEFINE_GAMEPLAY_TAG_STATIC(CrowdCueTag, "ProjectJ.Tests.Crowd.Cue");

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCrowdEffectsTest, "ProjectJ.GroupE.EffectsPoolLifetime",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)
bool FProjectJCrowdEffectsTest::RunTest(const FString&)
{
	auto* System = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/SlashTrail_SoftTofu/Niagara/Basic/NS_SlashTrail_Basic.NS_SlashTrail_Basic"));
	if (!TestNotNull(TEXT("Authored player trail"), System)) { return false; }
	auto* Pool = IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Combat.Presentation.Pool"));
	const int32 Previous = Pool->GetInt();
	FString CSV = TEXT("pool,cycle,count,spawn_ms,stop_ms,unique_components\n");
	for (const int32 Mode : {0, 1})
	{
		Pool->Set(Mode, ECVF_SetByCode);
		auto* World = UWorld::CreateWorld(EWorldType::Game, false);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
		auto* Set = NewObject<UProject_JCombatPresentationSet>(World);
		auto* Profile = NewObject<UProject_JAttackPresentationProfile>(Set);
		FProject_JCombatVFXCueDefinition Cue; Cue.CueTag = CrowdCueTag; Cue.NiagaraSystem = System;
		Cue.AttachmentTarget = EProject_JCombatVFXAttachmentTarget::World; Cue.bLooping = true; Cue.bDestroyImmediatelyOnStop = true;
		Profile->Cues.Add(Cue);
		FProject_JCombatAttackPresentationEntry Entry; Entry.AttackTag = CrowdAttackTag; Entry.Profile = Profile; Set->AttackPresentations.Add(Entry);
		TArray<UProject_JCombatPresentationComponent*> Components;
		FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		for (int32 I = 0; I < 100; ++I)
		{
			auto* Pawn = World->SpawnActor<AProject_JNPCCharacter>(AProject_JNPCCharacter::StaticClass(), FVector((I % 10) * 150, (I / 10) * 150, 100), FRotator::ZeroRotator, Params);
			auto* C = NewObject<UProject_JCombatPresentationComponent>(Pawn); C->RegisterComponent(); C->BasePresentationSet = Set; Components.Add(C);
		}
		TSet<FObjectKey> Identities;
		const uint64 Region = TRACE_BEGIN_REGION_WITH_ID(Mode ? TEXT("ProjectJ.Effects.Pooled") : TEXT("ProjectJ.Effects.Unpooled"));
		for (int32 Cycle = 0; Cycle < 12; ++Cycle)
		{
			const double Start = FPlatformTime::Seconds();
			for (auto* C : Components)
			{
				C->BeginAttackPresentation(CrowdAttackTag); C->PlayCue(CrowdCueTag);
				auto* N = C->ActiveLoopingCues.FindRef(CrowdCueTag).Get();
				if (TestNotNull(TEXT("Every simultaneous cue has a tracked Niagara component"), N))
				{
					Identities.Add(FObjectKey(N));
					if (Mode) { TestEqual(TEXT("Loop retains manual ownership until its notify stops"), N->PoolingMethod, ENCPoolMethod::ManualRelease); }
				}
			}
			const double SpawnMs = (FPlatformTime::Seconds() - Start) * 1000;
			for (int32 Frame = 0; Frame < 3; ++Frame) { World->Tick(LEVELTICK_All, 1.f / 60); }
			const double Stop = FPlatformTime::Seconds();
			for (auto* C : Components)
			{
				if (Cycle % 3 == 0) { C->StopCue(CrowdCueTag); }
				else if (Cycle % 3 == 1) { C->RefreshPresentation(); }
				else { C->EndAttackPresentation(); }
				TestEqual(TEXT("Notify, equipment refresh and attack end release all tracked references"), C->ActiveLoopingCues.Num(), 0);
			}
			CSV += FString::Printf(TEXT("%d,%d,100,%.6f,%.6f,%d\n"), Mode, Cycle, SpawnMs, (FPlatformTime::Seconds() - Stop) * 1000, Identities.Num());
			World->Tick(LEVELTICK_All, 1.f / 60);
		}
		if (Mode) { TestTrue(TEXT("Engine pool reuses components across attacks"), Identities.Num() < 1200); }
		// Exercise graceful completion separately from immediate trail removal.
		Profile->Cues[0].bDestroyImmediatelyOnStop = false;
		Components[0]->BeginAttackPresentation(CrowdAttackTag); Components[0]->PlayCue(CrowdCueTag);
		Components[0]->EndAttackPresentation();
		TestEqual(TEXT("Graceful stop drops project ownership too"), Components[0]->ActiveLoopingCues.Num(), 0);
		TRACE_END_REGION_WITH_ID(Region);
		World->Tick(LEVELTICK_All, 1.f / 60); World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
	}
	Pool->Set(Previous, ECVF_SetByCode);
	FString Output; FParse::Value(FCommandLine::Get(), TEXT("ProjectJCrowdOutput="), Output);
	if (!Output.IsEmpty()) { IFileManager::Get().MakeDirectory(*Output, true); TestTrue(TEXT("Effect measurements saved"), FFileHelper::SaveStringToFile(CSV, *(Output / TEXT("effects-pool.csv")))); }
	AddInfo(TEXT("100 simultaneous authored GPU trails x12 rounds per mode; real RHI/world simulation, no gameplay viewport. Spawn/stop timings include contract checks, not GPU frame cost or visual-quality approval."));
	return true;
}
#endif
