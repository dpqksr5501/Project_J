#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Project_JGreatswordCharacter.h"
#include "Components/Project_JPlayerInputBindingComponent.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"

namespace ProjectJQueuedInputTests
{
TUniquePtr<FEnhancedInputActionEventBinding> CopyEvent(UEnhancedInputComponent* Input, const UInputAction* Action, ETriggerEvent Event)
{
    for (const auto& Binding : Input->GetActionEventBindings())
    {
        if (Binding->GetAction() == Action && Binding->GetTriggerEvent() == Event) { return Binding->Clone(); }
    }
    return {};
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJQueuedInputOriginTest, "ProjectJ.Continuation.QueuedInputOrigin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJQueuedInputOriginTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Previous = World->SpawnActor<AProject_JGreatswordCharacter>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
    auto* Current = World->SpawnActor<AProject_JGreatswordCharacter>(FVector(1000, 0, 0), FRotator::ZeroRotator, Spawn);
    auto* Binding = Previous->FindComponentByClass<UProject_JPlayerInputBindingComponent>();
    auto* OldInput = NewObject<UEnhancedInputComponent>(Previous); OldInput->RegisterComponent();
    auto* NewInput = NewObject<UEnhancedInputComponent>(Current); NewInput->RegisterComponent();
    FProject_JPlayerInputActionSet Actions;
    Actions.MoveIntentForwardAction = NewObject<UInputAction>(); Actions.MoveIntentBackwardAction = NewObject<UInputAction>();
    Actions.MoveIntentLeftAction = NewObject<UInputAction>(); Actions.MoveIntentRightAction = NewObject<UInputAction>();
    TestTrue(TEXT("Original action bindings"), Binding->BindInput(OldInput, Previous, Actions));
    // EnhancedPlayerInput copies these delegates before executing any of them.
    auto OldStart = ProjectJQueuedInputTests::CopyEvent(OldInput, Actions.MoveIntentForwardAction, ETriggerEvent::Started);
    auto OldRelease = ProjectJQueuedInputTests::CopyEvent(OldInput, Actions.MoveIntentBackwardAction, ETriggerEvent::Completed);
    if (!TestTrue(TEXT("Real engine binding clones retained"), OldStart && OldRelease)) { return false; }
    TestTrue(TEXT("A prior queued callback rebinds to the new pawn/device"), Binding->BindInput(NewInput, Current, Actions));
    TestEqual(TEXT("Old owned bindings are removed"), OldInput->GetActionEventBindings().Num(), 0);
    TestFalse(TEXT("New binding initially has no semantic update"), Binding->IsComponentTickEnabled());
    OldStart->Execute(FInputActionInstance(Actions.MoveIntentForwardAction));
    TestFalse(TEXT("Queued old press cannot enqueue semantic input on the new pawn"), Binding->IsComponentTickEnabled());
    Binding->TickComponent(0, LEVELTICK_All, nullptr);
    auto NewStart = ProjectJQueuedInputTests::CopyEvent(NewInput, Actions.MoveIntentBackwardAction, ETriggerEvent::Started);
    if (!TestTrue(TEXT("Fresh engine binding exists"), NewStart.IsValid())) { return false; }
    NewStart->Execute(FInputActionInstance(Actions.MoveIntentBackwardAction));
    TestTrue(TEXT("Fresh press still schedules its own semantic input"), Binding->IsComponentTickEnabled());
    Binding->TickComponent(0, LEVELTICK_All, nullptr);
    OldRelease->Execute(FInputActionInstance(Actions.MoveIntentBackwardAction));
    TestFalse(TEXT("Queued old release cannot retire the new pawn's held direction"), Binding->IsComponentTickEnabled());
    Binding->UnbindInput();
    NewStart->Execute(FInputActionInstance(Actions.MoveIntentBackwardAction));
    TestFalse(TEXT("An unbound clone cannot restore a retired semantic update"), Binding->IsComponentTickEnabled());
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMoveIntentOrderTest, "ProjectJ.Continuation.MoveIntentOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMoveIntentOrderTest::RunTest(const FString&)
{
    using D = EProject_JMoveIntentDirection;
    FProject_JMoveIntentState State;
    TestFalse(TEXT("No held direction"), State.HasActiveIntent());
    TestTrue(TEXT("Empty intent is zero"), State.GetIntent().IsNearlyZero());
    TestFalse(TEXT("Unsupported direction is rejected"), State.Press(static_cast<D>(255)));
    State.Press(D::Forward); State.Press(D::Backward);
    TestTrue(TEXT("Opposed vertical directions use the latest real press"), State.GetIntent().Equals(FVector2D(0, -1)));
    TestFalse(TEXT("Duplicate Started does not change original press order"), State.Press(D::Forward));
    TestTrue(TEXT("A repeated old press cannot steal priority"), State.GetIntent().Equals(FVector2D(0, -1)));
    State.Release(D::Backward);
    TestTrue(TEXT("Releasing the winner exposes the still-held direction"), State.GetIntent().Equals(FVector2D(0, 1)));
    TestFalse(TEXT("Duplicate Completed/Canceled is idempotent"), State.Release(D::Backward));
    State.Press(D::Left); State.Press(D::Right);
    TestTrue(TEXT("Diagonal intent stays normalized"), State.GetIntent().Equals(FVector2D(1, 1).GetSafeNormal()));
    State.Release(D::Right);
    TestTrue(TEXT("Horizontal release preserves other held intent"), State.GetIntent().Equals(FVector2D(-1, 1).GetSafeNormal()));
    State.Clear();
    for (int32 Index = 0; Index < 6000; ++Index)
    {
        State.Press(D::Forward); State.Press(D::Backward);
        if (!State.GetIntent().Equals(FVector2D(0, -1)) || State.GetPressRank(D::Forward) > 4 || State.GetPressRank(D::Backward) > 4)
        { AddError(TEXT("Long chord sequence changed semantic priority or exceeded bounded ranks")); break; }
        State.Release(D::Backward); State.Release(D::Forward);
    }
    TestFalse(TEXT("Long release sequence leaves no held state"), State.HasActiveIntent());
    return !HasAnyErrors();
}
#endif
