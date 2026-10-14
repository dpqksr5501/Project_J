#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Project_JGreatswordCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Combat/Project_JGameplayAbility_Melee.h"
#include "Combat/Project_JGameplayAbility_Sprint.h"
#include "Components/Project_JSkillInputExecutionComponent.h"
#include "Components/Project_JSkillInputRouterComponent.h"
#include "Components/Project_JPlayerInputBindingComponent.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "UObject/UnrealType.h"
#include "TimerManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJInputRetirementTest, "ProjectJ.RuntimeOwnership.InputRetirement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJInputRetirementTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Player = World->SpawnActor<AProject_JGreatswordCharacter>();
    auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Player);
    FindFProperty<FObjectPropertyBase>(AProject_JBaseCharacter::StaticClass(), TEXT("AbilitySystemComponent"))->SetObjectPropertyValue_InContainer(Player, ASC);
    ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Player, Player);
    const auto InputTag = FProject_JGameplayTags::Get().InputTag_Weapon_RMB;
    FGameplayAbilitySpec Spec(UProject_JGameplayAbility_Melee::StaticClass(), 1); Spec.GetDynamicSpecSourceTags().AddTag(InputTag);
    const auto Handle = ASC->GiveAbility(Spec);
    auto* Binding = Player->FindComponentByClass<UProject_JPlayerInputBindingComponent>();
    auto* DeviceInput = NewObject<UEnhancedInputComponent>(Player); DeviceInput->RegisterComponent();
    FProject_JPlayerInputActionSet Actions; Actions.AttackAction = NewObject<UInputAction>(); Actions.HeavyAttackAction = NewObject<UInputAction>();
    TestTrue(TEXT("Input owner acquires actual EnhancedInput bindings"), Binding->BindInput(DeviceInput, Player, Actions));
    TestTrue(TEXT("Device has acquired event bindings"), DeviceInput->GetActionEventBindings().Num() > 0);
    Binding->DestroyComponent();
    TestEqual(TEXT("Destroy before BeginPlay removes all owned device bindings"), DeviceInput->GetActionEventBindings().Num(), 0);
    TestFalse(TEXT("Destroyed input binding owner cannot rebind"), Binding->BindInput(DeviceInput, Player, Actions));
    auto* Input = Player->FindComponentByClass<UProject_JSkillInputExecutionComponent>(); Input->Initialize(Player);
    Input->HandleInputTagPressed(InputTag);
    TestTrue(TEXT("Actual GAS input is held before component retirement"), ASC->FindAbilitySpecFromHandle(Handle)->InputPressed);
    Input->DestroyComponent();
    TestFalse(TEXT("Destroy before BeginPlay releases the owned GAS input"), ASC->FindAbilitySpecFromHandle(Handle)->InputPressed);
    Input->HandleInputTagReleased(InputTag); // Leave a stable baseline for independent late-initialize coverage.
    Input->Initialize(Player); Input->HandleInputTagPressed(InputTag);
    TestFalse(TEXT("Late initialization cannot revive a retired executor"), ASC->FindAbilitySpecFromHandle(Handle)->InputPressed);
    ASC->AbilityInputTagReleased(InputTag);
    auto* LiveInput = NewObject<UProject_JSkillInputExecutionComponent>(Player); LiveInput->RegisterComponent(); LiveInput->Initialize(Player);
    auto* InputProperty = FindFProperty<FObjectPropertyBase>(AProject_JPlayerCharacter::StaticClass(), TEXT("SkillInputExecutionComponent"));
    if (!TestNotNull(TEXT("Independent live input execution seam"), InputProperty)) { return false; }
    InputProperty->SetObjectPropertyValue_InContainer(Player, LiveInput);
    auto* Router = Player->FindComponentByClass<UProject_JSkillInputRouterComponent>(); Router->Initialize(Player);
    auto* Mapping = NewObject<UProject_JSkillInputMappingData>(Router);
    auto& Right = Mapping->Chords.AddDefaulted_GetRef(); Right.bRequiresLMB = false; Right.bRequiresRMB = true; Right.InputTag = InputTag;
    Router->InputMappingData = Mapping;
    auto& Combined = Mapping->Chords.AddDefaulted_GetRef(); Combined.bRequiresLMB = Combined.bRequiresRMB = true;
    Combined.InputTag = FProject_JGameplayTags::Get().InputTag_Weapon_LMB;
    int32 Delivered = 0;
    ASC->GenericGameplayEventCallbacks.FindOrAdd(InputTag).AddLambda([&](const FGameplayEventData*) { ++Delivered; });
    Router->HandleButtonPressed(EProject_JSkillInputButton::RMB);
    TestTrue(TEXT("Router owns an actual pending chord timer"), World->GetTimerManager().TimerExists(Router->PendingChordTimerHandle));
    Router->DestroyComponent();
    TestFalse(TEXT("Router destruction retires its pending chord timer"), World->GetTimerManager().TimerExists(Router->PendingChordTimerHandle));
    Router->Initialize(Player);
    Router->HandleButtonPressed(EProject_JSkillInputButton::RMB); Router->HandleButtonReleased(EProject_JSkillInputButton::RMB);
    TestEqual(TEXT("Retired router cannot deliver a late quick tap"), Delivered, 0);
    ASC->GenericGameplayEventCallbacks.Empty();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJInputBindingReentryTest, "ProjectJ.RuntimeOwnership.InputBindingReentry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJInputBindingReentryTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Player = World->SpawnActor<AProject_JGreatswordCharacter>();
    auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Player);
    FindFProperty<FObjectPropertyBase>(AProject_JBaseCharacter::StaticClass(), TEXT("AbilitySystemComponent"))->SetObjectPropertyValue_InContainer(Player, ASC);
    ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Player, Player);
    Player->FindComponentByClass<UProject_JSkillInputExecutionComponent>()->Initialize(Player);
    Player->FindComponentByClass<UProject_JSkillInputRouterComponent>()->Initialize(Player);
    const auto InputTag = FProject_JGameplayTags::Get().InputTag_Weapon_RMB;
    FGameplayAbilitySpec Spec(UProject_JGameplayAbility_Sprint::StaticClass(), 1);
    Spec.GetDynamicSpecSourceTags().AddTag(InputTag); const auto Handle = ASC->GiveAbility(Spec);
    auto* Binding = Player->FindComponentByClass<UProject_JPlayerInputBindingComponent>();
    auto MakeInput = [&] { auto* Input = NewObject<UEnhancedInputComponent>(Player); Input->RegisterComponent(); return Input; };
    auto* Original = MakeInput(); auto* Superseded = MakeInput(); auto* Latest = MakeInput();
    auto* Action = NewObject<UInputAction>();
    auto* Mapping = NewObject<UProject_JSkillInputMappingData>();
    auto& Direct = Mapping->DirectSkillBindings.AddDefaulted_GetRef(); Direct.InputAction = Action; Direct.InputTag = InputTag;
    FProject_JPlayerInputActionSet Actions; Actions.SkillInputMappingData = Mapping;
    TestTrue(TEXT("Original device binds"), Binding->BindInput(Original, Player, Actions));
    for (const auto& Event : Original->GetActionEventBindings())
    {
        if (Event->GetAction() == Action && Event->GetTriggerEvent() == ETriggerEvent::Started)
        { Event->Execute(FInputActionInstance(Action)); break; }
    }
    if (!TestTrue(TEXT("Actual input activates the persistent sprint ability"), ASC->FindAbilitySpecFromHandle(Handle)->IsActive())) { return false; }
    bool bReentered = false; bool bLatestBound = false;
    const auto SprintTag = FProject_JGameplayTags::Get().State_Movement_Sprinting;
    const auto Callback = ASC->RegisterGameplayTagEvent(SprintTag, EGameplayTagEventType::NewOrRemoved).AddLambda(
        [&](FGameplayTag, int32 Count) {
            if (Count == 0 && !bReentered) { bReentered = true; bLatestBound = Binding->BindInput(Latest, Player, Actions); }
        });
    const bool bSupersededBound = Binding->BindInput(Superseded, Player, Actions);
    ASC->RegisterGameplayTagEvent(SprintTag, EGameplayTagEventType::NewOrRemoved).Remove(Callback);
    TestTrue(TEXT("Actual input release synchronously rebinds the same owner"), bReentered && bLatestBound);
    TestFalse(TEXT("Older outer bind cannot supersede callback's new connection"), bSupersededBound);
    TestEqual(TEXT("Retired original device is empty"), Original->GetActionEventBindings().Num(), 0);
    TestEqual(TEXT("Superseded device never acquires bindings"), Superseded->GetActionEventBindings().Num(), 0);
    TestEqual(TEXT("Latest device holds the three direct action edges"), Latest->GetActionEventBindings().Num(), 3);
    for (const auto& Event : Latest->GetActionEventBindings())
    {
        if (Event->GetAction() == Action && Event->GetTriggerEvent() == ETriggerEvent::Started)
        { Event->Execute(FInputActionInstance(Action)); break; }
    }
    TestTrue(TEXT("Latest connection continues routing actual input"), ASC->FindAbilitySpecFromHandle(Handle)->InputPressed);
    Binding->UnbindInput();
    TestEqual(TEXT("Latest connection's device handles can all be released"), Latest->GetActionEventBindings().Num(), 0);
    TestFalse(TEXT("Latest connection releases its own held GAS input"), ASC->FindAbilitySpecFromHandle(Handle)->InputPressed);
    ASC->ClearAbility(Handle);
    return true;
}
#endif
