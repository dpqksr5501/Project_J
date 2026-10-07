#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Project_JPlayerCharacter.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "Animation/AnimSequence.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchNormalizationSet.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCycleInventoryTest, "ProjectJ.GeneralTurning.CycleInventory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCycleInventoryTest::RunTest(const FString&)
{
	auto* Class = LoadClass<AProject_JPlayerCharacter>(nullptr, TEXT("/Game/Character_BPs/GreatSword/BP_GreatSword.BP_GreatSword_C"));
	if (!TestNotNull(TEXT("Production pawn class"), Class)) return false;
	const auto* Pawn = CastChecked<AProject_JPlayerCharacter>(Class->GetDefaultObject());
	FString Report;
	for (const auto* Set : {Pawn->GetMotionMatchingAssetSet(), Pawn->GetCombatStrafeMotionMatchingAssetSet()})
	{
		if (!TestNotNull(TEXT("Production asset set"), Set)) return false;
		for (const auto* Database : {Set->RunDatabases.Cycle.Get(), Set->RunDatabases.SettledCycle.Get()})
		{
			if (!Database) continue;
			Report += FString::Printf(TEXT("Database=%s Schema=%s Entries=%d\n"),
				*Database->GetPathName(), *GetPathNameSafe(Database->Schema), Database->GetNumAnimationAssets());
			for (int32 Index = 0; Index < Database->GetNumAnimationAssets(); ++Index)
			{
				const auto* Entry = Database->GetDatabaseAnimationAsset(Index);
				Report += FString::Printf(TEXT("Entry=%d Enabled=%d Clip=%s\n"), Index,
					Entry && Entry->IsEnabled(), *GetNameSafe(Database->GetAnimationAsset(Index)));
			}
		}
	}
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Validation/StrafeCycle_20261006");
	IFileManager::Get().MakeDirectory(*Directory, true);
	return TestTrue(TEXT("Save read-only candidate inventory report"), FFileHelper::SaveStringToFile(Report,
		*(Directory / TEXT("CycleInventory.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGeneralTurningDataTest, "ProjectJ.GeneralTurning.ProductionData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJGeneralTurningDataTest::RunTest(const FString&)
{
	auto* Class = LoadClass<AProject_JPlayerCharacter>(nullptr, TEXT("/Game/Character_BPs/GreatSword/BP_GreatSword.BP_GreatSword_C"));
	if (!TestNotNull(TEXT("Production player"), Class)) return false;
	const auto* Player = CastChecked<AProject_JPlayerCharacter>(Class->GetDefaultObject());
	const bool bApply = FParse::Param(FCommandLine::Get(), TEXT("ProjectJApplyGeneralTurning"));
	const bool bExpect = bApply || FParse::Param(FCommandLine::Get(), TEXT("ProjectJExpectGeneralTurning"));
	struct FFamily { UProject_JMotionMatchingAssetSet* Set; UPoseSearchDatabase* Source; FString Path; };
	FFamily Families[] = {
		{const_cast<UProject_JMotionMatchingAssetSet*>(Player->GetMotionMatchingAssetSet()),
		LoadObject<UPoseSearchDatabase>(nullptr, TEXT("/Game/Animation_Logic/PSD/PSD_Player_Locomotion/PSD_Run_Turn.PSD_Run_Turn")),
		TEXT("/Game/Animation_Logic/PSD/PSD_Player_Locomotion/PSD_Run_GeneralTurn")},
		{const_cast<UProject_JMotionMatchingAssetSet*>(Player->GetCombatStrafeMotionMatchingAssetSet()),
		LoadObject<UPoseSearchDatabase>(nullptr, TEXT("/Game/Animation_Logic/PSD/PSD_Player_Combat_Locomotion/PSD_Combat_Run_Turn.PSD_Combat_Run_Turn")),
		TEXT("/Game/Animation_Logic/PSD/PSD_Player_Combat_Locomotion/PSD_Combat_Run_GeneralTurn")}
	};
	const auto IsGeneral = [](const UObject* Asset)
	{
		const FString Name = GetNameSafe(Asset);
		return Name.Contains(TEXT("_Turn_")) && (Name.Contains(TEXT("_045_")) || Name.Contains(TEXT("_090_")) || Name.Contains(TEXT("_135_")));
	};
	// Audit both families before touching either. Existing entry sampling ranges,
	// mirroring and exclusions are copied, not reconstructed from clip names.
	for (const auto& F : Families)
	{
		if (!TestNotNull(TEXT("Authored family"), F.Set) || !TestNotNull(TEXT("Existing broad Turn PSD"), F.Source)) return false;
		auto* Cycle = F.Set->RunDatabases.Cycle.Get(); auto* Acute = F.Set->RunDatabases.TurnRedirect.Get();
		if (!TestNotNull(TEXT("Dynamic Cycle"), Cycle) || !TestNotNull(TEXT("Existing acute Turn"), Acute) ||
			!TestTrue(TEXT("Runtime acute and Cycle share a schema"), Cycle->Schema && Acute->Schema == Cycle->Schema) ||
			!TestTrue(TEXT("Read-only broad source has compatible animation features"), F.Source->Schema &&
				Cycle->Schema->AreSkeletonsCompatible(F.Source->Schema) && Cycle->Schema->SchemaCardinality == F.Source->Schema->SchemaCardinality)) return false;
		const auto* Norm = Cycle->NormalizationSet.Get();
		if (!TestTrue(TEXT("Existing shared normalization is coherent"), Norm && Norm == Acute->NormalizationSet && Norm->Databases.Contains(Cycle) && Norm->Databases.Contains(Acute))) return false;
		int32 GeneralCount = 0;
		for (int32 Index = 0; Index < F.Source->GetNumAnimationAssets(); ++Index)
		{
			const auto* Asset = F.Source->GetAnimationAsset(Index);
			const auto* Sequence = Cast<UAnimSequence>(Asset);
			const float RootYaw = Sequence ? Sequence->ExtractRootMotionFromRange(0, Sequence->GetPlayLength(), FAnimExtractContext()).Rotator().Yaw : 0;
			const bool bGeneral = IsGeneral(Asset); GeneralCount += bGeneral;
			AddInfo(FString::Printf(TEXT("GeneralTurnAudit Set=%s Source=%s Entry=%d Clip=%s General=%d RootYaw=%.2f"),
				*F.Set->GetPathName(), *F.Source->GetPathName(), Index, *GetNameSafe(Asset), bGeneral, RootYaw));
			if (bGeneral && !TestTrue(TEXT("General forward turn has finite authored root yaw between 20 and 160"),
				Sequence && FMath::IsFinite(RootYaw) && FMath::Abs(RootYaw) >= 20 && FMath::Abs(RootYaw) <= 160)) return false;
		}
		if (!TestEqual(TEXT("Twelve 45/90/135 left/right/foot entries"), GeneralCount, 12)) return false;
		if (bApply && !F.Set->RunDatabases.GeneralTurn)
		{
			if (!TestNull(TEXT("New PSD path unused"), LoadObject<UPoseSearchDatabase>(nullptr,
				*(F.Path + TEXT(".") + FPackageName::GetShortName(F.Path)), nullptr, LOAD_NoWarn))) return false;
			for (const UObject* Asset : {static_cast<const UObject*>(F.Set), static_cast<const UObject*>(Norm)})
				if (!TestFalse(TEXT("Refuse dirty authoring owner"), Asset->GetOutermost()->IsDirty())) return false;
		}
	}
	for (int32 FamilyIndex = 0; FamilyIndex < UE_ARRAY_COUNT(Families); ++FamilyIndex)
	{
		auto& F = Families[FamilyIndex]; auto* Cycle = F.Set->RunDatabases.Cycle.Get();
		auto* Norm = const_cast<UPoseSearchNormalizationSet*>(Cycle->NormalizationSet.Get());
		if (bApply && !F.Set->RunDatabases.GeneralTurn)
		{
			for (UObject* Asset : {static_cast<UObject*>(F.Set), static_cast<UObject*>(Norm)})
			{
				const FString File = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
				FString Relative = FPaths::ConvertRelativePathToFull(File);
				const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()); FPaths::MakePathRelativeTo(Relative, *Directory);
				const FString Backup = FPaths::ProjectSavedDir() / TEXT("Validation/GeneralTurning_20261006/Before") / Relative;
				IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true);
				if (!IFileManager::Get().FileExists(*Backup) && !TestEqual(TEXT("Backup authored owner"), IFileManager::Get().Copy(*Backup, *File), COPY_OK)) return false;
			}
			auto* Package = CreatePackage(*F.Path);
			auto* General = DuplicateObject<UPoseSearchDatabase>(F.Source, Package, *FPackageName::GetShortName(F.Path));
			General->SetFlags(RF_Public | RF_Standalone);
			General->Schema = Cycle->Schema;
			for (int32 Index = General->GetNumAnimationAssets() - 1; Index >= 0; --Index)
				if (!IsGeneral(General->GetAnimationAsset(Index))) General->RemoveAnimationAssetAt(Index);
			General->NormalizationSet = Norm;
			Norm->Modify(); Norm->Databases.AddUnique(General);
			F.Set->Modify(); F.Set->RunDatabases.GeneralTurn = General;
			FAssetRegistryModule::AssetCreated(General);
			General->PostEditChange(); Norm->PostEditChange(); F.Set->PostEditChange();
			for (UObject* Asset : {static_cast<UObject*>(General), static_cast<UObject*>(Norm), static_cast<UObject*>(F.Set)})
			{
				FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
				if (!TestTrue(TEXT("Save participating PSD/normalization/asset set only"), UPackage::SavePackage(Asset->GetOutermost(), Asset,
					*FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension()), Args))) return false;
			}
		}
		if (bExpect)
		{
			auto* General = F.Set->RunDatabases.GeneralTurn.Get();
			if (!TestNotNull(TEXT("General PSD is installed"), General)) return false;
			TestEqual(TEXT("General pool is bounded to twelve entries"), General->GetNumAnimationAssets(), 12);
			for (int32 Index = 0; Index < General->GetNumAnimationAssets(); ++Index)
				TestTrue(TEXT("Every general entry excludes acute 180"), IsGeneral(General->GetAnimationAsset(Index)));
			TestTrue(TEXT("Cooked normalization certificate authored"), F.Set->RunDatabases.bCookedTurnCycleCompatible && F.Set->RunDatabases.bCookedGeneralTurnCompatible);
			FProject_JMotionMatchingSelectionContext Context; Context.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
			Context.bGeneralTurnCandidates = true;
			if (FamilyIndex == 1) { Context.RotationMode = EProject_JLocomotionRotationMode::Strafe; Context.bUseGenericFamiliesForNonOrientToMovement = true; }
			TestEqual(TEXT("Production Cycle/general pair passes compatibility guard"), F.Set->FindTurnCycleCompanion(Context, Cycle), General);
			Context.bGeneralTurnCandidates = false; Context.bMovingTurn180 = true; Context.PhaseFamily = EProject_JLocomotionPhaseFamily::Turn;
			TestEqual(TEXT("Acute pair remains unchanged"), F.Set->FindTurnCycleCompanion(Context, F.Set->RunDatabases.TurnRedirect), Cycle);
		}
	}
	return true;
}
#endif
