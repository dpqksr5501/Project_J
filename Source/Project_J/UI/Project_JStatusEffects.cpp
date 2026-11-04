#include "UI/Project_JStatusEffects.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "GameplayEffectUIData_TextOnly.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

void UProject_JStatusEffectModel::Bind(UAbilitySystemComponent *Source)
{
	if (bRetiring || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)) return;
	TStrongObjectPtr<UProject_JStatusEffectModel> CallbackOwner(this);
	TStrongObjectPtr<UAbilitySystemComponent> KeepSource(IsValid(Source) ? Source : nullptr);
	Source = KeepSource.Get();
	if (ASC == Source) return;
	const uint64 Revision = BindingRevision + 1;
	Unbind();
	if (bRetiring || BindingRevision != Revision) return;
	ASC = Source;
	if (Source)
	{
		const TWeakObjectPtr<UAbilitySystemComponent> WeakSource(Source);
		AddedHandle = Source->OnActiveGameplayEffectAddedDelegateToSelf.AddWeakLambda(this,
			[this, WeakSource, Revision](UAbilitySystemComponent *, const FGameplayEffectSpec &, FActiveGameplayEffectHandle Handle)
			{
				if (BindingRevision == Revision && ASC == WeakSource && ObserveEffect(Handle)) RefreshObservedEffects();
			});
		RemovedHandle = Source->OnAnyGameplayEffectRemovedDelegate().AddWeakLambda(this,
			[this, WeakSource, Revision](const FActiveGameplayEffect& Effect)
			{
				if (BindingRevision != Revision || ASC != WeakSource || !ObservedEffects.Contains(Effect.Handle)) return;
				ForgetEffect(Effect.Handle);
				RefreshObservedEffects();
			});
	}
	Refresh();
}
void UProject_JStatusEffectModel::Unbind()
{
	TStrongObjectPtr<UProject_JStatusEffectModel> CallbackOwner(bRetiring ? nullptr : this);
	++BindingRevision;
	const auto Previous = ASC;
	const auto Added = AddedHandle, Removed = RemovedHandle;
	const auto PreviousTimer = Timer;
	const auto PreviousEffects = MoveTemp(ObservedEffects);
	ASC.Reset(); AddedHandle.Reset(); RemovedHandle.Reset(); Timer.Invalidate(); States.Reset();
	if (Previous.IsValid())
	{
		Previous->OnActiveGameplayEffectAddedDelegateToSelf.Remove(Added);
		Previous->OnAnyGameplayEffectRemovedDelegate().Remove(Removed);
		for (const auto& Pair : PreviousEffects) RemoveEffectBindings(Previous.Get(), Pair.Key, Pair.Value);
		if (Previous->GetWorld()) { auto Handle = PreviousTimer; Previous->GetWorld()->GetTimerManager().ClearTimer(Handle); }
	}
	if (!bRetiring) OnChanged.Broadcast();
}
void UProject_JStatusEffectModel::BeginDestroy() { bRetiring = true; OnChanged.Clear(); Unbind(); Super::BeginDestroy(); }

void UProject_JStatusEffectModel::RemoveEffectBindings(UAbilitySystemComponent* Source,
	FActiveGameplayEffectHandle Handle, const FEffectBindings& Bindings)
{
	if (auto* Delegate = Source->OnGameplayEffectStackChangeDelegate(Handle)) Delegate->Remove(Bindings.Stack);
	if (auto* Delegate = Source->OnGameplayEffectTimeChangeDelegate(Handle)) Delegate->Remove(Bindings.Time);
	if (auto* Delegate = Source->OnGameplayEffectInhibitionChangedDelegate(Handle)) Delegate->Remove(Bindings.Inhibition);
}

void UProject_JStatusEffectModel::ForgetEffect(FActiveGameplayEffectHandle Handle)
{
	FEffectBindings Previous;
	if (ObservedEffects.RemoveAndCopyValue(Handle, Previous) && ASC.IsValid()) RemoveEffectBindings(ASC.Get(), Handle, Previous);
}

bool UProject_JStatusEffectModel::ObserveEffect(FActiveGameplayEffectHandle Handle)
{
	auto* Source = ASC.Get();
	const auto* Active = Source ? Source->GetActiveGameplayEffect(Handle) : nullptr;
	if (!Active || !Active->Spec.Def || (!Active->Spec.Def->FindComponent<UProject_JStatusEffectUIData>()
		&& !Active->Spec.Def->FindComponent<UGameplayEffectUIData_TextOnly>())) return false;
	if (ObservedEffects.Contains(Handle)) return true;
	const uint64 Revision = BindingRevision;
	const TWeakObjectPtr<UAbilitySystemComponent> WeakSource(Source);
	const auto RefreshCurrent = [this, WeakSource, Revision](FActiveGameplayEffectHandle Changed)
	{
		if (BindingRevision == Revision && ASC == WeakSource && ObservedEffects.Contains(Changed)) RefreshObservedEffects();
	};
	FEffectBindings Bindings;
	if (auto* Delegate = Source->OnGameplayEffectStackChangeDelegate(Handle))
		Bindings.Stack = Delegate->AddWeakLambda(this, [RefreshCurrent](FActiveGameplayEffectHandle Changed, int32, int32) { RefreshCurrent(Changed); });
	if (auto* Delegate = Source->OnGameplayEffectTimeChangeDelegate(Handle))
		Bindings.Time = Delegate->AddWeakLambda(this, [RefreshCurrent](FActiveGameplayEffectHandle Changed, float, float) { RefreshCurrent(Changed); });
	if (auto* Delegate = Source->OnGameplayEffectInhibitionChangedDelegate(Handle))
		Bindings.Inhibition = Delegate->AddWeakLambda(this, [RefreshCurrent](FActiveGameplayEffectHandle Changed, bool) { RefreshCurrent(Changed); });
	ObservedEffects.Add(Handle, Bindings);
	return true;
}

void UProject_JStatusEffectModel::Refresh()
{
	if (bRetiring || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)) return;
	// Bind and explicit metadata refresh discover membership once. Native add/remove
	// events maintain it afterwards; countdowns request only the observed UI handles.
	// GAS's per-handle lookup may itself scan the active container.
	if (ASC.IsValid())
	{
		TSet<FActiveGameplayEffectHandle> Current;
		for (auto Handle : ASC->GetActiveEffects(FGameplayEffectQuery())) if (ObserveEffect(Handle)) Current.Add(Handle);
		for (auto It = ObservedEffects.CreateIterator(); It; ++It)
		{
			if (Current.Contains(It.Key())) continue;
			RemoveEffectBindings(ASC.Get(), It.Key(), It.Value());
			It.RemoveCurrent();
		}
	}
	RefreshObservedEffects();
}

void UProject_JStatusEffectModel::RefreshObservedEffects()
{
	if (bRetiring || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)) return;
	// An OnChanged listener may release the last external reference and run GC.
	TStrongObjectPtr<UProject_JStatusEffectModel> CallbackOwner(this);
	// Preserve the published view only for this refresh. The common HUD-sized
	// snapshot stays inline; larger views remain complete and may allocate.
	TArray<FProject_JStatusEffectState, TInlineAllocator<12>> Previous;
	Previous.Append(States);
	States.Reset();
	if (ASC.IsValid() && ASC->GetWorld())
	{
		const float Now = ASC->GetWorld()->GetTimeSeconds();
		bool bCountdown = false;
		for (const auto& Pair : ObservedEffects)
		{
			const auto Handle = Pair.Key;
			const auto *Active = ASC->GetActiveGameplayEffect(Handle);
			if (!Active || !Active->Spec.Def) continue;
			const auto *UI = Active->Spec.Def->FindComponent<UProject_JStatusEffectUIData>();
			const auto *TextUI = Active->Spec.Def->FindComponent<UGameplayEffectUIData_TextOnly>();
			if (!UI && !TextUI) continue;
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
			bCountdown |= State.Duration > 0;
		}
		States.Sort([](const auto &A, const auto &B)
		{
			if (A.bDebuff != B.bDebuff) return A.bDebuff;
			if (A.Priority != B.Priority) return A.Priority > B.Priority;
			return GetTypeHash(A.Handle) < GetTypeHash(B.Handle);
		});
		// Infinite/inhibited statuses are event-driven. Only visible countdowns
		// need periodic updates; the UI never applies, removes or expires a GAS effect.
		if (bCountdown) ASC->GetWorld()->GetTimerManager().SetTimer(Timer, this, &ThisClass::RefreshObservedEffects, .25f, false);
		else ASC->GetWorld()->GetTimerManager().ClearTimer(Timer);
	}
	bool bChanged = Previous.Num() != States.Num();
	for (int32 Index = 0; !bChanged && Index < States.Num(); ++Index)
	{
		const auto &Before = Previous[Index];
		const auto &After = States[Index];
		bChanged = Before.Handle != After.Handle || !Before.Name.EqualTo(After.Name)
			|| !Before.Description.EqualTo(After.Description) || Before.Icon != After.Icon
			|| Before.StackCount != After.StackCount || Before.Remaining != After.Remaining
			|| Before.Duration != After.Duration || Before.bDebuff != After.bDebuff
			|| Before.Priority != After.Priority;
	}
	if (bChanged) OnChanged.Broadcast();
}
