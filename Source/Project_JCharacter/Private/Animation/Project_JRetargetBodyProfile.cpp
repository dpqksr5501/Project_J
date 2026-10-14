#include "Animation/Project_JRetargetBodyProfile.h"
#include "Animation/Skeleton.h"
#include "Retargeter/IKRetargeter.h"

bool UProject_JRetargetFamilyProfile::Matches(const USkeleton* Source, const USkeleton* Target) const
{
	return Source && Target && SourceSkeleton == Source && TargetSkeleton == Target && Retargeter;
}

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#include "Engine/SkeletalMesh.h"

EDataValidationResult UProject_JRetargetFamilyProfile::IsDataValid(FDataValidationContext& Context) const
{
	const auto Base = Super::IsDataValid(Context);
	if (!SourceSkeleton || !TargetSkeleton || !Retargeter || !Retargeter->HasSourceIKRig() || !Retargeter->HasTargetIKRig())
	{
		Context.AddError(NSLOCTEXT("ProjectJRetarget", "IncompleteFamily", "A retarget family requires source/target skeletons and a retargeter with both IK rigs."));
		return EDataValidationResult::Invalid;
	}
	const auto* SourceMesh = Retargeter->GetPreviewMesh(ERetargetSourceOrTarget::Source);
	const auto* TargetMesh = Retargeter->GetPreviewMesh(ERetargetSourceOrTarget::Target);
	if ((SourceMesh && SourceMesh->GetSkeleton() != SourceSkeleton) ||
		(TargetMesh && TargetMesh->GetSkeleton() != TargetSkeleton))
	{
		Context.AddError(FText::Format(NSLOCTEXT("ProjectJRetarget", "FamilyRigMismatch",
			"Family '{0}' declares a source/target skeleton pair that disagrees with the Retargeter's preview meshes. Check the source and target rig/preview assignments."), FText::FromString(GetPathName())));
		return EDataValidationResult::Invalid;
	}
	if (!SourceMesh || !TargetMesh)
	{
		Context.AddWarning(NSLOCTEXT("ProjectJRetarget", "FamilyRigUnverified", "A Retargeter preview mesh is unavailable; the declared skeleton pair could not be verified."));
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
	if (Family->IsDataValid(Context) == EDataValidationResult::Invalid) { return EDataValidationResult::Invalid; }
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
