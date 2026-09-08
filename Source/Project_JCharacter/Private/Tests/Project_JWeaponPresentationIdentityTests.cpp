#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Equipment/Project_JWeaponPresentationProfile.h"
#include "Project_JGreatswordCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJWeaponPresentationIdentityTest, "ProjectJ.Combat.WeaponPresentationIdentity",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJWeaponPresentationIdentityTest::RunTest(const FString&)
{
 UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 auto* Pawn = World->SpawnActor<AProject_JGreatswordCharacter>();
 // Player equipment normally lives on PlayerState. This isolated fixture has no
 // controller/PlayerState; supply an explicit authority-side manager for it.
 auto* Manager = NewObject<UProject_JEquipmentManagerComponent>(Pawn);
 Pawn->AddInstanceComponent(Manager); Manager->RegisterComponent();
 auto* Runtime = Pawn->FindComponentByClass<UProject_JEquipmentRuntimeComponent>();
 auto* Presentation = Pawn->FindComponentByClass<UProject_JWeaponPresentationComponent>();
 Runtime->BindToEquipmentManager(Manager);
 auto* Style = NewObject<UProject_JCombatStyleDefinition>(World);
 auto* Profile = NewObject<UProject_JWeaponPresentationProfile>(World);
 Profile->WeaponActorClass = AStaticMeshActor::StaticClass();
 Profile->DrawnSocketName = NAME_None; Profile->SheathedSocketName = NAME_None;
 auto* Item = NewObject<UProject_JEquipmentItemDefinition>(World);
 Item->CombatStyleDefinition = Style; Item->WeaponPresentationProfile = Profile;
 int32 Spawned = 0;
 const auto SpawnHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([&](AActor*) { ++Spawned; }));

 Manager->EquipItem(Item);
 auto* First = Presentation->GetSpawnedWeapon();
 TestNotNull(TEXT("Equipment creates a weapon"), First);
 TestEqual(TEXT("Exactly one actor created for equip"), Spawned, 1);
 Presentation->bIndependentMotionActive = true; // observe preservation of notify-owned state
 Presentation->RefreshPresentation();
 Pawn->SetCurrentEquipmentConfiguration(Style, Profile);
 TestTrue(TEXT("Duplicate refresh keeps actor identity"), Presentation->GetSpawnedWeapon() == First);
 TestTrue(TEXT("Duplicate refresh keeps independent motion"), Presentation->IsIndependentMotionActive());

 // Invoke replication reactions in either order, without claiming this is a net-driver test.
 Pawn->ProcessEvent(Pawn->FindFunctionChecked(TEXT("OnRep_CurrentCombatStyle")), nullptr);
 Pawn->ProcessEvent(Pawn->FindFunctionChecked(TEXT("OnRep_CurrentWeaponPresentationProfile")), nullptr);
 Pawn->ProcessEvent(Pawn->FindFunctionChecked(TEXT("OnRep_CurrentWeaponPresentationProfile")), nullptr);
 Pawn->ProcessEvent(Pawn->FindFunctionChecked(TEXT("OnRep_CurrentCombatStyle")), nullptr);
 TestTrue(TEXT("Repeated replication notifications keep weapon identity"), Presentation->GetSpawnedWeapon() == First);
 TestTrue(TEXT("Repeated replication notifications keep independent motion"), Presentation->IsIndependentMotionActive());
 TestEqual(TEXT("Repeated callbacks do not spawn"), Spawned, 1);

 Manager->UnequipSlot(EProject_JEquipmentSlot::Weapon);
 TestEqual(TEXT("Unequip creates zero intermediate weapons"), Spawned, 1);
 TestNull(TEXT("Unequip removes visual"), Presentation->GetSpawnedWeapon());
 TestFalse(TEXT("Unequip ends independent motion"), Presentation->IsIndependentMotionActive());
 Presentation->RefreshPresentation(); Presentation->ExitCombatPresentation();
 TestEqual(TEXT("Empty-slot callbacks do not spawn"), Spawned, 1);
 Manager->EquipItem(Item);
 TestEqual(TEXT("Reequip creates exactly one new actor"), Spawned, 2);
 auto* Second = Presentation->GetSpawnedWeapon();
 auto* OtherStyle = NewObject<UProject_JCombatStyleDefinition>(World);
 Pawn->SetCurrentCombatStyle(OtherStyle);
 TestTrue(TEXT("Style-only change keeps the same weapon"), Presentation->GetSpawnedWeapon() == Second);

 auto* OtherProfile = NewObject<UProject_JWeaponPresentationProfile>(World);
 OtherProfile->WeaponActorClass = AStaticMeshActor::StaticClass();
 OtherProfile->DrawnSocketName = NAME_None; OtherProfile->SheathedSocketName = NAME_None;
 Pawn->SetCurrentWeaponPresentationProfile(OtherProfile);
 TestEqual(TEXT("Changed profile rebuilds once"), Spawned, 3);
 TestTrue(TEXT("Changed profile removes previous actor"), !IsValid(Second) || Second->IsActorBeingDestroyed());
 OtherProfile->WeaponActorClass = AActor::StaticClass();
 Presentation->RefreshPresentation();
 TestEqual(TEXT("Same asset with changed actor class rebuilds"), Spawned, 4);
 if (auto* Current = Presentation->GetSpawnedWeapon()) Current->Destroy();
 Presentation->RefreshPresentation();
 TestEqual(TEXT("External destruction can recover"), Spawned, 5);
 World->bIsTearingDown = true;
 Presentation->RefreshPresentation();
 TestNull(TEXT("Teardown only removes visual"), Presentation->GetSpawnedWeapon());
 TestEqual(TEXT("Teardown cannot spawn"), Spawned, 5);
 World->RemoveOnActorSpawnedHandler(SpawnHandle);
 World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
 return true;
}
#endif
