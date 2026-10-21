#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/Project_JCursorInput.h"
#include "Components/Project_JSkillInputRouterComponent.h"
#include "Project_JGameplayTags.h"
#include "Input/Project_JInputTriggerModifierTap.h"
#include "EnhancedPlayerInput.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCursorGestureTest, "ProjectJ.UI.Input.CursorModifierGesture",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCursorGestureTest::RunTest(const FString&)
{
    FProject_JCursorGesture Gesture;
    Gesture.KeyDown(EKeys::LeftAlt, false);
    Gesture.KeyDown(EKeys::LeftAlt, true);
    TestTrue(TEXT("Bare Left Alt toggles once on release"), Gesture.KeyUp(EKeys::LeftAlt));
    TestFalse(TEXT("Duplicate release does not toggle"), Gesture.KeyUp(EKeys::LeftAlt));
    Gesture.KeyDown(EKeys::LeftAlt, false); Gesture.KeyDown(EKeys::W, true);
    TestTrue(TEXT("Repeats from movement held before Alt do not create a new modifier chord"), Gesture.KeyUp(EKeys::LeftAlt));
    for (const FKey Key : {EKeys::Tab, EKeys::Enter, EKeys::F4, EKeys::W, EKeys::LeftControl, EKeys::RightAlt})
    {
        Gesture.KeyDown(EKeys::LeftAlt, false); Gesture.KeyDown(Key, false);
        Gesture.KeyUp(Key);
        TestFalse(TEXT("Alt OS/game shortcut never toggles, even if its other key is released first"), Gesture.KeyUp(EKeys::LeftAlt));
    }
    Gesture.KeyDown(EKeys::LeftAlt, false, true);
    TestFalse(TEXT("A modifier held before Alt also cancels cursor toggle"), Gesture.KeyUp(EKeys::LeftAlt));
    Gesture.KeyDown(EKeys::LeftAlt, false); Gesture.MouseDown();
    TestFalse(TEXT("Alt drag/click does not hide the cursor"), Gesture.KeyUp(EKeys::LeftAlt));
    Gesture.KeyDown(EKeys::LeftAlt, false); Gesture.Reset();
    TestFalse(TEXT("Focus loss or avatar change cancels a pending toggle"), Gesture.KeyUp(EKeys::LeftAlt));
    Gesture.KeyDown(EKeys::LeftControl, false);
    TestFalse(TEXT("Ctrl no longer owns cursor visibility"), Gesture.KeyUp(EKeys::LeftControl));
    Gesture.KeyDown(EKeys::RightAlt, false);
    TestFalse(TEXT("Right Alt / AltGr keeps its modifier role"), Gesture.KeyUp(EKeys::RightAlt));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJWalkModifierTapTest, "ProjectJ.UI.Input.WalkModifierTap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJWalkModifierTapTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* PC = World->SpawnActor<APlayerController>();
    auto* Input = NewObject<UEnhancedPlayerInput>(PC); PC->PlayerInput = Input;
    auto* Trigger = NewObject<UProject_JInputTriggerModifierTap>();
    int32 Toggles = 0;
    int32 EventId = 0;
    TArray<FKey> KnownKeys;
    // Supply evaluated raw key frames through the public key-state API. This
    // tests the production trigger, without accessing protected engine mappings.
    auto ClearEvents = [&]
    {
        for (const FKey& K : KnownKeys) if (auto* State = Input->GetKeyState(K))
        {
            for (auto& Events : State->EventCounts) Events.Reset();
            State->bWasJustFlushed = false;
        }
    };
    auto Evaluate = [&]
    {
        if (Trigger->UpdateState(Input, FInputActionValue(Input->IsPressed(EKeys::LeftControl)), 1.f/60.f) == ETriggerState::Triggered) ++Toggles;
    };
    auto Tick = [&]{ ClearEvents(); Evaluate(); };
    auto Key = [&](FKey K, EInputEvent Event)
    {
        ClearEvents();
        Input->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), K, Event, FPlatformTime::Cycles64()));
        KnownKeys.AddUnique(K);
        auto* State = Input->GetKeyState(K);
        State->EventCounts[Event].Add(++EventId);
        State->bDown = Event != IE_Released;
        Evaluate();
    };
    Key(EKeys::LeftControl, IE_Pressed); Tick(); Key(EKeys::LeftControl, IE_Repeat);
    TestEqual(TEXT("Held/repeating Ctrl never toggles before release"), Toggles, 0);
    Key(EKeys::LeftControl, IE_Released); Tick();
    TestEqual(TEXT("Bare Ctrl release toggles exactly once"), Toggles, 1);
    Key(EKeys::W, IE_Pressed); Key(EKeys::LeftControl, IE_Pressed); Key(EKeys::W, IE_Repeat);
    Key(EKeys::LeftControl, IE_Released); Key(EKeys::W, IE_Released);
    TestEqual(TEXT("Ctrl can change gait while movement is already held"), Toggles, 2);
    for (const FKey Chord : {EKeys::F, EKeys::M, EKeys::W, EKeys::LeftMouseButton, EKeys::Tab})
    {
        Key(EKeys::LeftControl, IE_Pressed); Key(Chord, IE_Pressed); Key(Chord, IE_Released); Key(EKeys::LeftControl, IE_Released);
    }
    TestEqual(TEXT("Skill/UI/mouse chords do not change walk intent"), Toggles, 2);
    Key(EKeys::LeftControl, IE_Pressed); Input->FlushPressedKeys(); Evaluate();
    Key(EKeys::LeftControl, IE_Released);
    TestEqual(TEXT("Flushed or missing press cannot create a new walk tap"), Toggles, 2);
    auto* Context = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_Default.IMC_Default"));
    auto* Walk = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Walk.IA_Walk"));
    if (!TestNotNull(TEXT("Production context"), Context) || !TestNotNull(TEXT("Existing walk action reused"), Walk)) return false;
    int32 WalkKeys = 0;
    for (const auto& Mapping : Context->GetMappings()) if (Mapping.Action == Walk)
    {
        ++WalkKeys; TestEqual(TEXT("Walk maps only to Left Ctrl"), Mapping.Key, EKeys::LeftControl);
    }
    TestEqual(TEXT("No old CapsLock or duplicate walk mapping"), WalkKeys, 1);
    TestTrue(TEXT("Production action uses native modifier-tap policy"), Walk->Triggers.ContainsByPredicate([](const UInputTrigger* T){return T && T->IsA<UProject_JInputTriggerModifierTap>();}));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJWalkPipelineTest, "ProjectJ.UI.Input.WalkProductionPipeline",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJWalkPipelineTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* PC = World->SpawnActor<APlayerController>();
    auto* Local = NewObject<ULocalPlayer>(GEngine);
    PC->Player = Local; Local->PlayerController = PC;
    auto* Input = NewObject<UEnhancedPlayerInput>(PC); PC->PlayerInput = Input;
    auto* Subsystem = NewObject<UEnhancedInputLocalPlayerSubsystem>(Local);
    auto* Context = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_Default.IMC_Default"));
    auto* Walk = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Walk.IA_Walk"));
    ON_SCOPE_EXIT { Subsystem->RemoveMappingContext(Context); PC->Player=nullptr; Local->PlayerController=nullptr; World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    FModifyContextOptions Options; Options.bForceImmediately = true; Options.bIgnoreAllPressedKeysUntilRelease = false;
    Subsystem->AddMappingContext(Context, 0, Options);
    auto* Component = NewObject<UEnhancedInputComponent>(PC);
    int32 Count = 0;
    Component->BindActionValueLambda(Walk, ETriggerEvent::Triggered, [&Count](const FInputActionValue&){++Count;});
    const TArray<UInputComponent*> Stack{Component};
    auto Key = [&](FKey K, EInputEvent Event)
    {
        Input->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),K,Event,FPlatformTime::Cycles64()));
        Input->ProcessInputStack(Stack,1.f/60.f,false);
    };
    Key(EKeys::LeftControl,IE_Pressed); Key(EKeys::LeftControl,IE_Repeat);
    TestEqual(TEXT("Production context holds Ctrl without gait toggle"),Count,0);
    Key(EKeys::LeftControl,IE_Released);
    TestEqual(TEXT("Actual Enhanced Input dispatches one walk toggle on Ctrl release"),Count,1);
    Key(EKeys::LeftControl,IE_Pressed); Key(EKeys::F,IE_Pressed); Key(EKeys::F,IE_Released); Key(EKeys::LeftControl,IE_Released);
    TestEqual(TEXT("Production Ctrl+F is not a walk tap"),Count,1);
    Key(EKeys::LeftControl,IE_Pressed); Input->FlushPressedKeys(); Input->ProcessInputStack(Stack,1.f/60.f,false);
    Key(EKeys::LeftControl,IE_Released);
    TestEqual(TEXT("Production flush cancels pending Ctrl tap"),Count,1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCursorMouseCancellationTest, "ProjectJ.UI.Input.MouseCancellationPreservesModifiers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCursorMouseCancellationTest::RunTest(const FString&)
{
    auto* Router = NewObject<UProject_JSkillInputRouterComponent>();
    const auto Tag = FProject_JGameplayTags::Get().InputTag_Weapon_RMB;
    Router->HandleModifierPressed(Tag);
    const FGameplayTagContainer Required(Tag), Empty;
    TestTrue(TEXT("Keyboard modifier is held"), Router->AreModifierTagsMatched(Required, Empty));
    Router->CancelMouseInput(); Router->CancelMouseInput();
    TestTrue(TEXT("Cursor transition only cancels mouse input, preserving held keyboard modifiers"), Router->AreModifierTagsMatched(Required, Empty));
    Router->ResetInputState();
    TestFalse(TEXT("Full avatar teardown also clears modifiers"), Router->AreModifierTagsMatched(Required, Empty));
    return true;
}
#endif
