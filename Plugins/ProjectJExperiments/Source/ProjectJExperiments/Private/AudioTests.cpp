#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "Sound/SoundConcurrency.h"
#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "AudioThread.h"
#include "ActiveSound.h"
#include "ExperimentOutput.h"

namespace ProjectJ::Experiments
{
struct FVoiceSample
{
    TSet<uint64> ComponentIDs;
    std::atomic<bool> Ready{false};
    int32 Active = 0, DeviceSources = 0;
};

class FAudioExperiment final : public IAutomationLatentCommand
{
public:
    explicit FAudioExperiment(FAutomationTestBase* InTest) : Test(InTest) {}
    virtual ~FAudioExperiment() override { DestroyWorld(); }
    virtual bool Update() override
    {
        if (!World)
        {
            World = UWorld::CreateWorld(EWorldType::Game, false);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            FAudioDeviceParams Params;
            Params.AssociatedWorld = World;
            Device = GEngine->GetAudioDeviceManager()->RequestAudioDevice(Params);
            if (!Test->TestTrue(TEXT("World-owned audio handle available"), Device.IsValid())) { return true; }
            World->SetAudioDevice(Device);
            World->InitializeActorsForPlay(FURL());
            World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
            auto* Owner = World->SpawnActor<AActor>();
            auto* Concurrency = NewObject<USoundConcurrency>(Owner);
            Concurrency->Concurrency.MaxCount = Mode == 1 ? 16 : 128;
            Concurrency->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::PreventNew;
            TArray<uint8> Silence; Silence.SetNumZeroed(48000 * sizeof(int16) * 5);
            for (int32 I = 0; I < 128; ++I)
            {
                auto* Wave = NewObject<USoundWaveProcedural>(Owner);
                Wave->SetSampleRate(48000); Wave->NumChannels = 1;
                Wave->Duration = INDEFINITELY_LOOPING_DURATION; Wave->bLooping = true;
                Wave->VirtualizationMode = EVirtualizationMode::Restart;
                Wave->ConcurrencySet.Add(Concurrency);
                // Silent PCM exercises actual voices without playing sound to
                // the user. No device, system volume or user asset is modified.
                Wave->QueueAudio(Silence.GetData(), Silence.Num());
                auto* C = NewObject<UAudioComponent>(Owner); Owner->AddInstanceComponent(C);
                C->bAutoActivate = false; C->bAutoDestroy = false; C->bIsUISound = true;
                C->bAllowSpatialization = Mode == 2; C->bOverrideAttenuation = Mode == 2;
                C->AttenuationOverrides.bAttenuate = true;
                C->AttenuationOverrides.AttenuationShapeExtents = FVector(100);
                C->AttenuationOverrides.FalloffDistance = 500;
                C->RegisterComponent(); C->SetWorldLocation(Mode == 2 ? FVector(1000000) : FVector::ZeroVector);
                C->SetSound(Wave); C->Play(); Components.Add(C); IDs.Add(C->GetAudioComponentID());
            }
            Frame = 0;
        }
        World->Tick(LEVELTICK_All, 1.0f / 60.0f);
        ++Frame;
        if (Frame < 60) { return false; }
        if (!Sample)
        {
            Sample = MakeShared<FVoiceSample, ESPMode::ThreadSafe>(); Sample->ComponentIDs = IDs;
            auto Output = Sample; auto Handle = Device;
            FAudioThread::RunCommandOnAudioThread([Output, Handle]
            {
                for (const auto* Sound : Handle->GetActiveSounds())
                { Output->Active += Output->ComponentIDs.Contains(Sound->GetAudioComponentID()); }
                Output->DeviceSources = Handle->GetNumActiveSources();
                Output->Ready.store(true, std::memory_order_release);
            });
            return false;
        }
        if (!Sample->Ready.load(std::memory_order_acquire)) { return false; }
        int32 Playing = 0, Virtual = 0;
        for (const auto* C : Components) { Playing += C->IsPlaying(); Virtual += C->IsVirtualized(); }
        Csv += FString::Printf(TEXT("%d,%d,%d,%d,%d,%d,%d\n"), Mode, Frame, bStopping, Sample->Active, Sample->DeviceSources, Playing, Virtual);
        if (bStopping)
        {
            if (Sample->Active == 0 && Playing == 0)
            {
                DestroyWorld(); Sample.Reset(); IDs.Empty(); bStopping = false;
                if (++Mode == 3) { Test->TestTrue(TEXT("Save voice measurements"), Save(TEXT("audio.csv"), Csv)); return true; }
                return false;
            }
            if (Frame > 300) { Test->AddError(TEXT("Audio owners retained active sounds after stop")); return true; }
        }
        else
        {
            if (Mode == 1) { Test->TestTrue(TEXT("Concurrency bounds live fixture sounds"), Sample->Active <= 16); }
            PeakActive = FMath::Max(PeakActive, Sample->Active);
            PeakVirtual = FMath::Max(PeakVirtual, Virtual);
            if (Frame >= 120)
            {
                if (Mode != 2) { Test->TestTrue(TEXT("Fixture actually produced active sounds"), PeakActive > 0); }
                else { Test->TestTrue(TEXT("Out of range looping sounds actually virtualize"), PeakVirtual > 0); }
                for (auto* C : Components) { C->Stop(); }
                bStopping = true;
            }
        }
        Sample.Reset(); return false;
    }
private:
    void DestroyWorld()
    {
        if (!World) { return; }
        for (auto* C : Components) { C->Stop(); }
        Components.Empty(); World->EndPlay(EEndPlayReason::Quit);
        World->DestroyWorld(false); GEngine->DestroyWorldContext(World); World = nullptr;
        PeakActive = PeakVirtual = 0;
    }
    FAutomationTestBase* Test;
    UWorld* World = nullptr;
    FAudioDeviceHandle Device;
    TArray<UAudioComponent*> Components;
    TSet<uint64> IDs;
    TSharedPtr<FVoiceSample, ESPMode::ThreadSafe> Sample;
    int32 Mode = 0, Frame = 0, PeakActive = 0, PeakVirtual = 0;
    bool bStopping = false;
    FString Csv = TEXT("mode,frame,stopping,fixture_active_sounds,device_sources,playing_components,virtual_components\n");
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAudioTest, "ProjectJ.GroupF.Audio.VoiceLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)
bool FProjectJAudioTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(ProjectJ::Experiments::FAudioExperiment(this)); return true;
}
