#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "Animation/AnimSequence.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchNormalizationSet.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJBackwardRefaceData, "ProjectJ.BackwardReface.ProductionData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJBackwardRefaceData::RunTest(const FString&)
{
	const auto* Set = LoadObject<UProject_JMotionMatchingAssetSet>(nullptr,
		TEXT("/Game/DataAssetSets/Animation_Profiles/Combat_MMProfile/DA_Player_Combat_Strafe.DA_Player_Combat_Strafe"));
	if (!TestNotNull(TEXT("Production Combat AssetSet"), Set)) return false;
	auto* Cycle = Set->RunDatabases.Cycle.Get();
	auto* Reface = Set->RunDatabases.BackwardReface.Get();
	auto* ScopedCycle = Set->RunDatabases.BackwardRefaceCycle.Get();
	if (!TestNotNull(TEXT("Production Run Cycle"), Cycle) || !TestNotNull(TEXT("Installed Reface PSD"), Reface) ||
		!TestNotNull(TEXT("Installed scoped Cycle PSD"), ScopedCycle) || !TestNotNull(TEXT("Production Schema"), Cycle->Schema.Get()) ||
		!TestNotNull(TEXT("Existing normalization"), Cycle->NormalizationSet.Get()) ||
		!TestNotNull(TEXT("Reface normalization"), Reface->NormalizationSet.Get())) return false;

	TestEqual(TEXT("Original Run normalization membership remains exactly three"), Cycle->NormalizationSet->Databases.Num(), 3);
	TestEqual(TEXT("Original Cycle is still the base Run family"), Cycle->GetPathName(),
		FString(TEXT("/Game/Animation_Logic/PSD/PSD_Player_Combat_Locomotion/PSD_Combat_Run_Cycle.PSD_Combat_Run_Cycle")));
	TestTrue(TEXT("Reface uses isolated shared statistics"), Reface->NormalizationSet == ScopedCycle->NormalizationSet &&
		Reface->NormalizationSet != Cycle->NormalizationSet);
	TestEqual(TEXT("Only scoped Cycle and Reface share the new normalization"), Reface->NormalizationSet->Databases.Num(), 2);
	TestTrue(TEXT("Shared statistics include both searched databases"),
		Reface->NormalizationSet->Databases.Contains(ScopedCycle) && Reface->NormalizationSet->Databases.Contains(Reface));
	TestTrue(TEXT("Reface and scoped Cycle share exact production features"), Reface->Schema == Cycle->Schema && ScopedCycle->Schema == Cycle->Schema);
	if (!TestEqual(TEXT("Only two authored Reface entries"), Reface->GetNumAnimationAssets(), 2)) return false;

	const auto* Property = FindFProperty<FArrayProperty>(UPoseSearchDatabase::StaticClass(), TEXT("DatabaseAnimationAssets"));
	if (!TestNotNull(TEXT("Database entry reflection is available"), Property)) return false;
	FString OriginalEntries, ScopedEntries;
	Property->ExportTextItem_Direct(OriginalEntries, Property->ContainerPtrToValuePtr<void>(Cycle), nullptr, Cycle, PPF_None);
	Property->ExportTextItem_Direct(ScopedEntries, Property->ContainerPtrToValuePtr<void>(ScopedCycle), nullptr, ScopedCycle, PPF_None);
	TestEqual(TEXT("Scoped Cycle preserves every authored entry setting"), ScopedEntries, OriginalEntries);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FString Name = FString::Printf(TEXT("M_Neutral_Run_Reface_Start_B_%s_180"), Index == 0 ? TEXT("L") : TEXT("R"));
		auto* Sequence = LoadObject<UAnimSequence>(nullptr, *(TEXT("/Game/Characters/UEFN_Mannequin/Animations/Run/") + Name + TEXT(".") + Name));
		const auto* Entry = Reface->GetDatabaseAnimationAsset(Index);
		if (!TestNotNull(TEXT("Original authored backward Reface"), Sequence) || !TestNotNull(TEXT("Reface entry exists"), Entry)) return false;
		TestEqual(TEXT("Correct original source, no generated/reversed clip"), Reface->GetAnimationAsset(Index), static_cast<UObject*>(Sequence));
		TestTrue(TEXT("Original root motion metadata is available"), Sequence->bEnableRootMotion);
		TestTrue(TEXT("Entry is enabled, original-only and prevents same-asset reselection"),
			Entry->IsEnabled() && Entry->IsDisableReselection() && Entry->GetMirrorOption() == EPoseSearchMirrorOption::UnmirroredOnly);
		TestTrue(TEXT("Search excludes the long straight backward tail"), FMath::IsNearlyEqual(Entry->GetSamplingRange().Min, 0.f) &&
			FMath::IsNearlyEqual(Entry->GetSamplingRange().Max, 1.1f));
	}
	TestTrue(TEXT("Cooked family compatibility was certified on save"), Set->RunDatabases.bCookedBackwardRefaceCompatible);
	TestNull(TEXT("Sprint data does not contain backward Reface"), Set->SprintDatabases.BackwardReface.Get());
	TestNull(TEXT("Sprint data does not contain the scoped backward Cycle"), Set->SprintDatabases.BackwardRefaceCycle.Get());
	AddInfo(FString::Printf(TEXT("Read-only backward Reface audit: Set=%s Reface=%s ScopedCycle=%s Entries=%d"),
		*Set->GetPathName(), *Reface->GetPathName(), *ScopedCycle->GetPathName(), ScopedCycle->GetNumAnimationAssets()));
	return true;
}
#endif
