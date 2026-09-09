#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Project_JNPCCharacter.h"
#include "Project_JGameplayTags.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCombatPosePolicyTest, "ProjectJ.GroupB.CombatPosePolicy",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCombatPosePolicyTest::RunTest(const FString&)
{
 auto* World = UWorld::CreateWorld(EWorldType::Game, false);
 auto* Owner = World->SpawnActor<AProject_JNPCCharacter>();
 auto* Mesh = Owner->GetMesh();
 auto* Hit = NewObject<UProject_JCombatHitValidationComponent>(Owner); Hit->RegisterComponent();
 auto* Attack = NewObject<UProject_JAttackDefinition>(Owner);
 const auto Tag = FProject_JGameplayTags::Get().InputTag_Weapon_LMB;
 const auto Reset = [&] { Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered; Mesh->bEnableUpdateRateOptimizations = true; Mesh->bSuppressNotifyEventDispatch = true; };
 Reset(); Hit->BeginAttackNode(Tag, Attack);
 TestTrue(TEXT("Attack refreshes hidden/DS bones"), Mesh->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones);
 TestFalse(TEXT("Attack disables URO skipping"), Mesh->bEnableUpdateRateOptimizations);
 TestFalse(TEXT("Attack enables required notify dispatch"), Mesh->bSuppressNotifyEventDispatch);
 Hit->BeginAttackNode(Tag, Attack); // Combo continuation must not overwrite the original snapshot.
 Hit->EndAttack(); Hit->EndAttack();
 TestTrue(TEXT("Final node restores original visibility once"), Mesh->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered);
 TestTrue(TEXT("URO restored"), Mesh->bEnableUpdateRateOptimizations);
 TestTrue(TEXT("Notify policy restored"), Mesh->bSuppressNotifyEventDispatch);
 Hit->BeginAttackNode(Tag, Attack);
 Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered;
 Hit->EndAttack();
 TestTrue(TEXT("New explicit visibility policy is preserved"), Mesh->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered);
 Reset(); Hit->BeginAttackNode(Tag, Attack); Hit->DestroyComponent();
 TestTrue(TEXT("Component destruction before BeginPlay restores mesh policy"), Mesh->bEnableUpdateRateOptimizations && Mesh->bSuppressNotifyEventDispatch);
 World->DestroyWorld(false);
 return true;
}
#endif
