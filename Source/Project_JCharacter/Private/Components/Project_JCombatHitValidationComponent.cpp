#include "Components/Project_JCombatHitValidationComponent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Combat/Project_JServerSideRewindComponent.h"
#include "Combat/Project_JAttackDefinition.h"
#include "GameplayEffect.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJCombatHitValidation, Log, All);

UProject_JCombatHitValidationComponent::UProject_JCombatHitValidationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UProject_JCombatHitValidationComponent::BeginAttackNode(const FGameplayTag AttackNodeTag, UProject_JAttackDefinition* AttackDefinition)
{
	ActiveAttackNodeTag = AttackNodeTag;
	ActiveAttackDefinition = AttackDefinition;
	bHitWindowOpen = false;
	ServerHitActors.Reset();
}

void UProject_JCombatHitValidationComponent::EndAttack()
{
	ActiveAttackNodeTag = FGameplayTag();
	ActiveAttackDefinition = nullptr;
	bHitWindowOpen = false;
	ServerHitActors.Reset();
}

void UProject_JCombatHitValidationComponent::SetHitWindowOpen(const bool bOpen)
{
	bHitWindowOpen = bOpen && ActiveAttackNodeTag.IsValid();
}

void UProject_JCombatHitValidationComponent::SubmitPredictedHit(AActor* HitActor, const float ClientTimestamp, const FVector& TraceStart, const FVector& TraceEnd)
{
	if (!HitActor || !ActiveAttackNodeTag.IsValid() || !bHitWindowOpen)
	{
		return;
	}

	ServerRequestSSRHit(HitActor, ClientTimestamp, TraceStart, TraceEnd, ActiveAttackNodeTag, ++LocalRequestSequence);
}

bool UProject_JCombatHitValidationComponent::ProcessAuthorityHit(AActor* HitActor)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !HitActor || !ActiveAttackNodeTag.IsValid() || !bHitWindowOpen || ServerHitActors.Contains(HitActor))
	{
		return false;
	}

	FProject_JCombatHitRequest Request;
	Request.Target = HitActor;
	Request.AttackNodeTag = ActiveAttackNodeTag;
	if (HitValidationPolicy.ValidateActors(GetOwner(), Request) != EProject_JCombatHitValidationFailure::None)
	{
		return false;
	}

	ServerHitActors.Add(HitActor);
	return ApplyConfirmedHit(HitActor);
}

void UProject_JCombatHitValidationComponent::ServerRequestSSRHit_Implementation(AActor* HitActor, float ClientTimestamp, FVector TraceStart, FVector TraceEnd, FGameplayTag AttackNodeTag, int32 RequestSequence)
{
	FProject_JCombatHitRequest Request;
	Request.Target = HitActor;
	Request.ClientTimestamp = ClientTimestamp;
	Request.TraceStart = TraceStart;
	Request.TraceEnd = TraceEnd;
	Request.AttackNodeTag = AttackNodeTag;
	Request.RequestSequence = RequestSequence;

	if (const EProject_JCombatHitValidationFailure ActiveFailure = ValidateActiveAttack(Request); ActiveFailure != EProject_JCombatHitValidationFailure::None)
	{
		UE_LOG(LogProjectJCombatHitValidation, Verbose, TEXT("SSR active attack rejected. Requester=%s Target=%s Node=%s Reason=%s"), *GetNameSafe(GetOwner()), *GetNameSafe(HitActor), *AttackNodeTag.ToString(), LexToString(ActiveFailure));
		return;
	}

	const FProject_JCombatHitValidationResult ValidationResult = ValidateServerHitRequest(Request);
	if (!ValidationResult.bAccepted)
	{
		UE_LOG(LogProjectJCombatHitValidation, Verbose, TEXT("SSR hit request rejected. Requester=%s Target=%s Reason=%s"), *GetNameSafe(GetOwner()), *GetNameSafe(HitActor), LexToString(ValidationResult.Failure));
		return;
	}

	bool bHitConfirmed = false;
	if (UProject_JServerSideRewindComponent* SSRComp = HitActor->FindComponentByClass<UProject_JServerSideRewindComponent>())
	{
		bHitConfirmed = SSRComp->ServerVerifyHit(ClientTimestamp, TraceStart, TraceEnd);
	}
	else
	{
		bHitConfirmed = HitActor->GetComponentsBoundingBox().Intersect(FBox(TraceStart.ComponentMin(TraceEnd), TraceStart.ComponentMax(TraceEnd)).ExpandBy(25.0f));
	}

	if (!bHitConfirmed)
	{
		return;
	}

	ServerHitActors.Add(HitActor);
	ApplyConfirmedHit(HitActor);
}

EProject_JCombatHitValidationFailure UProject_JCombatHitValidationComponent::ValidateActiveAttack(const FProject_JCombatHitRequest& Request)
{
	if (!ActiveAttackNodeTag.IsValid())
	{
		return EProject_JCombatHitValidationFailure::NoActiveAttack;
	}
	if (!Request.AttackNodeTag.MatchesTagExact(ActiveAttackNodeTag))
	{
		return EProject_JCombatHitValidationFailure::AttackNodeMismatch;
	}
	if (!bHitWindowOpen)
	{
		return EProject_JCombatHitValidationFailure::HitWindowClosed;
	}
	if (ServerHitActors.Contains(Request.Target))
	{
		return EProject_JCombatHitValidationFailure::DuplicateTarget;
	}
	if (Request.RequestSequence <= LastServerRequestSequence)
	{
		return EProject_JCombatHitValidationFailure::RequestRateLimited;
	}

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (Now - RateWindowStartSeconds >= 1.0)
	{
		RateWindowStartSeconds = Now;
		RequestsInRateWindow = 0;
	}
	if (++RequestsInRateWindow > MaxRequestsPerSecond)
	{
		return EProject_JCombatHitValidationFailure::RequestRateLimited;
	}
	LastServerRequestSequence = Request.RequestSequence;
	return EProject_JCombatHitValidationFailure::None;
}

bool UProject_JCombatHitValidationComponent::ApplyConfirmedHit(AActor* HitActor)
{
	UAbilitySystemComponent* OwnerASC = ResolveOwnerAbilitySystemComponent();
	const TSubclassOf<UGameplayEffect> DamageEffect = ActiveAttackDefinition ? ActiveAttackDefinition->DamageEffect : nullptr;
	if (!HitActor || !DamageEffect || !OwnerASC)
	{
		return false;
	}

	UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(HitActor);
	if (!TargetASC)
	{
		return false;
	}

	FGameplayEffectContextHandle EffectContext = OwnerASC->MakeEffectContext();
	EffectContext.AddInstigator(GetOwner(), GetOwner());
	const FGameplayEffectSpecHandle SpecHandle = OwnerASC->MakeOutgoingSpec(DamageEffect, 1.0f, EffectContext);
	if (SpecHandle.IsValid())
	{
		OwnerASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);
		return true;
	}
	return false;
}

FProject_JCombatHitValidationResult UProject_JCombatHitValidationComponent::ValidateServerHitRequest(const FProject_JCombatHitRequest& Request) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return FProject_JCombatHitValidationResult::Rejected(EProject_JCombatHitValidationFailure::InvalidRequester);
	}
	if (const EProject_JCombatHitValidationFailure Failure = HitValidationPolicy.ValidateRequestData(Request, World->GetTimeSeconds()); Failure != EProject_JCombatHitValidationFailure::None)
	{
		return FProject_JCombatHitValidationResult::Rejected(Failure);
	}
	if (const EProject_JCombatHitValidationFailure Failure = HitValidationPolicy.ValidateActors(GetOwner(), Request); Failure != EProject_JCombatHitValidationFailure::None)
	{
		return FProject_JCombatHitValidationResult::Rejected(Failure);
	}
	return FProject_JCombatHitValidationResult::Accepted();
}

UAbilitySystemComponent* UProject_JCombatHitValidationComponent::ResolveOwnerAbilitySystemComponent() const
{
	return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
}
