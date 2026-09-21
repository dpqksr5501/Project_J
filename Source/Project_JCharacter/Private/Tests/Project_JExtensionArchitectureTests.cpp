#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "Combat/Project_JCombatConfiguration.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Combat/Project_JComboDefinition.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Project_JNPCCharacter.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JInventoryComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Abilities/GameplayAbility.h"
#include "Animation/AnimMontage.h"
#include "Animation/Project_JWeaponAnimProfile.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace
{
constexpr auto ExtensionTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
struct FExtensionWorld
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FExtensionWorld() { GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World); }
	~FExtensionWorld() { if (World->GetBegunPlay()) World->EndPlay(EEndPlayReason::LevelTransition); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
};
UProject_JAbilitySet* MakeSet()
{
	auto* Set = NewObject<UProject_JAbilitySet>();
	FProject_JAbilitySet_GameplayAbility Entry;
	Entry.Ability = UGameplayAbility::StaticClass();
	Set->GrantedAbilityEntries.Add(Entry);
	return Set;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGrantLeaseTest, "ProjectJ.Extension.Grants.SharedOwnership", ExtensionTestFlags)
bool FProjectJGrantLeaseTest::RunTest(const FString&)
{
	FExtensionWorld Scope;
	auto* Owner = Scope.World->SpawnActor<AProject_JNPCCharacter>();
	auto* ASC = CastChecked<UProject_JAbilitySystemComponent>(Owner->GetAbilitySystemComponent());
	ASC->InitAbilityActorInfo(Owner, Owner);
	auto* Set = MakeSet();
	FProject_JAbilitySet_GrantedHandles Class, Equipment;
	Set->GiveToAbilitySystem(ASC, &Class, Owner, TEXT("SharedStyle"));
	Set->GiveToAbilitySystem(ASC, &Class, Owner, TEXT("SharedStyle"));
	Set->GiveToAbilitySystem(ASC, &Equipment, Owner, TEXT("SharedStyle"));
	TestEqual(TEXT("Two providers and repeated grant create one spec"), ASC->GetActivatableAbilities().Num(), 1);
	Set->TakeFromAbilitySystem(ASC, &Class);
	TestEqual(TEXT("Equipment still owns the shared ability"), ASC->GetActivatableAbilities().Num(), 1);
	Set->TakeFromAbilitySystem(ASC, &Class);
	TestEqual(TEXT("Repeated release cannot consume another provider's lease"), ASC->GetActivatableAbilities().Num(), 1);
	Set->TakeFromAbilitySystem(ASC, &Equipment);
	TestEqual(TEXT("Last provider removes the spec"), ASC->GetActivatableAbilities().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJProgressionTest, "ProjectJ.Extension.Progression.BranchesAndGrants", ExtensionTestFlags)
bool FProjectJProgressionTest::RunTest(const FString&)
{
	FExtensionWorld Scope;
	auto* Owner = Scope.World->SpawnActor<AProject_JNPCCharacter>();
	auto* ASC = CastChecked<UProject_JAbilitySystemComponent>(Owner->GetAbilitySystemComponent());
	ASC->InitAbilityActorInfo(Owner, Owner);
	auto* Progression = Owner->GetProgressionComponent();
	auto* Class = NewObject<UProject_JCharacterClassDefinition>(); Class->ClassId = TEXT("Warrior");
	auto* Style = NewObject<UProject_JCombatStyleDefinition>(); Style->AbilitySets.Add(MakeSet()); Class->DefaultCombatStyle = Style;
	Progression->InitializeDefaults(Class, nullptr, 10);
	auto* First = NewObject<UProject_JCharacterAdvancementDefinition>(); First->AdvancementId = TEXT("Knight"); First->BaseClass = Class;
	First->CombatStyleOverride = Style; First->ExclusiveBranch = TEXT("WarriorBranch"); First->AdditionalAbilitySets.Add(MakeSet());
	auto* Second = NewObject<UProject_JCharacterAdvancementDefinition>(); Second->AdvancementId = TEXT("Paladin"); Second->BaseClass = Class;
	Second->RequiredAdvancementIds.Add(First->AdvancementId); Second->ExclusiveBranch = First->ExclusiveBranch;
	Second->AdditionalAbilitySets.Add(MakeSet());
	TestFalse(TEXT("Missing prerequisite cannot advance"), Progression->ApplyAdvancement(Second));
	TestTrue(TEXT("First advancement succeeds"), Owner->ApplyAdvancementDefinition(First));
	TestEqual(TEXT("Class and advancement share one style spec"), ASC->GetActivatableAbilities().Num(), 2);
	TestFalse(TEXT("Acquired advancement cannot be repeated"), Progression->ApplyAdvancement(First));
	TestFalse(TEXT("Additive cannot activate a competing branch"), Progression->ApplyAdvancement(Second));
	Second->AbilityGrantPolicy = EProject_JAdvancementAbilityGrantPolicy::ReplacePreviousAdvancement;
	bool bReentryRejected = false;
	const auto Delegate = Progression->OnChanged.AddLambda([&] { bReentryRejected = !Progression->SetLevel(99); });
	TestTrue(TEXT("Explicit replacement succeeds"), Owner->ApplyAdvancementDefinition(Second));
	Progression->OnChanged.Remove(Delegate);
	TestTrue(TEXT("Mutation callbacks cannot reenter progression"), bReentryRejected);
	TestEqual(TEXT("Replacement retires advancement grants but retains shared class style"), ASC->GetActivatableAbilities().Num(), 2);
	TestEqual(TEXT("Replacement preserves acquisition history"), Progression->GetAcquiredAdvancements().Num(), 2);
	Progression->InitializeDefaults(nullptr, First, 1);
	TestEqual(TEXT("Avatar defaults cannot roll back persisted level"), Progression->GetState().Level, 10);
	TestEqual(TEXT("Avatar defaults cannot roll back advancement"), Owner->GetAdvancementId(), Second->AdvancementId);
	auto Snapshot = Progression->CaptureSnapshot();
	auto* RestoredOwner = Scope.World->SpawnActor<AProject_JNPCCharacter>();
	auto* RestoredASC = CastChecked<UProject_JAbilitySystemComponent>(RestoredOwner->GetAbilitySystemComponent());
	RestoredASC->InitAbilityActorInfo(RestoredOwner, RestoredOwner);
	auto* Restored = RestoredOwner->GetProgressionComponent();
	const TArray<UProject_JCharacterAdvancementDefinition*> History {First, Second};
	Snapshot.SchemaVersion = 999;
	TestFalse(TEXT("Unknown snapshot version rejected"), Restored->RestoreSnapshot(Snapshot, Class, History));
	TestEqual(TEXT("Rejected restore creates no specs"), RestoredASC->GetActivatableAbilities().Num(), 0);
	Snapshot.SchemaVersion = 1;
	TestTrue(TEXT("Validated snapshot restores into a fresh owner"), Restored->RestoreSnapshot(Snapshot, Class, History));
	TestEqual(TEXT("Restore grants only class and active advancement"), RestoredASC->GetActivatableAbilities().Num(), 2);
	TestFalse(TEXT("Duplicate restore rejected"), Restored->RestoreSnapshot(Snapshot, Class, History));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJProgressionGraphTest, "ProjectJ.Extension.Progression.GraphValidation", ExtensionTestFlags)
bool FProjectJProgressionGraphTest::RunTest(const FString&)
{
	auto* Class = NewObject<UProject_JCharacterClassDefinition>(); Class->ClassId = TEXT("Base");
	auto* First = NewObject<UProject_JCharacterAdvancementDefinition>(); First->AdvancementId = TEXT("First"); First->BaseClass = Class;
	auto* Second = NewObject<UProject_JCharacterAdvancementDefinition>(); Second->AdvancementId = TEXT("Second"); Second->BaseClass = Class;
	Second->RequiredAdvancementIds.Add(First->AdvancementId);
	TArray<UProject_JCharacterClassDefinition*> Classes {Class};
	TArray<UProject_JCharacterAdvancementDefinition*> Advancements {First, Second};
	TArray<FText> Errors;
	TestTrue(TEXT("Acyclic chain is valid"), ProjectJ::ValidateAdvancementGraph(Classes, Advancements, Errors));
	First->RequiredAdvancementIds.Add(Second->AdvancementId);
	TestFalse(TEXT("Cycle rejected"), ProjectJ::ValidateAdvancementGraph(Classes, Advancements, Errors));
	First->RequiredAdvancementIds.Reset(); Second->RequiredAdvancementIds = {TEXT("Missing")}; Errors.Reset();
	TestFalse(TEXT("Missing prerequisite rejected"), ProjectJ::ValidateAdvancementGraph(Classes, Advancements, Errors));
	Second->RequiredAdvancementIds.Reset();
	auto* Imposter = NewObject<UProject_JCharacterClassDefinition>(); Imposter->ClassId = Class->ClassId; Second->BaseClass = Imposter; Errors.Reset();
	TestFalse(TEXT("Same class ID does not authorize another asset"), ProjectJ::ValidateAdvancementGraph(Classes, Advancements, Errors));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJStyleAuthoringTest, "ProjectJ.Extension.Authoring.DerivedCatalogAndInlineAbilities", ExtensionTestFlags)
bool FProjectJStyleAuthoringTest::RunTest(const FString&)
{
	const auto& Tags = FProject_JGameplayTags::Get();
	auto* Style = NewObject<UProject_JCombatStyleDefinition>(); Style->CombatStyleTag = Tags.State_CombatMode;
	Style->bUsesCombo = false; Style->bRequiresWeaponAnimation = false;
	TArray<FText> Errors;
	TestTrue(TEXT("GAS-only style needs neither combo nor weapon animation"), Style->ValidateDefinition(Errors));
	Style->InlineAbilities = MakeSet()->GrantedAbilityEntries;
	const auto* Compiled = Style->GetRuntimeAbilitySets()[0].Get();
	TestTrue(TEXT("Repeated reads reuse the compiled ability set"), Compiled == Style->GetRuntimeAbilitySets()[0]);
	TestTrue(TEXT("Compiled adapter is transient"), Compiled->HasAnyFlags(RF_Transient));
	Style->InlineAbilities[0].InputTag = Tags.InputTag_Weapon_LMB;
	auto* Shared = MakeSet(); Shared->GrantedAbilityEntries[0].InputTag = Tags.InputTag_Weapon_LMB; Style->AbilitySets.Add(Shared);
	TestFalse(TEXT("Inline/shared input collision rejected"), Style->ValidateDefinition(Errors));
	Style->AbilitySets.Reset(); Style->InvalidateRuntimeData();
	auto* Attack = NewObject<UProject_JAttackDefinition>(); Attack->AttackTag = Tags.InputTag_Weapon_LMB; Attack->Montage = NewObject<UAnimMontage>();
	auto* Combo = NewObject<UProject_JComboDefinition>(); FProject_JComboNode Node; Node.AttackDefinition = Attack; Combo->Nodes = {Node, Node};
	Style->ComboDefinition = Combo; Style->bUsesCombo = true; Style->bDeriveAttackCatalog = true;
	Style->InvalidateRuntimeData();
	TestTrue(TEXT("Repeated combo references derive one catalog entry"), Style->GetRuntimeAttackSet() && Style->GetRuntimeAttackSet()->Attacks.Num() == 1);
	auto* Conflict = NewObject<UProject_JAttackDefinition>(); Conflict->AttackTag = Attack->AttackTag;
	Style->AdditionalAttacks.Add(Conflict); Style->InvalidateRuntimeData();
	TestFalse(TEXT("Conflicting identity rejected"), Style->ValidateDefinition(Errors));
	TestNull(TEXT("Invalid derived catalog is never returned"), Style->GetRuntimeAttackSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJConfigurationTest, "ProjectJ.Extension.Configuration.DomainComposition", ExtensionTestFlags)
bool FProjectJConfigurationTest::RunTest(const FString&)
{
	auto* ClassStyle = NewObject<UProject_JCombatStyleDefinition>();
	auto* AdvancementStyle = NewObject<UProject_JCombatStyleDefinition>();
	auto* WeaponStyle = NewObject<UProject_JCombatStyleDefinition>();
	auto* Advancement = NewObject<UProject_JCharacterAdvancementDefinition>(); Advancement->CombatStyleOverride = AdvancementStyle;
	auto Legacy = FProject_JCombatConfiguration::Resolve(ClassStyle, Advancement, WeaponStyle);
	TestTrue(TEXT("Legacy equipment priority remains"), Legacy.GameplayStyle == WeaponStyle && Legacy.AnimationStyle == WeaponStyle);
	Advancement->bOverrideEquippedGameplay = true;
	auto Composed = FProject_JCombatConfiguration::Resolve(ClassStyle, Advancement, WeaponStyle);
	TestTrue(TEXT("Opt-in advancement gameplay and weapon animation compose independently"), Composed.GameplayStyle == AdvancementStyle && Composed.AnimationStyle == WeaponStyle);
	TestEqual(TEXT("Gameplay provenance retained"), Composed.GameplaySource, EProject_JCombatConfigurationSource::Advancement);
	auto Unequipped = FProject_JCombatConfiguration::Resolve(ClassStyle, Advancement, nullptr);
	TestTrue(TEXT("Unequip restores advancement animation"), Unequipped.AnimationStyle == AdvancementStyle);
	auto Base = FProject_JCombatConfiguration::Resolve(ClassStyle, nullptr, nullptr);
	TestTrue(TEXT("Class fallback preserved"), Base.GameplayStyle == ClassStyle);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJInvalidStyleEquipTest, "ProjectJ.Extension.Authoring.InvalidEquipPreservesState", ExtensionTestFlags)
bool FProjectJInvalidStyleEquipTest::RunTest(const FString&)
{
	FExtensionWorld Scope;
	auto* Owner = Scope.World->SpawnActor<AActor>();
	auto* Inventory = NewObject<UProject_JInventoryComponent>(Owner); Inventory->RegisterComponent();
	auto* Equipment = NewObject<UProject_JEquipmentManagerComponent>(Owner); Equipment->RegisterComponent();
	auto* Existing = NewObject<UProject_JEquipmentItemDefinition>(); Existing->EquipmentSlot = EProject_JEquipmentSlot::Weapon;
	auto* Invalid = NewObject<UProject_JEquipmentItemDefinition>(); Invalid->EquipmentSlot = EProject_JEquipmentSlot::Weapon;
	auto* Style = NewObject<UProject_JCombatStyleDefinition>(); Style->InlineAbilities.AddDefaulted(); Invalid->CombatStyleDefinition = Style;
	auto OldItem = Inventory->AddItemDefinition(Existing);
	auto NewItem = Inventory->AddItemDefinition(Invalid);
	TestTrue(TEXT("Valid item equipped"), Equipment->TryEquipItemInstanceById(OldItem.InstanceId).bSucceeded);
	const auto Result = Equipment->TryEquipItemInstanceById(NewItem.InstanceId);
	TestEqual(TEXT("Invalid inline style rejected before mutation"), Result.Failure, EProject_JEquipmentOperationFailure::InvalidDefinition);
	TestTrue(TEXT("Old equipment preserved"), Equipment->GetEquippedItemInSlot(EProject_JEquipmentSlot::Weapon) == Existing);
	TestTrue(TEXT("Old inventory lock preserved"), Inventory->IsItemInstanceLocked(OldItem.InstanceId));
	TestFalse(TEXT("Rejected item not locked"), Inventory->IsItemInstanceLocked(NewItem.InstanceId));
	TestTrue(TEXT("Invalid compiler returns no partial abilities"), Style->GetRuntimeAbilitySets().IsEmpty());
	return true;
}
#endif
