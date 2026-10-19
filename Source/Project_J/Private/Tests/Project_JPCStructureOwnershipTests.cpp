#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Game/Project_JPlayerState.h"
#include "Social/Project_JSocialSubsystem.h"
#include "UI/Project_JPlayerUIComponent.h"
#include "UI/Project_JPlayerHUDWidget.h"
#include "Project_JGreatswordCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Combat/Project_JGameplayAbility_Sprint.h"
#include "Components/Project_JSkillInputExecutionComponent.h"
#include "Components/Project_JCombatStateComponent.h"
#include "Components/InputComponent.h"
#include "Components/TimelineComponent.h"
#include "Mount/Project_JFlyingMountCharacter.h"
#include "Project_JNPCCharacter.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Combat/Project_JCombatConfiguration.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Script.h"
#include "Blueprint/BlueprintExceptionInfo.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAuthoredDoorOptionsTest, "ProjectJ.PCStructure.AuthoredDoorOptions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAuthoredDoorOptionsTest::RunTest(const FString&)
{
    UClass* Class = LoadClass<AActor>(nullptr, TEXT("/Game/Map_Resource/Variants/ProjectExisting/LevelPrototyping/Interactable/Door/BP_DoorFrame.BP_DoorFrame_C"));
    if (!TestNotNull(TEXT("Actual prototype door class"), Class)) return false;
    auto* AddDoor = FindFProperty<FBoolProperty>(Class, TEXT("add Door"));
    auto* SplitDoor = FindFProperty<FBoolProperty>(Class, TEXT("split Door"));
    auto* DoorProperty = FindFProperty<FObjectPropertyBase>(Class, TEXT("Door"));
    auto* Door2Property = FindFProperty<FObjectPropertyBase>(Class, TEXT("Door2"));
    if (!TestTrue(TEXT("Authored option and component properties exist"), AddDoor && SplitDoor && DoorProperty && Door2Property)) return false;
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::LevelTransition); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
    for (int32 Mode : {0, 1, 2, 3})
    {
        const FTransform Spawn(FVector(Mode * 2000, 0, 0));
        auto* Door = World->SpawnActorDeferred<AActor>(Class, Spawn, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!TestNotNull(TEXT("Deferred authored actor"), Door)) return false;
        AddDoor->SetPropertyValue_InContainer(Door, Mode != 0);
        SplitDoor->SetPropertyValue_InContainer(Door, Mode >= 2);
        int32 Exceptions = 0;
        const auto ExceptionHandle = FBlueprintCoreDelegates::OnScriptException.AddLambda(
            [&](const UObject* Object, const FFrame&, const FBlueprintExceptionInfo& Info)
            { if (Object == Door && Info.GetType() == EBlueprintExceptionType::AccessViolation) ++Exceptions; });
        ON_SCOPE_EXIT { FBlueprintCoreDelegates::OnScriptException.Remove(ExceptionHandle); };
        Door->FinishSpawning(Spawn);
        auto* First = Cast<USceneComponent>(DoorProperty->GetObjectPropertyValue_InContainer(Door));
        auto* Second = Cast<USceneComponent>(Door2Property->GetObjectPropertyValue_InContainer(Door));
        TestEqual(TEXT("Primary component follows AddDoor"), IsValid(First), Mode != 0);
        TestEqual(TEXT("Secondary component follows SplitDoor"), IsValid(Second), Mode >= 2);
        const FVector FirstClosed = First ? First->GetRelativeLocation() : FVector::ZeroVector;
        const FVector SecondClosed = Second ? Second->GetRelativeLocation() : FVector::ZeroVector;
        if (Mode == 3) { First->DestroyComponent(); DoorProperty->SetObjectPropertyValue_InContainer(Door, nullptr); }
        TInlineComponentArray<UTimelineComponent*> Timelines(Door);
        if (!TestEqual(TEXT("The authored timeline is preserved"), Timelines.Num(), 1)) return false;
        auto* Timeline = Timelines[0];
        auto* Open = Door->FindFunction(TEXT("Open Door")); auto* Close = Door->FindFunction(TEXT("Close Door"));
        if (!TestTrue(TEXT("Both public authored events exist"), Open && Close)) return false;
        Door->ProcessEvent(Open, nullptr);
        TestTrue(TEXT("OpenDoor plays the existing timeline"), Timeline->IsPlaying());
        Timeline->SetPlaybackPosition(Timeline->GetTimelineLength(), true);
        if (Mode == 1 || Mode == 2) TestFalse(TEXT("Opening moves the primary panel"), First->GetRelativeLocation().Equals(FirstClosed));
        if (Mode >= 2) TestFalse(TEXT("A surviving secondary panel moves independently"), Second->GetRelativeLocation().Equals(SecondClosed));
        Door->ProcessEvent(Close, nullptr);
        TestTrue(TEXT("CloseDoor reverses the existing timeline"), Timeline->IsReversing());
        Timeline->SetPlaybackPosition(0.f, true);
        if (Mode == 1 || Mode == 2) TestTrue(TEXT("Closing restores the primary panel"), First->GetRelativeLocation().Equals(FirstClosed));
        if (Mode >= 2) TestTrue(TEXT("Closing restores the surviving secondary panel"), Second->GetRelativeLocation().Equals(SecondClosed));
        TestEqual(FString::Printf(TEXT("Option %d direct events produce no script exceptions"), Mode), Exceptions, 0);
        Door->Destroy();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAuthoredClassGrantOwnershipTest, "ProjectJ.PCStructure.AuthoredClassGrantOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAuthoredClassGrantOwnershipTest::RunTest(const FString&)
{
    auto* Class = LoadObject<UProject_JCharacterClassDefinition>(nullptr, TEXT("/Game/DataAssetSets/Class/DA_Class_Greatsword.DA_Class_Greatsword"));
    auto* Set = LoadObject<UProject_JAbilitySet>(nullptr, TEXT("/Game/AS/AS_Greatsword_Base.AS_Greatsword_Base"));
    if (!TestNotNull(TEXT("Actual class asset"), Class) || !TestNotNull(TEXT("Actual weapon set"), Set) ||
        !TestNotNull(TEXT("Default style remains the weapon grant owner"), Class->DefaultCombatStyle.Get())) return false;
    auto* Style = Class->DefaultCombatStyle.Get();
    TestTrue(TEXT("The default style grants the actual weapon set"), Style->GetRuntimeAbilitySets().Contains(Set));
    TestFalse(TEXT("The class does not independently grant the same weapon set"), Class->AbilitySets.Contains(Set));
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Owner = World->SpawnActor<AProject_JNPCCharacter>();
    auto* ASC = CastChecked<UProject_JAbilitySystemComponent>(Owner->GetAbilitySystemComponent());
    ASC->InitAbilityActorInfo(Owner, Owner);
    auto* Progression = Owner->GetProgressionComponent();
    Progression->InitializeDefaults(Class, nullptr, 1);
    TestEqual(TEXT("The authored class and style grant eight specs once, including core combat toggle"), ASC->GetActivatableAbilities().Num(), 8);
    for (const auto& Entry : Set->GrantedAbilityEntries)
    {
        int32 Matching = 0;
        for (const auto& Spec : ASC->GetActivatableAbilities())
        {
            if (Spec.Ability && Spec.Ability->GetClass() == Entry.Ability && Spec.Level == Entry.AbilityLevel &&
                Spec.GetDynamicSpecSourceTags().HasTagExact(Entry.InputTag)) ++Matching;
        }
        TestEqual(FString::Printf(TEXT("Actual input %s has one matching class/level spec"), *Entry.InputTag.ToString()), Matching, 1);
    }
    TestTrue(TEXT("Level changes preserve the grant owner"), Progression->SetLevel(12));
    TestEqual(TEXT("Level publication does not grant a second copy"), ASC->GetActivatableAbilities().Num(), 8);
    TArray<FText> Conflicts;
    FProject_JCombatConfiguration::FindInputGrantConflicts(*ASC, Conflicts);
    TestTrue(TEXT("Actual default composition has no cross-source input collisions"), Conflicts.IsEmpty());
    const int32 BeforeEquipment = ASC->GetActivatableAbilities().Num();
    FProject_JAbilitySet_GrantedHandles Equipment;
    Set->GiveToAbilitySystem(ASC, &Equipment, Owner, FName(*(TEXT("CombatStyle.") + Style->GetPathName() + TEXT(".") + Set->GetPathName())));
    TestEqual(TEXT("An equipment provider shares the style source without new specs"), ASC->GetActivatableAbilities().Num(), BeforeEquipment);
    Progression->DestroyComponent();
    TestEqual(TEXT("Retiring progression preserves the equipment-owned weapon specs only"), ASC->GetActivatableAbilities().Num(), Set->GrantedAbilityEntries.Num());
    Set->TakeFromAbilitySystem(ASC, &Equipment);
    TestEqual(TEXT("The final provider retires every weapon spec"), ASC->GetActivatableAbilities().Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAuthoredMountTickOwnershipTest, "ProjectJ.PCStructure.AuthoredMountTickOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAuthoredMountTickOwnershipTest::RunTest(const FString&)
{
    auto* Class = LoadClass<AProject_JFlyingMountCharacter>(nullptr, TEXT("/Game/Mount_Assets/Wyvern/BP_Wyvern.BP_Wyvern_C"));
    if (!TestNotNull(TEXT("Current authored Wyvern uses the native flying mount"), Class)) return false;
    TestFalse(TEXT("An unconnected authored Tick node does not install a script tick implementation"), Class->IsFunctionImplementedInScript(TEXT("ReceiveTick")));
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { if (World->GetBegunPlay()) World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Mount = World->SpawnActor<AProject_JFlyingMountCharacter>(Class);
    if (!TestNotNull(TEXT("Actual authored mount spawns"), Mount)) return false;
    World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
    TestEqual(TEXT("The actual authored mount starts grounded"), Mount->GetFlightState(), EProject_JMountFlightState::Grounded);
    TestFalse(TEXT("Idle actor lifetime remains owned by the native flight state machine"), Mount->IsActorTickEnabled());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJIdentityRetirementTest, "ProjectJ.PCStructure.IdentityRetirement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJIdentityRetirementTest::RunTest(const FString&)
{
    AddExpectedError(TEXT("Character data registry is empty."), EAutomationExpectedErrorFlags::Contains, 1);
    TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));
    GI->InitializeStandalone();
    UWorld* World = GI->GetWorld();
    ON_SCOPE_EXIT { if (World->GetBegunPlay()) World->EndPlay(EEndPlayReason::Quit); GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Social = GI->GetSubsystem<UProject_JSocialSubsystem>();
    if (!TestNotNull(TEXT("Actual GameInstance owns the social service"), Social)) return false;
    FProject_JAccountId Account; Account.Value = FGuid::NewGuid();
    FProject_JCharacterId Next; Next.Value = FGuid::NewGuid();
    FProject_JSocialGroupSnapshot Group; Group.GroupId = TEXT("IdentityRetired"); Group.LeaderCharacterId = Next.Value; Group.Members = {Next.Value};
    if (!TestTrue(TEXT("Trusted party data exists before identity publication"), Social->RestoreGroupSnapshot(EProject_JSocialGroupKind::Party, Group))) return false;
    TStrongObjectPtr<AProject_JPlayerState> Player(World->SpawnActor<AProject_JPlayerState>());
    if (!TestNotNull(TEXT("Spawned PlayerState is world owned"), Player.Get())) return false;
    bool bDestroyed = false;
    auto Callback = Player->OnCharacterIdentityChanged.AddLambda([&] { bDestroyed = Player->Destroy(); CollectGarbage(RF_NoFlags); });
    ON_SCOPE_EXIT { Player->OnCharacterIdentityChanged.Remove(Callback); };
    Player->SetIdentity(Account, Next);
    TestTrue(TEXT("Identity listener actually retires the PlayerState"), bDestroyed && Player->IsActorBeingDestroyed());
    TestTrue(TEXT("Identity completion cannot publish new social data to the retired player"), Player->GetPartyId_Implementation().IsNone());
    TestFalse(TEXT("The common social service cannot admit a retired player"), Social->CreateGuild(Player.Get()).bSucceeded);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJIdentityProjectionDetachTest, "ProjectJ.PCStructure.IdentityProjectionDetach",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJIdentityProjectionDetachTest::RunTest(const FString&)
{
    AddExpectedError(TEXT("Character data registry is empty."), EAutomationExpectedErrorFlags::Contains, 1);
    TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine)); GI->InitializeStandalone();
    UWorld* World = GI->GetWorld();
    ON_SCOPE_EXIT { GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Social = GI->GetSubsystem<UProject_JSocialSubsystem>();
    auto* Player = World->SpawnActor<AProject_JPlayerState>();
    FProject_JAccountId Account; Account.Value = FGuid::NewGuid();
    FProject_JCharacterId Character; Character.Value = FGuid::NewGuid();
    Player->SetIdentity(Account, Character);
    if (!TestTrue(TEXT("Original identity has an actual party"), Social->CreateParty(Player).bSucceeded) ||
        !TestTrue(TEXT("Original identity has an actual guild"), Social->CreateGuild(Player).bSucceeded)) return false;
    const FName Party = Player->GetPartyId_Implementation(), Guild = Player->GetGuildId_Implementation();
    bool bSawDetachedProjection = false;
    const auto Listener = Player->OnCharacterIdentityChanged.AddLambda([&]
    {
        if (!Player->GetCharacterId().IsValid())
            bSawDetachedProjection = Player->GetPartyId_Implementation().IsNone() && Player->GetGuildId_Implementation().IsNone()
                && !Player->GetPartyLeaderCharacterId().IsValid() && !Player->GetGuildLeaderCharacterId().IsValid();
    });
    ON_SCOPE_EXIT { Player->OnCharacterIdentityChanged.Remove(Listener); };
    Player->SetIdentity(Account, FProject_JCharacterId());
    TestTrue(TEXT("Identity listeners observe the detached identity and projection together"), bSawDetachedProjection);
    TestTrue(TEXT("A detached identity has no party or guild projection"), Player->GetPartyId_Implementation().IsNone() && Player->GetGuildId_Implementation().IsNone());
    TestEqual(TEXT("Identity detach preserves actual party membership"), Social->GetPartyIdForCharacter(Character.Value), Party);
    TestEqual(TEXT("Identity detach preserves actual guild membership"), Social->GetGuildIdForCharacter(Character.Value), Guild);
    auto* Reattached = World->SpawnActor<AProject_JPlayerState>();
    Reattached->SetIdentity(FProject_JAccountId(), Character);
    TestEqual(TEXT("Character membership restores independently of account representation"), Reattached->GetPartyId_Implementation(), Party);
    TestEqual(TEXT("Guild membership restores to the new state"), Reattached->GetGuildId_Implementation(), Guild);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSkillButtonOriginTest, "ProjectJ.PCStructure.SkillButtonOrigin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJSkillButtonOriginTest::RunTest(const FString&)
{
    for (int32 Mode : {0, 1, 2, 3, 4}) // keyboard collision, stale widget, re-press, native binding, activation retirement
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
        auto* State = World->SpawnActor<AProject_JPlayerState>();
        auto* Controller = World->SpawnActor<APlayerController>();
        auto* Player = World->SpawnActor<AProject_JGreatswordCharacter>();
        if (!State || !Controller || !Player) { AddError(TEXT("Input fixture spawn failed")); return false; }
        State->SetOwner(Controller); Controller->SetPlayerState(State); Player->SetPlayerState(State);
        Controller->SetAsLocalPlayerController(); Controller->Possess(Player);
        auto* ASC = State->GetProjectJAbilitySystemComponent(); ASC->InitAbilityActorInfo(State, Player);
        Player->FindComponentByClass<UProject_JSkillInputExecutionComponent>()->Initialize(Player);
        Player->FindComponentByClass<UProject_JCombatStateComponent>()->BindToAbilitySystem(ASC);
        if (!TestTrue(TEXT("Current avatar uses the persistent PlayerState ASC"), Player->GetAbilitySystemComponent() == ASC && ASC->GetAvatarActor() == Player)) return false;
        auto* UI = NewObject<UProject_JPlayerUIComponent>(Controller); Controller->AddInstanceComponent(UI);
        UI->RegisterComponent(); UI->RegisterAllComponentTickFunctions(true); UI->UActorComponent::BeginPlay();
        const FGameplayTag Tags[] = {FProject_JGameplayTags::Get().InputTag_Weapon_LMB, FProject_JGameplayTags::Get().InputTag_Skill_Q};
        TArray<FGameplayAbilitySpecHandle> Handles;
        for (FGameplayTag Tag : Tags)
        {
            FGameplayAbilitySpec Spec(UProject_JGameplayAbility_Sprint::StaticClass(), 1);
            Spec.GetDynamicSpecSourceTags().AddTag(Tag); Handles.Add(ASC->GiveAbility(Spec));
            FProject_JUISkillSlot Slot; Slot.InputTag = Tag; UI->PresentedSkills.Add(Slot);
        }
        UI->HUDPreferences.Slots.SetNum(1); UI->HUDPreferences.Slots[0].Kind = EProject_JQuickSlotKind::Skill;
        UI->HUDPreferences.Slots[0].InputTag = Tags[0];
        TStrongObjectPtr<UProject_JSkillButton> Button(NewObject<UProject_JSkillButton>(Controller));
        Button->Initialize(); Button->Configure(UI, 0);
        auto SlateButton = Button->TakeWidget(); // A real pointer event reaches an already constructed widget.
        Button->bAvailable = true;
        const FPointerEvent Mouse(0, FVector2D::ZeroVector, FVector2D::ZeroVector, TSet<FKey>{EKeys::LeftMouseButton}, EKeys::LeftMouseButton, 0, FModifierKeysState());
        bool bReentered = false;
        const auto EndCallback = ASC->AbilityEndedCallbacks.AddLambda([&](UGameplayAbility*)
        {
            if ((Mode != 1 && Mode != 2) || bReentered) return;
            bReentered = true; UI->HUDPreferences.Slots[0].InputTag = Tags[1];
            if (Mode == 2) Button->NativeOnMouseButtonDown(FGeometry(), Mouse); else UI->PressSkill(0);
        });
        ON_SCOPE_EXIT { ASC->AbilityEndedCallbacks.Remove(EndCallback); UI->ReleaseAllSkills(); ASC->CancelAllAbilities(); UI->EndPlay(EEndPlayReason::Destroyed); };
        if (Mode == 3)
        {
            auto* Input = NewObject<UInputComponent>(Controller);
            Controller->AddInstanceComponent(Input);
            UI->InputKeys.Keys = {EKeys::F1}; UI->BindMenuInput(Input);
            const auto GetKey = [&](EInputEvent Event)
            {
                return Input->KeyBindings.FindByPredicate([&](const FInputKeyBinding& Key) { return Key.KeyEvent == Event && Key.Chord.Key == EKeys::F1; });
            };
            if (!TestNotNull(TEXT("Actual quickslot pressed key binding"), GetKey(IE_Pressed)) || !TestNotNull(TEXT("Actual quickslot released key binding"), GetKey(IE_Released))) return false;
            auto OldDown = *GetKey(IE_Pressed), OldUp = *GetKey(IE_Released);
            OldDown.KeyDelegate.Execute(EKeys::F1);
            TestTrue(TEXT("Actual native key delegate acquires input"), ASC->FindAbilitySpecFromHandle(Handles[0])->InputPressed);
            UI->RebuildKeyBindings();
            TestFalse(TEXT("Rebuild returns the retired binding's input"), ASC->FindAbilitySpecFromHandle(Handles[0])->InputPressed);
            OldDown.KeyDelegate.Execute(EKeys::F1);
            TestFalse(TEXT("Copied retired press delegate cannot acquire a new input"), ASC->FindAbilitySpecFromHandle(Handles[0])->InputPressed);
            Button->NativeOnMouseButtonDown(FGeometry(), Mouse);
            if (!TestNotNull(TEXT("Rebuilt current pressed key remains installed"), GetKey(IE_Pressed)) || !TestNotNull(TEXT("Rebuilt current released key remains installed"), GetKey(IE_Released))) return false;
            auto Down = *GetKey(IE_Pressed), Up = *GetKey(IE_Released);
            Down.KeyDelegate.Execute(EKeys::F1); Up.KeyDelegate.Execute(EKeys::F1);
            TestTrue(TEXT("A native key rejected by a mouse press cannot release that mouse input"), ASC->FindAbilitySpecFromHandle(Handles[0])->InputPressed);
            Button->NativeOnMouseButtonUp(FGeometry(), Mouse);
            Down.KeyDelegate.Execute(EKeys::F1); OldUp.KeyDelegate.Execute(EKeys::F1);
            TestTrue(TEXT("Copied old release cannot consume a replacement key binding's input"), ASC->FindAbilitySpecFromHandle(Handles[0])->InputPressed);
            Up.KeyDelegate.Execute(EKeys::F1);
            TestFalse(TEXT("Current native key releases its acquired input"), ASC->FindAbilitySpecFromHandle(Handles[0])->InputPressed);
            continue;
        }
        if (Mode == 4)
        {
            bool bActivated = false;
            auto Activate = ASC->AbilityActivatedCallbacks.AddLambda([&](UGameplayAbility*) { bActivated = true; UI->ReleaseAllSkills(); });
            ON_SCOPE_EXIT { ASC->AbilityActivatedCallbacks.Remove(Activate); };
            const FReply Reply = Button->NativeOnMouseButtonDown(FGeometry(), Mouse);
            TestTrue(TEXT("Actual activation callback retired the press"), bActivated);
            TestTrue(TEXT("Activation callback left no UI receipt"), UI->PressedSkills.IsEmpty());
            TestFalse(TEXT("A retired activation cannot request phantom mouse capture"), Reply.GetMouseCaptor().IsValid());
            continue;
        }
        if (Mode == 0) UI->PressSkill(0);
        Button->NativeOnMouseButtonDown(FGeometry(), Mouse);
        if (!TestTrue(FString::Printf(TEXT("Mode %d: original input is actually active"), Mode), ASC->FindAbilitySpecFromHandle(Handles[0])->IsActive())) return false;
        if (Mode == 0)
        {
            Button->NativeOnMouseButtonUp(FGeometry(), Mouse);
            TestTrue(TEXT("A mouse press rejected by an existing keyboard press cannot release it"), ASC->FindAbilitySpecFromHandle(Handles[0])->InputPressed);
        }
        else
        {
            if (Mode == 1) { UI->ReleaseAllSkills(); Button->NativeDestruct(); }
            else Button->NativeOnMouseButtonUp(FGeometry(), Mouse);
            TestTrue(TEXT("Real GAS completion re-pressed the slot"), bReentered);
            TestTrue(TEXT("Old widget cleanup preserves a newer same-slot receipt"), ASC->FindAbilitySpecFromHandle(Handles[1])->InputPressed);
            if (Mode == 2)
            {
                Button->NativeOnMouseButtonUp(FGeometry(), Mouse);
                TestFalse(TEXT("The later mouse-up releases its own new press"), ASC->FindAbilitySpecFromHandle(Handles[1])->InputPressed);
            }
        }
    }
    return true;
}
#endif
