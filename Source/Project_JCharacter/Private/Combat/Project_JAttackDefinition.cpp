#include "Combat/Project_JAttackDefinition.h"
#include "GameplayEffect.h"

#if WITH_EDITOR
#include "Validation/Project_JDataValidation.h"
#endif

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
	if (!FMath::IsFinite(HitSpec.TraceDistance) || HitSpec.TraceDistance <= 0.0f ||
		!FMath::IsFinite(HitSpec.TraceRadius) || HitSpec.TraceRadius < 0.0f)
	{
		Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJAttackDefinition", "InvalidTraceDimensions", "TraceDistance must be positive and TraceRadius must be non-negative; TraceDistance bounds submitted root travel per frame, not blade reach."));
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
