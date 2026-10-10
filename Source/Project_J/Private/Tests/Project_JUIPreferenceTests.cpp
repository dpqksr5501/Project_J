#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/Project_JUILayoutSettings.h"
#include "UI/Project_JEquipmentComparison.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Kismet/GameplayStatics.h"
#include "GameplayEffect.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"

namespace
{
class FMemoryPreferences final : public FProject_JUILayoutStorage
{
  public:
	TMap<FString, TArray<uint8>> Files;
	bool bFailWrites = false;
	int32 Writes = 0;
	bool Exists(const FString &Slot) override { return Files.Contains(Slot); }
	UProject_JUILayoutSave *Load(const FString &Slot) override
	{
		const auto *Bytes = Files.Find(Slot);
		return Bytes && !Bytes->IsEmpty() ? Cast<UProject_JUILayoutSave>(UGameplayStatics::LoadGameFromMemory(*Bytes)) : nullptr;
	}
	bool Store(UProject_JUILayoutSave *Value, const FString &Slot) override
	{
		++Writes;
		if (bFailWrites) return false;
		TArray<uint8> Bytes;
		if (!UGameplayStatics::SaveGameToMemory(Value, Bytes)) return false;
		Files.Add(Slot, MoveTemp(Bytes)); return true;
	}
};
const FString PreferenceTestSlot = TEXT("ProjectJ_UI_Player_0");
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJUIPreferenceFailureTest, "ProjectJ.UI.Preferences.CoalescingFailureMergeAndBackup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJUIPreferenceFailureTest::RunTest(const FString &)
{
	auto Disk = MakeShared<FMemoryPreferences>();
	auto *Player = NewObject<ULocalPlayer>(GEngine); Player->SetControllerId(0);
	auto *A = NewObject<UProject_JUILayoutSettings>(Player); A->SetStorageForTesting(Disk);
	auto *B = NewObject<UProject_JUILayoutSettings>(Player); B->SetStorageForTesting(Disk);
	B->ReadKeys(); // A second local screen reads before the first saves.
	A->Write(TEXT("A"), FVector2D(.1, .2)); A->Write(TEXT("A"), FVector2D(.3, .4));
	TestEqual(TEXT("Burst writes are deferred"), Disk->Writes, 0);
	TestTrue(TEXT("Explicit flush succeeds"), A->FlushNow());
	B->Write(TEXT("B"), FVector2D(.5, .6)); TestTrue(TEXT("Other screen merges changed keys only"), B->FlushNow());
	FVector2D Value;
	TestTrue(TEXT("A survives B's save"), Disk->Load(PreferenceTestSlot)->Read(TEXT("A"), Value));
	TestEqual(TEXT("Last burst value saved"), Value, FVector2D(.3, .4));
	TestTrue(TEXT("Backup contains previous healthy A state"), Disk->Load(PreferenceTestSlot + TEXT("_Backup"))->Read(TEXT("A"), Value));
	Disk->bFailWrites = true;
	A->Write(TEXT("A"), FVector2D(.7, .8)); TestFalse(TEXT("Failed writes are reported"), A->FlushNow());
	TestEqual(TEXT("Failed state exposed to settings UI"), A->GetSaveState(), EProject_JUIPreferenceState::Failed);
	A->Read(TEXT("A"), Value); TestEqual(TEXT("Unsaved preference retained in memory"), Value, FVector2D(.7, .8));
	Disk->bFailWrites = false; TestTrue(TEXT("Manual retry succeeds"), A->FlushNow());
	TestTrue(TEXT("Retry preserves another player's field"), Disk->Load(PreferenceTestSlot)->Read(TEXT("B"), Value));
	Disk->Files.Add(PreferenceTestSlot, {}); // Corrupt primary. Keep a serialized healthy backup.
	auto *Recovered = NewObject<UProject_JUILayoutSettings>(Player); Recovered->SetStorageForTesting(Disk);
	TestTrue(TEXT("Healthy backup recovers preferences"), Recovered->Read(TEXT("B"), Value));
	TestEqual(TEXT("Recovery is visible"), Recovered->GetSaveState(), EProject_JUIPreferenceState::Recovered);
	TestTrue(TEXT("Recovered file can be repaired"), Recovered->FlushNow());
	A->Deinitialize(); B->Deinitialize(); Recovered->Deinitialize(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJUIPreferenceMigrationTest, "ProjectJ.UI.Preferences.MigrationAndFutureProtection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJUIPreferenceMigrationTest::RunTest(const FString &)
{
	auto *Old = NewObject<UProject_JUILayoutSave>(); Old->Version = 1;
	Old->MenuPositions.Add(TEXT("Good"), FVector2D(2, -.5));
	Old->MenuPositions.Add(TEXT(""), FVector2D(.5, .5));
	Old->HUDPreferences.Add(TEXT("Invalid"), {}); Old->InputKeys.Keys.Reset();
	TestTrue(TEXT("v1 accepted"), Old->Normalize());
	TestEqual(TEXT("Migrated schema"), Old->Version, UProject_JUILayoutSave::CurrentVersion);
	FVector2D Value; Old->Read(TEXT("Good"), Value);
	TestEqual(TEXT("Finite position clamped"), Value, FVector2D(1, 0));
	TestFalse(TEXT("Bad entry removed without losing good ones"), Old->MenuPositions.Contains(TEXT("")));
	TestTrue(TEXT("Invalid keys repaired"), Old->InputKeys.IsValid());
	TestTrue(TEXT("Invalid HUD record dropped"), Old->HUDPreferences.IsEmpty());
	auto Disk = MakeShared<FMemoryPreferences>(); Old->Version = 99; Disk->Store(Old, PreferenceTestSlot);
	const auto Original = Disk->Files[PreferenceTestSlot];
	auto *Player = NewObject<ULocalPlayer>(GEngine); Player->SetControllerId(0);
	auto *Settings = NewObject<UProject_JUILayoutSettings>(Player); Settings->SetStorageForTesting(Disk);
	Settings->Write(TEXT("Transient"), FVector2D(.2, .3));
	TestFalse(TEXT("Future format never overwritten"), Settings->FlushNow());
	TestEqual(TEXT("Future format state exposed"), Settings->GetSaveState(), EProject_JUIPreferenceState::NewerVersion);
	TestTrue(TEXT("Original bytes untouched"), Disk->Files[PreferenceTestSlot] == Original);
	TestTrue(TEXT("Session-only preference still usable"), Settings->Read(TEXT("Transient"), Value));
	Settings->Deinitialize(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJUIEquipmentPolicyTest, "ProjectJ.UI.Equipment.PolicyAwareComparison",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJUIEquipmentPolicyTest::RunTest(const FString &)
{
	auto *Candidate = NewObject<UProject_JEquipmentItemDefinition>();
	auto *Equipped = NewObject<UProject_JEquipmentItemDefinition>();
	FProject_JEquipmentStatModifier Attack; Attack.Value = 10;
	Candidate->StatModifiers = {Attack, Attack}; Equipped->StatModifiers = {Attack};
	auto Compare = FProject_JEquipmentComparison::Build(Candidate, Equipped);
	TestTrue(TEXT("No removable effect uses fixed fallback"), Compare.bCanCompare);
	TestEqual(TEXT("Duplicate stat bonuses accumulated"), Compare.Rows[0].Difference, 10.f);
	Candidate->EquipmentEffects.Add(UGameplayEffect::StaticClass());
	TestTrue(TEXT("Instant equipment effects ignored like runtime"), FProject_JEquipmentComparison::Build(Candidate, Equipped).bCanCompare);
	{
		TGuardValue<EGameplayEffectDurationType> Duration(GetMutableDefault<UGameplayEffect>()->DurationPolicy, EGameplayEffectDurationType::Infinite);
		Compare = FProject_JEquipmentComparison::Build(Candidate, Equipped);
		TestFalse(TEXT("Potential GE application suppresses invented exact difference"), Compare.bCanCompare);
		TestEqual(TEXT("Fallback clearly conditional"), Compare.CandidateMode, EProject_JEquipmentBonusPreview::ConditionalFallback);
		Candidate->StatApplicationPolicy = EProject_JEquipmentStatApplicationPolicy::GameplayEffectsOnly;
		Compare = FProject_JEquipmentComparison::Build(Candidate, Equipped);
		TestFalse(TEXT("Effect-only candidate cannot imply removal-only negative bonus"), Compare.bCanCompare);
		TestEqual(TEXT("Ignored fixed modifiers not advertised"), Compare.Rows[0].Candidate, 0.f);
		Candidate->StatApplicationPolicy = EProject_JEquipmentStatApplicationPolicy::StatModifiersOnly;
		TestTrue(TEXT("Stat-only ignores configured effects"), FProject_JEquipmentComparison::Build(Candidate, Equipped).bCanCompare);
	}
	Compare = FProject_JEquipmentComparison::Build(nullptr, Equipped);
	TestEqual(TEXT("Empty replacement includes lost equipped bonus"), Compare.Rows[0].Difference, -10.f);
	return true;
}
#endif
