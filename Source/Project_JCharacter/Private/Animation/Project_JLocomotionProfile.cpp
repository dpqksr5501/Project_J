// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JLocomotionProfile.h"

#if WITH_EDITOR
#include "Chooser.h"
#include "FloatRangeColumn.h"
#include "ObjectChooser_Asset.h"
#include "Validation/Project_JDataValidation.h"
#endif

#if WITH_EDITOR
namespace
{
bool BindsLiveGroundSpeed(const FFloatRangeColumn& Column)
{
	if (!Column.InputValue.IsValid() ||
		Column.InputValue.GetScriptStruct() != FFloatContextProperty::StaticStruct())
	{
		return false;
	}

	const FFloatContextProperty& Property = Column.InputValue.Get<FFloatContextProperty>();
	return Property.Binding.PropertyBindingChain.Contains(FName(TEXT("GetThreadSafeGroundSpeed")));
}

void GatherReferencedChooserTables(
	const UChooserTable& Table,
	TArray<const UChooserTable*>& OutReferencedTables)
{
	for (const TObjectPtr<UChooserTable>& NestedChooser : Table.NestedChoosers)
	{
		if (NestedChooser)
		{
			OutReferencedTables.Add(NestedChooser);
		}
	}

	for (const FInstancedStruct& Result : Table.ResultsStructs)
	{
		if (!Result.IsValid())
		{
			continue;
		}

		if (Result.GetScriptStruct() == FAssetChooser::StaticStruct())
		{
			if (const UChooserTable* ReferencedTable = Cast<UChooserTable>(Result.Get<FAssetChooser>().Asset))
			{
				OutReferencedTables.Add(ReferencedTable);
			}
		}
		else if (Result.GetScriptStruct() == FEvaluateChooser::StaticStruct())
		{
			if (const UChooserTable* ReferencedTable = Result.Get<FEvaluateChooser>().Chooser)
			{
				OutReferencedTables.Add(ReferencedTable);
			}
		}
		else if (Result.GetScriptStruct() == FNestedChooser::StaticStruct())
		{
			if (const UChooserTable* ReferencedTable = Result.Get<FNestedChooser>().Chooser)
			{
				OutReferencedTables.Add(ReferencedTable);
			}
		}
	}
}

void ValidateStateControllerLandingChoosers(
	const UChooserTable* RootTable,
	FDataValidationContext& Context,
	bool& bHasError)
{
	if (!RootTable)
	{
		return;
	}

	TArray<const UChooserTable*> PendingTables{RootTable};
	TSet<const UChooserTable*> VisitedTables;
	while (!PendingTables.IsEmpty())
	{
		const UChooserTable* Table = PendingTables.Pop(EAllowShrinking::No);
		if (!Table || VisitedTables.Contains(Table))
		{
			continue;
		}
		VisitedTables.Add(Table);

		const bool bLandingTable = Table->GetName().Contains(TEXT("Land"), ESearchCase::IgnoreCase);
		if (bLandingTable)
		{
			for (const FInstancedStruct& ColumnStruct : Table->ColumnsStructs)
			{
				if (ColumnStruct.IsValid() &&
					ColumnStruct.GetScriptStruct() == FFloatRangeColumn::StaticStruct() &&
					BindsLiveGroundSpeed(ColumnStruct.Get<FFloatRangeColumn>()))
				{
					Project_J::DataValidation::AddError(
						Context,
						bHasError,
						FText::Format(
							NSLOCTEXT(
								"ProjectJLocomotionProfile",
								"LiveGroundSpeedLandingChooser",
								"Landing Chooser '{0}' filters by live GetThreadSafeGroundSpeed. Use the latched Walk/Run/Sprint landing semantics instead; live remote smoothing can leave no matching row near a speed boundary."),
							FText::FromString(Table->GetPathName())));
				}
			}
		}

		GatherReferencedChooserTables(*Table, PendingTables);
	}
}
}
#endif

bool FProject_JMotionMatchingSearchPolicy::ShouldSearchEveryUpdate(
	EProject_JLocomotionPhaseFamily PhaseFamily,
	bool bIsFallOffStart) const
{
	switch (PhaseFamily)
	{
	case EProject_JLocomotionPhaseFamily::JumpStart:
		return bSearchJumpStartEveryUpdate;

	case EProject_JLocomotionPhaseFamily::Fall:
		return bIsFallOffStart
			? bSearchFallOffEveryUpdate
			: bSearchAirborneLoopEveryUpdate;

	case EProject_JLocomotionPhaseFamily::Landing:
		return bSearchLandingEveryUpdate;

	case EProject_JLocomotionPhaseFamily::Start:
		return bSearchStartEveryUpdate;

	case EProject_JLocomotionPhaseFamily::Stop:
		return bSearchStopEveryUpdate;

	default:
		return true;
	}
}

float FProject_JMotionMatchingSearchPolicy::ResolveSearchThrottleTime(
	EProject_JLocomotionPhaseFamily PhaseFamily,
	bool bIsFallOffStart,
	float DefaultSearchThrottleTime,
	bool bDatabaseChanged) const
{
	return ShouldSearchEveryUpdate(PhaseFamily, bIsFallOffStart) || bDatabaseChanged
		? FMath::Max(0.0f, DefaultSearchThrottleTime)
		: FMath::Max(0.0f, SuppressedSearchThrottleTime);
}

float FProject_JMotionMatchingSearchPolicy::ResolveBlendTime(
	bool bIsInAir,
	bool bWasInAir,
	float VerticalSpeed) const
{
	// Mirrors GASP Get_MMBlendTime. This is a presentation policy only: phase
	// ownership and Start/Stop completion remain in the locomotion component.
	if (!bIsInAir)
	{
		return FMath::Max(0.0f, bWasInAir ? LandingBlendTime : DefaultBlendTime);
	}

	return FMath::Max(0.0f, VerticalSpeed > 100.0f ? JumpBlendTime : AirBlendTime);
}

UProject_JLocomotionProfile::UProject_JLocomotionProfile()
{
	FootPlacementPlantSettingsStops.SpeedThreshold = 80.0f;
	FootPlacementPlantSettingsStops.UnplantRadius = 25.0f;
	FootPlacementPlantSettingsStops.UnplantAngle = 35.0f;
	FootPlacementPlantSettingsStops.ReplantRadiusRatio = 0.5f;
	FootPlacementPlantSettingsStops.ReplantAngleRatio = 0.65f;

	FootPlacementInterpolationSettingsStops.UnplantLinearStiffness = 500.0f;
	FootPlacementInterpolationSettingsStops.UnplantAngularStiffness = 700.0f;
	FootPlacementInterpolationSettingsStops.FloorLinearStiffness = 1200.0f;
	FootPlacementInterpolationSettingsStops.FloorAngularStiffness = 650.0f;
}

#if WITH_EDITOR
EDataValidationResult UProject_JLocomotionProfile::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	bool bHasError = Result == EDataValidationResult::Invalid;
	ValidateStateControllerLandingChoosers(
		MotionMatchingSearchPolicy.StateControllerAnimationChooserTable,
		Context,
		bHasError);
	return Project_J::DataValidation::MakeResult(Result, bHasError);
}
#endif

FProject_JAnimationBudgetSettings UProject_JLocomotionProfile::GetResolvedAnimationBudgetSettings() const
{
	FProject_JAnimationBudgetSettings ResolvedSettings = AnimationBudget;
	ResolvedSettings.NearDistance = NearMotionMatchingDistance;
	ResolvedSettings.MidDistance = MidMotionMatchingDistance;
	ResolvedSettings.FarDistance = FarMotionMatchingDistance;
	ResolvedSettings.MidUpdateInterval = MidMotionMatchingUpdateInterval;
	ResolvedSettings.FarUpdateInterval = FarMotionMatchingUpdateInterval;
	ResolvedSettings.HiddenUpdateInterval = AnimInstanceHiddenRemoteUpdateInterval;
	ResolvedSettings.bDisableMotionMatchingBeyondFarDistance = bDisableMotionMatchingBeyondFarDistance;
	return ResolvedSettings;
}
