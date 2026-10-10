#include "UI/Project_JStatusEffects.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "GameplayEffectUIData_TextOnly.h"
#include "Engine/World.h"

void UProject_JStatusEffectModel::Bind(UAbilitySystemComponent *Source)
{
	if (ASC == Source) return;
	Unbind();
	ASC = Source;
	if (Source)
	{
		AddedHandle = Source->OnActiveGameplayEffectAddedDelegateToSelf.AddWeakLambda(this,
			[this](UAbilitySystemComponent *, const FGameplayEffectSpec &, FActiveGameplayEffectHandle) { Refresh(); });
		RemovedHandle = Source->OnAnyGameplayEffectRemovedDelegate().AddWeakLambda(this,
			[this](const FActiveGameplayEffect &) { Refresh(); });
	}
	Refresh();
}
void UProject_JStatusEffectModel::Unbind()
{
	if (ASC.IsValid())
	{
		ASC->OnActiveGameplayEffectAddedDelegateToSelf.Remove(AddedHandle);
		ASC->OnAnyGameplayEffectRemovedDelegate().Remove(RemovedHandle);
		if (ASC->GetWorld()) ASC->GetWorld()->GetTimerManager().ClearTimer(Timer);
	}
	ASC.Reset();
	States.Reset();
	OnChanged.Broadcast();
}
void UProject_JStatusEffectModel::BeginDestroy() { Unbind(); Super::BeginDestroy(); }
void UProject_JStatusEffectModel::Refresh()
{
	States.Reset();
	if (ASC.IsValid() && ASC->GetWorld())
	{
		const float Now = ASC->GetWorld()->GetTimeSeconds();
		const auto Handles = ASC->GetActiveEffects(FGameplayEffectQuery());
		bool bObserveUIEffects = false;
		for (auto Handle : Handles)
		{
			const auto *Active = ASC->GetActiveGameplayEffect(Handle);
			if (!Active || !Active->Spec.Def) continue;
			const auto *UI = Active->Spec.Def->FindComponent<UProject_JStatusEffectUIData>();
			const auto *TextUI = Active->Spec.Def->FindComponent<UGameplayEffectUIData_TextOnly>();
			if (!UI && !TextUI) continue;
			bObserveUIEffects = true;
			if (Active->bIsInhibited) continue;
			FProject_JStatusEffectState State;
			State.Handle = Handle;
			State.Duration = Active->GetDuration();
			State.Remaining = State.Duration > 0 ? FMath::Max(0.f, Active->GetTimeRemaining(Now)) : -1.f;
			if (!FMath::IsFinite(State.Duration) || !FMath::IsFinite(State.Remaining) || (State.Duration > 0 && State.Remaining <= 0)) continue;
			State.StackCount = FMath::Max(1, Active->Spec.GetStackCount());
			if (UI)
			{
				State.Name = UI->DisplayName;
				State.Description = UI->Description;
				State.Icon = UI->Icon;
				State.bDebuff = UI->bDebuff;
				State.Priority = UI->Priority;
			}
			else { State.Name = TextUI->Description; State.Description = TextUI->Description; }
			if (State.Name.IsEmpty()) State.Name = NSLOCTEXT("ProjectJUI", "UnnamedEffect", "효과");
			States.Add(State);
		}
		States.Sort([](const auto &A, const auto &B)
		{
			if (A.bDebuff != B.bDebuff) return A.bDebuff;
			if (A.Priority != B.Priority) return A.Priority > B.Priority;
			return GetTypeHash(A.Handle) < GetTypeHash(B.Handle);
		});
		// Track stack/inhibition changes even for infinite UI effects; internal effects keep no UI timer running.
		if (bObserveUIEffects) ASC->GetWorld()->GetTimerManager().SetTimer(Timer, this, &ThisClass::Refresh, .25f, false);
		else ASC->GetWorld()->GetTimerManager().ClearTimer(Timer);
	}
	OnChanged.Broadcast();
}
