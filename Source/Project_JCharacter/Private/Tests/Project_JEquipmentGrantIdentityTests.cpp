#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Tests/Project_JPlayerMaturityFixtures.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "AbilitySystem/Project_JAbilitySet.h"
#include "Combat/Project_JGameplayAbility_Sprint.h"
#include "Project_JAbilitySystemComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJEquipmentGrantIdentityTest,
    "ProjectJ.Continuation.EquipmentGrantIdentity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJEquipmentGrantIdentityTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Previous = World->SpawnActor<AProject_JPlayerMaturityASCFixture>();
    auto* Current = World->SpawnActor<AProject_JPlayerMaturityASCFixture>();
    auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Previous);
    ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Previous, Previous);
    Previous->CurrentASC = Current->CurrentASC = ASC;
    auto* PreviousManager = NewObject<UProject_JEquipmentManagerComponent>(Previous); PreviousManager->RegisterComponent();
    auto* CurrentManager = NewObject<UProject_JEquipmentManagerComponent>(Current); CurrentManager->RegisterComponent();
    auto* PreviousRuntime = NewObject<UProject_JEquipmentRuntimeComponent>(Previous); PreviousRuntime->RegisterComponent();
    auto* CurrentRuntime = NewObject<UProject_JEquipmentRuntimeComponent>(Current); CurrentRuntime->RegisterComponent();
    PreviousRuntime->BindToEquipmentManager(PreviousManager); CurrentRuntime->BindToEquipmentManager(CurrentManager);
    auto* PreviousDef = NewObject<UProject_JEquipmentItemDefinition>(Previous, TEXT("SameEquipmentName"));
    auto* CurrentDef = NewObject<UProject_JEquipmentItemDefinition>(Current, TEXT("SameEquipmentName"));
    PreviousDef->EquipmentSlot = CurrentDef->EquipmentSlot = EProject_JEquipmentSlot::Head;
    PreviousDef->ItemId = TEXT("PreviousItem"); CurrentDef->ItemId = TEXT("CurrentItem");
    PreviousDef->AbilitySet = NewObject<UProject_JAbilitySet>(PreviousDef);
    CurrentDef->AbilitySet = NewObject<UProject_JAbilitySet>(CurrentDef);
    PreviousDef->AbilitySet->GrantedAbilityEntries.AddDefaulted_GetRef().Ability = UProject_JPlayerMaturityMeleeFixture::StaticClass();
    CurrentDef->AbilitySet->GrantedAbilityEntries.AddDefaulted_GetRef().Ability = UProject_JGameplayAbility_Sprint::StaticClass();
    TestEqual(TEXT("Definitions share a basename"), PreviousDef->GetFName(), CurrentDef->GetFName());
    TestNotEqual(TEXT("Definitions have distinct source identities"), PreviousDef->GetPathName(), CurrentDef->GetPathName());
    PreviousManager->EquipItem(PreviousDef); CurrentManager->EquipItem(CurrentDef);
    TestEqual(TEXT("Overlapping avatar providers retain both distinct equipment grants"), ASC->GetActivatableAbilities().Num(), 2);
    PreviousRuntime->BindToEquipmentManager(nullptr);
    TestEqual(TEXT("Old provider teardown leaves one current grant"), ASC->GetActivatableAbilities().Num(), 1);
    TestNotNull(TEXT("Current definition's ability survives the previous provider release"), ASC->FindAbilitySpecFromClass(UProject_JGameplayAbility_Sprint::StaticClass()));
    CurrentRuntime->BindToEquipmentManager(nullptr);
    TestEqual(TEXT("Both providers can release their own grant"), ASC->GetActivatableAbilities().Num(), 0);
    CurrentManager->UnequipItem(CurrentDef); CurrentManager->EquipItem(PreviousDef);
    PreviousRuntime->BindToEquipmentManager(PreviousManager); CurrentRuntime->BindToEquipmentManager(CurrentManager);
    TestEqual(TEXT("The same definition across avatar providers still shares one grant"), ASC->GetActivatableAbilities().Num(), 1);
    PreviousRuntime->BindToEquipmentManager(nullptr);
    TestEqual(TEXT("One release cannot remove another provider of the same definition"), ASC->GetActivatableAbilities().Num(), 1);
    CurrentRuntime->BindToEquipmentManager(nullptr);
    TestEqual(TEXT("The final shared provider returns its grant"), ASC->GetActivatableAbilities().Num(), 0);
    return true;
}
#endif
