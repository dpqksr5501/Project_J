#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJDefinitionRegistryAtomicTest, "ProjectJ.Foundation.Definitions.AtomicRefresh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJDefinitionRegistryAtomicTest::RunTest(const FString&)
{
	auto* GI = NewObject<UGameInstance>(GEngine); GI->InitializeStandalone();
	auto* World = GI->GetWorld();
	ON_SCOPE_EXIT { GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Registry = GI->GetSubsystem<UProject_JCharacterDataSubsystem>();
	auto* Authored = LoadObject<UProject_JCharacterClassDefinition>(nullptr, TEXT("/Game/DataAssetSets/Class/DA_Class_Spellblade.DA_Class_Spellblade"));
	auto* Item = LoadObject<UProject_JEquipmentItemDefinition>(nullptr, TEXT("/Game/DataAssetSets/Animation_Profiles/Equip/DA_Greatsword_Equip.DA_Greatsword_Equip"));
	if (!TestNotNull(TEXT("Real registry"), Registry) || !TestNotNull(TEXT("Authored class"), Authored) || !TestNotNull(TEXT("Authored item"), Item)) return false;
	const uint64 BootstrapRefreshes = Registry->GetRegistryStats().Refreshes;
	TestTrue(TEXT("Runtime ensure consumes the initialized core registry"), Registry->EnsurePrimaryAssetDefinitions());
	TestEqual(TEXT("Warm ensure does not scan or revalidate the catalog"), Registry->GetRegistryStats().Refreshes, BootstrapRefreshes);
	Registry->ReloadConfig(); // Actual UObject reload path calls PostReloadConfig after importing Config properties.
	TestEqual(TEXT("Standard Config reload performs no eager catalog refresh"), Registry->GetRegistryStats().Refreshes, BootstrapRefreshes);
	TestEqual(TEXT("Config reload retains the previous published getter values until refresh"), Registry->GetClassDefinition(Authored->ClassId), Authored);
	TestTrue(TEXT("The next runtime ensure refreshes reloaded Config atomically"), Registry->EnsurePrimaryAssetDefinitions());
	TestEqual(TEXT("Config reload causes exactly one deferred refresh"), Registry->GetRegistryStats().Refreshes, BootstrapRefreshes + 1);
	TestTrue(TEXT("Later runtime consumers reuse the refreshed Config publication"), Registry->EnsurePrimaryAssetDefinitions());
	TestEqual(TEXT("A subsequent ensure adds no second refresh"), Registry->GetRegistryStats().Refreshes, BootstrapRefreshes + 1);
	auto* Extra = NewObject<UProject_JCharacterClassDefinition>(Registry); Extra->ClassId = TEXT("RegistryAtomic.Extra");
	auto* Advance = NewObject<UProject_JCharacterAdvancementDefinition>(Registry); Advance->AdvancementId = TEXT("RegistryAtomic.Advance"); Advance->BaseClass = Extra;
	Registry->ClassDefinitions = {Authored, Extra}; Registry->AdvancementDefinitions = {Advance};
	TestTrue(TEXT("Explicit sources and PrimaryAsset overlap publish together"), Registry->LoadPrimaryAssetDefinitions());
	TestEqual(TEXT("Configured class is a real member"), Registry->GetClassDefinition(Extra->ClassId), Extra);
	TestEqual(TEXT("Configured advancement is a real member"), Registry->GetAdvancementDefinition(Advance->AdvancementId), Advance);
	TestEqual(TEXT("Item registry participates in the same publication"), Registry->GetItemDefinition(Item->ItemId, Item->SchemaVersion), static_cast<UProject_JItemDefinition*>(Item));
	TestEqual(TEXT("Loaded paths do not issue more synchronous load attempts"), Registry->GetRegistryStats().LastSynchronousLoads, 0);
	const uint64 PublishedRefreshes = Registry->GetRegistryStats().Refreshes;
	for (int32 Consumer = 0; Consumer < 32; ++Consumer)
		TestTrue(TEXT("Repeated runtime consumers reuse one valid catalog publication"), Registry->EnsurePrimaryAssetDefinitions());
	TestEqual(TEXT("Thirty-two warm consumers perform no catalog refresh"), Registry->GetRegistryStats().Refreshes, PublishedRefreshes);

	Registry->ClassDefinitions.Add(Extra);
	TestFalse(TEXT("Duplicate explicit path fails refresh"), Registry->LoadPrimaryAssetDefinitions());
	TestFalse(TEXT("Failed explicit refresh closes ensure instead of trusting stale readiness"), Registry->EnsurePrimaryAssetDefinitions());
	TestEqual(TEXT("Failed refresh does not partially remove old members"), Registry->GetClassDefinition(Extra->ClassId), Extra);
	Registry->ClassDefinitions.Pop();
	TestTrue(TEXT("Fixed source can recover a failed refresh through normal ensure admission"), Registry->EnsurePrimaryAssetDefinitions());
	auto* Duplicate = NewObject<UProject_JCharacterClassDefinition>(Registry); Duplicate->ClassId = Extra->ClassId;
	Registry->ClassDefinitions.Add(Duplicate);
	TestFalse(TEXT("Different assets with the same gameplay ID fail"), Registry->LoadPrimaryAssetDefinitions());
	Registry->ClassDefinitions.Pop();
	Advance->RequiredAdvancementIds = {TEXT("Missing.Prerequisite")};
	TestFalse(TEXT("Invalid graph cannot publish a partial set"), Registry->LoadPrimaryAssetDefinitions());
	TestEqual(TEXT("Unrelated item membership remains published"), Registry->GetItemDefinition(Item->ItemId, Item->SchemaVersion), static_cast<UProject_JItemDefinition*>(Item));
	Advance->RequiredAdvancementIds.Reset();
	Registry->ClassDefinitions.Init(Authored, Registry->MaxCharacterDefinitions + 1);
	TestFalse(TEXT("Source count is bounded before resolving paths"), Registry->LoadPrimaryAssetDefinitions());
	TestEqual(TEXT("Oversized source did no loads"), Registry->GetRegistryStats().LastResolvedPaths, 0);
	Registry->ClassDefinitions = {Authored}; Registry->AdvancementDefinitions.Reset();
	TestTrue(TEXT("Current valid sources replace the previous set"), Registry->LoadPrimaryAssetDefinitions());
	TestNull(TEXT("Removed explicit class does not survive in a merged old map"), Registry->GetClassDefinition(Extra->ClassId));
	TestNull(TEXT("Removed explicit advancement is absent"), Registry->GetAdvancementDefinition(Advance->AdvancementId));
	TestEqual(TEXT("PrimaryAsset class remains available"), Registry->GetClassDefinition(Authored->ClassId), Authored);
	TestTrue(TEXT("Refresh failure pressure is observable"), Registry->GetRegistryStats().FailedRefreshes >= 4);
	TestEqual(TEXT("Ready admission preserves the current authored class"), Registry->GetClassDefinition(Authored->ClassId), Authored);
	return true;
}
#endif
