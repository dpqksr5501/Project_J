#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "UI/Project_JMinimapWidget.h"
#include "UObject/UnrealType.h"
#include "Animation/Project_JAnimationProfileValidation.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Project_JGreatswordCharacter.h"
#include "Misc/DataValidation.h"
#include "UI/Project_JInventoryViewModel.h"
#include "UI/Project_JUILayoutSettings.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/StrongObjectPtr.h"
#include "Equipment/Project_JWeaponPresentationProfile.h"
#include "Equipment/Project_JWeaponPresentationActor.h"
#include "Game/Project_JQuestComponent.h"
#include "UI/Project_JPlayerUIComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/WorldSettings.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMapProjectionFailureTest,
    "ProjectJ.PCStructure.MapProjectionFailure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMapProjectionFailureTest::RunTest(const FString&)
{
    FVector2D UV(7, 9);
    TestTrue(TEXT("Normal map bounds project"), UProject_JMapDefinition::WorldToUV(FVector(5000, 5000, 0), FVector2D::ZeroVector, FVector2D(5000), UV));
    TestTrue(TEXT("North/east axes are preserved"), UV.Equals(FVector2D(1, 0)));
    const FVector2D Sentinel(7, 9);
    UV = Sentinel;
    TestFalse(TEXT("Finite inputs with overflowing difference cannot publish invalid UV"),
        UProject_JMapDefinition::WorldToUV(FVector(0, 1.7e308, 0), FVector2D(0, -1.7e308), FVector2D(5000), UV));
    TestTrue(TEXT("Projection failure preserves the caller output"), UV.Equals(Sentinel));
    TestFalse(TEXT("Invalid authored marker position fails admission"),
        UProject_JMapDefinition::WorldToUV(FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0), FVector2D::ZeroVector, FVector2D(5000), UV));
    TestTrue(TEXT("Rejected marker also preserves the output"), UV.Equals(Sentinel));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMinimapReconstructionTest,
    "ProjectJ.Reaudit.MinimapReconstruction", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMinimapReconstructionTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT {World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    auto* Owner=World->SpawnActor<APlayerController>();
    auto* Widget=NewObject<UProject_JMinimapWidget>(Owner);Widget->Initialize();
    auto* Definition=NewObject<UProject_JMapDefinition>(Widget);
    Definition->LevelName=TEXT("DifferentFromThisFixtureWorld");Widget->Definition=Definition;
    auto* TextureField=FindFProperty<FObjectPropertyBase>(Widget->GetClass(),TEXT("MapTexture"));
    if(!TestNotNull(TEXT("Native map texture cache is a reflected UObject field"),TextureField)){return false;}
    auto* PreviousTexture=NewObject<UTexture2D>(Widget);
    TextureField->SetObjectPropertyValue_InContainer(Widget,PreviousTexture);
    // TakeWidget executes the actual native construction path, without a test-only setter.
    auto Slate=Widget->TakeWidget();
    TestNull(TEXT("A definition for another world is rejected"),Widget->Definition.Get());
    TestNull(TEXT("Rejected map reconstruction cannot retain the previous world's texture"),TextureField->GetObjectPropertyValue_InContainer(Widget));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMovementDataValidationTest,
    "ProjectJ.Reaudit.MovementDataValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMovementDataValidationTest::RunTest(const FString&)
{
    auto* Profile=NewObject<UProject_JLocomotionProfile>();
    Profile->WalkSpeed=std::numeric_limits<float>::quiet_NaN();
    FDataValidationContext Context;
    TestEqual(TEXT("The authored data validator rejects a nonfinite movement speed"),Profile->IsDataValid(Context),EDataValidationResult::Invalid);
    TArray<FString> Warnings;
    Project_J::AnimationProfileValidation::ValidatePlayerAnimationConfiguration(
        *GetDefault<AProject_JGreatswordCharacter>(),Profile,nullptr,nullptr,nullptr,Warnings);
    TestTrue(TEXT("Runtime configuration diagnostics identify the invalid WalkSpeed"),Warnings.ContainsByPredicate([](const FString& W){return W.Contains(TEXT("WalkSpeed"));}));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJInventoryProjectionGenerationTest,
    "ProjectJ.Reaudit.InventoryProjectionGeneration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJInventoryProjectionGenerationTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT {World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    auto* Owner=World->SpawnActor<AActor>();
    auto* Inventory=NewObject<UProject_JInventoryComponent>(Owner);Inventory->RegisterComponent();
    auto* Equipment=NewObject<UProject_JEquipmentManagerComponent>(Owner);Equipment->RegisterComponent();
    auto* Candidate=NewObject<UProject_JEquipmentItemDefinition>(Owner);Candidate->ItemId=TEXT("ReauditCandidate");Candidate->EquipmentSlot=EProject_JEquipmentSlot::Head;
    auto* Replacement=NewObject<UProject_JEquipmentItemDefinition>(Owner);Replacement->ItemId=TEXT("ReauditReplacement");Replacement->EquipmentSlot=EProject_JEquipmentSlot::Head;
    Replacement->ItemName=FText::FromString(TEXT("ReauditReplacementSentinel"));
    const auto Item=Inventory->AddItemDefinition(Candidate);const auto Other=Inventory->AddItemDefinition(Replacement);
    auto* Model=NewObject<UProject_JInventoryViewModel>();Model->Bind(Inventory,Equipment);
    const auto* Row=Model->InventoryEntries.FindByPredicate([&](const auto& Entry){return Entry->Item.InstanceId==Item.InstanceId;});
    if(!TestNotNull(TEXT("Candidate row is projected"),Row)){return false;}
    TStrongObjectPtr<UProject_JInventoryEntry> Frozen(DuplicateObject<UProject_JInventoryEntry>(Row->Get(),Model));
    TestTrue(TEXT("Current row can compare fixed equipment"),Model->BuildEquipmentComparison(Frozen.Get()).bCanCompare);
    Model->Unbind();Model->Bind(Inventory,Equipment);
    TestTrue(TEXT("Replacement request uses live source"),Model->Equip(Other.InstanceId,EProject_JEquipmentSlot::Head));
    TestFalse(TEXT("Old generation cannot compare against rebound equipment"),Model->BuildEquipmentComparison(Frozen.Get()).bCanCompare);
    TestFalse(TEXT("Old tooltip cannot expose rebound equipment metadata"),Model->BuildTooltip(Frozen.Get()).ToString().Contains(TEXT("ReauditReplacementSentinel")));
    Model->Unbind();return true;
}

namespace
{
class FReauditPreferenceStorage final : public FProject_JUILayoutStorage
{
public:
    TMap<FString,TArray<uint8>> Files;
    int32 Writes=0;
    bool Exists(const FString& Slot) override {return Files.Contains(Slot);}
    UProject_JUILayoutSave* Load(const FString& Slot) override
    {
        const auto* Bytes=Files.Find(Slot);
        return Bytes&&!Bytes->IsEmpty()?Cast<UProject_JUILayoutSave>(UGameplayStatics::LoadGameFromMemory(*Bytes)):nullptr;
    }
    bool Store(UProject_JUILayoutSave* Value,const FString& Slot) override
    {
        ++Writes;TArray<uint8> Bytes;
        if(!UGameplayStatics::SaveGameToMemory(Value,Bytes)){return false;}
        Files.Add(Slot,MoveTemp(Bytes));return true;
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJUIPreferenceRetirementTest,
    "ProjectJ.Reaudit.UIPreferenceRetirement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJUIPreferenceRetirementTest::RunTest(const FString&)
{
    auto Disk=MakeShared<FReauditPreferenceStorage>();
    auto* Player=NewObject<ULocalPlayer>(GEngine);Player->SetControllerId(0);
    TStrongObjectPtr<UProject_JUILayoutSettings> Settings(NewObject<UProject_JUILayoutSettings>(Player));Settings->SetStorageForTesting(Disk);
    bool bRetiredInCallback=false;
    Settings->OnSaveStateChanged.AddLambda([&](){
        if(!bRetiredInCallback&&Settings->GetSaveState()==EProject_JUIPreferenceState::Pending){bRetiredInCallback=true;Settings->Deinitialize();}
    });
    Settings->Write(TEXT("BeforeRetirement"),FVector2D(.1,.2));
    TestTrue(TEXT("Native save-state callback retired the subsystem"),bRetiredInCallback);
    // Replace the retired storage seam so even the broken implementation cannot touch a user's slot.
    Settings->SetStorageForTesting(Disk);
    const int32 WritesAtRetirement=Disk->Writes;
    Settings->Write(TEXT("LateWrite"),FVector2D(.7,.8));
    FVector2D Position;
    TestFalse(TEXT("Retired subsystem rejects reopening a late preference"),Settings->Read(TEXT("LateWrite"),Position));
    Settings->FlushNow();
    TestEqual(TEXT("Late operations cannot restart persistence"),Disk->Writes,WritesAtRetirement);
    Settings->Deinitialize();
    const FString Slot=TEXT("ProjectJ_UI_Player_0");
    const TArray<uint8> BackupBytes=Disk->Files.FindChecked(Slot);
    Disk->Files.Add(Slot+TEXT("_Backup"),BackupBytes);
    Disk->Files.Add(Slot,{});
    auto* Recovery=NewObject<UProject_JUILayoutSettings>(Player);Recovery->SetStorageForTesting(Disk);
    bool bRetiredRecovery=false;
    Recovery->OnSaveStateChanged.AddLambda([&](){
        if(!bRetiredRecovery&&Recovery->GetSaveState()==EProject_JUIPreferenceState::Recovered){
            bRetiredRecovery=true;Recovery->Deinitialize();CollectGarbage(RF_NoFlags,true);
        }
    });
    TestFalse(TEXT("Retirement during backup recovery stops the old read safely"),Recovery->Read(TEXT("BeforeRetirement"),Position));
    TestTrue(TEXT("Actual recovery state notification entered retirement and GC"),bRetiredRecovery);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGroundContactDataValidationTest,
    "ProjectJ.Reaudit.GroundContactDataValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJGroundContactDataValidationTest::RunTest(const FString&)
{
    auto* Profile=NewObject<UProject_JWeaponPresentationProfile>();Profile->WeaponActorClass=AProject_JWeaponPresentationActor::StaticClass();
    Profile->MotionPresentation.bSupportsIndependentMotion=true;
    Profile->MotionPresentation.GroundContact.bEnableGroundContact=true;
    FDataValidationContext ValidContext;
    TestEqual(TEXT("Normal ground contact defaults remain valid"),Profile->IsDataValid(ValidContext),EDataValidationResult::Valid);
    Profile->MotionPresentation.GroundContact.TraceLength=std::numeric_limits<float>::quiet_NaN();
    FDataValidationContext Context;
    TestEqual(TEXT("Authored ground contact rejects nonfinite trace length"),Profile->IsDataValid(Context),EDataValidationResult::Invalid);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJQuestPlayReuseTest,
    "ProjectJ.Reaudit.QuestPlayReuse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJQuestPlayReuseTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT {World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    auto* Owner=World->SpawnActor<AActor>();
    auto* Quest=NewObject<UProject_JQuestComponent>(Owner);Owner->AddInstanceComponent(Quest);
    for(const FName Id:{FName(TEXT("Reaudit.First")),FName(TEXT("Reaudit.Second"))})
    {
        auto* Definition=NewObject<UProject_JQuestDefinition>(Quest);Definition->QuestId=Id;Definition->ObjectiveEvent=TEXT("Reaudit.Objective");
        Quest->Definitions.Add(Definition);
    }
    Quest->RegisterComponent();
    World->InitializeActorsForPlay(FURL());World->GetWorldSettings()->NotifyBeginPlay();World->GetWorldSettings()->NotifyMatchStarted();
    if(!TestTrue(TEXT("First play accepts the first valid quest"),Quest->Accept(TEXT("Reaudit.First")))){return false;}
    Owner->RouteEndPlay(EEndPlayReason::RemovedFromWorld);Owner->DispatchBeginPlay(true);
    TestTrue(TEXT("New play accepts a different valid quest"),Quest->Accept(TEXT("Reaudit.Second")));
    TestEqual(TEXT("Reentry preserves old progress and appends the new quest"),Quest->States.Num(),2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJControllerUIPlayReuseTest,
    "ProjectJ.Reaudit.ControllerUIPlayReuse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJControllerUIPlayReuseTest::RunTest(const FString&)
{
    AddExpectedError(TEXT("does not have a valid PlayerInput object. Failed to load user settings."),EAutomationExpectedErrorFlags::Contains,1);
    auto* GameInstance=NewObject<UGameInstance>(GEngine);GameInstance->InitializeStandalone();
    auto* World=GameInstance->GetWorld();
    auto* Context=GEngine->GetWorldContextFromWorld(World);
    auto* Viewport=NewObject<UGameViewportClient>(GEngine);Context->GameViewport=Viewport;
    Viewport->Init(*Context,GameInstance,false);
    ON_SCOPE_EXIT {World->EndPlay(EEndPlayReason::Quit);GameInstance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    FString PlayerError;
    auto* LocalPlayer=GameInstance->CreateLocalPlayer(0,PlayerError,false);
    if(!TestNotNull(TEXT("Engine creates an initialized LocalPlayer with its correct outer"),LocalPlayer)){return false;}
    if(!TestNotNull(TEXT("Actual LocalPlayer supplies its required layout subsystem"),LocalPlayer->GetSubsystem<UProject_JUILayoutSettings>())){return false;}
    auto* Controller=World->SpawnActor<APlayerController>();Controller->SetAsLocalPlayerController();Controller->SetPlayer(LocalPlayer);
    auto* UI=NewObject<UProject_JPlayerUIComponent>(Controller);Controller->AddInstanceComponent(UI);UI->RegisterComponent();
    World->InitializeActorsForPlay(FURL());World->GetWorldSettings()->NotifyBeginPlay();World->GetWorldSettings()->NotifyMatchStarted();
    UI->RefreshSources();
    auto* FirstModel=UI->GetInventoryModel();
    if(!TestNotNull(TEXT("A valid actual local player creates the first UI projection"),FirstModel)){return false;}
    TStrongObjectPtr<UProject_JInventoryViewModel> PinnedOldModel(FirstModel);
    Controller->RouteEndPlay(EEndPlayReason::RemovedFromWorld);
    TestNull(TEXT("EndPlay retires the old UI projection"),UI->GetInventoryModel());
    Controller->SetPlayer(LocalPlayer);Controller->DispatchBeginPlay(true);UI->RefreshSources();
    auto* SecondModel=UI->GetInventoryModel();
    TestNotNull(TEXT("New play creates a live UI projection again"),SecondModel);
    TestTrue(TEXT("New play owns a fresh model instead of reviving the retired model"),SecondModel&&SecondModel!=FirstModel);
    return true;
}
#endif
