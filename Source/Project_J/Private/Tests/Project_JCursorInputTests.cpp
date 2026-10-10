#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/Project_JCursorInput.h"
#include "Components/Project_JSkillInputRouterComponent.h"
#include "Project_JGameplayTags.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCursorGestureTest, "ProjectJ.UI.Input.CursorModifierGesture",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCursorGestureTest::RunTest(const FString&)
{
    FProject_JCursorGesture Gesture;
    Gesture.KeyDown(EKeys::LeftControl, false);
    Gesture.KeyDown(EKeys::LeftControl, true);
    TestTrue(TEXT("Bare Left Ctrl toggles once on release"), Gesture.KeyUp(EKeys::LeftControl));
    TestFalse(TEXT("Duplicate release does not toggle"), Gesture.KeyUp(EKeys::LeftControl));
    Gesture.KeyDown(EKeys::LeftControl, false); Gesture.KeyDown(EKeys::W, true);
    TestTrue(TEXT("Repeats from movement held before Ctrl do not create a new modifier chord"), Gesture.KeyUp(EKeys::LeftControl));
    for (const FKey Key : {EKeys::F, EKeys::M, EKeys::W, EKeys::RightControl})
    {
        Gesture.KeyDown(EKeys::LeftControl, false); Gesture.KeyDown(Key, false);
        Gesture.KeyUp(Key);
        TestFalse(TEXT("Modifier chord never toggles, even if its other key is released first"), Gesture.KeyUp(EKeys::LeftControl));
    }
    Gesture.KeyDown(EKeys::LeftControl, false); Gesture.MouseDown();
    TestFalse(TEXT("Ctrl drag/click does not hide the cursor"), Gesture.KeyUp(EKeys::LeftControl));
    Gesture.KeyDown(EKeys::LeftControl, false); Gesture.Reset();
    TestFalse(TEXT("Focus loss or avatar change cancels a pending toggle"), Gesture.KeyUp(EKeys::LeftControl));
    Gesture.KeyDown(EKeys::RightControl, false);
    TestFalse(TEXT("Right Ctrl keeps its modifier role"), Gesture.KeyUp(EKeys::RightControl));
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
