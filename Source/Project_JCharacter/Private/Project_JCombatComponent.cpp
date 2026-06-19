// Fill out your copyright notice in the Description page of Project Settings.

#include "Project_JCombatComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameplayEffect.h"
#include "Combat/Project_JServerSideRewindComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"

UProject_JCombatComponent::UProject_JCombatComponent()
{
	// Disable ticking by default as it's not needed for base combat capabilities
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UProject_JCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* Owner = GetOwner())
	{
		BindToGAS(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Owner));
	}
}

void UProject_JCombatComponent::Attack()
{
	const FGameplayTag EffectiveInputTag = PrimaryAttackInputTag.IsValid()
		? PrimaryAttackInputTag
		: FProject_JGameplayTags::Get().InputTag_Weapon_LightAttack;
	if (!TryActivateAbilityByInputTag(EffectiveInputTag))
	{
		TryActivateAbilityByTag(PrimaryAttackAbilityTag);
	}
}

void UProject_JCombatComponent::BindToGAS(UAbilitySystemComponent* ASC)
{
	OwnerASC = ASC;
}

bool UProject_JCombatComponent::TryActivateAbilityByInputTag(const FGameplayTag& InputTag) const
{
	UProject_JAbilitySystemComponent* ProjectJASC = Cast<UProject_JAbilitySystemComponent>(OwnerASC);
	return ProjectJASC && InputTag.IsValid()
		? ProjectJASC->TryActivateAbilitiesByInputTag(InputTag)
		: false;
}

bool UProject_JCombatComponent::TryActivateAbilityByTag(const FGameplayTag& AbilityTag) const
{
	if (!OwnerASC || !AbilityTag.IsValid())
	{
		return false;
	}

	FGameplayTagContainer AbilityTags;
	AbilityTags.AddTag(AbilityTag);
	return OwnerASC->TryActivateAbilitiesByTag(AbilityTags);
}

void UProject_JCombatComponent::ServerRequestSSRHit_Implementation(AActor* HitActor, float ClientTimestamp, FVector TraceStart, FVector TraceEnd)
{
	FProject_JCombatHitRequest Request;
	Request.Target = HitActor;
	Request.ClientTimestamp = ClientTimestamp;
	Request.TraceStart = TraceStart;
	Request.TraceEnd = TraceEnd;

	const FProject_JCombatHitValidationResult ValidationResult = ValidateServerHitRequest(Request);
	if (!ValidationResult.bAccepted)
	{
		return;
	}

	bool bHitConfirmed = false;
	if (UProject_JServerSideRewindComponent* SSRComp = HitActor->FindComponentByClass<UProject_JServerSideRewindComponent>())
	{
		bHitConfirmed = SSRComp->ServerVerifyHit(ClientTimestamp, TraceStart, TraceEnd);
	}
	else
	{
		// Targets without rewind history are validated against their current bounds.
		bHitConfirmed = HitActor->GetComponentsBoundingBox().Intersect(
			FBox(TraceStart.ComponentMin(TraceEnd), TraceStart.ComponentMax(TraceEnd)).ExpandBy(25.0f));
	}

	if (!bHitConfirmed || !ConfirmedHitGameplayEffect || !OwnerASC)
	{
		return;
	}

	UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(HitActor);
	if (!TargetASC)
	{
		return;
	}

	FGameplayEffectContextHandle EffectContext = OwnerASC->MakeEffectContext();
	EffectContext.AddInstigator(GetOwner(), GetOwner());
	const FGameplayEffectSpecHandle SpecHandle = OwnerASC->MakeOutgoingSpec(ConfirmedHitGameplayEffect, 1.0f, EffectContext);
	if (SpecHandle.IsValid())
	{
		OwnerASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);
	}
}

FProject_JCombatHitValidationResult UProject_JCombatComponent::ValidateServerHitRequest(
	const FProject_JCombatHitRequest& Request) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return FProject_JCombatHitValidationResult::Rejected(EProject_JCombatHitValidationFailure::InvalidRequester);
	}

	if (const EProject_JCombatHitValidationFailure Failure =
			HitValidationPolicy.ValidateRequestData(Request, World->GetTimeSeconds());
		Failure != EProject_JCombatHitValidationFailure::None)
	{
		return FProject_JCombatHitValidationResult::Rejected(Failure);
	}

	if (const EProject_JCombatHitValidationFailure Failure =
			HitValidationPolicy.ValidateActors(GetOwner(), Request);
		Failure != EProject_JCombatHitValidationFailure::None)
	{
		return FProject_JCombatHitValidationResult::Rejected(Failure);
	}

	return FProject_JCombatHitValidationResult::Accepted();
}
