#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Tests/Project_JPlayerMaturityFixtures.h"
#include "Project_JAttributeSet.h"
#include "Project_JAbilitySystemComponent.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Inventory/Project_JConsumableDefinition.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "AbilitySystem/Project_JAbilitySet.h"
#include "Combat/Project_JGameplayAbility_Sprint.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJInventoryRetirementTest, "ProjectJ.RuntimeOwnership.AggregateRetirement.Inventory",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJInventoryRetirementTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Owner = World->SpawnActor<AProject_JPlayerMaturityASCFixture>();
    auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Owner); Owner->CurrentASC = ASC;
    ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Owner, Owner);
    ASC->AddAttributeSetSubobject(NewObject<UProject_JAttributeSet>(Owner));
    ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(), 100);
    ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 50);
    ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxManaAttribute(), 100);
    ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetManaAttribute(), 50);
    auto* Inventory = NewObject<UProject_JInventoryComponent>(Owner); Inventory->RegisterComponent();
    auto* Potion = NewObject<UProject_JConsumableDefinition>(); Potion->MaxStackCount = 20;
    Potion->RestoreHealth = 10; Potion->RestoreMana = 0;
    const auto Item = Inventory->AddItemDefinition(Potion, 4);
    if (!TestTrue(TEXT("Live inventory accepts the native consumable"), Item.InstanceId.IsValid())) { return false; }
    Inventory->DestroyComponent();
    TestEqual(TEXT("Destroyed inventory rejects late consumable use"), Inventory->TryUseItem(Item.InstanceId), EProject_JItemUseResult::Unavailable);
    TestEqual(TEXT("Retired component cannot heal its still-live owner's ASC"), ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()), 50.f);
    TestFalse(TEXT("Retired inventory cannot change stack counts"), Inventory->SetItemStackCount(Item.InstanceId, 2));
    TestFalse(TEXT("Retired inventory cannot consume remaining items"), Inventory->ConsumeItemStack(Item.InstanceId));
    TestFalse(TEXT("Retired inventory cannot add another item"), Inventory->AddItemDefinition(Potion).InstanceId.IsValid());
    TestFalse(TEXT("Retired inventory cannot remove the preserved item"), Inventory->RemoveItemInstance(Item.InstanceId));
    FProject_JItemInstanceData Preserved;
    TestTrue(TEXT("Read-only state remains available for persistence"), Inventory->FindItemInstance(Item.InstanceId, Preserved));
    TestEqual(TEXT("Retirement preserves the original stack"), Preserved.StackCount, 4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJEquipmentAggregateRetirementTest, "ProjectJ.RuntimeOwnership.AggregateRetirement.Equipment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJEquipmentAggregateRetirementTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Owner = World->SpawnActor<AActor>();
    auto* Inventory = NewObject<UProject_JInventoryComponent>(Owner); Inventory->RegisterComponent();
    auto* Manager = NewObject<UProject_JEquipmentManagerComponent>(Owner); Manager->RegisterComponent();
    auto* Original = NewObject<UProject_JEquipmentItemDefinition>();
    auto* Replacement = NewObject<UProject_JEquipmentItemDefinition>();
    Manager->EquipItem(Original);
    const auto Item = Inventory->AddItemDefinition(Replacement);
    if (!TestTrue(TEXT("Live equipment aggregate commits its native weapon"), Manager->GetEquippedItemInSlot(EProject_JEquipmentSlot::Weapon) == Original)) { return false; }
    Manager->DestroyComponent();
    Manager->UnequipSlot(EProject_JEquipmentSlot::Weapon);
    TestTrue(TEXT("Retired aggregate cannot unequip its persisted state"), Manager->GetEquippedItemInSlot(EProject_JEquipmentSlot::Weapon) == Original);
    Manager->EquipItem(Replacement);
    TestTrue(TEXT("Retired aggregate cannot replace its persisted state"), Manager->GetEquippedItemInSlot(EProject_JEquipmentSlot::Weapon) == Original);
    TestFalse(TEXT("Retired aggregate rejects the owned inventory request"), Manager->TryEquipItemInstanceById(Item.InstanceId).bSucceeded);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJRemoteJumpFreshnessTest, "ProjectJ.Animation.StateOwnership.RemoteJumpFreshness",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJRemoteJumpFreshnessTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Player = World->SpawnActor<AProject_JGreatswordCharacter>();
    auto* State = Player->FindComponentByClass<UProject_JLocomotionAnimStateComponent>();
    State->HandleConfirmedRemoteJump(5, 0, FVector(0, 0, 400));
    if (!TestEqual(TEXT("Fresh confirmation carries actual upward speed"), State->LastFallSpeed, 400.f)) { return false; }
    State->bIsLanding = true;
    State->HandleConfirmedRemoteJump(4, 0, FVector(0, 0, 800));
    TestEqual(TEXT("Older confirmation cannot replace current airborne evidence"), State->LastFallSpeed, 400.f);
    TestTrue(TEXT("Older confirmation cannot restart a jump over newer landing"), State->bIsLanding);
    State->HandleConfirmedRemoteJump(6, 0, FVector(0, 0, 500));
    TestEqual(TEXT("A genuinely newer confirmation remains admissible"), State->LastFallSpeed, 500.f);
    TestFalse(TEXT("New accepted jump still replaces old landing"), State->bIsLanding);
    TestFalse(TEXT("Duplicate confirmation is not accepted again"), State->HandleConfirmedRemoteJump(6, 0, FVector(0, 0, 900)));
    State->DestroyComponent();
    TestFalse(TEXT("Retired locomotion owner cannot accept a late confirmation"), State->HandleConfirmedRemoteJump(7, 0, FVector(0, 0, 900)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJEquipmentProviderRetirementTest, "ProjectJ.RuntimeOwnership.AggregateRetirement.EquipmentProvider",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJEquipmentProviderRetirementTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Character = World->SpawnActor<AProject_JPlayerMaturityASCFixture>();
    auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Character); Character->CurrentASC = ASC;
    ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Character, Character);
    const auto OtherProvider = ASC->GiveAbility(FGameplayAbilitySpec(UProject_JGameplayAbility_Sprint::StaticClass(), 1));
    auto* Set = NewObject<UProject_JAbilitySet>();
    auto& Ability = Set->GrantedAbilityEntries.AddDefaulted_GetRef(); Ability.Ability = UProject_JGameplayAbility_Sprint::StaticClass();
    auto* Item = NewObject<UProject_JEquipmentItemDefinition>(); Item->AbilitySet = Set;
    Item->StatApplicationPolicy = EProject_JEquipmentStatApplicationPolicy::GameplayEffectsOnly;
    auto* Manager = NewObject<UProject_JEquipmentManagerComponent>(Character); Manager->RegisterComponent(); Manager->EquipItem(Item);
    auto* Runtime = NewObject<UProject_JEquipmentRuntimeComponent>(Character); Runtime->RegisterComponent(); Runtime->BindToEquipmentManager(Manager);
    TestEqual(TEXT("Equipment runtime and independent provider each own a spec"), ASC->GetActivatableAbilities().Num(), 2);
    Manager->DestroyComponent();
    TestEqual(TEXT("Provider retirement releases its consumer's actual grant"), ASC->GetActivatableAbilities().Num(), 1);
    TestNotNull(TEXT("Provider retirement preserves an unrelated spec"), ASC->FindAbilitySpecFromHandle(OtherProvider));
    Runtime->BindToEquipmentManager(Manager);
    TestEqual(TEXT("Retired provider cannot reacquire a runtime grant"), ASC->GetActivatableAbilities().Num(), 1);
    ASC->ClearAbility(OtherProvider);
    return true;
}
#endif
