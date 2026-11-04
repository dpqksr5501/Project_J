// Fill out your copyright notice in the Description page of Project Settings.

#include "Project_JAttributeSet.h"
#include "Project_JAbilitySystemComponent.h"
#include "GameplayEffectAggregator.h"
#include "Net/UnrealNetwork.h"
#include "GameplayEffectExtension.h"
#include "GameFramework/Actor.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
FProject_JHealthGEExecutionProof CaptureHealthGEProof(const FGameplayEffectModCallbackData& Data)
{
	FProject_JHealthGEExecutionProof Proof;
	Proof.TargetASC = &Data.Target;
	Proof.TargetOwner = Data.Target.GetOwnerActor(); Proof.TargetAvatar = Data.Target.GetAvatarActor();
	if (const auto* ASC = Cast<UProject_JAbilitySystemComponent>(&Data.Target)) Proof.TargetEpoch = ASC->GetActorInfoRevision();
	const auto& Context = Data.EffectSpec.GetContext();
	Proof.Instigator = Context.GetOriginalInstigator(); Proof.EffectCauser = Context.GetEffectCauser();
	Proof.EffectDefinition = Data.EffectSpec.Def.Get();
	if (auto* Source = Context.GetOriginalInstigatorAbilitySystemComponent())
	{
		Proof.SourceASC = Source; Proof.SourceOwner = Source->GetOwnerActor(); Proof.SourceAvatar = Source->GetAvatarActor();
		if (const auto* ASC = Cast<UProject_JAbilitySystemComponent>(Source)) Proof.SourceEpoch = ASC->GetActorInfoRevision();
	}
	return Proof;
}
}

void UProject_JAttributeSet::RecordHealthGEObservationFailure()
{
	if (HealthGEObservationFailures != MAX_uint64) ++HealthGEObservationFailures;
}

bool UProject_JAttributeSet::CanBeginHealthGEObservation() const
{
	return GetClass() == StaticClass() && HealthGEExecutions.IsEmpty() && SuppressedHealthGEDepth == 0
		&& FScopedAggregatorOnDirtyBatch::GlobalBatchCount == 0;
}

void UProject_JAttributeSet::ReleaseUnusedHealthGEObservationStorage()
{
	check(IsInGameThread());
	if (HealthGEExecutions.IsEmpty() && !SuppressedHealthGEDepth
		&& !OnConfirmedHealthGELoss.IsBound() && !OnHealthGEObservationFailed.IsBound()) HealthGEExecutions.Empty();
}

UProject_JAttributeSet::UProject_JAttributeSet()
{
}

void UProject_JAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UProject_JAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UProject_JAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UProject_JAttributeSet, Mana, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UProject_JAttributeSet, MaxMana, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UProject_JAttributeSet, AttackPower, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UProject_JAttributeSet, Defense, COND_OwnerOnly, REPNOTIFY_Always);
}

void UProject_JAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

void UProject_JAttributeSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

void UProject_JAttributeSet::ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const
{
	if (!FMath::IsFinite(NewValue)) { NewValue = 0.0f; }

	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, FMath::Max(0.0f, GetMaxHealth()));
	}
	else if (Attribute == GetManaAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, FMath::Max(0.0f, GetMaxMana()));
	}
	else if (Attribute == GetMaxHealthAttribute() || Attribute == GetMaxManaAttribute()
		|| Attribute == GetAttackPowerAttribute() || Attribute == GetDefenseAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.0f);
	}
}

void UProject_JAttributeSet::PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
	if (Attribute == GetHealthAttribute() && !HealthGEExecutions.IsEmpty())
	{
		// Consume before any observer can apply another GE, set Health directly, retire a lease or run GC.
		FProject_JConfirmedHealthGELoss Loss;
		bool bEmit = false;
		{
			auto& Execution = HealthGEExecutions.Last();
			if (!Execution.bConsumed)
			{
				Execution.bConsumed = true;
				Loss.Proof = Execution.Proof; Loss.OldValue = OldValue; Loss.NewValue = NewValue;
				bEmit = FMath::IsFinite(OldValue) && FMath::IsFinite(NewValue) && OldValue > NewValue;
			}
		}
		if (bEmit) OnConfirmedHealthGELoss.Broadcast(Loss); // No frame/array reference crosses callbacks.
	}
	Super::PostAttributeChange(Attribute, OldValue, NewValue);
	if (!GetOwningActor() || !GetOwningActor()->HasAuthority()) { return; }
	// Absolute resource amounts are preserved on a maximum increase and clamped
	// on a decrease, including duration/infinite GE removal (which is not execute).
	if (Attribute == GetMaxHealthAttribute() && GetHealth() > NewValue) { SetHealth(NewValue); }
	else if (Attribute == GetMaxManaAttribute() && GetMana() > NewValue) { SetMana(NewValue); }
}

bool UProject_JAttributeSet::PreGameplayEffectExecute(FGameplayEffectModCallbackData& Data)
{
	if (!Super::PreGameplayEffectExecute(Data) || !FMath::IsFinite(Data.EvaluatedData.Magnitude)) return false;
	if (Data.EvaluatedData.Attribute != GetHealthAttribute()) return true;
	bool bObserve = OnConfirmedHealthGELoss.IsBound() || OnHealthGEObservationFailed.IsBound();
	// Keep pairing nested executions until an existing observed execution unwinds, even if listeners detach.
	if (!bObserve && HealthGEExecutions.IsEmpty() && SuppressedHealthGEDepth == 0) return true;
	check(IsInGameThread());
	const auto* Owner = Data.Target.GetOwnerActor();
	bObserve = bObserve && IsValid(Owner) && !Owner->IsActorBeingDestroyed() && Owner->HasAuthority();
	if (!bObserve && HealthGEExecutions.IsEmpty() && SuppressedHealthGEDepth == 0) return true;
	if (SuppressedHealthGEDepth || HealthGEExecutions.Num() >= MaxObservedHealthGEExecutions || GetClass() != StaticClass())
	{
		if (!SuppressedHealthGEDepth)
		{
			SuppressedHealthGEProof = CaptureHealthGEProof(Data);
			SuppressedHealthGEReason = GetClass() == StaticClass()
				? EProject_JHealthGEObservationFailure::ScopeLimit : EProject_JHealthGEObservationFailure::UnsupportedAttributeSet;
			RecordHealthGEObservationFailure();
		}
		++SuppressedHealthGEDepth;
		return true; // Observation pressure never rejects or alters authoritative damage.
	}
	if (HealthGEExecutions.Max() == 0) HealthGEExecutions.Reserve(MaxObservedHealthGEExecutions);
	FHealthGEExecution Execution;
	Execution.Identity = &Data; Execution.bObserve = bObserve; Execution.bConsumed = !bObserve;
	Execution.Pins.Emplace(this); Execution.Pins.Emplace(&Data.Target);
	Execution.Proof = CaptureHealthGEProof(Data);
	Execution.Pins.Emplace(Execution.Proof.TargetOwner.Get()); Execution.Pins.Emplace(Execution.Proof.TargetAvatar.Get());
	Execution.Pins.Emplace(Execution.Proof.SourceASC.Get()); Execution.Pins.Emplace(Execution.Proof.SourceOwner.Get());
	Execution.Pins.Emplace(Execution.Proof.SourceAvatar.Get()); Execution.Pins.Emplace(Execution.Proof.Instigator.Get());
	Execution.Pins.Emplace(Execution.Proof.EffectCauser.Get());
	Execution.Pins.Emplace(Execution.Proof.EffectDefinition.Get());
	HealthGEExecutions.Add(MoveTemp(Execution));
	return true;
}

void UProject_JAttributeSet::PostGameplayEffectExecute(const struct FGameplayEffectModCallbackData& Data)
{
	if (Data.EvaluatedData.Attribute != GetHealthAttribute() || (HealthGEExecutions.IsEmpty() && !SuppressedHealthGEDepth))
	{
		FinalizeExecutedAttribute(Data);
		return; // Default OFF avoids constructing or pinning observation payloads.
	}
	// The local completed frame pins the set/ASCs/actors across all cleanup and observer callbacks.
	FHealthGEExecution Completed;
	bool bFailed = false;
	FProject_JHealthGEExecutionProof FailedProof;
	EProject_JHealthGEObservationFailure Failure = EProject_JHealthGEObservationFailure::UnobservedExecution;
	if (Data.EvaluatedData.Attribute == GetHealthAttribute())
	{
		if (SuppressedHealthGEDepth)
		{
			if (--SuppressedHealthGEDepth == 0)
			{
				bFailed = true; FailedProof = SuppressedHealthGEProof; Failure = SuppressedHealthGEReason;
				SuppressedHealthGEProof = {}; // Detach before reentrant failure observers.
			}
		}
		else if (!HealthGEExecutions.IsEmpty() && HealthGEExecutions.Last().Identity == &Data)
		{
			Completed = MoveTemp(HealthGEExecutions.Last());
			HealthGEExecutions.Pop(EAllowShrinking::No);
			if (Completed.bObserve && !Completed.bConsumed)
			{
				RecordHealthGEObservationFailure(); bFailed = true; FailedProof = Completed.Proof;
			}
		}
	}
	TStrongObjectPtr<ThisClass> KeepSelf;
	TStrongObjectPtr<UAbilitySystemComponent> KeepASC;
	if (bFailed) { KeepSelf.Reset(this); KeepASC.Reset(&Data.Target); }
	FinalizeExecutedAttribute(Data);
	if (bFailed) OnHealthGEObservationFailed.Broadcast(FailedProof, Failure);
	ReleaseUnusedHealthGEObservationStorage();
}


void UProject_JAttributeSet::FinalizeExecutedAttribute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	if (Data.EvaluatedData.Attribute == GetHealthAttribute())
	{
		SetHealth(FMath::Clamp(GetHealth(), 0.0f, GetMaxHealth()));
	}
	else if (Data.EvaluatedData.Attribute == GetManaAttribute())
	{
		SetMana(FMath::Clamp(GetMana(), 0.0f, GetMaxMana()));
	}
	else if (Data.EvaluatedData.Attribute == GetMaxHealthAttribute())
	{
		SetHealth(FMath::Clamp(GetHealth(), 0.0f, GetMaxHealth()));
	}
	else if (Data.EvaluatedData.Attribute == GetMaxManaAttribute())
	{
		SetMana(FMath::Clamp(GetMana(), 0.0f, GetMaxMana()));
	}
	else if (Data.EvaluatedData.Attribute == GetAttackPowerAttribute())
	{
		SetAttackPower(FMath::Max(GetAttackPower(), 0.0f));
	}
	else if (Data.EvaluatedData.Attribute == GetDefenseAttribute())
	{
		SetDefense(FMath::Max(GetDefense(), 0.0f));
	}
}

void UProject_JAttributeSet::OnRep_Health(const FGameplayAttributeData& OldHealth)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UProject_JAttributeSet, Health, OldHealth);
}

void UProject_JAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UProject_JAttributeSet, MaxHealth, OldMaxHealth);
}

void UProject_JAttributeSet::OnRep_Mana(const FGameplayAttributeData& OldMana)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UProject_JAttributeSet, Mana, OldMana);
}

void UProject_JAttributeSet::OnRep_MaxMana(const FGameplayAttributeData& OldMaxMana)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UProject_JAttributeSet, MaxMana, OldMaxMana);
}

void UProject_JAttributeSet::OnRep_AttackPower(const FGameplayAttributeData& OldAttackPower)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UProject_JAttributeSet, AttackPower, OldAttackPower);
}

void UProject_JAttributeSet::OnRep_Defense(const FGameplayAttributeData& OldDefense)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UProject_JAttributeSet, Defense, OldDefense);
}

void UProject_JAttributeSet::PublishInitializedResources()
{
	check(IsInGameThread());
	if (!IsValid(GetOwningActor()) || !GetOwningActor()->HasAuthority() || GetOwningActor()->IsActorBeingDestroyed()) return;
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	OnInitializedResources.Broadcast();
}
