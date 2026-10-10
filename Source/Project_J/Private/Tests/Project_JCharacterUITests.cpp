#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/Project_JCharacterUIProfile.h"
#include "UI/Project_JUILayoutSettings.h"
#include "Project_JAttributeSet.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Kismet/GameplayStatics.h"
#include "UI/Project_JPlayerHUDWidget.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJUIProfileSelectionTest, "ProjectJ.UI.Character.ProfileSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJUIProfileSelectionTest::RunTest(const FString&)
{
	auto* Catalog = NewObject<UProject_JUIProfileCatalog>();
	auto* Default = NewObject<UProject_JCharacterUIProfile>(); Default->ProfileId = TEXT("Default");
	auto* Class = NewObject<UProject_JCharacterUIProfile>(); Class->ProfileId = TEXT("Warrior"); Class->ClassId = TEXT("Warrior");
	auto* Advance = NewObject<UProject_JCharacterUIProfile>(); Advance->ProfileId = TEXT("Awakened"); Advance->ClassId = TEXT("Warrior"); Advance->AdvancementId = TEXT("Awakened");
	const FGameplayTag Tag = FProject_JGameplayTags::Get().InputTag_Skill_Q;
	auto* Mode = NewObject<UProject_JCharacterUIProfile>(); Mode->ProfileId = TEXT("Mode"); Mode->Priority = 1; Mode->RequiredTags.AddTag(Tag);
	Catalog->Profiles = {Mode, Advance, Default, Class}; FProject_JUIContext Context;
	TestEqual(TEXT("Unknown class receives safe fallback"), Catalog->Resolve(Context), Default);
	Context.ClassId = TEXT("Warrior"); TestEqual(TEXT("Class selects its profile"), Catalog->Resolve(Context), Class);
	Context.AdvancementId = TEXT("Awakened"); TestEqual(TEXT("Advancement is more specific"), Catalog->Resolve(Context), Advance);
	Context.OwnedTags.AddTag(Tag); TestEqual(TEXT("Priority selects combat mode"), Catalog->Resolve(Context), Mode);
	Mode->BlockedTags.AddTag(Tag); TestEqual(TEXT("Blocked mode is excluded"), Catalog->Resolve(Context), Advance);
	Mode->BlockedTags.Reset(); Context.OwnedTags.Reset();
	auto* Tie = NewObject<UProject_JCharacterUIProfile>(); Tie->ProfileId = TEXT("AAA"); Tie->ClassId = Class->ClassId; Tie->AdvancementId = Advance->AdvancementId;
	Catalog->Profiles.Add(Tie); TestEqual(TEXT("Equal priority/specificity has a stable ID tie break"), Catalog->Resolve(Context), Tie);
	Algo::Reverse(Catalog->Profiles); TestEqual(TEXT("Asset array reorder does not change result"), Catalog->Resolve(Context), Tie);
	Tie->ProfileId = Advance->ProfileId; TestEqual(TEXT("Duplicate IDs fail closed"), Catalog->Resolve(Context), Class);
	Class->bOverrideSkills = true; Class->Skills = {{Tag, FText()}, {Tag, FText()}};
	TestEqual(TEXT("Duplicate input slots are rejected"), Catalog->Resolve(Context), Default);
	Class->Skills.Reset(); TestEqual(TEXT("Explicit empty override hides skills"), Catalog->Resolve(Context), Class);
	Catalog->Profiles.Reset(); TestNull(TEXT("Empty catalog falls back to native presentation"), Catalog->Resolve(Context));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJUIResourceTest, "ProjectJ.UI.Character.Resources",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJUIResourceTest::RunTest(const FString&)
{
	auto* ASC = NewObject<UProject_JAbilitySystemComponent>();
	auto* Attributes = NewObject<UProject_JAttributeSet>(ASC); ASC->AddAttributeSetSubobject(Attributes);
	TArray<FProject_JUIResourceDefinition> Definitions = {{FName(TEXT("Health")), FText(), UProject_JAttributeSet::GetHealthAttribute(), UProject_JAttributeSet::GetMaxHealthAttribute(), FLinearColor::Red}};
	Attributes->InitHealth(50); Attributes->InitMaxHealth(100);
	auto States = ProjectJUI::ReadResources(ASC, Definitions);
	TestEqual(TEXT("Resource comes from actual GAS attributes"), States.Num(), 1);
	if (States.Num()) TestEqual(TEXT("Correct percentage"), States[0].Fraction, 0.5f);
	Attributes->InitHealth(150); States = ProjectJUI::ReadResources(ASC, Definitions);
	if (States.Num()) TestEqual(TEXT("Over cap display safely clamps"), States[0].Fraction, 1.f);
	Attributes->InitMaxHealth(0); TestTrue(TEXT("Zero maximum hides the gauge"), ProjectJUI::ReadResources(ASC, Definitions).IsEmpty());
	TestTrue(TEXT("Missing attribute set does not invent a resource"), ProjectJUI::ReadResources(NewObject<UProject_JAbilitySystemComponent>(), Definitions).IsEmpty());
	TestTrue(TEXT("No ASC hides resources"), ProjectJUI::ReadResources(nullptr, Definitions).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJUILayoutIsolationTest, "ProjectJ.UI.Character.LayoutPersistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJUILayoutIsolationTest::RunTest(const FString&)
{
	auto* Save = NewObject<UProject_JUILayoutSave>(); const FGuid A = FGuid::NewGuid(), B = FGuid::NewGuid();
	const FString ADefault = Save->MakeKey(A, TEXT("Default")), AMode = Save->MakeKey(A, TEXT("Mode")), BDefault = Save->MakeKey(B, TEXT("Default"));
	TestTrue(TEXT("Unknown identity never has a shared save key"), Save->MakeKey(FGuid(), TEXT("Default")).IsEmpty());
	TestFalse(TEXT("Unknown identity refuses a write"), Save->Write(TEXT(""), FVector2D(0.5)));
	Save->Write(ADefault, FVector2D(0.1, 0.2)); Save->Write(AMode, FVector2D(0.3, 0.4)); Save->Write(BDefault, FVector2D(0.8, 0.9));
	TArray<uint8> Bytes; TestTrue(TEXT("Save serializes"), UGameplayStatics::SaveGameToMemory(Save, Bytes));
	auto* Loaded = Cast<UProject_JUILayoutSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!TestNotNull(TEXT("Save deserializes"), Loaded)) return false;
	FVector2D Position; Loaded->Read(ADefault, Position); TestEqual(TEXT("Character A layout"), Position, FVector2D(0.1, 0.2));
	Loaded->Read(AMode, Position); TestEqual(TEXT("Same character's other profile"), Position, FVector2D(0.3, 0.4));
	Loaded->Read(BDefault, Position); TestEqual(TEXT("Character B remains isolated"), Position, FVector2D(0.8, 0.9));
	Loaded->Write(ADefault, FVector2D(-3, 4)); Loaded->Read(ADefault, Position); TestEqual(TEXT("Off screen coordinates clamp"), Position, FVector2D(0, 1));
	TestFalse(TEXT("Non finite coordinates refuse persistence"), Loaded->Write(ADefault, FVector2D(std::numeric_limits<double>::quiet_NaN(), 0)));
	Loaded->Version = 99; TestFalse(TEXT("Unsupported schema not applied"), Loaded->Read(ADefault, Position));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJUIAssetContractTest, "ProjectJ.UI.Character.DefaultAssetContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJUIAssetContractTest::RunTest(const FString&)
{
	auto* TileClass = LoadClass<UProject_JItemWidget>(nullptr, TEXT("/Game/UI/WBP_ProjectJItemTile.WBP_ProjectJItemTile_C"));
	if (TestNotNull(TEXT("Tile BP exists in a fresh process"), TileClass)) TestTrue(TEXT("Tile CDO persists compact layout"), TileClass->GetDefaultObject<UProject_JItemWidget>()->bCompactTile);
	auto* Catalog = LoadObject<UProject_JUIProfileCatalog>(nullptr, TEXT("/Game/UI/DA_ProjectJUIProfiles.DA_ProjectJUIProfiles"));
	if (TestNotNull(TEXT("Default catalog loads"), Catalog))
	{
		auto* Profile = Catalog->Resolve(FProject_JUIContext());
		if (TestNotNull(TEXT("Default profile resolves"), Profile))
		{
			TestEqual(TEXT("Stable default layout identity"), Profile->ProfileId, FName(TEXT("Default")));
			TestFalse(TEXT("Default keeps real controller skills"), Profile->bOverrideSkills);
			TestFalse(TEXT("Default keeps real HP/MP attributes"), Profile->bOverrideResources);
		}
	}
	return true;
}
#endif
