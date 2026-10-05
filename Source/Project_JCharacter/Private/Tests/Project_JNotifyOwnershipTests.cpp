#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Project_JGameplayTags.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJHitWindowOwnershipTest, "ProjectJ.Maturity.Combat.HitWindowOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJHitWindowOwnershipTest::RunTest(const FString&)
{
	auto* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Owner = World->SpawnActor<ACharacter>();
	auto* Hit = NewObject<UProject_JCombatHitValidationComponent>(Owner); Hit->RegisterComponent();
	auto* Attack = NewObject<UProject_JAttackDefinition>(Owner);
	const FGameplayTag Tag = FProject_JGameplayTags::Get().InputTag_Weapon_LMB;
	Hit->BeginAttackNode(Tag, Attack, 1);
	const uint64 A = Hit->BeginHitWindow(), B = Hit->BeginHitWindow();
	Hit->EndHitWindow(A);
	TestTrue(TEXT("Ending A preserves overlapping B"), Hit->IsHitWindowCurrent(B));
	Hit->BeginAttackNode(Tag, Attack, 2);
	const uint64 C = Hit->BeginHitWindow();
	TestFalse(TEXT("New same-tag attack invalidates old B"), Hit->IsHitWindowCurrent(B));
	Hit->EndHitWindow(B); Hit->EndHitWindow(0);
	TestTrue(TEXT("Old/rejected End cannot close the new attack"), Hit->IsHitWindowCurrent(C));
	Hit->EndAttack();
	TestFalse(TEXT("Ending the attack releases its windows"), Hit->IsHitWindowCurrent(C));
	return true;
}
#endif
