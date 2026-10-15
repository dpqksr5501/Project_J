#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Testing/Project_JAuthoredAnimationFixture.h"
#include "Components/SkeletalMeshComponent.h"
#include "AnimationBlueprintLibrary.h"
#include "Animation/AnimMontage.h"
#include "Animation/Project_JAnimNotifyState_WeaponMotion.h"
#include "Animation/Project_JAnimNotifyState_WeaponGroundContact.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#include "ProfilingDebugging/MiscTrace.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSharedNotifyGCLifetime, "ProjectJ.Continuation.SharedNotifyGCLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJSharedNotifyGCLifetime::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->BeginTearingDown(); World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    World->InitializeActorsForPlay(FURL());
    World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
    TStrongObjectPtr<UProject_JAnimNotifyState_WeaponMotion> Motion(NewObject<UProject_JAnimNotifyState_WeaponMotion>());
    TStrongObjectPtr<UProject_JAnimNotifyState_WeaponGroundContact> Ground(NewObject<UProject_JAnimNotifyState_WeaponGroundContact>());
    TArray<AProject_JPlayerCharacter*> Players;
    for (int32 I = 0; I < 2; ++I) {
        auto* Controller = World->SpawnActor<AAIController>();
        auto* State = World->SpawnActor<AProject_JPlayerState>();
        State->SetOwner(Controller); Controller->SetPlayerState(State);
        auto* Player = ProjectJAuthoredAnimationFixture::Spawn(World, Controller, State, FVector(I * 200, 0, 1000));
        if (!TestNotNull(TEXT("Authored GC participant"), Player)) return false;
        Controller->UnPossess(); Player->SetPlayerState(State);
        Player->GetAbilitySystemComponent()->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
        Player->FindComponentByClass<UProject_JWeaponPresentationComponent>()->EnterCombatPresentation();
        Players.Add(Player);
    }
    for (int32 I = 0; I < 3; ++I) World->Tick(LEVELTICK_All, 1.f / 60.f);
    FAnimNotifyEventReference Ref; Ref.SetNotifyInstanceID(78001);
    for (auto* Player : Players) {
        Motion->NotifyBegin(Player->GetMesh(), nullptr, 1.f, Ref);
        Ground->NotifyBegin(Player->GetMesh(), nullptr, 1.f, Ref);
    }
    TestEqual(TEXT("Motion registry separates both actual meshes"), Motion->RuntimeStates.Num(), 2);
    TestEqual(TEXT("Ground registry separates both actual meshes"), Ground->RuntimeStates.Num(), 2);
    TWeakObjectPtr<USkeletalMeshComponent> RetiredMesh(Players[0]->GetMesh());
    Players[0]->Destroy(); Players[0] = nullptr;
    CollectGarbage(RF_NoFlags, true);
    TestFalse(TEXT("Retired mesh is no longer a valid participant"), RetiredMesh.IsValid());
    TestEqual(TEXT("GC returns the retired Motion registry entry without another Begin"), Motion->RuntimeStates.Num(), 1);
    TestEqual(TEXT("GC returns the retired Ground registry entry without another Begin"), Ground->RuntimeStates.Num(), 1);
    auto* Presentation = Players[1]->FindComponentByClass<UProject_JWeaponPresentationComponent>();
    TestTrue(TEXT("GC preserves the living participant's motion lease"), Presentation->IsIndependentMotionActive());
    Ground->NotifyEnd(Players[1]->GetMesh(), nullptr, Ref);
    Motion->NotifyEnd(Players[1]->GetMesh(), nullptr, Ref);
    TestFalse(TEXT("Matching End still releases the living motion lease"), Presentation->IsIndependentMotionActive());
    TestEqual(TEXT("Matching End returns the last Motion registry entry"), Motion->RuntimeStates.Num(), 0);
    TestEqual(TEXT("Matching End returns the last Ground registry entry"), Ground->RuntimeStates.Num(), 0);
    TWeakObjectPtr<UObject> WeakMotion(Motion.Get()), WeakGround(Ground.Get());
    Motion.Reset(); Ground.Reset(); CollectGarbage(RF_NoFlags, true);
    TestFalse(TEXT("Post-GC registration does not root the Motion notify"), WeakMotion.IsValid());
    TestFalse(TEXT("Post-GC registration does not root the Ground notify"), WeakGround.IsValid());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSharedNotifyPerformance, "ProjectJ.Animation.SharedNotifyLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)
bool FProjectJSharedNotifyPerformance::RunTest(const FString&)
{
    FString Output;
    FParse::Value(FCommandLine::Get(), TEXT("ProjectJNotifyOutput="), Output);
    TStrongObjectPtr<UAnimMontage> Montage(LoadObject<UAnimMontage>(nullptr,
        TEXT("/Game/Anim_Assets/Great_Sword/Animations/Sword/Montage/AM_Greatsword_R.AM_Greatsword_R")));
    if (!TestNotNull(TEXT("Authored motion montage"), Montage.Get())) return false;
    TArray<FAnimNotifyEvent> Events;
    UAnimationBlueprintLibrary::GetAnimationNotifyEvents(Montage.Get(), Events);
    const FAnimNotifyEvent* Event = Events.FindByPredicate([](const FAnimNotifyEvent& E) {
        return E.NotifyStateClass && E.NotifyStateClass->IsA<UProject_JAnimNotifyState_WeaponMotion>();
    });
    if (!TestNotNull(TEXT("The public copy API returns an actual authored Motion state"), Event)) return false;
    TStrongObjectPtr<UProject_JAnimNotifyState_WeaponMotion> Motion(CastChecked<UProject_JAnimNotifyState_WeaponMotion>(Event->NotifyStateClass));
    // No GroundContact was found in the inspected montage subset. This part is a
    // separate native registry fixture, not a claim that those montages execute it.
    TStrongObjectPtr<UProject_JAnimNotifyState_WeaponGroundContact> Ground(NewObject<UProject_JAnimNotifyState_WeaponGroundContact>());
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (int32 Count : {50, 100, 200})
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        ON_SCOPE_EXIT { World->BeginTearingDown(); World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
        World->InitializeActorsForPlay(FURL());
        World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
        TArray<AProject_JPlayerCharacter*> Players;
        for (int32 I = 0; I < Count; ++I)
        {
            auto* Controller = World->SpawnActor<AAIController>();
            auto* State = World->SpawnActor<AProject_JPlayerState>();
            State->SetOwner(Controller); Controller->SetPlayerState(State);
            auto* Player = ProjectJAuthoredAnimationFixture::Spawn(World, Controller, State, FVector(I * 200, 0, 1000));
            if (!TestNotNull(TEXT("Actual authored BP body/equipment participant"), Player)) return false;
            Controller->UnPossess(); Player->SetPlayerState(State);
            Player->GetAbilitySystemComponent()->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
            Player->FindComponentByClass<UProject_JWeaponPresentationComponent>()->EnterCombatPresentation();
            Players.Add(Player);
        }
        for (int32 Tick = 0; Tick < 3; ++Tick) World->Tick(LEVELTICK_All, 1.f / 60.f);
        for (int32 Repeat = -1; Repeat < 4; ++Repeat)
        {
            FAnimNotifyEventReference Ref(Event, Montage.Get()); Ref.SetNotifyInstanceID(7000 + Repeat + 1);
            uint64 Region = 0;
            if (Repeat >= 0) { Region = TRACE_BEGIN_REGION_WITH_ID(*FString::Printf(TEXT("ProjectJ.NotifyLifecycle.Count%d.R%d"), Count, Repeat)); }
            ON_SCOPE_EXIT { if (Region) TRACE_END_REGION_WITH_ID(Region); };
            const double Start = FPlatformTime::Seconds();
            for (auto* Player : Players) {
                Motion->NotifyBegin(Player->GetMesh(), Montage.Get(), Event->GetDuration(), Ref);
                Ground->NotifyBegin(Player->GetMesh(), Montage.Get(), Event->GetDuration(), Ref);
            }
            const double Began = FPlatformTime::Seconds();
            for (auto* Player : Players) if (!TestTrue(TEXT("Every participant acquired actual independent motion"),
                Player->FindComponentByClass<UProject_JWeaponPresentationComponent>()->IsIndependentMotionActive())) return false;
            const double TickStart = FPlatformTime::Seconds();
            for (int32 Tick = 0; Tick < 4; ++Tick)
                for (auto* Player : Players) Motion->NotifyTick(Player->GetMesh(), Montage.Get(), 1.f / 60.f, Ref);
            const double Ticked = FPlatformTime::Seconds();
            for (auto* Player : Players) {
                Ground->NotifyEnd(Player->GetMesh(), Montage.Get(), Ref);
                Motion->NotifyEnd(Player->GetMesh(), Montage.Get(), Ref);
            }
            const double Ended = FPlatformTime::Seconds();
            if (Region) { TRACE_END_REGION_WITH_ID(Region); Region = 0; }
            for (auto* Player : Players) TestFalse(TEXT("The matching end returns its authored motion lease"),
                Player->FindComponentByClass<UProject_JWeaponPresentationComponent>()->IsIndependentMotionActive());
            if (Repeat >= 0) {
                auto Row = MakeShared<FJsonObject>();
                Row->SetNumberField(TEXT("count"), Count); Row->SetNumberField(TEXT("repeat"), Repeat);
                Row->SetNumberField(TEXT("begin_ms"), (Began - Start) * 1000);
                Row->SetNumberField(TEXT("tick_ms"), (Ticked - TickStart) * 1000);
                Row->SetNumberField(TEXT("end_ms"), (Ended - Ticked) * 1000);
                Rows.Add(MakeShared<FJsonValueObject>(Row));
            }
        }
    }
    auto Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("scope_ko"), TEXT("Direct native notify stages on actual authored BP bodies/equipment: Motion uses public copied authored R notify; GroundContact is separate native synthetic registry fixture. 50/100/200, warmup1/repeat4, no world ticks inside measured stages; tick uses event-reference time0. Not full montage/GAS/world CPU, rendering/GPU, remote sockets or hand-contact quality."));
    Result->SetBoolField(TEXT("completed"), !HasAnyErrors());
    Result->SetStringField(TEXT("motion_asset"), Montage->GetPathName());
    Result->SetStringField(TEXT("notify_object"), Motion->GetPathName());
    Result->SetNumberField(TEXT("notify_start_seconds"), Event->GetTime());
    Result->SetNumberField(TEXT("notify_duration_seconds"), Event->GetDuration());
    Result->SetArrayField(TEXT("rows"), Rows);
    FString JSON; FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&JSON));
    if (!Output.IsEmpty()) {
        if (IFileManager::Get().FileExists(*Output)) { AddError(TEXT("Preserve prior notify measurement")); return false; }
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output), true);
        TestTrue(TEXT("Saved bounded notify stage raw rows"), FFileHelper::SaveStringToFile(JSON, *Output));
    }
    else AddInfo(JSON);
    return !HasAnyErrors();
}
#endif
