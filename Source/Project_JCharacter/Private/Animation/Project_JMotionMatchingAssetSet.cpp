// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JMotionMatchingAssetSet.h"

#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchNormalizationSet.h"
#if WITH_EDITOR
#include "UObject/ObjectSaveContext.h"
#endif

namespace
{
UPoseSearchDatabase* SelectGaitDatabase(
	const FProject_JMotionMatchingGaitDatabaseFamily& DatabaseFamily,
	EProject_JLocomotionPhaseFamily PhaseFamily,
	bool bUseSettledCycle)
{
	switch (PhaseFamily)
	{
	case EProject_JLocomotionPhaseFamily::Turn:
		return DatabaseFamily.TurnRedirect.Get();
	case EProject_JLocomotionPhaseFamily::Cycle:
		return bUseSettledCycle && DatabaseFamily.SettledCycle
			? DatabaseFamily.SettledCycle.Get()
			: DatabaseFamily.Cycle.Get();
	default:
		return nullptr;
	}
}

void ValidateDatabaseSlot(
	const UProject_JMotionMatchingAssetSet* AssetSet,
	const UObject* ValidationContext,
	TArray<FString>& OutWarnings,
	const TCHAR* SlotName,
	const UPoseSearchDatabase* Database)
{
	if (Database)
	{
		return;
	}

	OutWarnings.Add(FString::Printf(
		TEXT("%s uses MotionMatchingAssetSet %s, but %s is missing."),
		*GetNameSafe(ValidationContext),
		*GetNameSafe(AssetSet),
		SlotName));
}

void ValidateGaitDatabaseFamily(
	const UProject_JMotionMatchingAssetSet* AssetSet,
	const UObject* ValidationContext,
	TArray<FString>& OutWarnings,
	const TCHAR* FamilyName,
	const FProject_JMotionMatchingGaitDatabaseFamily& DatabaseFamily)
{
	ValidateDatabaseSlot(AssetSet, ValidationContext, OutWarnings, *FString::Printf(TEXT("%s.Cycle"), FamilyName), DatabaseFamily.Cycle.Get());
	ValidateDatabaseSlot(AssetSet, ValidationContext, OutWarnings, *FString::Printf(TEXT("%s.TurnRedirect"), FamilyName), DatabaseFamily.TurnRedirect.Get());
}
}

UPoseSearchDatabase* UProject_JMotionMatchingAssetSet::FindDatabaseForContext(const FProject_JMotionMatchingSelectionContext& Context) const
{
	const EProject_JLocomotionGaitIntent GaitIntent = Context.GaitIntent;
	const EProject_JLocomotionRotationMode RotationMode = Context.RotationMode;
	const EProject_JLocomotionPhaseFamily PhaseFamily = Context.PhaseFamily;
	if (RotationMode == EProject_JLocomotionRotationMode::Strafe &&
		PhaseFamily == EProject_JLocomotionPhaseFamily::Turn && !Context.bMovingTurn180)
	{
		// A semantic Turn from an old/remote owner must not open the restricted
		// forward 180-degree PSD without the current local geometry/lifetime gate.
		return nullptr;
	}
	if (PhaseFamily == EProject_JLocomotionPhaseFamily::Idle)
	{
		if (IdlePoseSearchDatabase)
		{
			return IdlePoseSearchDatabase.Get();
		}
	}

	if (RotationMode == EProject_JLocomotionRotationMode::OrientToMovement ||
		Context.bUseGenericFamiliesForNonOrientToMovement)
	{
		const FProject_JMotionMatchingGaitDatabaseFamily& GaitFamily =
			GaitIntent == EProject_JLocomotionGaitIntent::Sprint ? SprintDatabases : RunDatabases;
		if (UPoseSearchDatabase* GaitDatabase = SelectGaitDatabase(GaitFamily, PhaseFamily, Context.bUseSettledCycle))
		{
			return GaitDatabase;
		}
	}

	return nullptr;
}

UPoseSearchDatabase* UProject_JMotionMatchingAssetSet::FindTurnCycleCompanion(
	const FProject_JMotionMatchingSelectionContext& Context, const UPoseSearchDatabase* Primary) const
{
	if (!bEnableTurnCycleCandidates || !Primary ||
		(Context.RotationMode != EProject_JLocomotionRotationMode::OrientToMovement &&
			!Context.bUseGenericFamiliesForNonOrientToMovement)) return nullptr;
	const auto& Family = Context.GaitIntent == EProject_JLocomotionGaitIntent::Sprint ? SprintDatabases : RunDatabases;
	UPoseSearchDatabase* Companion = nullptr;
	if (Context.bMovingTurn180 && Context.PhaseFamily == EProject_JLocomotionPhaseFamily::Turn && Primary == Family.TurnRedirect)
		Companion = Family.Cycle.Get();
	else if (!Context.bMovingTurn180 && Context.bGeneralTurnCandidates &&
		Context.PhaseFamily == EProject_JLocomotionPhaseFamily::Cycle && Primary == Family.Cycle)
		Companion = Family.GeneralTurn.Get();
	if (!Companion || Companion == Primary || !Primary->Schema || Primary->Schema != Companion->Schema) return nullptr;
#if WITH_EDITORONLY_DATA
	if (Primary->Schema->DataPreprocessor != EPoseSearchDataPreprocessor::None)
	{
		const auto* Normalization = Primary->NormalizationSet.Get();
		if (!Normalization || Normalization != Companion->NormalizationSet ||
			!Normalization->Databases.Contains(Primary) || !Normalization->Databases.Contains(Companion)) return nullptr;
	}
#else
	// Both DataPreprocessor and NormalizationSet are editor-only. PreSave
	// certifies either no preprocessing or shared statistics baked into indexes.
	if (!(Primary == Family.TurnRedirect ? Family.bCookedTurnCycleCompatible : Family.bCookedGeneralTurnCompatible)) return nullptr;
#endif
	return Companion;
}

#if WITH_EDITOR
void UProject_JMotionMatchingAssetSet::PreSave(FObjectPreSaveContext SaveContext)
{
	const auto Compatible = [](const UPoseSearchDatabase* Cycle, const UPoseSearchDatabase* Turn)
	{
		if (!Cycle || !Turn || Cycle == Turn || !Cycle->Schema || Cycle->Schema != Turn->Schema) return false;
		if (Cycle->Schema->DataPreprocessor == EPoseSearchDataPreprocessor::None) return true;
		const auto* Norm = Cycle->NormalizationSet.Get();
		return Norm && Norm == Turn->NormalizationSet && Norm->Databases.Contains(Cycle) && Norm->Databases.Contains(Turn);
	};
	for (auto* Family : {&RunDatabases, &SprintDatabases})
	{
		Family->bCookedTurnCycleCompatible = Compatible(Family->Cycle, Family->TurnRedirect);
		Family->bCookedGeneralTurnCompatible = Compatible(Family->Cycle, Family->GeneralTurn);
	}
	Super::PreSave(SaveContext);
}
#endif

bool UProject_JMotionMatchingAssetSet::ValidateForProjectJLocomotion(
	const UObject* ValidationContext,
	TArray<FString>& OutWarnings) const
{
	const int32 InitialWarningCount = OutWarnings.Num();

	ValidateDatabaseSlot(this, ValidationContext, OutWarnings, TEXT("DefaultPoseSearchDatabase"), DefaultPoseSearchDatabase.Get());
	ValidateDatabaseSlot(this, ValidationContext, OutWarnings, TEXT("IdlePoseSearchDatabase"), IdlePoseSearchDatabase.Get());
	ValidateGaitDatabaseFamily(this, ValidationContext, OutWarnings, TEXT("RunDatabases"), RunDatabases);
	ValidateGaitDatabaseFamily(this, ValidationContext, OutWarnings, TEXT("SprintDatabases"), SprintDatabases);
	return OutWarnings.Num() == InitialWarningCount;
}

bool UProject_JMotionMatchingAssetSet::ValidateCombatStrafeForProjectJLocomotion(
	const UObject* ValidationContext,
	TArray<FString>& OutWarnings) const
{
	const int32 InitialWarningCount = OutWarnings.Num();
	ValidateDatabaseSlot(this, ValidationContext, OutWarnings, TEXT("Combat.DefaultPoseSearchDatabase"), DefaultPoseSearchDatabase.Get());
	ValidateDatabaseSlot(this, ValidationContext, OutWarnings, TEXT("Combat.IdlePoseSearchDatabase"), IdlePoseSearchDatabase.Get());
	ValidateDatabaseSlot(this, ValidationContext, OutWarnings, TEXT("Combat.RunDatabases.Cycle"), RunDatabases.Cycle.Get());
	ValidateDatabaseSlot(this, ValidationContext, OutWarnings, TEXT("Combat.SprintDatabases.Cycle"), SprintDatabases.Cycle.Get());
	return OutWarnings.Num() == InitialWarningCount;
}
