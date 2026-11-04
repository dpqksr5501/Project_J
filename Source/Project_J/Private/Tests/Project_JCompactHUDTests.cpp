#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Game/Project_JPlayerState.h"
#include "Game/Project_JQuestComponent.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Inventory/Project_JConsumableDefinition.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "UI/Project_JInventoryViewModel.h"
#include "UI/Project_JUIModels.h"
#include "UI/Project_JMinimapWidget.h"
#include "UI/Project_JUILayoutSettings.h"
#include "UI/Project_JStatusEffects.h"
#include "GameplayEffect.h"
#include "UObject/UnrealType.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

namespace ProjectJCompactTests
{
struct FWorld
{
	UWorld *World = UWorld::CreateWorld(EWorldType::Game, false);
	AProject_JPlayerState *State;
	UProject_JProgressionComponent *Progression;
	FWorld()
	{
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		State = World->SpawnActor<AProject_JPlayerState>();
		State->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(State, State);
		auto *Attributes = State->GetProjectJAttributeSet();
		State->GetProjectJAbilitySystemComponent()->AddAttributeSetSubobject(Attributes);
		Attributes->InitMaxHealth(100);
		Attributes->InitHealth(50);
		Attributes->InitMaxMana(100);
		Attributes->InitMana(30);
		Progression = State->FindComponentByClass<UProject_JProgressionComponent>();
	}
	~FWorld()
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
};
} // namespace ProjectJCompactTests
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJExperienceTest, "ProjectJ.UI.Compact.ExperienceRewardsAndSnapshots",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJExperienceTest::RunTest(const FString &)
{
	ProjectJCompactTests::FWorld W;
	W.Progression->InitializeDefaults(nullptr, nullptr, 1);
	TestFalse(TEXT("Negative reward rejected"), W.Progression->GrantExperience(-1));
	TestTrue(TEXT("Trusted reward"), W.Progression->GrantExperience(450));
	TestEqual(TEXT("Multiple levels"), W.Progression->GetState().Level, 3);
	TestEqual(TEXT("Remainder retained"), W.Progression->GetExperience(), 150LL);
	TestFalse(TEXT("Overflow rejected"), W.Progression->GrantExperience(MAX_int64));
	auto Snapshot = W.Progression->CaptureSnapshot();
	ProjectJCompactTests::FWorld Other;
	TestTrue(TEXT("v2 restore"), Other.Progression->RestoreSnapshot(Snapshot, nullptr, {}));
	TestEqual(TEXT("Restored XP"), Other.Progression->GetExperience(), 150LL);
	TestFalse(TEXT("Repeated restore rejected"), Other.Progression->RestoreSnapshot(Snapshot, nullptr, {}));
	ProjectJCompactTests::FWorld Invalid;
	Snapshot.Experience = 300;
	TestFalse(TEXT("Unnormalized XP rejected before mutation"),
			  Invalid.Progression->RestoreSnapshot(Snapshot, nullptr, {}));
	TestEqual(TEXT("Invalid restore leaves level"), Invalid.Progression->GetState().Level, 1);
	Snapshot.SchemaVersion = 1;
	Snapshot.Experience = 0;
	TestTrue(TEXT("Old snapshot accepted"), Invalid.Progression->RestoreSnapshot(Snapshot, nullptr, {}));
	W.Progression->MaximumLevel = 3;
	TestFalse(TEXT("Cap has no XP reward"), W.Progression->GrantExperience(1));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJConsumableTest, "ProjectJ.UI.Compact.ConsumableAuthorityAndCooldown",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJConsumableTest::RunTest(const FString &)
{
	ProjectJCompactTests::FWorld W;
	auto *Def = NewObject<UProject_JConsumableDefinition>(W.State);
	Def->ItemId = TEXT("Recovery");
	Def->MaxStackCount = 20;
	Def->RestoreHealth = 25;
	Def->RestoreMana = 20;
	Def->CooldownSeconds = 10;
	auto *Inventory = W.State->GetInventoryComponent();
	const auto Item = Inventory->AddItemDefinition(Def, 3);
	TestEqual(TEXT("Restore and consume"), Inventory->TryUseItem(Item.InstanceId), EProject_JItemUseResult::Success);
	TestEqual(TEXT("Real GAS health"), W.State->GetProjectJAttributeSet()->GetHealth(), 75.f);
	TestEqual(TEXT("Real GAS mana"), W.State->GetProjectJAttributeSet()->GetMana(), 50.f);
	FProject_JItemInstanceData Current;
	Inventory->FindItemInstance(Item.InstanceId, Current);
	TestEqual(TEXT("Exactly one consumed"), Current.StackCount, 2);
	TestEqual(TEXT("Shared cooldown enforced"), Inventory->TryUseItem(Item.InstanceId),
			  EProject_JItemUseResult::Cooldown);
	Inventory->FindItemInstance(Item.InstanceId, Current);
	TestEqual(TEXT("Cooldown cannot consume"), Current.StackCount, 2);
	Inventory->SetItemInstanceLocked(Item.InstanceId, true);
	TestEqual(TEXT("Locked rejected"), Inventory->TryUseItem(Item.InstanceId), EProject_JItemUseResult::Locked);
	TestEqual(TEXT("Unknown identity rejected"), Inventory->TryUseItem(FGuid::NewGuid()),
			  EProject_JItemUseResult::Invalid);
	ProjectJCompactTests::FWorld Full;
	Full.State->GetProjectJAttributeSet()->InitHealth(100);
	Full.State->GetProjectJAttributeSet()->InitMana(100);
	const auto FullItem = Full.State->GetInventoryComponent()->AddItemDefinition(Def, 2);
	TestEqual(TEXT("Full resources do not waste item"),
			  Full.State->GetInventoryComponent()->TryUseItem(FullItem.InstanceId), EProject_JItemUseResult::NoEffect);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJQuestRewardTest, "ProjectJ.UI.Compact.QuestRewardReplayAndRestore",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJQuestRewardTest::RunTest(const FString &)
{
	ProjectJCompactTests::FWorld W;
	W.Progression->InitializeDefaults(nullptr, nullptr, 1);
	auto *Quests = W.State->FindComponentByClass<UProject_JQuestComponent>();
	auto *Def = NewObject<UProject_JQuestDefinition>(W.State);
	Def->QuestId = TEXT("TestQuest");
	Def->ObjectiveEvent = TEXT("Interact");
	Def->RequiredCount = 2;
	Def->RewardExperience = 100;
	Quests->Definitions = {Def};
	TestTrue(TEXT("Accept registered quest"), Quests->Accept(Def->QuestId));
	TestFalse(TEXT("Unknown cannot accept"), Quests->Accept(TEXT("Unknown")));
	TestFalse(TEXT("Incomplete cannot claim"), Quests->Claim(Def->QuestId));
	Quests->RecordObjective(TEXT("Wrong"), 20);
	TestEqual(TEXT("Unrelated objective ignored"), Quests->States[0].Count, 0);
	Quests->RecordObjective(TEXT("Interact"), 3);
	TestEqual(TEXT("Count clamps"), Quests->States[0].Count, 2);
	bool bReentrant = true;
	W.Progression->OnChanged.AddLambda([&]() { bReentrant = Quests->Claim(Def->QuestId); });
	TestTrue(TEXT("Complete claim succeeds"), Quests->Claim(Def->QuestId));
	TestFalse(TEXT("Delegate replay rejected"), bReentrant);
	TestFalse(TEXT("RPC replay cannot pay twice"), Quests->Claim(Def->QuestId));
	TestEqual(TEXT("Reward paid once"), W.Progression->GetState().Level, 2);
	auto Snapshot = Quests->CaptureSnapshot();
	ProjectJCompactTests::FWorld Other;
	auto *OtherQuests = Other.State->FindComponentByClass<UProject_JQuestComponent>();
	OtherQuests->Definitions = {Def};
	TestTrue(TEXT("Quest restore"), OtherQuests->RestoreSnapshot(Snapshot));
	TestFalse(TEXT("Restored claim stays claimed"), OtherQuests->Claim(Def->QuestId));
	W.Progression->OnChanged.RemoveAll(Quests);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJHUDPreferencesTest, "ProjectJ.UI.Compact.PreferencesSaveValidation",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJHUDPreferencesTest::RunTest(const FString &)
{
	FProject_JHUDPreferences Prefs;
	Prefs.Slots.SetNum(10);
	Prefs.Slots[5].Kind = EProject_JQuickSlotKind::Item;
	Prefs.Slots[5].ItemId = TEXT("Potion");
	TestTrue(TEXT("Mixed bindings valid"), Prefs.IsValid());
	auto *Save = NewObject<UProject_JUILayoutSave>();
	const FString Key = UProject_JUILayoutSave::MakeKey(FGuid::NewGuid(), TEXT("Default"));
	Save->HUDPreferences.Add(Key, Prefs);
	TArray<uint8> Bytes;
	TestTrue(TEXT("Serialize settings"), UGameplayStatics::SaveGameToMemory(Save, Bytes));
	auto *Restored = Cast<UProject_JUILayoutSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
	TestNotNull(TEXT("Restored settings"), Restored);
	if (Restored)
		TestEqual(TEXT("Stable item definition ref"), Restored->HUDPreferences[Key].Slots[5].ItemId,
				  FName(TEXT("Potion")));
	Prefs.Slots[5].ItemId = NAME_None;
	TestFalse(TEXT("Missing item ref rejected"), Prefs.IsValid());
	Prefs.Slots[5] = FProject_JQuickSlotBinding();
	Prefs.Scale = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("NaN rejected"), Prefs.IsValid());
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMinimapCoordinatesTest, "ProjectJ.UI.Compact.MapCoordinates",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMinimapCoordinatesTest::RunTest(const FString &)
{
	FVector2D UV;
	TestTrue(TEXT("Valid bounds"),
			 UProject_JMapDefinition::WorldToUV(FVector::ZeroVector, FVector2D::ZeroVector, FVector2D(100, 200), UV));
	TestTrue(TEXT("Origin is centre"), UV.Equals(FVector2D(0.5, 0.5)));
	UProject_JMapDefinition::WorldToUV(FVector(100, 200, 0), FVector2D::ZeroVector, FVector2D(100, 200), UV);
	TestTrue(TEXT("North up east right"), UV.Equals(FVector2D(1, 0)));
	TestFalse(TEXT("Degenerate bounds rejected"),
			  UProject_JMapDefinition::WorldToUV(FVector::ZeroVector, FVector2D::ZeroVector, FVector2D(0, 100), UV));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJInventoryFilterTest, "ProjectJ.UI.Compact.InventoryFiltering",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJInventoryFilterTest::RunTest(const FString &)
{
	ProjectJCompactTests::FWorld W;
	auto *Potion = NewObject<UProject_JConsumableDefinition>(W.State);
	Potion->ItemId = TEXT("Potion");
	Potion->ItemName = FText::FromString(TEXT("회복 물약"));
	Potion->MaxStackCount = 20;
	auto *Helmet = NewObject<UProject_JEquipmentItemDefinition>(W.State);
	Helmet->ItemId = TEXT("Helmet");
	Helmet->EquipmentSlot = EProject_JEquipmentSlot::Head;
	W.State->GetInventoryComponent()->AddItemDefinition(Potion, 2);
	W.State->GetInventoryComponent()->AddItemDefinition(Helmet);
	auto *Model = NewObject<UProject_JInventoryViewModel>();
	Model->Bind(W.State->GetInventoryComponent(), W.State->GetEquipmentManagerComponent());
	Model->SetFilter(TEXT("물약"), 0, true);
	TestEqual(TEXT("Localized name search"), Model->VisibleEntries.Num(), 1);
	Model->SetFilter(TEXT("POTION"), 0, false);
	TestEqual(TEXT("Case-insensitive ID search"), Model->VisibleEntries.Num(), 1);
	Model->SetFilter(TEXT(""), 1, false);
	TestEqual(TEXT("Gear filter"), Model->VisibleEntries.Num(), 1);
	TestEqual(TEXT("Server projection retained"), Model->InventoryEntries.Num(), 2);
	Model->SetFilter(TEXT(""), 2, false);
	TestEqual(TEXT("Consumable filter"), Model->VisibleEntries.Num(), 1);
	Model->Unbind();
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJLargeInventoryTest, "ProjectJ.UI.Compact.LargeProjectionStability",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJLargeInventoryTest::RunTest(const FString &)
{
	ProjectJCompactTests::FWorld W;
	auto *Def = NewObject<UProject_JConsumableDefinition>(W.State);
	Def->ItemId = TEXT("BulkPotion");
	Def->ItemName = FText::FromString(TEXT("Bulk"));
	for (int32 I = 0; I < 1000; ++I)
		W.State->GetInventoryComponent()->AddItemDefinition(Def);
	auto *Model = NewObject<UProject_JInventoryViewModel>();
	Model->Bind(W.State->GetInventoryComponent(), W.State->GetEquipmentManagerComponent());
	TestEqual(TEXT("Large owner projection"), Model->VisibleEntries.Num(), 1000);
	const auto *First = Model->InventoryEntries[0].Get();
	Model->Refresh();
	TestTrue(TEXT("No row UObject churn"), Model->InventoryEntries[0].Get() == First);
	Model->SetFilter(TEXT("no_match"), 0, true);
	TestTrue(TEXT("Empty filter projection"), Model->VisibleEntries.IsEmpty());
	TestEqual(TEXT("Items retained"), Model->InventoryEntries.Num(), 1000);
	Model->Unbind();
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJBagAtomicTest, "ProjectJ.UI.Extended.AtomicBagEdits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJBagAtomicTest::RunTest(const FString &)
{
	ProjectJCompactTests::FWorld W;
	auto *Inventory = W.State->GetInventoryComponent();
	auto *Def = NewObject<UProject_JConsumableDefinition>(W.State);
	Def->MaxStackCount = 20;
	auto Source = Inventory->AddItemDefinition(Def, 15, 2);
	auto Target = Inventory->AddItemDefinition(Def, 8, 2);
	auto Request = [](EProject_JBagOperation Op, const FProject_JItemInstanceData &A, const FProject_JItemInstanceData &B, int32 Count)
	{
		FProject_JBagRequest R;
		R.Operation = Op; R.Source = A.InstanceId; R.Target = B.InstanceId; R.Count = Count;
		R.ExpectedSourceCount = A.StackCount; R.ExpectedTargetCount = B.StackCount;
		R.ExpectedSourceOrder = A.BagOrder; R.ExpectedTargetOrder = B.BagOrder;
		return R;
	};
	TestEqual(TEXT("Invalid split cannot duplicate"), Inventory->TryBagChange(Request(EProject_JBagOperation::Split, Source, {}, 15)), EProject_JBagResult::Invalid);
	auto Split = Request(EProject_JBagOperation::Split, Source, {}, 5);
	TestEqual(TEXT("Atomic split"), Inventory->TryBagChange(Split), EProject_JBagResult::Success);
	TestEqual(TEXT("Stale split rejected"), Inventory->TryBagChange(Split), EProject_JBagResult::Changed);
	int32 Total = 0;
	FProject_JItemInstanceData NewStack;
	for (const auto &Item : Inventory->GetItemInstances()) { Total += Item.StackCount; if (Item.InstanceId != Source.InstanceId && Item.InstanceId != Target.InstanceId) NewStack = Item; }
	TestEqual(TEXT("No units minted"), Total, 23);
	TestTrue(TEXT("New stack has own identity and preserves level"), NewStack.InstanceId.IsValid() && NewStack.ItemLevel == 2);
	Inventory->FindItemInstance(Source.InstanceId, Source);
	TestEqual(TEXT("Merge respects cap"), Inventory->TryBagChange(Request(EProject_JBagOperation::Merge, Source, Target, 10)), EProject_JBagResult::Success);
	TestFalse(TEXT("Fully merged source removed"), Inventory->HasItemInstance(Source.InstanceId));
	Inventory->FindItemInstance(Target.InstanceId, Target);
	TestEqual(TEXT("Target count"), Target.StackCount, 18);
	TestEqual(TEXT("Partial merge"), Inventory->TryBagChange(Request(EProject_JBagOperation::Merge, NewStack, Target, 2)), EProject_JBagResult::Success);
	Inventory->FindItemInstance(NewStack.InstanceId, NewStack);
	Inventory->FindItemInstance(Target.InstanceId, Target);
	TestEqual(TEXT("Remainder retained"), NewStack.StackCount, 3);
	TestEqual(TEXT("Full stack rejected"), Inventory->TryBagChange(Request(EProject_JBagOperation::Merge, NewStack, Target, 1)), EProject_JBagResult::Full);
	auto SwapRequest = Request(EProject_JBagOperation::Swap, NewStack, Target, 0);
	Inventory->RequestBagChange(FGuid(1, 2, 3, 4), SwapRequest);
	Inventory->RequestBagChange(FGuid(1, 2, 3, 4), SwapRequest);
	FProject_JItemInstanceData After;
	Inventory->FindItemInstance(NewStack.InstanceId, After);
	TestEqual(TEXT("RPC replay swaps only once"), After.BagOrder, Target.BagOrder);
	TestEqual(TEXT("Stale order rejected"), Inventory->TryBagChange(SwapRequest), EProject_JBagResult::Changed);
	Inventory->FindItemInstance(Target.InstanceId, Target);
	Inventory->SetItemInstanceLocked(Target.InstanceId, true);
	TestEqual(TEXT("Locked destination rejected"), Inventory->TryBagChange(Request(EProject_JBagOperation::Swap, After, Target, 0)), EProject_JBagResult::Locked);
	Inventory->SetItemInstanceLocked(Target.InstanceId, false);
	auto DifferentLevel = Inventory->AddItemDefinition(Def, 1, 1);
	TestEqual(TEXT("Different level not merged"), Inventory->TryBagChange(Request(EProject_JBagOperation::Merge, After, DifferentLevel, 1)), EProject_JBagResult::Incompatible);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJKeySettingsTest, "ProjectJ.UI.Extended.KeyboardSaveAndConflicts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJKeySettingsTest::RunTest(const FString &)
{
	FProject_JUIKeys Keys;
	TestTrue(TEXT("Default layout valid"), Keys.IsValid());
	TestTrue(TEXT("Rebind collision swaps"), Keys.Rebind(0, EKeys::I));
	TestEqual(TEXT("Menu receives displaced key"), Keys.Keys[10], EKeys::One);
	TestFalse(TEXT("Movement reserved"), Keys.Rebind(0, EKeys::W));
	TestFalse(TEXT("Escape reserved"), Keys.Rebind(0, EKeys::Escape));
	TestFalse(TEXT("Mouse reserved"), Keys.Rebind(0, EKeys::LeftMouseButton));
	TestFalse(TEXT("Bad index rejected"), Keys.Rebind(14, EKeys::F5));
	auto *Save = NewObject<UProject_JUILayoutSave>();
	Save->InputKeys = Keys;
	TArray<uint8> Bytes;
	TestTrue(TEXT("Serialize keyboard settings"), UGameplayStatics::SaveGameToMemory(Save, Bytes));
	auto *Restored = Cast<UProject_JUILayoutSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
	TestTrue(TEXT("Restore keyboard settings"), Restored && Restored->InputKeys.IsValid() && Restored->InputKeys.Keys[0] == EKeys::I);
	Keys.Keys[1] = Keys.Keys[0];
	TestFalse(TEXT("Invalid duplicate save rejected"), Keys.IsValid());
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJStatusEffectTest, "ProjectJ.UI.Extended.StatusEffectLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJStatusEffectTest::RunTest(const FString &)
{
	ProjectJCompactTests::FWorld W;
	auto *ASC = W.State->GetProjectJAbilitySystemComponent();
	auto *Model = NewObject<UProject_JStatusEffectModel>();
	int32 Publications = 0;
	Model->OnChanged.AddLambda([&Publications] { ++Publications; });
	Model->Bind(ASC);
	auto *Hidden = NewObject<UGameplayEffect>();
	Hidden->DurationPolicy = EGameplayEffectDurationType::Infinite;
	ASC->ApplyGameplayEffectToSelf(Hidden, 1, ASC->MakeEffectContext());
	TestTrue(TEXT("Internal effects hidden"), Model->States.IsEmpty());
	TestTrue(TEXT("Internal effects acquire no status observers"), Model->ObservedEffects.IsEmpty());
	TestFalse(TEXT("Internal effects start no UI timer"), W.World->GetTimerManager().TimerExists(Model->Timer));
	auto *Effect = NewObject<UGameplayEffect>();
	Effect->DurationPolicy = EGameplayEffectDurationType::HasDuration;
	Effect->DurationMagnitude = FScalableFloat(10.f);
	auto &UI = Effect->FindOrAddComponent<UProject_JStatusEffectUIData>();
	UI.DisplayName = FText::FromString(TEXT("Recovery"));
	UI.Description = FText::FromString(TEXT("Player facing effect"));
	const auto Handle = ASC->ApplyGameplayEffectToSelf(Effect, 1, ASC->MakeEffectContext());
	TestEqual(TEXT("Intentional status visible"), Model->States.Num(), 1);
	if (!Model->States.IsEmpty()) TestTrue(TEXT("Finite remaining time"), Model->States[0].Remaining > 0 && Model->States[0].Remaining <= 10);
	TestTrue(TEXT("Visible finite status starts a countdown timer"), W.World->GetTimerManager().TimerExists(Model->Timer));
	const float BeforeTimeChange = Model->States.IsEmpty() ? 0.f : Model->States[0].Remaining;
	ASC->ModifyActiveEffectStartTime(Handle, -1.f);
	if (!Model->States.IsEmpty()) TestTrue(TEXT("Native time change updates the view without manual refresh"), Model->States[0].Remaining < BeforeTimeChange);
	const int32 FinitePublication = Publications;
	const float PreviousRemaining = Model->States.IsEmpty() ? 0.f : Model->States[0].Remaining;
	W.World->Tick(LEVELTICK_All, .25f);
	Model->Refresh();
	TestTrue(TEXT("Exact finite remaining-time changes still publish"), Publications > FinitePublication);
	if (!Model->States.IsEmpty()) TestTrue(TEXT("Real world time decreases the finite UI countdown"), Model->States[0].Remaining < PreviousRemaining);
	ASC->RemoveActiveGameplayEffect(Handle);
	TestTrue(TEXT("Removed effect disappears"), Model->States.IsEmpty());
	TestFalse(TEXT("Last finite removal retires the countdown timer"), W.World->GetTimerManager().TimerExists(Model->Timer));

	auto *Infinite = NewObject<UGameplayEffect>();
	Infinite->DurationPolicy = EGameplayEffectDurationType::Infinite;
	// This installed engine does not export the editor SetStackingType symbol.
	// Author the transient definition's editable enum, then use normal GAS stacking.
	auto *Stacking = FindFProperty<FEnumProperty>(UGameplayEffect::StaticClass(), TEXT("StackingType"));
	if (!TestNotNull(TEXT("Stacking rule remains an editable definition property"), Stacking)) { Model->Unbind(); return false; }
	Stacking->GetUnderlyingProperty()->SetIntPropertyValue(Stacking->ContainerPtrToValuePtr<void>(Infinite),
		int64(EGameplayEffectStackingType::AggregateByTarget));
	TestTrue(TEXT("Transient definition has the authored stacking rule"), Infinite->GetStackingType() == EGameplayEffectStackingType::AggregateByTarget);
	Infinite->StackLimitCount = 3;
	auto &InfiniteUI = Infinite->FindOrAddComponent<UProject_JStatusEffectUIData>();
	InfiniteUI.DisplayName = FText::FromString(TEXT("Persistent status"));
	InfiniteUI.Description = FText::FromString(TEXT("Stable owner UI"));
	auto InfiniteHandle = ASC->ApplyGameplayEffectToSelf(Infinite, 1, ASC->MakeEffectContext());
	if (!TestTrue(TEXT("Real infinite GAS effect remains active"), InfiniteHandle.IsValid()) ||
		!TestEqual(TEXT("Infinite owner status is visible"), Model->States.Num(), 1)) { Model->Unbind(); return false; }
	TestFalse(TEXT("Infinite status uses native changes with no idle timer"), W.World->GetTimerManager().TimerExists(Model->Timer));
	TestEqual(TEXT("Only UI effects are observed"), Model->ObservedEffects.Num(), 1);
	const int32 StablePublication = Publications;
	for (int32 RefreshIndex = 0; RefreshIndex < 3; ++RefreshIndex) Model->Refresh();
	TestEqual(TEXT("Identical infinite views do not notify the HUD again"), Publications, StablePublication);
	W.World->Tick(LEVELTICK_All, .3f);
	Model->Refresh();
	TestEqual(TEXT("Idle infinite status omits duplicate publication"), Publications, StablePublication);
	const auto StackedHandle = ASC->ApplyGameplayEffectToSelf(Infinite, 1, ASC->MakeEffectContext());
	TestEqual(TEXT("Normal GAS stacking retains the status handle"), StackedHandle, InfiniteHandle);
	if (!Model->States.IsEmpty()) TestEqual(TEXT("Normal GAS stack change reaches the view"), Model->States[0].StackCount, 2);
	TestTrue(TEXT("Stack changes publish"), Publications > StablePublication);
	const int32 StackedPublication = Publications;
	Model->Refresh();
	TestEqual(TEXT("Repeated stacked view is silent"), Publications, StackedPublication);
	InfiniteHandle = ASC->SetActiveGameplayEffectInhibit(MoveTemp(InfiniteHandle), true, true);
	TestTrue(TEXT("Inhibited status disappears"), Model->States.IsEmpty());
	TestTrue(TEXT("Inhibited status keeps its change observers"), Model->ObservedEffects.Contains(InfiniteHandle));
	TestFalse(TEXT("Inhibited status needs no idle timer"), W.World->GetTimerManager().TimerExists(Model->Timer));
	TestTrue(TEXT("Inhibition changes publish"), Publications > StackedPublication);
	const int32 InhibitedPublication = Publications;
	InfiniteHandle = ASC->SetActiveGameplayEffectInhibit(MoveTemp(InfiniteHandle), false, true);
	TestEqual(TEXT("Uninhibited status returns"), Model->States.Num(), 1);
	TestTrue(TEXT("Uninhibition changes publish"), Publications > InhibitedPublication);
	const int32 MetadataPublication = Publications;
	InfiniteUI.DisplayName = FText::FromString(TEXT("Updated status"));
	InfiniteUI.Description = FText::FromString(TEXT("Updated owner tooltip"));
	InfiniteUI.Icon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture")));
	InfiniteUI.bDebuff = true;
	InfiniteUI.Priority = 7;
	Model->Refresh();
	TestTrue(TEXT("Displayed metadata changes publish"), Publications > MetadataPublication);
	if (!Model->States.IsEmpty()) TestTrue(TEXT("Updated metadata reaches the current view"), Model->States[0].Name.EqualTo(InfiniteUI.DisplayName)
		&& Model->States[0].Description.EqualTo(InfiniteUI.Description) && Model->States[0].Icon == InfiniteUI.Icon
		&& Model->States[0].bDebuff && Model->States[0].Priority == 7);
	auto *ShortEffect = NewObject<UGameplayEffect>();
	ShortEffect->DurationPolicy = EGameplayEffectDurationType::HasDuration;
	ShortEffect->DurationMagnitude = FScalableFloat(.1f);
	ShortEffect->FindOrAddComponent<UProject_JStatusEffectUIData>().DisplayName = FText::FromString(TEXT("Expiring status"));
	const auto ExpiringHandle = ASC->ApplyGameplayEffectToSelf(ShortEffect, 1, ASC->MakeEffectContext());
	TestEqual(TEXT("Finite and infinite statuses coexist"), Model->States.Num(), 2);
	const int32 BeforeExpiryPublication = Publications;
	W.World->Tick(LEVELTICK_All, .2f);
	Model->Refresh();
	TestFalse(TEXT("Elapsed finite effect leaves the view"), Model->States.ContainsByPredicate([ExpiringHandle](const auto &State) { return State.Handle == ExpiringHandle; }));
	TestTrue(TEXT("Expiry changes publish"), Publications > BeforeExpiryPublication);
	ProjectJCompactTests::FWorld Other;
	const int32 BeforeRebindPublication = Publications;
	Model->Bind(Other.State->GetProjectJAbilitySystemComponent());
	if (auto* Delegate = ASC->OnGameplayEffectStackChangeDelegate(InfiniteHandle))
		TestFalse(TEXT("Retired source stack observer is detached"), Delegate->IsBoundToObject(Model));
	if (auto* Delegate = ASC->OnGameplayEffectTimeChangeDelegate(InfiniteHandle))
		TestFalse(TEXT("Retired source time observer is detached"), Delegate->IsBoundToObject(Model));
	if (auto* Delegate = ASC->OnGameplayEffectInhibitionChangedDelegate(InfiniteHandle))
		TestFalse(TEXT("Retired source inhibition observer is detached"), Delegate->IsBoundToObject(Model));
	TestTrue(TEXT("ASC replacement still publishes the explicit reset"), Publications > BeforeRebindPublication);
	const int32 AfterRebindPublication = Publications;
	ASC->ApplyGameplayEffectToSelf(Effect, 1, ASC->MakeEffectContext());
	TestTrue(TEXT("Old source no longer updates model"), Model->States.IsEmpty());
	TestEqual(TEXT("Retired source produces no status publication"), Publications, AfterRebindPublication);
	Model->Unbind();
	TestEqual(TEXT("Explicit Unbind publishes even an empty reset"), Publications, AfterRebindPublication + 1);
	return true;
}

namespace ProjectJCompactTests
{
class FStatusCountdownCommand : public IAutomationLatentCommand
{
public:
	explicit FStatusCountdownCommand(FAutomationTestBase* InTest) : Test(InTest), Model(NewObject<UProject_JStatusEffectModel>())
	{
		auto* Source = W.State->GetProjectJAbilitySystemComponent();
		Model->Bind(Source);
		auto* Effect = NewObject<UGameplayEffect>();
		Effect->DurationPolicy = EGameplayEffectDurationType::HasDuration;
		Effect->DurationMagnitude = FScalableFloat(10.f);
		Effect->FindOrAddComponent<UProject_JStatusEffectUIData>().DisplayName = FText::FromString(TEXT("Countdown"));
		Handle = Source->ApplyGameplayEffectToSelf(Effect, 1, Source->MakeEffectContext());
		InitialRemaining = Model->States.IsEmpty() ? 0.f : Model->States[0].Remaining;
		Model->OnChanged.AddLambda([this] { ++Publications; });
	}
	~FStatusCountdownCommand() { Model->Unbind(); }
	bool Update() override
	{
		// Separate latent updates advance actual engine frames, allowing the real
		// world timer to fire. No direct Refresh or synthetic delegate broadcast.
		W.World->Tick(LEVELTICK_All, Step < 3 ? .3f : .6f);
		if (++Step < 3) return false;
		if (Step == 3)
		{
			Test->TestTrue(TEXT("Native countdown timer publishes without a GAS change"), Publications > 0);
			Test->TestTrue(TEXT("Countdown consumes current world time without explicit refresh"),
				Model->States.Num() == 1 && Model->States[0].Remaining < InitialRemaining);
			Handle = W.State->GetProjectJAbilitySystemComponent()->SetActiveGameplayEffectInhibit(MoveTemp(Handle), true, true);
			IdlePublications = Publications;
			return false;
		}
		Test->TestTrue(TEXT("Hidden finite status remains applied while countdown is idle"),
			W.State->GetProjectJAbilitySystemComponent()->GetActiveGameplayEffect(Handle) && Model->States.IsEmpty());
		Test->TestEqual(TEXT("Inhibited countdown publishes no idle updates"), Publications, IdlePublications);
		Handle = W.State->GetProjectJAbilitySystemComponent()->SetActiveGameplayEffectInhibit(MoveTemp(Handle), false, true);
		Test->TestTrue(TEXT("Native uninhibition immediately restores the finite view"), Model->States.Num() == 1);
		return true;
	}
private:
	FAutomationTestBase* Test;
	FWorld W;
	TStrongObjectPtr<UProject_JStatusEffectModel> Model;
	FActiveGameplayEffectHandle Handle;
	int32 Step = 0, Publications = 0, IdlePublications = 0;
	float InitialRemaining = 0;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJStatusCountdownTest, "ProjectJ.UI.Extended.StatusCountdownEvents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJStatusCountdownTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(ProjectJCompactTests::FStatusCountdownCommand(this));
	return true;
}
#endif
