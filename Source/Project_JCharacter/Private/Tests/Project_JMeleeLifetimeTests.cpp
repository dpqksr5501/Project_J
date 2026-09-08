#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Combat/Project_JGameplayAbility_Melee.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Combat/Project_JComboDefinition.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Components/Project_JCombatPresentationComponent.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Components/Project_JSkillInputExecutionComponent.h"
#include "Project_JGreatswordCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMeleeLifetimeTest, "ProjectJ.Combat.MeleeLifetime",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMeleeLifetimeTest::RunTest(const FString& Parameters)
{
 UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 auto* Player = World->SpawnActor<AProject_JGreatswordCharacter>();
 auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Player);
 FindFProperty<FObjectPropertyBase>(AProject_JBaseCharacter::StaticClass(), TEXT("AbilitySystemComponent"))->SetObjectPropertyValue_InContainer(Player, ASC);
 ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Player, Player);
 auto* Style = LoadObject<UProject_JCombatStyleDefinition>(nullptr, TEXT("/Game/DataAssetSets/CombatStyle/DA_CombatStyle_Greatsword.DA_CombatStyle_Greatsword"));
 auto* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"));
 UClass* AbilityClass = LoadClass<UProject_JGameplayAbility_Melee>(nullptr, TEXT("/Game/GAs/GreatSword/GA_Greatsword.GA_Greatsword_C"));
 if (!TestNotNull(TEXT("Existing combat style"), Style) || !TestNotNull(TEXT("Existing mesh"), Mesh) || !TestNotNull(TEXT("Existing melee Blueprint class"), AbilityClass))
 { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return false; }
 FindFProperty<FObjectPropertyBase>(AProject_JPlayerCharacter::StaticClass(), TEXT("CurrentCombatStyle"))->SetObjectPropertyValue_InContainer(Player, Style);
 Player->GetMesh()->SetSkeletalMesh(Mesh); Player->GetMesh()->SetAnimInstanceClass(UAnimInstance::StaticClass());
 const auto& Tags = FProject_JGameplayTags::Get();
 ASC->AddLooseGameplayTag(Tags.State_CombatMode);
 auto* Input = Player->FindComponentByClass<UProject_JSkillInputExecutionComponent>(); Input->Initialize(Player);
 auto* Presentation = Player->FindComponentByClass<UProject_JCombatPresentationComponent>();
 auto* Hit = Player->FindComponentByClass<UProject_JCombatHitValidationComponent>();
 // Separate grant sources may supply the same combo class. Neither grant is silently removed.
 FGameplayAbilitySpec FirstSpec(AbilityClass, 1); FirstSpec.GetDynamicSpecSourceTags().AddTag(Tags.InputTag_Weapon_LMB);
 FGameplayAbilitySpec SecondSpec(AbilityClass, 1); SecondSpec.GetDynamicSpecSourceTags().AddTag(Tags.InputTag_Weapon_LMB);
 const auto FirstHandle = ASC->GiveAbility(FirstSpec); const auto SecondHandle = ASC->GiveAbility(SecondSpec);
 auto* First = CastChecked<UProject_JGameplayAbility_Melee>(ASC->FindAbilitySpecFromHandle(FirstHandle)->GetPrimaryInstance());
 auto* Second = CastChecked<UProject_JGameplayAbility_Melee>(ASC->FindAbilitySpecFromHandle(SecondHandle)->GetPrimaryInstance());
 Input->HandleInputTagPressed(Tags.InputTag_Weapon_LMB);
 TestTrue(TEXT("First combo owns the attack"), First->IsActive());
 TestFalse(TEXT("Second grant cannot interrupt the same input's attack"), Second->IsActive());
 TestTrue(TEXT("Presentation remains active before the authored trail notify"), Presentation->GetActiveAttackTag().IsValid());
 TestNotNull(TEXT("Hit definition remains active"), Hit->GetActiveAttackDefinition());
 TestTrue(TEXT("Montage is playing"), Player->GetMesh()->GetAnimInstance()->IsAnyMontagePlaying());
 ASC->CancelAbilityHandle(FirstHandle); ASC->CancelAbilityHandle(SecondHandle);
 TestFalse(TEXT("Cancellation clears presentation"), Presentation->GetActiveAttackTag().IsValid());
 Input->ClearCommandInputHistory(); Input->HandleInputTagPressed(Tags.InputTag_Weapon_LMB);
 TestTrue(TEXT("A fresh attack starts after cancellation"), First->IsActive());
 TestTrue(TEXT("Repeated attack restores presentation"), Presentation->GetActiveAttackTag().IsValid());
 Second->EndAbility(SecondHandle, First->GetCurrentActorInfo(), FGameplayAbilityActivationInfo(), true, true);
 TestTrue(TEXT("Inactive sibling's late end cannot clear current presentation"), Presentation->GetActiveAttackTag().IsValid());
 TestNotNull(TEXT("Inactive sibling's late end cannot clear the hit definition"), Hit->GetActiveAttackDefinition());
 FGameplayTagContainer OwnerTags; ASC->GetOwnedGameplayTags(OwnerTags);
 const auto* StartNode = Style->ComboDefinition->FindStartNode(Tags.InputTag_Weapon_LMB, OwnerTags);
 const auto* Transition = StartNode ? Style->ComboDefinition->FindTransition(*StartNode, Tags.InputTag_Weapon_LMB, OwnerTags) : nullptr;
 const auto* NextNode = Transition ? Style->ComboDefinition->FindNode(Transition->TargetNodeTag) : nullptr;
 if (TestNotNull(TEXT("Existing LMB combo transition"), NextNode))
 {
  FGameplayEventData Payload; Payload.EventTag = Tags.InputTag_Weapon_LMB;
  ASC->HandleGameplayEvent(Payload.EventTag, &Payload);
  TestEqual(TEXT("Buffered input preserves the current attack"), Presentation->GetActiveAttackTag(), StartNode->AttackDefinition->AttackTag);
  Payload.EventTag = Tags.Event_Combat_ComboWindow; Payload.EventMagnitude = 1;
  ASC->HandleGameplayEvent(Payload.EventTag, &Payload);
  TestTrue(TEXT("Same ability continues the combo"), First->IsActive());
  TestEqual(TEXT("Opening the combo window advances presentation"), Presentation->GetActiveAttackTag(), NextNode->AttackDefinition->AttackTag);
  TestTrue(TEXT("Opening the combo window advances hit validation"), Hit->GetActiveAttackDefinition() == NextNode->AttackDefinition);
 }
 ASC->CancelAllAbilities();
 World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
 return true;
}
#endif
