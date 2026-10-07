#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Project_JPlayerCharacter.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchNormalizationSet.h"
#include "PoseSearch/PoseSearchFeatureChannel_Trajectory.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace
{
const TCHAR* RunSchemaPath = TEXT("/Game/Animation_Logic/PSS/PSS_Combat_Run_Continuity");

void SplitRunVelocityWeights(UPoseSearchSchema* Schema)
{
	auto* Trajectory = const_cast<UPoseSearchFeatureChannel_Trajectory*>(Schema->FindFirstChannelOfType<UPoseSearchFeatureChannel_Trajectory>());
	check(Trajectory);
	const int32 Position = int32(EPoseSearchTrajectoryFlags::Position | EPoseSearchTrajectoryFlags::PositionXY);
	const int32 Velocity = int32(EPoseSearchTrajectoryFlags::Velocity | EPoseSearchTrajectoryFlags::VelocityXY |
		EPoseSearchTrajectoryFlags::VelocityDirection | EPoseSearchTrajectoryFlags::VelocityDirectionXY);
	const int32 Facing = int32(EPoseSearchTrajectoryFlags::FacingDirection | EPoseSearchTrajectoryFlags::FacingDirectionXY);
	TArray<FPoseSearchTrajectorySample> Samples;
	for (const auto& Sample : Trajectory->Samples)
		for (int32 Mask : {Position, Velocity, Facing})
		{
			if (!(Sample.Flags & Mask)) continue;
			auto Part = Sample; Part.Flags &= Mask;
			if (Mask == Velocity) Part.Weight *= 16.f;
			Samples.Add(Part);
		}
	Trajectory->Samples = Samples;
	Schema->PostEditChange();
}

FString ExportEntries(const UPoseSearchDatabase* Database)
{
	const auto* Property = FindFProperty<FArrayProperty>(UPoseSearchDatabase::StaticClass(), TEXT("DatabaseAnimationAssets"));
	check(Property);
	FString Result;
	Property->ExportTextItem_Direct(Result, Property->ContainerPtrToValuePtr<void>(Database), nullptr,
		const_cast<UPoseSearchDatabase*>(Database), PPF_None);
	return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJStrafeDirectionSchemaTest, "ProjectJ.StrafeDirection.ProductionSchema",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJStrafeDirectionSchemaTest::RunTest(const FString&)
{
	const bool bApply = FParse::Param(FCommandLine::Get(), TEXT("ProjectJApplyStrafeDirectionSchema"));
	auto* Class = LoadClass<AProject_JPlayerCharacter>(nullptr, TEXT("/Game/Character_BPs/GreatSword/BP_GreatSword.BP_GreatSword_C"));
	if (!TestNotNull(TEXT("Production pawn"), Class)) return false;
	const auto* Pawn = CastChecked<AProject_JPlayerCharacter>(Class->GetDefaultObject());
	const auto* Set = Pawn->GetCombatStrafeMotionMatchingAssetSet();
	auto* Original = LoadObject<UPoseSearchSchema>(nullptr, TEXT("/Game/Animation_Logic/PSS/PSS_Combat.PSS_Combat"));
	if (!TestNotNull(TEXT("Combat family"), Set) || !TestNotNull(TEXT("Shared original schema"), Original) ||
		!TestNotNull(TEXT("Original trajectory"), Original->FindFirstChannelOfType<UPoseSearchFeatureChannel_Trajectory>())) return false;
	UPoseSearchDatabase* Databases[] = {Set->RunDatabases.Cycle.Get(), Set->RunDatabases.TurnRedirect.Get(), Set->RunDatabases.GeneralTurn.Get()};
	if (!TestNotNull(TEXT("Run Cycle"), Databases[0])) return false;
	const auto* Normalization = Databases[0]->NormalizationSet.Get();
	if (!TestNotNull(TEXT("Existing normalization"), Normalization) ||
		!TestEqual(TEXT("Preserve existing three-database normalization dataset"), Normalization->Databases.Num(), 3)) return false;
	auto* Schema = LoadObject<UPoseSearchSchema>(nullptr,
		*(FString(RunSchemaPath) + TEXT(".") + FPackageName::GetShortName(RunSchemaPath)), nullptr, LOAD_NoWarn);
	TArray<FString> Entries;
	for (auto* Database : Databases)
	{
		if (!TestNotNull(TEXT("Runtime Run database"), Database) ||
			!TestTrue(TEXT("Existing shared normalization membership"), Database->NormalizationSet == Normalization && Normalization->Databases.Contains(Database)) ||
			!TestTrue(TEXT("Refuse replacing unrelated authored schema"), Database->Schema == Original || (Schema && Database->Schema == Schema))) return false;
		Entries.Add(ExportEntries(Database));
		if (bApply && !TestFalse(TEXT("Refuse dirty authoring database"), Database->GetOutermost()->IsDirty())) return false;
	}
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Validation/StrafeDirectionApplied_20261006");
	IFileManager::Get().MakeDirectory(*Directory, true);
	if (bApply)
	{
		if (Schema && !TestFalse(TEXT("Refuse dirty existing dedicated schema"), Schema->GetOutermost()->IsDirty())) return false;
		// Back up current user-authored files before mutating any of the three links.
		for (auto* Database : Databases)
		{
			const FString File = FPackageName::LongPackageNameToFilename(Database->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
			const FString Backup = Directory / TEXT("Before") / FPaths::GetCleanFilename(File);
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true);
			if (!IFileManager::Get().FileExists(*Backup) && !TestEqual(TEXT("Backup current authored database"), IFileManager::Get().Copy(*Backup, *File), COPY_OK)) return false;
		}
		if (!Schema)
		{
			auto* Package = CreatePackage(RunSchemaPath);
			Schema = DuplicateObject<UPoseSearchSchema>(Original, Package, *FPackageName::GetShortName(RunSchemaPath));
			Schema->SetFlags(RF_Public | RF_Standalone);
			SplitRunVelocityWeights(Schema);
			FAssetRegistryModule::AssetCreated(Schema);
		}
	}
	if (!TestNotNull(TEXT("Dedicated Combat Run schema installed"), Schema)) return false;
	// Compare against the same transformation used by the successful transient trial.
	auto* Expected = DuplicateObject<UPoseSearchSchema>(Original, GetTransientPackage());
	SplitRunVelocityWeights(Expected);
	const auto* ActualTrajectory = Schema->FindFirstChannelOfType<UPoseSearchFeatureChannel_Trajectory>();
	const auto* ExpectedTrajectory = Expected->FindFirstChannelOfType<UPoseSearchFeatureChannel_Trajectory>();
	if (!TestNotNull(TEXT("Dedicated trajectory"), ActualTrajectory) ||
		!TestEqual(TEXT("Preserve feature cardinality"), Schema->SchemaCardinality, Original->SchemaCardinality) ||
		!TestEqual(TEXT("Preserve sample rate"), Schema->SampleRate, Original->SampleRate) ||
		!TestEqual(TEXT("Independent trajectory sample count"), ActualTrajectory->Samples.Num(), ExpectedTrajectory->Samples.Num())) return false;
	for (int32 Index = 0; Index < ActualTrajectory->Samples.Num(); ++Index)
	{
		const auto& A = ActualTrajectory->Samples[Index]; const auto& E = ExpectedTrajectory->Samples[Index];
		if (!TestTrue(TEXT("Only velocity weight differs from shared original schema"),
			A.Offset == E.Offset && A.Flags == E.Flags && A.Weight == E.Weight && A.NormalizationGroup == E.NormalizationGroup)) return false;
	}
	if (bApply)
	{
		using namespace UE::PoseSearch;
		for (auto* Database : Databases)
		{
			// Finish any initial index load before changing its schema.
			if (!TestTrue(TEXT("Initial indexing completed"), FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database,
				ERequestAsyncBuildFlag::ContinueRequest | ERequestAsyncBuildFlag::WaitForCompletion) == EAsyncBuildIndexResult::Success)) return false;
		}
		// A schema change invalidates every member of the shared normalization set.
		// Change all links before requesting fresh indexes for the consistent set.
		for (auto* Database : Databases)
		{
			Database->Modify(); Database->Schema = Schema; Database->PostEditChange();
		}
		for (auto* Database : Databases)
		{
			if (!TestTrue(TEXT("Fresh index is ready for saving"), FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database,
				ERequestAsyncBuildFlag::NewRequest | ERequestAsyncBuildFlag::WaitForCompletion) == EAsyncBuildIndexResult::Success)) return false;
		}
		for (UObject* Asset : {static_cast<UObject*>(Schema), static_cast<UObject*>(Databases[0]), static_cast<UObject*>(Databases[1]), static_cast<UObject*>(Databases[2])})
		{
			FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
			if (!TestTrue(TEXT("Save only authorized schema and three Run PSDs"), UPackage::SavePackage(Asset->GetOutermost(), Asset,
				*FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension()), Args))) return false;
		}
	}
	FString Report = FString::Printf(TEXT("Applied=%d Schema=%s Reference=%s Cardinality=%d SampleRate=%d\n"),
		bApply, *Schema->GetPathName(), *Original->GetPathName(), Schema->SchemaCardinality, Schema->SampleRate);
	for (const auto& Sample : ActualTrajectory->Samples)
		Report += FString::Printf(TEXT("Sample Time=%.3f Flags=%d Weight=%.3f Group=%s\n"), Sample.Offset, Sample.Flags, Sample.Weight, *Sample.NormalizationGroup.ToString());
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Databases); ++Index)
	{
		auto* Database = Databases[Index];
		TestTrue(TEXT("Production database uses saved dedicated schema"), Database->Schema == Schema);
		TestEqual(TEXT("Animation entries, flags, ranges and metadata preserved"), ExportEntries(Database), Entries[Index]);
		Report += FString::Printf(TEXT("Database=%s Schema=%s Entries=%d Mode=%d PCA=%d Neighbors=%d Normalization=%s\n"),
			*Database->GetPathName(), *Database->Schema->GetPathName(), Database->GetNumAnimationAssets(), int32(Database->PoseSearchMode),
			Database->NumberOfPrincipalComponents, Database->KDTreeQueryNumNeighbors, *Normalization->GetPathName());
	}
	FProject_JMotionMatchingSelectionContext Context;
	Context.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle; Context.RotationMode = EProject_JLocomotionRotationMode::Strafe;
	Context.bUseGenericFamiliesForNonOrientToMovement = true; Context.bGeneralTurnCandidates = true;
	TestEqual(TEXT("GeneralTurn still competes with Cycle"), Set->FindTurnCycleCompanion(Context, Databases[0]), Databases[2]);
	Context.PhaseFamily = EProject_JLocomotionPhaseFamily::Turn; Context.bGeneralTurnCandidates = false; Context.bMovingTurn180 = true;
	TestEqual(TEXT("Acute 180-degree redirect still competes with Cycle"), Set->FindTurnCycleCompanion(Context, Databases[1]), Databases[0]);
	return TestTrue(TEXT("Save installed-schema audit"), FFileHelper::SaveStringToFile(Report,
		*(Directory / TEXT("ProductionSchema.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCombatTurnLinkAudit, "ProjectJ.StrafeDirection.CombatTurnLink",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCombatTurnLinkAudit::RunTest(const FString&)
{
	const bool bRestore = FParse::Param(FCommandLine::Get(), TEXT("ProjectJRestoreCombatTurnLink"));
	auto* Class = LoadClass<AProject_JPlayerCharacter>(nullptr, TEXT("/Game/Character_BPs/GreatSword/BP_GreatSword.BP_GreatSword_C"));
	if (!TestNotNull(TEXT("Production pawn"), Class)) return false;
	auto* Set = const_cast<UProject_JMotionMatchingAssetSet*>(CastChecked<AProject_JPlayerCharacter>(Class->GetDefaultObject())->GetCombatStrafeMotionMatchingAssetSet());
	if (!TestNotNull(TEXT("Combat runtime set"), Set)) return false;
	auto* Expected = LoadObject<UPoseSearchDatabase>(nullptr,
		TEXT("/Game/Animation_Logic/PSD/PSD_Player_Locomotion/PSD_Combat_Run_Turn.PSD_Combat_Run_Turn"));
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Validation/TurnFrequency_20261007");
	IFileManager::Get().MakeDirectory(*Directory, true);
	FString Report = FString::Printf(TEXT("Set=%s Cycle=%s TurnRedirect=%s GeneralTurn=%s ExpectedAcute=%s\n"),
		*Set->GetPathName(), *GetPathNameSafe(Set->RunDatabases.Cycle), *GetPathNameSafe(Set->RunDatabases.TurnRedirect),
		*GetPathNameSafe(Set->RunDatabases.GeneralTurn), *GetPathNameSafe(Expected));
	if (Set->RunDatabases.GeneralTurn) Report += TEXT("UserAuthoredGeneralEntries=") + ExportEntries(Set->RunDatabases.GeneralTurn) + TEXT("\n");
	if (bRestore)
	{
		auto* Cycle = Set->RunDatabases.Cycle.Get();
		if (!TestNotNull(TEXT("Expected original acute PSD"), Expected) || !TestNotNull(TEXT("Runtime Cycle"), Cycle) ||
			!TestTrue(TEXT("Refuse replacing a different authored Turn"), !Set->RunDatabases.TurnRedirect || Set->RunDatabases.TurnRedirect == Expected) ||
			!TestTrue(TEXT("Same schema and shared normalization"), Cycle->Schema == Expected->Schema && Cycle->NormalizationSet &&
				Cycle->NormalizationSet == Expected->NormalizationSet && Cycle->NormalizationSet->Databases.Contains(Expected)) ||
			!TestFalse(TEXT("Refuse dirty runtime set"), Set->GetOutermost()->IsDirty())) return false;
		const FString File = FPackageName::LongPackageNameToFilename(Set->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		const FString Backup = Directory / TEXT("BeforeTurnLink") / FPaths::GetCleanFilename(File);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true);
		if (!IFileManager::Get().FileExists(*Backup) && !TestEqual(TEXT("Backup current user-authored set"), IFileManager::Get().Copy(*Backup, *File), COPY_OK)) return false;
		Set->Modify(); Set->RunDatabases.TurnRedirect = Expected; Set->PostEditChange();
		FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
		if (!TestTrue(TEXT("Save only explicitly authorized Combat set"), UPackage::SavePackage(Set->GetOutermost(), Set, *File, Args))) return false;
		Report += TEXT("RestoredOriginalTurnLink=1\n");
	}
	return TestTrue(TEXT("Save read-only link audit"), FFileHelper::SaveStringToFile(Report,
		*(Directory / TEXT("CombatTurnLink.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
}
#endif
