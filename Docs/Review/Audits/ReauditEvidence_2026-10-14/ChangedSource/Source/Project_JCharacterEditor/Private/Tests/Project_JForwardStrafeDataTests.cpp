#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Project_JPlayerCharacter.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchNormalizationSet.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "Animation/AnimSequence.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UObject/UnrealType.h"

namespace
{
FString ForwardEntryText(const UPoseSearchDatabase* Database,
	const TArray<FPoseSearchDatabaseAnimationAsset>* Entries = nullptr)
{
	const auto* Property = FindFProperty<FArrayProperty>(UPoseSearchDatabase::StaticClass(), TEXT("DatabaseAnimationAssets"));
	check(Property);
	FString Text;
	const void* Value = Entries ? static_cast<const void*>(Entries) : Property->ContainerPtrToValuePtr<void>(Database);
	Property->ExportTextItem_Direct(Text, Value, nullptr,
		const_cast<UPoseSearchDatabase*>(Database), PPF_None);
	return Text;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJForwardStrafeDataTest, "ProjectJ.StrafeForward.ProductionData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJForwardStrafeDataTest::RunTest(const FString&)
{
	auto* PawnClass = LoadClass<AProject_JPlayerCharacter>(nullptr, TEXT("/Game/Character_BPs/GreatSword/BP_GreatSword.BP_GreatSword_C"));
	if (!TestNotNull(TEXT("Production pawn"), PawnClass)) return false;
	const auto* Set = CastChecked<AProject_JPlayerCharacter>(PawnClass->GetDefaultObject())->GetCombatStrafeMotionMatchingAssetSet();
	if (!TestNotNull(TEXT("Production combat set"), Set)) return false;
	auto* Cycle = Set->RunDatabases.Cycle.Get(); auto* Loop = Set->RunDatabases.SettledCycle.Get();
	if (!TestNotNull(TEXT("Original Cycle"), Cycle) || !TestNotNull(TEXT("Original Loop"), Loop) ||
		!TestNotNull(TEXT("Original schema"), Cycle->Schema.Get())) return false;
	const FString CycleBefore = ForwardEntryText(Cycle), LoopBefore = ForwardEntryText(Loop);
	const auto* OriginalNormalization = Cycle->NormalizationSet.Get();
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Validation/StrafeForward_20261012");
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString DatabasePath = TEXT("/Game/Animation_Logic/PSD/PSD_Player_Combat_Locomotion/PSD_Combat_Run_ForwardCycle");
	const FString NormPath = TEXT("/Game/Animation_Logic/PSD/PSD_Player_Combat_Locomotion/NS_Combat_Run_ForwardCycle");
	auto* Forward = LoadObject<UPoseSearchDatabase>(nullptr,
		*(DatabasePath + TEXT(".") + FPackageName::GetShortName(DatabasePath)), nullptr, LOAD_NoWarn);
	auto* Norm = LoadObject<UPoseSearchNormalizationSet>(nullptr,
		*(NormPath + TEXT(".") + FPackageName::GetShortName(NormPath)), nullptr, LOAD_NoWarn);
	if (!TestNotNull(TEXT("Original curve normalization"), OriginalNormalization)) return false;
	decltype(Norm->Databases) ExpectedNormalization;
	for (const auto& Database : OriginalNormalization->Databases)
		ExpectedNormalization.Add(Database == Cycle ? Forward : Database.Get());
	auto* ForwardSchema = LoadObject<UPoseSearchSchema>(nullptr,
		TEXT("/Game/Animation_Logic/PSS/PSS_Combat_Run_ForwardTrajectory.PSS_Combat_Run_ForwardTrajectory"), nullptr, LOAD_NoWarn);
	if (!TestNotNull(TEXT("Forward pool installed"), Forward) || !TestNotNull(TEXT("Forward normalization"), Norm) ||
		!TestNotNull(TEXT("Forward trajectory schema installed"), ForwardSchema) ||
		!TestTrue(TEXT("Use the dedicated schema without changing fallback schema"), Forward->Schema == ForwardSchema) ||
		!TestTrue(TEXT("Isolated statistics leave existing search families unchanged"), Forward->NormalizationSet == Norm &&
			Norm != OriginalNormalization && Norm->Databases == ExpectedNormalization && Norm->Databases.Contains(Forward)) ||
		!TestTrue(TEXT("Keep an authored forward family"), Forward->GetNumAnimationAssets() > Loop->GetNumAnimationAssets())) return false;
	if (!TestEqual(TEXT("Dedicated steady preference uses the engine's small loop bias"), Forward->LoopingCostBias, -.005f)) return false;
	// The authored forward intent keeps continuous turning arcs and steady loops.
	// Labels here verify saved data; runtime selection never branches on clip names.
	// Comparing authored entries needs values, not a registered database carrying
	// copied derived-index/normalization state and asynchronous cache work.
	TArray<FPoseSearchDatabaseAnimationAsset> Expected;
	int32 AuthoredArcEntries = 0;
	for (int32 Index = 0; Index < Cycle->GetNumAnimationAssets(); ++Index)
	{
		if (GetNameSafe(Cycle->GetAnimationAsset(Index)).Contains(TEXT("_Arc_")))
		{ ++AuthoredArcEntries; Expected.Add(*Cycle->GetDatabaseAnimationAsset(Index)); }
	}
	for (int32 Index = 0; Index < Loop->GetNumAnimationAssets(); ++Index) Expected.Add(*Loop->GetDatabaseAnimationAsset(Index));
	if (!TestEqual(TEXT("Six authored Arc variants cover forward curvature"), AuthoredArcEntries, 6) ||
		!TestEqual(TEXT("Curated pool preserves all Arc and Loop entries"), Forward->GetNumAnimationAssets(), AuthoredArcEntries + Loop->GetNumAnimationAssets())) return false;
	if (!TestEqual(TEXT("Forward entries exactly preserve source ranges and flags"), ForwardEntryText(Forward), ForwardEntryText(Forward, &Expected))) return false;
	using namespace UE::PoseSearch;
	if (!TestTrue(TEXT("Ready canonical source index"), FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Cycle,
		ERequestAsyncBuildFlag::ContinueRequest |
		ERequestAsyncBuildFlag::WaitForCompletion) == EAsyncBuildIndexResult::Success)) return false;
	for (const auto& Asset : Cycle->GetSearchIndex().Assets)
	{
		if (!TestTrue(TEXT("Canonical index source entry is in range"), Asset.GetSourceAssetIdx() >= 0 &&
			Asset.GetSourceAssetIdx() < Cycle->GetNumAnimationAssets()) ||
			!TestNotNull(TEXT("Canonical index retains its authored animation"), Cycle->GetAnimationAsset(Asset.GetSourceAssetIdx()))) return false;
	}
	if (!TestTrue(TEXT("Ready forward index"), FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Forward,
		ERequestAsyncBuildFlag::ContinueRequest |
		ERequestAsyncBuildFlag::WaitForCompletion) == EAsyncBuildIndexResult::Success)) return false;
	FString Report = FString::Printf(TEXT("OriginalEntries=%d LoopEntries=%d ForwardEntries=%d Forward=%s Normalization=%s\n"),
		Cycle->GetNumAnimationAssets(), Loop->GetNumAnimationAssets(), Forward->GetNumAnimationAssets(), *Forward->GetPathName(), *Norm->GetPathName());
	Report += FString::Printf(TEXT("CanonicalIndexAssets=%d CanonicalPoses=%d ExpectedValues=%d NoTransientDatabaseComparison=1\n"),
		Cycle->GetSearchIndex().Assets.Num(), Cycle->GetSearchIndex().GetNumPoses(), Expected.Num());
	for (const auto& Database : Norm->Databases) Report += FString::Printf(TEXT("NormalizationReference=%s\n"), *GetPathNameSafe(Database));
	int32 LoopPoses = 0, CurvePoses = 0;
	for (const auto& Asset : Forward->GetSearchIndex().Assets)
	{
		const auto* Entry = Forward->GetDatabaseAnimationAsset(Asset.GetSourceAssetIdx());
		if (!TestNotNull(TEXT("Indexed entry"), Entry)) return false;
		if (Asset.IsLooping()) LoopPoses += Asset.GetNumPoses(); else CurvePoses += Asset.GetNumPoses();
		Report += FString::Printf(TEXT("Clip=%s Loop=%d Begin=%.3f End=%.3f Poses=%d SamplingMin=%.3f SamplingMax=%.3f\n"),
			*GetNameSafe(Entry->GetAnimationAsset()), Asset.IsLooping(), Asset.GetFirstSampleTime(Forward->Schema->SampleRate),
			Asset.GetLastSampleTime(Forward->Schema->SampleRate), Asset.GetNumPoses(), Entry->GetSamplingRange().Min, Entry->GetSamplingRange().Max);
	}
	if (!TestTrue(TEXT("Loop and curves both have searchable poses"), LoopPoses > 0 && CurvePoses > 0)) return false;
	Report += FString::Printf(TEXT("LoopPoses=%d CurvePoses=%d ValueBytes=%lld\n"), LoopPoses, CurvePoses,
		int64(Forward->GetSearchIndex().Values.Num()) * sizeof(float));
	TestEqual(TEXT("Production forward slot"), Set->RunDatabases.ForwardCycle.Get(), Forward);
	TestEqual(TEXT("Existing Cycle entries unchanged"), ForwardEntryText(Cycle), CycleBefore);
	TestEqual(TEXT("Existing Loop entries unchanged"), ForwardEntryText(Loop), LoopBefore);
	TestTrue(TEXT("Existing normalization unchanged"), Cycle->NormalizationSet == OriginalNormalization);
	return TestTrue(TEXT("Save read-only index audit"), FFileHelper::SaveStringToFile(Report,
		*(Directory / TEXT("VerifiedIndex.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
}
#endif
