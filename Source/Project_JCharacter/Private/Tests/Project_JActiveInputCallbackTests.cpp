#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Project_JGreatswordCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Combat/Project_JGameplayAbility_Sprint.h"
#include "Components/Project_JCombatStateComponent.h"
#include "Components/Project_JSkillInputExecutionComponent.h"
#include "Components/Project_JSkillInputRouterComponent.h"
#include "Components/Project_JPlayerInputBindingComponent.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "UObject/UnrealType.h"

namespace
{
UProject_JAbilitySystemComponent* InstallInputASC(AProject_JGreatswordCharacter* Player)
{
    auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Player);
    FindFProperty<FObjectPropertyBase>(AProject_JBaseCharacter::StaticClass(), TEXT("AbilitySystemComponent"))->SetObjectPropertyValue_InContainer(Player, ASC);
    ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Player, Player);
    Player->FindComponentByClass<UProject_JCombatStateComponent>()->BindToAbilitySystem(ASC);
    Player->FindComponentByClass<UProject_JSkillInputExecutionComponent>()->Initialize(Player);
    Player->FindComponentByClass<UProject_JSkillInputRouterComponent>()->Initialize(Player);
    return ASC;
}
FGameplayAbilitySpecHandle GiveInputSprint(UProject_JAbilitySystemComponent* ASC, FGameplayTag Tag)
{
    FGameplayAbilitySpec Spec(UProject_JGameplayAbility_Sprint::StaticClass(), 1);
    Spec.GetDynamicSpecSourceTags().AddTag(Tag);
    return ASC->GiveAbility(Spec);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJRouterCancelReentryTest, "ProjectJ.Continuation.RouterCancelReentry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJRouterCancelReentryTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Previous = World->SpawnActor<AProject_JGreatswordCharacter>();
    auto* Current = World->SpawnActor<AProject_JGreatswordCharacter>();
    auto* PreviousASC = InstallInputASC(Previous);
    auto* CurrentASC = InstallInputASC(Current);
    const auto& Tags = FProject_JGameplayTags::Get();
    const auto First = GiveInputSprint(PreviousASC, Tags.InputTag_Weapon_LMB);
    const auto Second = GiveInputSprint(PreviousASC, Tags.InputTag_Weapon_RMB);
    const auto NewInput = GiveInputSprint(CurrentASC, Tags.InputTag_Weapon_RMB);
    auto* Router = Previous->FindComponentByClass<UProject_JSkillInputRouterComponent>();
    Router->HandleButtonPressed(EProject_JSkillInputButton::LMB);
    Router->HandleButtonPressed(EProject_JSkillInputButton::RMB);
    if (!TestTrue(TEXT("Both old mouse intents activate actual GAS abilities"),
        PreviousASC->FindAbilitySpecFromHandle(First)->IsActive() && PreviousASC->FindAbilitySpecFromHandle(Second)->IsActive())) return false;
    bool Rebound = false;
    const auto Callback = PreviousASC->RegisterGameplayTagEvent(Tags.State_Movement_Sprinting, EGameplayTagEventType::AnyCountChange).AddLambda(
        [&](FGameplayTag, int32 Count) {
            if (Count == 1 && !Rebound) {
                Rebound = true; Router->Initialize(Current);
                Router->HandleButtonPressed(EProject_JSkillInputButton::RMB);
            }
        });
    Router->CancelMouseInput();
    PreviousASC->RegisterGameplayTagEvent(Tags.State_Movement_Sprinting, EGameplayTagEventType::AnyCountChange).Remove(Callback);
    TestTrue(TEXT("First actual ability release rebinds the router"), Rebound);
    TestFalse(TEXT("Remaining old input returns to its original ASC"), PreviousASC->FindAbilitySpecFromHandle(Second)->InputPressed);
    TestTrue(TEXT("Old cancel cannot release the new avatar's input"), CurrentASC->FindAbilitySpecFromHandle(NewInput)->InputPressed);
    Router->CancelMouseInput();
    TestFalse(TEXT("New input remains independently releasable"), CurrentASC->FindAbilitySpecFromHandle(NewInput)->InputPressed);
    PreviousASC->ClearAllAbilities(); CurrentASC->ClearAllAbilities();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJActiveMoveReentryTest, "ProjectJ.Continuation.ActiveMoveReentry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJActiveMoveReentryTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Previous = World->SpawnActor<AProject_JGreatswordCharacter>();
    auto* Current = World->SpawnActor<AProject_JGreatswordCharacter>();
    auto* Controller = World->SpawnActor<APlayerController>();
    Controller->SetAsLocalPlayerController(); Controller->Possess(Previous);
    auto* ASC = InstallInputASC(Previous);
    const auto& Tags = FProject_JGameplayTags::Get();
    Previous->SprintAbilityTag = Tags.State_Movement_Sprinting;
    const auto Sprint = GiveInputSprint(ASC, Tags.InputTag_Weapon_RMB);
    auto* Binding = Previous->FindComponentByClass<UProject_JPlayerInputBindingComponent>();
    auto* OldDevice = NewObject<UEnhancedInputComponent>(Previous); OldDevice->RegisterComponent();
    auto* NewDevice = NewObject<UEnhancedInputComponent>(Current); NewDevice->RegisterComponent();
    FProject_JPlayerInputActionSet Actions; Actions.MoveAction = NewObject<UInputAction>(); Actions.MoveAction->ValueType = EInputActionValueType::Axis2D;
    TestTrue(TEXT("Old device acquires movement bindings"), Binding->BindInput(OldDevice, Previous, Actions));
    TestTrue(TEXT("Actual sprint is active before movement's cancel callback"), ASC->TryActivateAbility(Sprint));
    bool Rebound = false, NewBound = false;
    const auto Callback = ASC->RegisterGameplayTagEvent(Tags.State_Movement_Sprinting, EGameplayTagEventType::NewOrRemoved).AddLambda(
        [&](FGameplayTag, int32 Count) {
            if (Count == 0 && !Rebound) { Rebound = true; NewBound = Binding->BindInput(NewDevice, Current, Actions); }
        });
    Binding->HandleMove(FInputActionValue(FVector2D(0.0, -1.0)));
    ASC->RegisterGameplayTagEvent(Tags.State_Movement_Sprinting, EGameplayTagEventType::NewOrRemoved).Remove(Callback);
    TestTrue(TEXT("Movement's actual GAS cancel callback installs a new connection"), Rebound && NewBound);
    TestFalse(TEXT("The old move must not publish a start event on the new pawn"), Current->bHadMoveInputForReplication);
    TestEqual(TEXT("New device remains bound"), NewDevice->GetActionEventBindings().Num(), 3);
    Binding->HandleMove(FInputActionValue(FVector2D(0.0, 1.0)));
    TestTrue(TEXT("A new connection's own movement still publishes its start"), Current->bHadMoveInputForReplication);
    Binding->UnbindInput(); ASC->ClearAllAbilities();
    return true;
}
#endif
