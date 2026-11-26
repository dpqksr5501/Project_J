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

FName UProject_JAttackDefinition::GetPlaybackEntrySection() const
{
	if (MontageSectionName.IsNone() && WarningPolicy == EProject_JAttackWarningPolicy::MontageAttention && Montage)
		return Montage->GetSectionName(Montage->GetSectionIndexFromPosition(0.f));
	return MontageSectionName;
}

FString UProject_JAttackDefinition::GetWarningContractFailure() const
{
	if (WarningPolicy == EProject_JAttackWarningPolicy::GroundArea)
	{
		return !bMontageDriven && DamageEffect && AttackTag.IsValid() && MovementPolicy == EProject_JAttackMovementPolicy::InPlace
			? FString()
			: TEXT("GroundArea needs a non-montage, in-place attack with an attack tag and a damage effect.");
	}
	if (WarningPolicy != EProject_JAttackWarningPolicy::MontageAttention) return {};
	if (!bMontageDriven || !Montage || !RequiredWarningCue.IsValid())
		return TEXT("MontageAttention requires a montage-driven attack, its montage and RequiredWarningCue.");
	const float EffectiveRate = PlayRate * Montage->RateScale;
	if (!FMath::IsFinite(PlayRate) || PlayRate <= 0.f || !FMath::IsFinite(EffectiveRate) || EffectiveRate <= 0.f)
		return TEXT("The required warning needs finite forward montage playback.");
	const int32 Section = Montage->GetSectionIndex(GetPlaybackEntrySection());
	if (Section == INDEX_NONE) return TEXT("The required warning's entry section does not exist.");
	float EntryStart, EntryEnd;
	Montage->GetSectionStartAndEndTime(Section, EntryStart, EntryEnd);
	float FirstHit = EntryEnd;
	float WarningStart = EntryEnd;
	int32 Producers = 0;
	for (const FAnimNotifyEvent& Event : Montage->Notifies)
	{
		const float Time = Event.GetTime();
		if (!(Time >= EntryStart && Time < EntryEnd)) continue;
		if (Cast<UProject_JAnimNotifyState_MeleeHit>(Event.NotifyStateClass))
			FirstHit = FMath::Min(FirstHit, Event.GetTriggerTime());
		const auto* Cue = Cast<UProject_JAnimNotifyState_CombatPresentationCue>(Event.NotifyStateClass);
		if (!Cue || !Cue->IsGameplayWarning() || Cue->GetCueTag() != RequiredWarningCue) continue;
		if (const FString Failure = Cue->GetWarningEventFailure(Event); !Failure.IsEmpty()) return Failure;
		if (Event.GetTime() + Event.GetDuration() > EntryEnd + KINDA_SMALL_NUMBER)
			return TEXT("The required warning must finish within the attack entry section.");
		++Producers;
		WarningStart = Event.GetTriggerTime();
	}
	if (Producers != 1) return FString::Printf(TEXT("Expected one Gameplay Warning '%s' in the attack entry section; found %d."), *RequiredWarningCue.ToString(), Producers);
	if (!(WarningStart < FirstHit)) return TEXT("The required warning must begin before the first melee hit window.");
	return {};
}

FString UProject_JAttackDefinition::GetMeleeTrajectoryFailure(const USkeletalMesh* SourceMesh) const
{
    if (!MeleeTrajectory.bEnabled) return {};
    if (const FString Failure = GetMeleeTrajectoryFailure(SourceMesh, MeleeTrajectory); !Failure.IsEmpty()) return Failure;
    if (AdditionalMeleeTrajectories.Num() >= Montage->Notifies.Num())
        return TEXT("Enabled attack has too many additional melee paths.");
    int32 PreviousIndex = MeleeTrajectory.NotifyIndex;
    float PreviousEnd = MeleeTrajectory.WindowEnd;
    for (const auto& Path : AdditionalMeleeTrajectories)
    {
        if (Path.NotifyIndex <= PreviousIndex || Path.WindowStart < PreviousEnd)
            return TEXT("Additional melee paths require unique ordered and disjoint notify windows.");
        if (const FString Failure = GetMeleeTrajectoryFailure(SourceMesh, Path); !Failure.IsEmpty()) return Failure;
        PreviousIndex = Path.NotifyIndex;
        PreviousEnd = Path.WindowEnd;
    }
    return {};
}

const FProject_JMeleeTrajectory* UProject_JAttackDefinition::FindMeleeTrajectory(const FAnimNotifyEvent& Event) const
{
    if (!MeleeTrajectory.bEnabled || !Montage || Event.GetLinkedMontage() != Montage.Get()) return nullptr;
    const auto Matches = [&](const FProject_JMeleeTrajectory& Path)
    {
        return Path.bEnabled && Montage->Notifies.IsValidIndex(Path.NotifyIndex)
            && Montage->Notifies[Path.NotifyIndex].NotifyStateClass == Event.NotifyStateClass
            && Path.WindowStart == Event.GetTriggerTime() && Path.WindowEnd == Event.GetEndTriggerTime();
    };
    if (Matches(MeleeTrajectory)) return &MeleeTrajectory;
    return AdditionalMeleeTrajectories.FindByPredicate(Matches);
}

FString UProject_JAttackDefinition::GetMeleeTrajectoryFailure(const USkeletalMesh* SourceMesh, const FProject_JMeleeTrajectory& Path) const
{
    if (const FString Failure=Path.GetValidationFailure(); !Failure.IsEmpty()) return Failure;
    if (!Path.bEnabled || !bMontageDriven || !Montage || !MontageSectionName.IsNone() ||
        MovementPolicy==EProject_JAttackMovementPolicy::RootMotionWarped || !FMath::IsFinite(PlayRate) || PlayRate<=0 ||
        !SourceMesh || Path.SourceMesh.ToSoftObjectPath()!=FSoftObjectPath(SourceMesh) || Path.SourceMontage.ToSoftObjectPath()!=FSoftObjectPath(Montage.Get()))
        return TEXT("Enabled melee path requires its selected mesh, montage and forward unwarped execution.");
    if (!Montage->Notifies.IsValidIndex(Path.NotifyIndex) || Montage->Notifies.Num()>64 || Montage->SlotAnimTracks.Num()!=1)
        return TEXT("Enabled melee path has an unsupported montage or missing notify.");
    const auto& Event=Montage->Notifies[Path.NotifyIndex];
    const auto* Notify=Cast<UProject_JAnimNotifyState_MeleeHit>(Event.NotifyStateClass.Get());
    const FAnimSegment* Segment = FProject_JMeleeTrajectory::FindWindowSegment(*Montage, Path.WindowStart, Path.WindowEnd);
    if (Event.IsBranchingPoint() || Event.GetLinkedMontage()!=Montage.Get() || !Notify || Notify->GetTraceSocketName()!=Path.SocketName ||
        Event.GetTriggerTime()!=Path.WindowStart || Event.GetEndTriggerTime()!=Path.WindowEnd ||
        Path.bUseBladeTipOffset!=HitSpec.bUseCanonicalBladeTipOffset || Path.BladeTipOffset!=(HitSpec.bUseCanonicalBladeTipOffset?HitSpec.CanonicalBladeTipOffset:FVector::ZeroVector) ||
        !Segment || Segment->LoopingCount!=1 || !FMath::IsFinite(Segment->AnimPlayRate) || Segment->AnimPlayRate<=0 ||
        Event.NotifyStateClass->GetClass()!=UProject_JAnimNotifyState_MeleeHit::StaticClass())
        return TEXT("Enabled melee path no longer matches its queued source window, segment, socket or tip.");
    const auto* Sequence=Cast<UAnimSequence>(Segment->GetAnimReference());
    if (!Sequence || Sequence->IsValidAdditive() ||
        !FProject_JMeleeTrajectory::HasDeclaredSkeletonCompatibility(Sequence->GetSkeleton(), SourceMesh->GetSkeleton()) ||
        !FProject_JMeleeTrajectory::HasDeclaredSkeletonCompatibility(Sequence->GetSkeleton(), Montage->GetSkeleton()))
        return TEXT("Enabled melee path requires a non-additive sequence compatible with the montage and mesh skeletons.");
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
	if (const FString Failure = GetWarningContractFailure(); !Failure.IsEmpty())
		Project_J::DataValidation::AddError(Context, bHasError, FText::FromString(Failure));
	if (WarningPolicy == EProject_JAttackWarningPolicy::Legacy &&
		(Context.GetValidationUsecase() == EDataValidationUsecase::Save || Context.GetValidationUsecase() == EDataValidationUsecase::PreSubmit))
	{
		Project_J::DataValidation::AddWarning(Context, NSLOCTEXT("ProjectJAttackDefinition", "UnclassifiedWarningPolicy",
			"WarningPolicy is Legacy. Classify this attack when editing its warning behavior; existing execution is preserved."));
	}
	if (const FString Failure=MeleeTrajectory.GetValidationFailure(); !Failure.IsEmpty())
	{ Project_J::DataValidation::AddError(Context,bHasError,FText::FromString(Failure)); }
	if (!MeleeTrajectory.bEnabled && !AdditionalMeleeTrajectories.IsEmpty())
	{ Project_J::DataValidation::AddError(Context,bHasError,NSLOCTEXT("ProjectJAttackDefinition", "MissingPrimaryTrajectory", "Additional melee windows require an enabled primary trajectory.")); }
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
