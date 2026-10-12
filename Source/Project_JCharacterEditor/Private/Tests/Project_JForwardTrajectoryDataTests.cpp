#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Project_JPlayerCharacter.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchNormalizationSet.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchFeatureChannel_Trajectory.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJForwardTrajectoryDataTest, "ProjectJ.StrafeForward.TrajectorySchema",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJForwardTrajectoryDataTest::RunTest(const FString&)
{
	auto* PawnClass = LoadClass<AProject_JPlayerCharacter>(nullptr, TEXT("/Game/Character_BPs/GreatSword/BP_GreatSword.BP_GreatSword_C"));
	if (!TestNotNull(TEXT("Production pawn"), PawnClass)) return false;
	const auto* Set = CastChecked<AProject_JPlayerCharacter>(PawnClass->GetDefaultObject())->GetCombatStrafeMotionMatchingAssetSet();
	if (!TestNotNull(TEXT("Combat set"), Set)) return false;
	auto* Forward = Set->RunDatabases.ForwardCycle.Get();
	auto* Cycle = Set->RunDatabases.Cycle.Get();
	if (!TestNotNull(TEXT("Forward pool"), Forward) || !TestNotNull(TEXT("Fallback Cycle"), Cycle)) return false;
	const auto* Source = Cycle->Schema.Get();
	const auto* SourceTrajectory = Source ? Source->FindFirstChannelOfType<UPoseSearchFeatureChannel_Trajectory>() : nullptr;
	if (!TestNotNull(TEXT("Immutable source trajectory"), SourceTrajectory)) return false;
	const auto* Normalization = Forward->NormalizationSet.Get();
	if (!TestNotNull(TEXT("Keep forward normalization"), Normalization)) return false;
	const auto ReferencesBefore = Normalization->Databases;
	const FString Path = TEXT("/Game/Animation_Logic/PSS/PSS_Combat_Run_ForwardTrajectory");
	auto* Schema = LoadObject<UPoseSearchSchema>(nullptr, *(Path + TEXT(".") + FPackageName::GetShortName(Path)), nullptr, LOAD_NoWarn);
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Validation/StrafeArcBox_20261012");
	IFileManager::Get().MakeDirectory(*Directory, true);
	if (!TestNotNull(TEXT("Dedicated forward trajectory schema installed"), Schema)) return false;
	const auto* Trajectory = Schema->FindFirstChannelOfType<UPoseSearchFeatureChannel_Trajectory>();
	if (!TestNotNull(TEXT("Dedicated trajectory"), Trajectory) ||
		!TestTrue(TEXT("Only forward pool uses the dedicated compatible schema"), Schema != Source && Schema->AreSkeletonsCompatible(Source)) ||
		!TestEqual(TEXT("No additional feature dimensions"), Schema->SchemaCardinality, Source->SchemaCardinality) ||
		!TestEqual(TEXT("Same sample rate"), Schema->SampleRate, Source->SampleRate) ||
		!TestEqual(TEXT("Same trajectory sample count"), Trajectory->Samples.Num(), SourceTrajectory->Samples.Num()) ||
		!TestEqual(TEXT("Keep total trajectory channel weight"), Trajectory->Weight, SourceTrajectory->Weight)) return false;
	int32 ChangedFutureSamples = 0;
	for (int32 Index = 0; Index < Trajectory->Samples.Num(); ++Index)
	{
		const auto& A = Trajectory->Samples[Index]; const auto& B = SourceTrajectory->Samples[Index];
		const bool bFutureShape = B.Offset > 0 && (B.Flags & int32(EPoseSearchTrajectoryFlags::PositionXY | EPoseSearchTrajectoryFlags::FacingDirectionXY));
		if (!TestTrue(TEXT("Keep all sample times, flags and normalization groups"), A.Offset == B.Offset && A.Flags == B.Flags && A.NormalizationGroup == B.NormalizationGroup) ||
			!TestEqual(TEXT("Future position/facing weight only; current velocity and history preserved"), A.Weight, B.Weight * (bFutureShape ? 4.f : 1.f))) return false;
		ChangedFutureSamples += bFutureShape;
	}
	if (!TestEqual(TEXT("Four future position/facing samples"), ChangedFutureSamples, 4)) return false;
	using namespace UE::PoseSearch;
	if (!TestTrue(TEXT("Final forward search index ready"), FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Forward,
		ERequestAsyncBuildFlag::ContinueRequest | ERequestAsyncBuildFlag::WaitForCompletion) == EAsyncBuildIndexResult::Success)) return false;
	if (!TestTrue(TEXT("Production forward schema link"), Forward->Schema == Schema) ||
		!TestTrue(TEXT("Fallback schema remains original"), Cycle->Schema == Source) ||
		!TestTrue(TEXT("Normalization membership unchanged"), Forward->NormalizationSet == Normalization && Normalization->Databases == ReferencesBefore)) return false;
	const FString Report = FString::Printf(TEXT("Schema=%s Database=%s Cardinality=%d Samples=%d ChangedFutureSamples=%d Poses=%d ValueBytes=%lld\n"),
		*Schema->GetPathName(), *Forward->GetPathName(), Schema->SchemaCardinality, Trajectory->Samples.Num(), ChangedFutureSamples,
		Forward->GetSearchIndex().GetNumPoses(), int64(Forward->GetSearchIndex().Values.Num()) * sizeof(float));
	return TestTrue(TEXT("Save read-only schema audit"), FFileHelper::SaveStringToFile(Report,
		*(Directory / TEXT("VerifiedSchema.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
}
#endif
