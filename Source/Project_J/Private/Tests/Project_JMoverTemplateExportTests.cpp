#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Movement/Project_JMoverComponent.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMoverTemplateExportsTest, "ProjectJ.Foundation.Movement.TemplateExports",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMoverTemplateExportsTest::RunTest(const FString&)
{
 const TStrongObjectPtr<UProject_JMoverComponent> Template(NewObject<UProject_JMoverComponent>(GetTransientPackage(), NAME_None, RF_ArchetypeObject));
 auto* PrivateMode = NewObject<UProject_JAttachedMovementMode>(Template.Get(), TEXT("PrivateExportFixture"), RF_ArchetypeObject);
 Template->MovementModes.Add(TEXT("ExportFixture"), PrivateMode);
 TestFalse(TEXT("Fixture starts with a private archetype"), PrivateMode->HasAnyFlags(RF_Public));
 TestTrue(TEXT("Actual template normalizer repairs an owned export"), Template->NormalizeTemplateModeExports() > 0);
 TestTrue(TEXT("Class archetype becomes a legal public import"), PrivateMode->HasAnyFlags(RF_Public));
 TestTrue(TEXT("Identity and class/tuning object are retained"), Template->MovementModes.FindRef(TEXT("ExportFixture")) == PrivateMode);
 TestEqual(TEXT("Normalization is idempotent"), Template->NormalizeTemplateModeExports(), 0);
 const TStrongObjectPtr<UProject_JMoverComponent> Instance(NewObject<UProject_JMoverComponent>());
 auto* InstanceMode = NewObject<UProject_JAttachedMovementMode>(Instance.Get());
 Instance->MovementModes.Add(TEXT("ExportFixture"), InstanceMode);
 TestEqual(TEXT("Runtime instance is not promoted to an export"), Instance->NormalizeTemplateModeExports(), 0);
 TestFalse(TEXT("Private runtime ownership is retained"), InstanceMode->HasAnyFlags(RF_Public));

 auto* World = UWorld::CreateWorld(EWorldType::Game, false);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
 for (const TCHAR* Path : {TEXT("/Game/NPCs/BP_NPC_Test.BP_NPC_Test_C"), TEXT("/Game/Character_BPs/BP_Player_Master.BP_Player_Master_C"), TEXT("/Game/Character_BPs/Spellblade/BP_Spellblade.BP_Spellblade_C")})
 {
  auto* Class = LoadClass<AActor>(nullptr, Path);
  if (!TestNotNull(TEXT("Actual gameplay Blueprint class"), Class)) return false;
  auto* Defaults = CastChecked<AActor>(Class->GetDefaultObject());
  auto* Mover = Defaults->FindComponentByClass<UProject_JMoverComponent>();
  if (!TestNotNull(TEXT("Actual template Mover"), Mover)) return false;
  for (const auto& Pair : Mover->MovementModes)
   if (const auto* Mode = Pair.Value.Get(); Mode && Mode->GetOuter() == Mover && Mode->HasAnyFlags(RF_ArchetypeObject | RF_DefaultSubObject))
    TestTrue(FString::Printf(TEXT("Actual mode archetype is importable: %s"), *Mode->GetPathName()), Mode->HasAnyFlags(RF_Public));
  FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
  auto* First = World->SpawnActor<AActor>(Class, FTransform::Identity, Params);
  auto* Second = World->SpawnActor<AActor>(Class, FTransform::Identity, Params);
  if (!TestNotNull(TEXT("First gameplay instance"), First) || !TestNotNull(TEXT("Second gameplay instance"), Second)) return false;
  auto* A = First->FindComponentByClass<UProject_JMoverComponent>();
  auto* B = Second->FindComponentByClass<UProject_JMoverComponent>();
  if (!TestNotNull(TEXT("First instance Mover"), A) || !TestNotNull(TEXT("Second instance Mover"), B)) return false;
  auto* WalkingA = A->MovementModes.FindRef(DefaultModeNames::Walking).Get();
  auto* WalkingB = B->MovementModes.FindRef(DefaultModeNames::Walking).Get();
  if (!TestNotNull(TEXT("Actual first walking mode"), WalkingA) || !TestNotNull(TEXT("Actual second walking mode"), WalkingB)) return false;
  TestTrue(TEXT("Instances keep separate movement state"), WalkingA != WalkingB && WalkingA->GetOuter() == A && WalkingB->GetOuter() == B);
  TestTrue(TEXT("Cross-package instance archetype is public"), WalkingA->GetArchetype()->HasAnyFlags(RF_Public));
  auto* SettingsA = A->FindSharedSettings_Mutable<UCommonLegacyMovementSettings>();
  auto* SettingsB = B->FindSharedSettings_Mutable<UCommonLegacyMovementSettings>();
  if (!TestNotNull(TEXT("First actor's active settings"), SettingsA) || !TestNotNull(TEXT("Second actor's active settings"), SettingsB)) return false;
  TestTrue(TEXT("Mutable settings are owned per actor"), SettingsA != SettingsB && SettingsA->GetOuter() == A && SettingsB->GetOuter() == B);
  const float OtherSpeed = SettingsB->MaxSpeed;
  SettingsA->MaxSpeed = 123;
  TestEqual(TEXT("One actor cannot tune another actor's settings"), SettingsB->MaxSpeed, OtherSpeed);
 }
 AddInfo(TEXT("Actual NPC/player Blueprint templates and separate instances checked without saving source assets. A fresh real Cook remains the cross-package serialization check."));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMoverNativeSettingsIdentityTest, "ProjectJ.Foundation.Movement.NativeSettingsIdentity",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMoverNativeSettingsIdentityTest::RunTest(const FString&)
{
 for (const TCHAR* Path : {TEXT("/Script/Project_JCharacter.Project_JNPCCharacter"), TEXT("/Script/Project_JCharacter.Project_JPlayerCharacter")})
 {
  auto* Class = LoadClass<AActor>(nullptr, Path);
  if (!TestNotNull(TEXT("Actual native gameplay class"), Class)) return false;
  auto* Mover = CastChecked<AActor>(Class->GetDefaultObject())->FindComponentByClass<UProject_JMoverComponent>();
  if (!TestNotNull(TEXT("Native class template Mover"), Mover)) return false;
  auto* Settings = Mover->FindSharedSettings_Mutable<UCommonLegacyMovementSettings>();
  if (!TestNotNull(TEXT("Common settings exist before runtime registration"), Settings)) return false;
  TestTrue(TEXT("Native settings import resolves by stable default-subobject name"),
   Mover->GetDefaultSubobjectByName(TEXT("CommonLegacyMovementSettings_0")) == Settings);
  TestTrue(TEXT("Settings are a real default subobject"), Settings->HasAnyFlags(RF_DefaultSubObject));
  TestTrue(TEXT("Native template settings are public"), Settings->HasAnyFlags(RF_Public));
  TestTrue(TEXT("Native settings have their own component outer"), Settings->GetOuter() == Mover);
 }
 return true;
}
#endif
