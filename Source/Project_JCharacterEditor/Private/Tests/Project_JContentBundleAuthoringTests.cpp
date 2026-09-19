#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Authoring/Project_JContentBundleAuthoring.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Combat/Project_JComboDefinition.h"
#include "Animation/AnimMontage.h"

namespace
{
constexpr auto BundleTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
UProject_JContentBundleRequest* MakeBundleRequest()
{
	auto* Request = NewObject<UProject_JContentBundleRequest>();
	Request->Identifier = TEXT("BundleTestNewClass");
	Request->BaseClass = NewObject<UProject_JCharacterClassDefinition>();
	Request->BaseClass->ClassId = TEXT("BundleTestSourceClass");
	auto* Style = NewObject<UProject_JCombatStyleDefinition>();
	Style->CombatStyleTag = FGameplayTag::RequestGameplayTag(TEXT("State.CombatMode"));
	Style->bUsesCombo = false;
	Style->bRequiresWeaponAnimation = false;
	Request->BaseClass->DefaultCombatStyle = Style;
	return Request;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJBundlePlanValidationTest, "ProjectJ.Authoring.Bundle.PlanValidation", BundleTestFlags)
bool FProjectJBundlePlanValidationTest::RunTest(const FString&)
{
	using namespace ProjectJ::ContentAuthoring;
	auto* Request = MakeBundleRequest();
	FBundlePlan Plan;
	TArray<FText> Errors;
	TestTrue(TEXT("Valid plan"), MakePlan(*Request, Plan, Errors));
	TestEqual(TEXT("No unnecessary combo asset for GAS-only style"), Plan.GetPackages().Num(), 2);
	TestTrue(TEXT("Registry entry addresses the root asset"), Plan.RegistrationLine.Contains(TEXT("+ClassDefinitions=/Game/DataAssetSets/Classes/DA_Class_BundleTestNewClass.DA_Class_BundleTestNewClass")));
	Request->Identifier = TEXT("../Escape");
	TestFalse(TEXT("Reject path injection in identity"), MakePlan(*Request, Plan, Errors));
	TestTrue(TEXT("Rejected request returns no partial plan"), Plan.RootPackage.IsEmpty());
	Request->Identifier = TEXT("None");
	TestFalse(TEXT("Reject None runtime ID"), MakePlan(*Request, Plan, Errors));
	Request->Identifier = Request->BaseClass->ClassId.ToString();
	TestFalse(TEXT("Cannot copy source ClassId"), MakePlan(*Request, Plan, Errors));
	Request->Identifier = TEXT("NewIdentity");
	Request->DestinationFolder = TEXT("/Engine/DataAssetSets");
	TestFalse(TEXT("Cannot create into engine content"), MakePlan(*Request, Plan, Errors));
	Request->DestinationFolder = TEXT("/Game/DataAssetSetsExtra");
	TestFalse(TEXT("Scan-root prefix must be a directory boundary"), MakePlan(*Request, Plan, Errors));
	Request->DestinationFolder = TEXT("/Game/DataAssetSets/../Other");
	TestFalse(TEXT("Cannot escape configured scan root"), MakePlan(*Request, Plan, Errors));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJBundleDraftIsolationTest, "ProjectJ.Authoring.Bundle.DraftIsolation", BundleTestFlags)
bool FProjectJBundleDraftIsolationTest::RunTest(const FString&)
{
	using namespace ProjectJ::ContentAuthoring;
	auto* Request = MakeBundleRequest();
	auto* SourceStyle = Request->BaseClass->DefaultCombatStyle.Get();
	auto* Attack = NewObject<UProject_JAttackDefinition>();
	Attack->AttackTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Weapon.LMB"));
	Attack->Montage = NewObject<UAnimMontage>();
	auto* ExtraAttack = NewObject<UProject_JAttackDefinition>();
	ExtraAttack->AttackTag = FGameplayTag::RequestGameplayTag(TEXT("State.CombatMode"));
	SourceStyle->bUsesCombo = true;
	SourceStyle->ComboDefinition = NewObject<UProject_JComboDefinition>();
	FProject_JComboNode Node;
	Node.NodeTag = Attack->AttackTag;
	Node.StartInputTags.AddTag(Attack->AttackTag);
	Node.AttackDefinition = Attack;
	SourceStyle->ComboDefinition->Nodes.Add(Node);
	SourceStyle->AttackSet = NewObject<UProject_JAttackSet>();
	SourceStyle->AttackSet->Attacks = {Attack, ExtraAttack};
	TArray<FText> Errors;
	auto* Draft = BuildDraft(*Request, Errors);
	if (!TestNotNull(TEXT("Valid draft"), Draft)) return false;
	auto* Class = Cast<UProject_JCharacterClassDefinition>(Draft->Root);
	TestNotNull(TEXT("Class root"), Class);
	if (!Class) return false;
	TestTrue(TEXT("Root points to its private style"), Class->DefaultCombatStyle == Draft->Style);
	TestTrue(TEXT("Style points to its private combo"), Draft->Style->ComboDefinition == Draft->Combo);
	TestTrue(TEXT("Combo is copied"), Draft->Combo != SourceStyle->ComboDefinition);
	TestTrue(TEXT("Attack remains shared"), Draft->Combo->Nodes[0].AttackDefinition == Attack);
	TestNull(TEXT("No separately maintained attack set"), Draft->Style->AttackSet.Get());
	TestTrue(TEXT("Derived catalog enabled"), Draft->Style->bDeriveAttackCatalog);
	TestEqual(TEXT("Only non-combo legacy attacks are retained explicitly"), Draft->Style->AdditionalAttacks.Num(), 1);
	TestTrue(TEXT("Legacy extra attack retained"), Draft->Style->AdditionalAttacks.Contains(ExtraAttack));
	TestFalse(TEXT("Preview does not create an asset"), Draft->Root->IsAsset());
	Draft->Combo->Nodes.Reset();
	TestEqual(TEXT("Editing cloned combo does not change source"), SourceStyle->ComboDefinition->Nodes.Num(), 1);
	TestFalse(TEXT("Source authoring mode untouched"), SourceStyle->bDeriveAttackCatalog);
	TestEqual(TEXT("Source identity untouched"), Request->BaseClass->ClassId, FName(TEXT("BundleTestSourceClass")));
	SourceStyle->CombatStyleTag = FGameplayTag();
	TestNull(TEXT("Invalid styles cannot be published through a draft"), BuildDraft(*Request, Errors));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJBundleAdvancementTest, "ProjectJ.Authoring.Bundle.AdvancementSafety", BundleTestFlags)
bool FProjectJBundleAdvancementTest::RunTest(const FString&)
{
	using namespace ProjectJ::ContentAuthoring;
	auto* Request = MakeBundleRequest();
	Request->Kind = EProject_JContentBundleKind::Advancement;
	auto* Previous = NewObject<UProject_JCharacterAdvancementDefinition>();
	Previous->AdvancementId = TEXT("ExistingAdvancement");
	Previous->BaseClass = Request->BaseClass;
	Previous->RequiredLevel = 20;
	Previous->ExclusiveBranch = TEXT("ExistingExclusiveBranch");
	Previous->AbilityGrantPolicy = EProject_JAdvancementAbilityGrantPolicy::ReplacePreviousAdvancement;
	Request->PreviousAdvancement = Previous;
	TArray<FText> Errors;
	auto* Draft = BuildDraft(*Request, Errors);
	if (!TestNotNull(TEXT("Advancement draft"), Draft)) return false;
	auto* Advancement = Cast<UProject_JCharacterAdvancementDefinition>(Draft->Root);
	if (!TestNotNull(TEXT("Correct root type"), Advancement)) return false;
	TestTrue(TEXT("Base class is reused, not duplicated"), Advancement->BaseClass == Request->BaseClass);
	TestEqual(TEXT("Explicit predecessor retained"), Advancement->RequiredAdvancementIds[0], Previous->AdvancementId);
	TestEqual(TEXT("Does not lower predecessor level"), Advancement->RequiredLevel, 20);
	TestTrue(TEXT("Branch is a deliberate author decision"), Advancement->ExclusiveBranch.IsNone());
	TestTrue(TEXT("Replacement policy is not copied"), Advancement->AbilityGrantPolicy == EProject_JAdvancementAbilityGrantPolicy::Additive);
	Previous->BaseClass = NewObject<UProject_JCharacterClassDefinition>();
	FBundlePlan Plan;
	TestFalse(TEXT("Cross-class prerequisite rejected"), MakePlan(*Request, Plan, Errors));
	return true;
}
#endif
