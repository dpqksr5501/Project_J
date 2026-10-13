#include "Animation/Project_JRetargetBodyProfile.h"
#include "Animation/Skeleton.h"
#include "Retargeter/IKRetargeter.h"

bool UProject_JRetargetFamilyProfile::Matches(const USkeleton* Source, const USkeleton* Target) const
{
	return Source && Target && SourceSkeleton == Source && TargetSkeleton == Target && Retargeter;
}

#if WITH_EDITOR
#include "Misc/DataValidation.h"

EDataValidationResult UProject_JRetargetFamilyProfile::IsDataValid(FDataValidationContext& Context) const
{
	const auto Base = Super::IsDataValid(Context);
	if (!SourceSkeleton || !TargetSkeleton || !Retargeter || !Retargeter->HasSourceIKRig() || !Retargeter->HasTargetIKRig())
	{
		Context.AddError(NSLOCTEXT("ProjectJRetarget", "IncompleteFamily", "A retarget family requires source/target skeletons and a retargeter with both IK rigs."));
		return EDataValidationResult::Invalid;
	}
	return Base == EDataValidationResult::Invalid ? Base : EDataValidationResult::Valid;
}

EDataValidationResult UProject_JRetargetBodyProfile::IsDataValid(FDataValidationContext& Context) const
{
	const auto Base = Super::IsDataValid(Context);
	if (!Family)
	{
		Context.AddError(NSLOCTEXT("ProjectJRetarget", "MissingFamily", "A body profile requires a skeleton family."));
		return EDataValidationResult::Invalid;
	}
	TSet<FName> Names;
	if (bOverrideRetargetSets) for (FName Name : RetargetOverrideSets)
	{
		if (Name.IsNone() || Names.Contains(Name) || !Family->Retargeter || !Family->Retargeter->GetOverrideSetByName(Name))
		{
			Context.AddError(NSLOCTEXT("ProjectJRetarget", "InvalidSets", "Override set names must be nonempty, unique and present in the family's Retargeter."));
			return EDataValidationResult::Invalid;
		}
		Names.Add(Name);
	}
	return Base == EDataValidationResult::Invalid ? Base : EDataValidationResult::Valid;
}
#endif
