#include "Combat/Project_JAttackDefinition.h"
#include "Animation/Skeleton.h"
#include "GameplayEffect.h"
#include "GameplayEffectComponents/AdditionalEffectsGameplayEffectComponent.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Project_JAnimNotifyState_MeleeHit.h"
#include "Animation/Project_JAnimNotifyState_CombatPresentationCue.h"
#include "Engine/SkeletalMesh.h"

#if WITH_EDITOR
#include "Movement/Project_JMontageLocomotionHandoff.h"
#include "Validation/Project_JDataValidation.h"
#endif

FString UProject_JAttackDefinition::GetMeleeTrajectoryFailure(const USkeletalMesh* SourceMesh) const
{
    const auto& Path=MeleeTrajectory;
    if (!Path.bEnabled) return {};
    if (const FString Failure=Path.GetValidationFailure(); !Failure.IsEmpty()) return Failure;
    if (!bMontageDriven || !Montage || MovementPolicy==EProject_JAttackMovementPolicy::RootMotionWarped || !FMath::IsFinite(PlayRate) || PlayRate<=0 ||
        !SourceMesh || Path.SourceMesh.ToSoftObjectPath()!=FSoftObjectPath(SourceMesh) || Path.SourceMontage.ToSoftObjectPath()!=FSoftObjectPath(Montage.Get()))
        return TEXT("Enabled melee path requires its selected mesh, montage and forward unwarped execution.");
    if (!Montage->Notifies.IsValidIndex(Path.NotifyIndex) || Montage->Notifies.Num()>64 || Montage->SlotAnimTracks.Num()!=1)
        return TEXT("Enabled melee path has an unsupported montage or missing notify.");
    const auto& Event=Montage->Notifies[Path.NotifyIndex];
    const auto* Notify=Cast<UProject_JAnimNotifyState_MeleeHit>(Event.NotifyStateClass.Get());
    const auto& Segments=Montage->SlotAnimTracks[0].AnimTrack.AnimSegments;
    if (Event.IsBranchingPoint() || Event.GetLinkedMontage()!=Montage.Get() || !Notify || Notify->GetTraceSocketName()!=Path.SocketName ||
        Event.GetTriggerTime()!=Path.WindowStart || Event.GetEndTriggerTime()!=Path.WindowEnd ||
        Path.bUseBladeTipOffset!=HitSpec.bUseCanonicalBladeTipOffset || Path.BladeTipOffset!=(HitSpec.bUseCanonicalBladeTipOffset?HitSpec.CanonicalBladeTipOffset:FVector::ZeroVector) ||
        Segments.Num()!=1 || Segments[0].LoopingCount!=1 || Segments[0].AnimPlayRate<=0 ||
        Path.WindowStart<Segments[0].StartPos || Path.WindowEnd>Segments[0].GetEndPos())
        return TEXT("Enabled melee path no longer matches its queued source window, segment, socket or tip.");
    const auto* Sequence=Cast<UAnimSequence>(Segments[0].GetAnimReference());
    if (!Sequence || Sequence->IsValidAdditive() || !IsValid(Sequence->GetSkeleton()) ||
        Sequence->GetSkeleton()!=Montage->GetSkeleton() || !IsValid(SourceMesh->GetSkeleton()))
        return TEXT("Enabled melee path requires one non-additive sequence on its montage skeleton and a valid selected mesh skeleton.");
    const USkeleton* SourceSkeleton=Sequence->GetSkeleton();
    const USkeleton* TargetSkeleton=SourceMesh->GetSkeleton();
    if (SourceSkeleton!=TargetSkeleton)
    {
        // Admission only: no soft loads or pose evaluation. The offline builder
        // validates RAW index layout and stamps both skeletons' retarget inputs.
        const auto Declares=[](const USkeleton* A,const USkeleton* B)
        {
            const FSoftObjectPath Path(B);
            return A->GetCompatibleSkeletons().ContainsByPredicate([&](const TSoftObjectPtr<USkeleton>& Other)
                { return Other.ToSoftObjectPath()==Path; });
        };
        if (!Declares(TargetSkeleton,SourceSkeleton) && !Declares(SourceSkeleton,TargetSkeleton))
            return TEXT("Enabled melee path selected skeletons no longer declare native compatibility.");
    }
    return {};
}

const UProject_JAttackDefinition* UProject_JAttackSet::FindAttack(const FGameplayTag AttackTag) const
{
	if (!AttackTag.IsValid())
	{
		return nullptr;
	}

	for (const UProject_JAttackDefinition* Attack : Attacks)
	{
		if (Attack && Attack->AttackTag.MatchesTagExact(AttackTag))
		{
			return Attack;
		}
	}
	return nullptr;
}

#if WITH_EDITOR
EDataValidationResult UProject_JAttackDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	bool bHasError = Result == EDataValidationResult::Invalid;
	if (const FString Failure=MeleeTrajectory.GetValidationFailure(); !Failure.IsEmpty())
	{ Project_J::DataValidation::AddError(Context,bHasError,FText::FromString(Failure)); }
	if (!AttackTag.IsValid())
	{
		Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJAttackDefinition", "MissingTag", "AttackTag is required."));
	}
	if ((bMontageDriven || MovementPolicy != EProject_JAttackMovementPolicy::InPlace) && !Montage)
	{
		Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJAttackDefinition", "MissingMontage", "Montage is required."));
	}
	if (!FMath::IsFinite(PlayRate) || PlayRate <= 0.0f)
	{
		Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJAttackDefinition", "InvalidPlayRate", "PlayRate must be greater than zero."));
	}
	if (!MontageSectionName.IsNone())
	{
		const UAnimMontage* LoadedMontage = Montage.Get();
		if (!LoadedMontage || LoadedMontage->GetSectionIndex(MontageSectionName) == INDEX_NONE)
		{
			Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJAttackDefinition", "MissingSection", "MontageSectionName does not exist in the referenced montage."));
		}
	}
	if (MovementPolicy == EProject_JAttackMovementPolicy::RootMotionWarped)
	{
		Project_J::DataValidation::AddWarning(Context, NSLOCTEXT("ProjectJAttackDefinition", "WarpExecutor", "RootMotionWarped requires an executor that configures Motion Warping targets; native Melee does not supply targets."));
	}
	if (Montage && MovementPolicy == EProject_JAttackMovementPolicy::RootMotionMontage && !Montage->HasRootMotion())
	{
		Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJAttackDefinition", "MissingMontageRootMotion",
			"RootMotionMontage requires a montage with an enabled root-motion animation sequence."));
	}
	if (const auto* Handoff = Montage ? Montage->FindMetaDataByClass<UProject_JMontageLocomotionHandoff>() : nullptr)
	{
		if (const FString Failure = Handoff->GetValidationFailure(*Montage); !Failure.IsEmpty())
		{
			Project_J::DataValidation::AddError(Context, bHasError, FText::FromString(Failure));
		}
		if (Handoff->RecoveryStart >= 0.f && Montage->Notifies.ContainsByPredicate([Handoff](const FAnimNotifyEvent& Notify)
		{
			return Notify.GetEndTriggerTime() > Handoff->RecoveryStart && Notify.NotifyStateClass
				&& (Notify.NotifyStateClass->IsA<UProject_JAnimNotifyState_MeleeHit>()
					|| Notify.NotifyStateClass->IsA<UProject_JAnimNotifyState_CombatPresentationCue>());
		}))
		{
			Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJAttackDefinition", "EarlyRecovery",
				"Locomotion recovery must follow the melee hit and combat presentation windows."));
		}
	}
	if (bOverrideGripIK && (!FMath::IsFinite(PrimaryGripIKAlpha) || PrimaryGripIKAlpha < 0.0f || PrimaryGripIKAlpha > 1.0f ||
		!FMath::IsFinite(SecondaryGripIKAlpha) || SecondaryGripIKAlpha < 0.0f || SecondaryGripIKAlpha > 1.0f))
	{
		Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJAttackDefinition", "InvalidGripAlpha", "Attack grip IK alphas must be between zero and one."));
	}
	if (!FMath::IsFinite(HitSpec.TraceDistance) || HitSpec.TraceDistance <= 0.0f ||
		!FMath::IsFinite(HitSpec.TraceRadius) || HitSpec.TraceRadius < 0.0f)
	{
		Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJAttackDefinition", "InvalidTraceDimensions", "TraceDistance must be positive and TraceRadius must be non-negative; TraceDistance bounds submitted root travel per frame, not blade reach."));
	}
	if (NPCMaxMeleeTargets < 1 || NPCMaxMeleeTargets > MaxNPCMeleeTargets)
	{
		Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJAttackDefinition", "InvalidNPCTargetLimit",
			"NPCMaxMeleeTargets must be between 1 and 32; 1 retains the locked-target NPC attack."));
	}
	if (HitSpec.bUseCanonicalBladeTipOffset &&
		(HitSpec.CanonicalBladeTipOffset.IsNearlyZero() || HitSpec.CanonicalBladeTipOffset.ContainsNaN()))
	{
		Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJAttackDefinition", "InvalidCanonicalBladeTip", "CanonicalBladeTipOffset must be finite and non-zero when enabled; set it relative to the Melee Hit Notify Leader SocketName."));
	}
	if (!DamageEffect)
	{
		Project_J::DataValidation::AddWarning(Context, NSLOCTEXT("ProjectJAttackDefinition", "MissingDamageEffect", "DamageEffect is empty; this attack will not apply server damage."));
	}
	else if (const auto* Effect = DamageEffect->GetDefaultObject<UGameplayEffect>();
		Effect->Modifiers.IsEmpty() && Effect->Executions.IsEmpty() &&
		(!Effect->FindComponent<UAdditionalEffectsGameplayEffectComponent>() ||
			Effect->FindComponent<UAdditionalEffectsGameplayEffectComponent>()->OnApplicationGameplayEffects.IsEmpty()))
	{
		Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJAttackDefinition", "NoDamagePayload",
			"DamageEffect has no modifier, execution or conditional effect. A persistent combat-state effect cannot serve as damage."));
	}
	return Project_J::DataValidation::MakeResult(Result, bHasError);
}

EDataValidationResult UProject_JAttackSet::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	bool bHasError = Result == EDataValidationResult::Invalid;
	TSet<FGameplayTag> SeenTags;
	for (int32 Index = 0; Index < Attacks.Num(); ++Index)
	{
		const UProject_JAttackDefinition* Attack = Attacks[Index];
		if (!Attack)
		{
			Project_J::DataValidation::AddError(Context, bHasError, FText::Format(NSLOCTEXT("ProjectJAttackSet", "NullAttack", "Attacks[{0}] is null."), FText::AsNumber(Index)));
			continue;
		}
		if (SeenTags.Contains(Attack->AttackTag))
		{
			Project_J::DataValidation::AddError(Context, bHasError, FText::Format(NSLOCTEXT("ProjectJAttackSet", "DuplicateAttack", "AttackTag '{0}' is duplicated."), FText::FromString(Attack->AttackTag.ToString())));
		}
		SeenTags.Add(Attack->AttackTag);
	}
	return Project_J::DataValidation::MakeResult(Result, bHasError);
}
#endif
