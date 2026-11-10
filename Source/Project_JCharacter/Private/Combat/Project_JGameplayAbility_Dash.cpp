#include "Combat/Project_JGameplayAbility_Dash.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "Movement/Project_JMoverComponent.h"
#include "Project_JGameplayTags.h"
#include "Project_JPlayerCharacter.h"

FVector UProject_JGameplayAbility_Dash::ResolveForwardDashDirection(const AProject_JPlayerCharacter& Player) const
{
	FVector Forward;
	const FVector Input = Player.GetGameplayMoveDirection(&Forward);
	if (Input.IsNearlyZero() || !FMath::IsFinite(ForwardInputHalfAngleDegrees)) { return FVector::ZeroVector; }
	const double HalfAngle = FMath::Clamp(ForwardInputHalfAngleDegrees, 0.f, 90.f);
	const double MinimumDot = FMath::Cos(FMath::DegreesToRadians(HalfAngle));
	return FVector::DotProduct(Input, Forward) >= MinimumDot - UE_DOUBLE_SMALL_NUMBER ? Forward : FVector::ZeroVector;
}

UProject_JGameplayAbility_Dash::UProject_JGameplayAbility_Dash()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	const auto& Tags = FProject_JGameplayTags::Get();
	ActivationOwnedTags.AddTag(Tags.State_Dodging);
	ActivationRequiredTags.AddTag(Tags.State_CombatMode);
	ActivationBlockedTags.AddTag(Tags.State_Dodging);
	ActivationBlockedTags.AddTag(Tags.State_Attacking);
	ActivationBlockedTags.AddTag(Tags.State_HitReacting);
	ActivationBlockedTags.AddTag(Tags.State_Dead);
}

bool UProject_JGameplayAbility_Dash::CanActivateAbility(FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	const auto* Player = ActorInfo ? Cast<AProject_JPlayerCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	return Montage && Montage->HasRootMotion() && Montage->RateScale > UE_KINDA_SMALL_NUMBER &&
		TravelEndPosition > 0.f && TravelEndPosition < Montage->GetPlayLength() &&
		Player && !Player->IsMoveInputIgnored() && Player->GetMover() && Player->GetMover()->IsOnGround() &&
		!ResolveForwardDashDirection(*Player).IsNearlyZero() &&
		Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
}

void UProject_JGameplayAbility_Dash::ActivateAbility(FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData*)
{
	auto* Player = Cast<AProject_JPlayerCharacter>(GetAvatarActorFromActorInfo());
	auto* Mover = Player ? Cast<UProject_JMoverComponent>(Player->GetMover()) : nullptr;
	const FVector Direction = Player ? ResolveForwardDashDirection(*Player) : FVector::ZeroVector;
	if (!Mover || Direction.IsNearlyZero() || !CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	if (!IsActive() || GetAvatarActorFromActorInfo() != Player) { return; }

	const float OriginalDuration = Montage->GetPlayLength() / Montage->RateScale;
	const float NewDuration = FMath::Max(OriginalDuration - DurationReduction, OriginalDuration * .5f);
	const float PlayRate = OriginalDuration / NewDuration;
	auto* Task = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, Montage, PlayRate, NAME_None, true, 1.f, 0.f, true);
	Task->OnCompleted.AddDynamic(this, &ThisClass::OnCompleted);
	Task->OnInterrupted.AddDynamic(this, &ThisClass::OnInterrupted);
	Task->OnCancelled.AddDynamic(this, &ThisClass::OnInterrupted);
	Mover->StartGameplayDashMontage(Montage, Direction, TravelEndPosition, [Task] { Task->ReadyForActivation(); });
}

void UProject_JGameplayAbility_Dash::OnCompleted()
{
	// The task is already ending naturally. Retire ASC ownership before task
	// destruction so it does not issue a second montage stop against Mover.
	if (auto* ASC = GetAbilitySystemComponentFromActorInfo()) { ASC->ClearAnimatingAbility(this); }
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UProject_JGameplayAbility_Dash::OnInterrupted()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}
