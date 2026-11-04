// Fill out your copyright notice in the Description page of Project Settings.

#include "Project_JAbilitySystemComponent.h"

#include "GameFramework/Actor.h"
#include "Project_JCombatInterface.h"
#include "Movement/Project_JMoverComponent.h"
#include "Project_JGameplayAbility.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/ScopeExit.h"
#include "Components/Project_JCommandBudgetComponent.h"

void UProject_JAbilitySystemComponent::NotifyAbilityEnded(FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability, bool bWasCancelled)
{
	check(IsInGameThread());
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	TArray<uint64, TInlineAllocator<1>> RetiredSerials;
	for (const auto& Entry : ClientPayloadExecutions)
		if (Entry.Handle == Handle && Entry.Instance.Get() == Ability && Entry.Phase == EClientPayloadPhase::Ending) RetiredSerials.Add(Entry.Serial);
	// End listeners may retire their grant. Keep the spec until GAS has finished
	// its post-broadcast cleanup, then let its normal pending-removal queue drain.
	FScopedAbilityListLock AbilityListLock(*this);
	Super::NotifyAbilityEnded(Handle, Ability, bWasCancelled);
	// Native Clear has already removed the old cache. Super listeners may publish a new execution.
	for (uint64 Serial : RetiredSerials) RetireClientPayloadExecution(Serial, false);
}

void UProject_JAbilitySystemComponent::NotifyAbilityActivated(const FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability)
{
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	const TStrongObjectPtr<UGameplayAbility> KeepAbility(Ability);
	PublishClientPayloadExecution(Handle, Ability); // Before extensible activation listeners and before Spec.ActiveCount increments.
	Super::NotifyAbilityActivated(Handle, Ability);
}

void UProject_JAbilitySystemComponent::OnRemoveAbility(FGameplayAbilitySpec& AbilitySpec)
{
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	FScopedAbilityListLock AbilityListLock(*this);
	RetireClientPayloadExecutions(AbilitySpec.Handle);
	Super::OnRemoveAbility(AbilitySpec);
}

void UProject_JAbilitySystemComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	RetireClientPayloadExecutions();
	Super::EndPlay(Reason);
}

void UProject_JAbilitySystemComponent::CancelAbilitySpec(FGameplayAbilitySpec& Spec,UGameplayAbility* Ignore)
{
	if (!GetWorld() || !GetWorld()->bIsTearingDown)
	{
		Super::CancelAbilitySpec(Spec,Ignore);
		return;
	}
	// Actor/ability cleanup can precede ASC destruction but follow Iris channel
	// removal. Keep cancellation and local End/Tag/Task cleanup without an RPC
	// into a closing world. Ordinary gameplay cancellation stays engine-owned.
	const auto Policy=Spec.Ability->GetInstancingPolicy();
	if (Policy==EGameplayAbilityInstancingPolicy::InstancedPerActor || Policy==EGameplayAbilityInstancingPolicy::InstancedPerExecution)
	{
		const TArray<UGameplayAbility*> Instances=Spec.GetAbilityInstances();
		for (UGameplayAbility* Instance:Instances)
			if (Instance && Instance!=Ignore)
				Instance->CancelAbility(Spec.Handle,AbilityActorInfo.Get(),Instance->GetCurrentActivationInfoRef(),false);
	}
	else Spec.Ability->CancelAbility(Spec.Handle,AbilityActorInfo.Get(),FGameplayAbilityActivationInfo(),false);
	MarkAbilitySpecDirty(Spec);
}

void UProject_JAbilitySystemComponent::InitAbilityActorInfo(AActor* Owner,AActor* Avatar)
{
	check(IsInGameThread());
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	const bool bOutermost = !bActorInfoChanging;
	ON_SCOPE_EXIT { if (bOutermost) PublishSettledActorInfo(); };
	const TStrongObjectPtr<AActor> KeepPreviousOwner(GetOwnerActor()), KeepPreviousAvatar(GetAvatarActor());
	const TStrongObjectPtr<AActor> KeepOwner(Owner), KeepAvatar(Avatar);
	if (++ActorInfoRevision == 0) ++ActorInfoRevision;
	const uint64 Revision = ActorInfoRevision;
	TGuardValue<bool> ChangingGuard(bActorInfoChanging, true);
	RetireClientPayloadExecutions();
	if (ActorInfoRevision != Revision || !IsValid(this) || IsBeingDestroyed()) return;
	OnActorInfoChanging.Broadcast(Owner, Avatar);
	// Cancellation can synchronously install another recipient. The newer request owns it.
	if (ActorInfoRevision != Revision || !IsValid(this) || IsBeingDestroyed()) return;
	if (!IsValid(Owner) || Owner->IsActorBeingDestroyed() || (Avatar && (!IsValid(Avatar) || Avatar->IsActorBeingDestroyed())))
	{
		// A cancellation callback may retire the proposed recipient while it is pinned.
		ClearActorInfo();
		return;
	}
	if (GetAvatarActor()!=Avatar) if (auto* Mover=MontageMover.Get()) Mover->StopGameplayMontageRootMotion();
	Super::InitAbilityActorInfo(Owner,Avatar);
}
void UProject_JAbilitySystemComponent::ClearActorInfo()
{
	check(IsInGameThread());
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	const bool bOutermost = !bActorInfoChanging;
	ON_SCOPE_EXIT { if (bOutermost) PublishSettledActorInfo(); };
	const TStrongObjectPtr<AActor> KeepPreviousOwner(GetOwnerActor()), KeepPreviousAvatar(GetAvatarActor());
	if (++ActorInfoRevision == 0) ++ActorInfoRevision;
	const uint64 Revision = ActorInfoRevision;
	TGuardValue<bool> ChangingGuard(bActorInfoChanging, true);
	RetireClientPayloadExecutions();
	if (ActorInfoRevision != Revision || !IsValid(this) || IsBeingDestroyed()) return;
	OnActorInfoChanging.Broadcast(nullptr, nullptr);
	if (ActorInfoRevision != Revision || !IsValid(this) || IsBeingDestroyed()) return;
	if (auto* Mover=MontageMover.Get()) Mover->StopGameplayMontageRootMotion();
	MontageMover.Reset();
	Super::ClearActorInfo();
}

void UProject_JAbilitySystemComponent::PublishSettledActorInfo()
{
	// The outer changing guard has unwound. OnAvatarSet/prechange callbacks may
	// have committed a newer Init/Clear; publish only the actual settled tuple.
	if (!IsValid(this) || IsBeingDestroyed() || bActorInfoChanging || !AbilityActorInfo.IsValid()) return;
	AActor* Owner = GetOwnerActor();
	AActor* Avatar = GetAvatarActor_Direct();
	if (AbilityActorInfo->OwnerActor.Get() != Owner || AbilityActorInfo->AvatarActor.Get() != Avatar
		|| (Owner && (!IsValid(Owner) || Owner->IsActorBeingDestroyed()))
		|| (Avatar && (!IsValid(Avatar) || Avatar->IsActorBeingDestroyed()))) return;
	OnActorInfoSettled.Broadcast(this);
}

void UProject_JAbilitySystemComponent::RefreshMoverMontage()
{
 auto* Avatar=GetAvatarActor();
 auto* Mover=Avatar?Avatar->FindComponentByClass<UProject_JMoverComponent>():nullptr;
 if (auto* Previous=MontageMover.Get(); Previous && Previous!=Mover) Previous->StopGameplayMontageRootMotion();
 MontageMover=Mover;
 if (Mover) Mover->FollowGameplayMontage(GetCurrentMontage());
}

float UProject_JAbilitySystemComponent::PlayMontage(UGameplayAbility* Ability,FGameplayAbilityActivationInfo Activation,UAnimMontage* Montage,float Rate,FName Section,float Start)
{
 const float Result=Super::PlayMontage(Ability,Activation,Montage,Rate,Section,Start);
 if (Result>0.f) RefreshMoverMontage();
 return Result;
}

void UProject_JAbilitySystemComponent::CurrentMontageStop(float BlendOut)
{
 auto* Montage=GetCurrentMontage();
 Super::CurrentMontageStop(BlendOut);
 if (auto* Mover=MontageMover.Get()) Mover->StopGameplayMontageRootMotion(Montage);
}

void UProject_JAbilitySystemComponent::CurrentMontageJumpToSection(FName Section)
{
 Super::CurrentMontageJumpToSection(Section);
 RefreshMoverMontage();
}

void UProject_JAbilitySystemComponent::CurrentMontageSetPlayRate(float Rate)
{
 Super::CurrentMontageSetPlayRate(Rate);
 RefreshMoverMontage();
}

void UProject_JAbilitySystemComponent::ServerCurrentMontageJumpToSectionName_Implementation(UAnimSequenceBase* ClientAnimation,FName SectionName)
{
 if (!CanAcceptClientSessionCommand()) return;
 Super::ServerCurrentMontageJumpToSectionName_Implementation(ClientAnimation,SectionName);
 RefreshMoverMontage();
}

void UProject_JAbilitySystemComponent::ServerCurrentMontageSetPlayRate_Implementation(UAnimSequenceBase* ClientAnimation,float Rate)
{
 if (!CanAcceptClientSessionCommand()) return;
 Super::ServerCurrentMontageSetPlayRate_Implementation(ClientAnimation,Rate);
 RefreshMoverMontage();
}

void UProject_JAbilitySystemComponent::ServerCurrentMontageSetNextSectionName_Implementation(UAnimSequenceBase* ClientAnimation,
	float ClientPosition, FName SectionName, FName NextSectionName)
{
	if (!CanAcceptClientSessionCommand()) return;
	Super::ServerCurrentMontageSetNextSectionName_Implementation(ClientAnimation, ClientPosition, SectionName, NextSectionName);
}

bool UProject_JAbilitySystemComponent::CanAcceptClientSessionCommand()
{
	check(IsInGameThread());
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	const TStrongObjectPtr<AActor> KeepOwner(GetOwner());
	if (IsBeingDestroyed() || !IsValid(KeepOwner.Get()) || KeepOwner->IsActorBeingDestroyed() || !KeepOwner->HasAuthority()
		|| !GetWorld() || GetWorld()->bIsTearingDown) return false;
	if (ClientSessionAuthority.IsBound() && !ClientSessionAuthority.Execute()) return false;
	return !IsBeingDestroyed() && IsValid(KeepOwner.Get()) && !KeepOwner->IsActorBeingDestroyed() && KeepOwner.Get() == GetOwner()
		&& GetWorld() && !GetWorld()->bIsTearingDown;
}

void UProject_JAbilitySystemComponent::RejectSessionActivation(FGameplayAbilitySpecHandle Handle, const FPredictionKey& PredictionKey)
{
	// Match GAS's ordinary rejection acknowledgement, avoiding RPCs during world/owner teardown.
	if (GetWorld() && !GetWorld()->bIsTearingDown && IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed())
		ClientActivateAbilityFailed(Handle, PredictionKey.Current);
}

void UProject_JAbilitySystemComponent::InternalServerTryActivateAbility(FGameplayAbilitySpecHandle Handle, bool InputPressed,
	const FPredictionKey& PredictionKey, const FGameplayEventData* TriggerEventData)
{
	// Consume the native batch's one activation credit at entry, before permission/admission callbacks.
	FBatchCommandAdmission* const PendingAdmission = ActiveBatchCommandAdmission;
	const bool bPrepaid = PendingAdmission && PendingAdmission->bAvailable && PendingAdmission->Handle == Handle
		&& PendingAdmission->PredictionKey == PredictionKey;
	if (bPrepaid) PendingAdmission->bAvailable = false;
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	const FClientCommandContext Context = bPrepaid ? PendingAdmission->Context : CaptureClientCommandContext();
	const TStrongObjectPtr<AActor> KeepOwner(Context.Owner.Get()), KeepAvatar(Context.Avatar.Get());
	const uint64 ReservationSerial = ReserveClientPayloadExecution(Handle, PredictionKey, Context);
	ON_SCOPE_EXIT { ReleaseClientPayloadReservation(ReservationSerial); };
	const bool bAdmitted = bPrepaid ? IsClientCommandContextAuthorized(Context) : AdmitClientGameplayCommand(Context);
	if (!bAdmitted || ReservationSerial == 0)
	{
		if (IsClientCommandContextCurrent(Context)) RejectSessionActivation(Handle, PredictionKey);
		return;
	}
	FClientActivationReservation Reservation{ReservationSerial, Handle, PredictionKey, true};
	TGuardValue<FClientActivationReservation*> ReservationGuard(ActiveClientActivationReservation, &Reservation);
	if (bPrepaid) PendingAdmission->bTargetDataAllowed = true;
	Super::InternalServerTryActivateAbility(Handle, InputPressed, PredictionKey, TriggerEventData);
}

void UProject_JAbilitySystemComponent::ServerAbilityRPCBatch_Internal(FServerAbilityRPCBatch& BatchInfo)
{
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	const FClientCommandContext Context = CaptureClientCommandContext();
	const TStrongObjectPtr<AActor> KeepOwner(Context.Owner.Get()), KeepAvatar(Context.Avatar.Get());
	const bool bAdmitted = AdmitClientGameplayCommand(Context);
	FBatchCommandAdmission Admission{BatchInfo.AbilitySpecHandle, BatchInfo.PredictionKey, Context, bAdmitted};
	TGuardValue<FBatchCommandAdmission*> BatchGuard(ActiveBatchCommandAdmission, &Admission);
	if (!bAdmitted)
	{
		// Native batch processing does not inspect Started (a NotReplicated local flag).
		// Do not forward activation/target data. Preserve its existing, validated End branch only.
		if (BatchInfo.Ended && IsClientCommandContextAuthorized(Context))
		{
			FGameplayAbilityActivationInfo EndInfo;
			EndInfo.ServerSetActivationPredictionKey(BatchInfo.PredictionKey);
			ServerEndAbility(BatchInfo.AbilitySpecHandle, EndInfo, BatchInfo.PredictionKey);
		}
		if (IsClientCommandContextCurrent(Context)) RejectSessionActivation(BatchInfo.AbilitySpecHandle, BatchInfo.PredictionKey);
		return;
	}
	Super::ServerAbilityRPCBatch_Internal(BatchInfo);
}

UProject_JAbilitySystemComponent::FClientCommandContext UProject_JAbilitySystemComponent::CaptureClientCommandContext() const
{
	return {GetOwner(), GetAvatarActor(), ActorInfoRevision};
}

bool UProject_JAbilitySystemComponent::IsClientCommandContextCurrent(const FClientCommandContext& Context) const
{
	return IsValid(this) && !IsBeingDestroyed() && Context.Owner.IsValid() && !Context.Owner->IsActorBeingDestroyed()
		&& GetOwner() == Context.Owner.Get() && GetAvatarActor() == Context.Avatar.Get() && !Context.Avatar.IsStale()
		&& (!Context.Avatar.IsValid() || !Context.Avatar->IsActorBeingDestroyed()) && ActorInfoRevision == Context.ActorInfoRevision
		&& GetWorld() && !GetWorld()->bIsTearingDown && Context.Owner->GetWorld() == GetWorld();
}

bool UProject_JAbilitySystemComponent::IsClientCommandContextAuthorized(const FClientCommandContext& Context)
{
	return IsClientCommandContextCurrent(Context) && CanAcceptClientSessionCommand() && IsClientCommandContextCurrent(Context);
}

bool UProject_JAbilitySystemComponent::AdmitClientGameplayCommand(const FClientCommandContext& Context)
{
	return IsClientCommandContextAuthorized(Context) && UProject_JCommandBudgetComponent::AdmitClientCommand(Context.Owner.Get())
		&& IsClientCommandContextAuthorized(Context);
}

bool UProject_JAbilitySystemComponent::HasClientPayloadAlias(FGameplayAbilitySpecHandle Handle, int16 Current, uint64 ExceptSerial, bool bIncludeReservations) const
{
	return ClientPayloadExecutions.ContainsByPredicate([&](const FClientPayloadExecution& Entry)
	{ return Entry.Serial != ExceptSerial && Entry.Handle == Handle && Entry.PredictionKey.Current == Current
		&& (bIncludeReservations || Entry.Phase != EClientPayloadPhase::Reserved); });
}

uint64 UProject_JAbilitySystemComponent::ReserveClientPayloadExecution(FGameplayAbilitySpecHandle Handle,
	const FPredictionKey& Key, const FClientCommandContext& Context)
{
	if (bRetiringClientPayloads || NextClientPayloadSerial == MAX_uint64 || !IsClientCommandContextCurrent(Context)
		|| !Context.Owner->HasAuthority()) return 0;
	const FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle);
	if (!Spec || !IsValid(Spec->Ability) || Spec->PendingRemove || HasClientPayloadAlias(Handle, Key.Current)) return 0;
	if (ClientPayloadExecutions.Num() >= FMath::Clamp(ClientPayloadExecutionLimit, 1, 128)) return 0;
	FClientPayloadExecution Entry;
	Entry.Serial = ++NextClientPayloadSerial; Entry.Handle = Handle; Entry.PredictionKey = Key;
	Entry.Context = Context;
	ClientPayloadExecutions.Add(Entry);
	return Entry.Serial;
}

void UProject_JAbilitySystemComponent::ReleaseClientPayloadReservation(uint64 Serial)
{
	const int32 Index = ClientPayloadExecutions.IndexOfByPredicate([&](const FClientPayloadExecution& Entry)
	{ return Entry.Serial == Serial && Entry.Phase == EClientPayloadPhase::Reserved; });
	if (Index != INDEX_NONE) ClientPayloadExecutions.RemoveAtSwap(Index, EAllowShrinking::No);
}

void UProject_JAbilitySystemComponent::PublishClientPayloadExecution(FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability)
{
	const auto* ProjectAbility = Cast<UProject_JGameplayAbility>(Ability);
	// Foreign and NonInstanced abilities keep native activation, but cannot opt into this payload lifetime contract.
	if (!IsValid(GetOwner()) || !GetOwner()->HasAuthority() || !IsValid(ProjectAbility)
		|| !ProjectAbility->CanReceiveClientExecutionData()) return;
	const auto Policy = Ability->GetNetExecutionPolicy();
	if (Policy != EGameplayAbilityNetExecutionPolicy::LocalPredicted && Policy != EGameplayAbilityNetExecutionPolicy::ServerInitiated) return;
	const FPredictionKey Key = Ability->GetCurrentActivationInfo().GetActivationPredictionKey();
	const auto* NativeInfo = Ability->GetCurrentActorInfo();
	const auto* NativeSpec = FindAbilitySpecFromHandle(Handle);
	if (!NativeInfo || NativeInfo->AbilitySystemComponent.Get() != this || Ability->GetCurrentAbilitySpecHandle() != Handle
		|| !NativeSpec || (!NativeSpec->ReplicatedInstances.Contains(Ability) && !NativeSpec->NonReplicatedInstances.Contains(Ability))) return;
	FClientActivationReservation* const Reservation = ActiveClientActivationReservation;
	const bool bClientNotification = Reservation && Reservation->bAvailable && Reservation->Handle == Handle
		&& Policy == EGameplayAbilityNetExecutionPolicy::LocalPredicted && Reservation->RequestedKey.IsValidKey()
		&& Reservation->RequestedKey == Key;
	if (bClientNotification) Reservation->bAvailable = false;
	const FClientCommandContext Context = CaptureClientCommandContext();
	if (!IsClientCommandContextCurrent(Context)) return;
	uint64 Serial = 0;
	if (bClientNotification)
	{
		if (bRetiringClientPayloads) return;
		const int32 Index = ClientPayloadExecutions.IndexOfByPredicate([&](const FClientPayloadExecution& Entry)
		{ return Entry.Serial == Reservation->Serial && Entry.Phase == EClientPayloadPhase::Reserved; });
		// A permission/activation callback can retire the reservation. Never relabel that stale request as trusted.
		if (Index == INDEX_NONE || !IsClientCommandContextCurrent(ClientPayloadExecutions[Index].Context)) return;
		if (HasClientPayloadAlias(Handle, Key.Current, Reservation->Serial)) return;
		Serial = Reservation->Serial;
		ClientPayloadExecutions[Index].PredictionKey = Key;
		ClientPayloadExecutions[Index].Instance = Ability;
		ClientPayloadExecutions[Index].Phase = EClientPayloadPhase::Active;
	}
	else
	{
		// Only actual native activation creates trusted records. A client payload never adds one.
		// A generated server key may share the request's Current. Only this call's still-Reserved
		// bookkeeping is excluded; another execution's Active/Ending cache identity is never bypassed.
		uint64 RewrittenReservation = 0;
		if (Reservation && Reservation->Handle == Handle && Key.bIsServerInitiated
			&& (Policy == EGameplayAbilityNetExecutionPolicy::ServerInitiated || !Reservation->RequestedKey.IsValidKey()))
			if (const auto* Pending = ClientPayloadExecutions.FindByPredicate([&](const FClientPayloadExecution& Entry)
				{ return Entry.Serial == Reservation->Serial && Entry.Phase == EClientPayloadPhase::Reserved; }))
				RewrittenReservation = Pending->Serial;
		if (NextClientPayloadSerial == MAX_uint64 || HasClientPayloadAlias(Handle, Key.Current, RewrittenReservation)) return;
		FClientPayloadExecution Entry;
		Entry.Serial = ++NextClientPayloadSerial; Entry.Handle = Handle; Entry.PredictionKey = Key;
		Entry.Context = Context; Entry.Instance = Ability; Entry.Phase = EClientPayloadPhase::Active;
		Serial = Entry.Serial; ClientPayloadExecutions.Add(Entry);
	}
	const FDelegateHandle EndHandle = Ability->OnGameplayAbilityEnded.AddWeakLambda(this,
		[this, Serial](UGameplayAbility*) { BeginClientPayloadExecutionEnding(Serial); });
	if (auto* Entry = ClientPayloadExecutions.FindByPredicate([&](const FClientPayloadExecution& Value) { return Value.Serial == Serial; }))
		Entry->EndHandle = EndHandle;
}

void UProject_JAbilitySystemComponent::BeginClientPayloadExecutionEnding(uint64 Serial)
{
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	const TStrongObjectPtr<AActor> KeepOwner(GetOwner()), KeepAvatar(GetAvatarActor());
	const int32 Index = ClientPayloadExecutions.IndexOfByPredicate([&](const FClientPayloadExecution& Entry) { return Entry.Serial == Serial; });
	if (Index == INDEX_NONE || ClientPayloadExecutions[Index].Phase != EClientPayloadPhase::Active) return;
	ClientPayloadExecutions[Index].Phase = EClientPayloadPhase::Ending;
	const FClientPayloadExecution Ending = ClientPayloadExecutions[Index];
	// This delegate runs before native Clear moves the cache to FreeData. Keep the Ending marker until NotifyAbilityEnded.
	if (GetOwner() == Ending.Context.Owner.Get() && !HasClientPayloadAlias(Ending.Handle, Ending.PredictionKey.Current, Serial, false))
		ConsumeAllReplicatedData(Ending.Handle, Ending.PredictionKey);
}

void UProject_JAbilitySystemComponent::RetireClientPayloadExecution(uint64 Serial, bool bClearNative)
{
	const int32 Index = ClientPayloadExecutions.IndexOfByPredicate([&](const FClientPayloadExecution& Entry) { return Entry.Serial == Serial; });
	if (Index == INDEX_NONE) return;
	const FClientPayloadExecution Retired = ClientPayloadExecutions[Index];
	ClientPayloadExecutions.RemoveAtSwap(Index, EAllowShrinking::No);
	if (auto* Instance = Retired.Instance.Get()) Instance->OnGameplayAbilityEnded.Remove(Retired.EndHandle);
	if (bClearNative && GetOwner() == Retired.Context.Owner.Get()
		&& !HasClientPayloadAlias(Retired.Handle, Retired.PredictionKey.Current, 0, false))
	{
		ConsumeAllReplicatedData(Retired.Handle, Retired.PredictionKey);
		if (GetOwner() == Retired.Context.Owner.Get() && !HasClientPayloadAlias(Retired.Handle, Retired.PredictionKey.Current, 0, false))
		{
			FGameplayAbilityActivationInfo Info; Info.ServerSetActivationPredictionKey(Retired.PredictionKey);
			ClearAbilityReplicatedDataCache(Retired.Handle, Info);
		}
	}
}

void UProject_JAbilitySystemComponent::RetireClientPayloadExecutions(FGameplayAbilitySpecHandle Handle)
{
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	const TStrongObjectPtr<AActor> KeepOwner(GetOwner()), KeepAvatar(GetAvatarActor());
	TGuardValue<bool> RetirementGuard(bRetiringClientPayloads, true);
	TArray<FClientPayloadExecution> Retired;
	for (int32 Index = ClientPayloadExecutions.Num() - 1; Index >= 0; --Index)
		if (!Handle.IsValid() || ClientPayloadExecutions[Index].Handle == Handle)
		{ Retired.Add(ClientPayloadExecutions[Index]); ClientPayloadExecutions.RemoveAtSwap(Index, EAllowShrinking::No); }
	// Detach every owned record/listener before releasing payloads; native reset/clear may release external data.
	for (const auto& Entry : Retired)
		if (auto* Instance = Entry.Instance.Get()) Instance->OnGameplayAbilityEnded.Remove(Entry.EndHandle);
	for (const auto& Entry : Retired)
	{
		if (GetOwner() != Entry.Context.Owner.Get() || HasClientPayloadAlias(Entry.Handle, Entry.PredictionKey.Current, 0, false)) continue;
		ConsumeAllReplicatedData(Entry.Handle, Entry.PredictionKey);
		if (GetOwner() != Entry.Context.Owner.Get() || HasClientPayloadAlias(Entry.Handle, Entry.PredictionKey.Current, 0, false)) continue;
		FGameplayAbilityActivationInfo Info; Info.ServerSetActivationPredictionKey(Entry.PredictionKey);
		ClearAbilityReplicatedDataCache(Entry.Handle, Info);
	}
}

UGameplayAbility* UProject_JAbilitySystemComponent::AuthorizeClientPayload(FGameplayAbilitySpecHandle Handle, const FPredictionKey& Key)
{
	if (bRetiringClientPayloads) return nullptr;
	const int32 Index = ClientPayloadExecutions.IndexOfByPredicate([&](const FClientPayloadExecution& Entry)
	{ return Entry.Handle == Handle && Entry.PredictionKey.Current == Key.Current && Entry.Phase != EClientPayloadPhase::Reserved; });
	if (Index == INDEX_NONE) return nullptr;
	const FClientPayloadExecution Captured = ClientPayloadExecutions[Index];
	const TStrongObjectPtr<UGameplayAbility> Instance(Captured.Instance.Get());
	const TStrongObjectPtr<AActor> Owner(Captured.Context.Owner.Get()), Avatar(Captured.Context.Avatar.Get());
	const auto* ProjectAbility = Cast<UProject_JGameplayAbility>(Instance.Get());
	if (Captured.Phase != EClientPayloadPhase::Active || Captured.PredictionKey != Key || !IsValid(ProjectAbility)
		|| !ProjectAbility->CanReceiveClientExecutionData() || Instance->GetCurrentAbilitySpecHandle() != Handle
		|| Instance->GetCurrentActivationInfo().GetActivationPredictionKey() != Key) return nullptr;
	const auto* Spec = FindAbilitySpecFromHandle(Handle);
	if (!Spec || Spec->PendingRemove || (!Spec->ReplicatedInstances.Contains(Instance.Get()) && !Spec->NonReplicatedInstances.Contains(Instance.Get()))) return nullptr;
	// The genuine project subclass reads native activation/ending state even before Spec.ActiveCount increments.
	// This also closes End callbacks regardless of multicast registration order or PerExecution::IsActive().
	if (!IsClientCommandContextAuthorized(Captured.Context)) return nullptr;
	const auto* Current = ClientPayloadExecutions.FindByPredicate([&](const FClientPayloadExecution& Entry) { return Entry.Serial == Captured.Serial; });
	return Current && Current->Phase == EClientPayloadPhase::Active && Current->Instance.Get() == Instance.Get()
		&& IsClientCommandContextCurrent(Captured.Context) ? Instance.Get() : nullptr;
}

void UProject_JAbilitySystemComponent::ServerSetReplicatedTargetData_Implementation(FGameplayAbilitySpecHandle AbilityHandle,
	FPredictionKey AbilityOriginalPredictionKey, const FGameplayAbilityTargetDataHandle& TargetData,
	FGameplayTag ApplicationTag, FPredictionKey CurrentPredictionKey)
{
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	if (ActiveBatchCommandAdmission && ActiveBatchCommandAdmission->Handle == AbilityHandle
		&& ActiveBatchCommandAdmission->PredictionKey == AbilityOriginalPredictionKey
		&& (!ActiveBatchCommandAdmission->bTargetDataAllowed || !IsClientCommandContextAuthorized(ActiveBatchCommandAdmission->Context))) return;
	const TStrongObjectPtr<UGameplayAbility> Execution(AuthorizeClientPayload(AbilityHandle, AbilityOriginalPredictionKey));
	if (!Execution.IsValid()) return;
	Super::ServerSetReplicatedTargetData_Implementation(AbilityHandle, AbilityOriginalPredictionKey, TargetData, ApplicationTag, CurrentPredictionKey);
}

void UProject_JAbilitySystemComponent::ServerSetReplicatedTargetDataCancelled_Implementation(FGameplayAbilitySpecHandle AbilityHandle,
	FPredictionKey AbilityOriginalPredictionKey, FPredictionKey CurrentPredictionKey)
{
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	const TStrongObjectPtr<UGameplayAbility> Execution(AuthorizeClientPayload(AbilityHandle, AbilityOriginalPredictionKey));
	if (!Execution.IsValid()) return;
	Super::ServerSetReplicatedTargetDataCancelled_Implementation(AbilityHandle, AbilityOriginalPredictionKey, CurrentPredictionKey);
}

void UProject_JAbilitySystemComponent::ServerSetReplicatedEvent_Implementation(EAbilityGenericReplicatedEvent::Type EventType,
	FGameplayAbilitySpecHandle AbilityHandle, FPredictionKey AbilityOriginalPredictionKey, FPredictionKey CurrentPredictionKey)
{
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	if (EventType >= EAbilityGenericReplicatedEvent::MAX) return;
	const TStrongObjectPtr<UGameplayAbility> Execution(AuthorizeClientPayload(AbilityHandle, AbilityOriginalPredictionKey));
	if (!Execution.IsValid()) return;
	Super::ServerSetReplicatedEvent_Implementation(EventType, AbilityHandle, AbilityOriginalPredictionKey, CurrentPredictionKey);
}

void UProject_JAbilitySystemComponent::ServerSetReplicatedEventWithPayload_Implementation(EAbilityGenericReplicatedEvent::Type EventType,
	FGameplayAbilitySpecHandle AbilityHandle, FPredictionKey AbilityOriginalPredictionKey, FPredictionKey CurrentPredictionKey,
	FVector_NetQuantize100 VectorPayload)
{
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	if (EventType >= EAbilityGenericReplicatedEvent::MAX) return;
	const TStrongObjectPtr<UGameplayAbility> Execution(AuthorizeClientPayload(AbilityHandle, AbilityOriginalPredictionKey));
	if (!Execution.IsValid()) return;
	Super::ServerSetReplicatedEventWithPayload_Implementation(EventType, AbilityHandle, AbilityOriginalPredictionKey, CurrentPredictionKey, VectorPayload);
}

void UProject_JAbilitySystemComponent::ServerEndAbility_Implementation(FGameplayAbilitySpecHandle AbilityToEnd,
	FGameplayAbilityActivationInfo ActivationInfo, FPredictionKey PredictionKey)
{
	if (ActiveBatchCommandAdmission && ActiveBatchCommandAdmission->Handle == AbilityToEnd
		&& ActiveBatchCommandAdmission->PredictionKey == PredictionKey
		&& ActiveBatchCommandAdmission->PredictionKey == ActivationInfo.GetActivationPredictionKey()
		&& !IsClientCommandContextAuthorized(ActiveBatchCommandAdmission->Context)) return;
	Super::ServerEndAbility_Implementation(AbilityToEnd, ActivationInfo, PredictionKey);
}

UProject_JAbilitySystemComponent::UProject_JAbilitySystemComponent()
{
	// Preserve GAS/GameplayTasks tick capability. The engine's GetShouldTick /
	// UpdateShouldTick owns activation for ticking tasks, replicated montages and
	// tickable attributes, then sleeps when idle. bCanEverTick=false prevents all
	// of those systems from waking the registered component.
}

void UProject_JAbilitySystemComponent::ServerSendCombatInputEvent_Implementation(AActor* ExpectedAvatar, FGameplayTag InputTag, double ClientTimestamp, uint32 InputSequence)
{
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	const FClientCommandContext Context = CaptureClientCommandContext();
	const TStrongObjectPtr<AActor> KeepOwner(Context.Owner.Get()), KeepAvatar(Context.Avatar.Get());
	if (!AdmitClientGameplayCommand(Context)) return;
	// A reliable packet queued before pawn replacement belongs to that old avatar.
	// GAS activation and intent share this ASC's actor channel and preserve order.
	if (!IsValid(ExpectedAvatar) || ExpectedAvatar->IsActorBeingDestroyed() || !ExpectedAvatar->HasAuthority()
		|| GetAvatarActor() != ExpectedAvatar) { return; }
	if (auto* Receiver = Cast<IProject_JCombatInterface>(ExpectedAvatar))
	{ Receiver->ReceiveAuthorityCombatInput(InputTag, ClientTimestamp, InputSequence); }
}

bool UProject_JAbilitySystemComponent::RefreshActorInfoForCurrentActor(AActor* Requester)
{
	check(IsInGameThread());
	if (!IsValid(Requester) || Requester->IsActorBeingDestroyed() || !AbilityActorInfo.IsValid()) { return false; }
	auto* Owner = AbilityActorInfo->OwnerActor.Get();
	auto* Avatar = AbilityActorInfo->AvatarActor.Get();
	if (!IsValid(Owner) || Owner->IsActorBeingDestroyed() || GetOwnerActor() != Owner || GetAvatarActor() != Avatar
		|| (Avatar && Avatar->IsActorBeingDestroyed()) || (Requester != Owner && Requester != Avatar)) { return false; }
	// RefreshAbilityActorInfo updates the cached PlayerController and components.
	// It does not run OnAvatarSet, grant abilities, or reset an active ability.
	RefreshAbilityActorInfo();
	return true;
}

void UProject_JAbilitySystemComponent::AddProjectJLooseGameplayTag(const FGameplayTag& GameplayTag, bool bReplicateOnAuthority)
{
	if (!GameplayTag.IsValid())
	{
		return;
	}

	const EGameplayTagReplicationState ReplicationState = (bReplicateOnAuthority && HasGameplayTagReplicationAuthority())
		? EGameplayTagReplicationState::TagAndCountToAll
		: EGameplayTagReplicationState::None;

	AddLooseGameplayTag(GameplayTag, 1, ReplicationState);
}

void UProject_JAbilitySystemComponent::RemoveProjectJLooseGameplayTag(const FGameplayTag& GameplayTag, bool bReplicateOnAuthority)
{
	if (!GameplayTag.IsValid())
	{
		return;
	}

	const EGameplayTagReplicationState ReplicationState = (bReplicateOnAuthority && HasGameplayTagReplicationAuthority())
		? EGameplayTagReplicationState::TagAndCountToAll
		: EGameplayTagReplicationState::None;

	RemoveLooseGameplayTag(GameplayTag, 1, ReplicationState);
}

bool UProject_JAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag& InputTag)
{
	if (!InputTag.IsValid())
	{
		return false;
	}

	bool bHandledAnyAbility = false;

	ABILITYLIST_SCOPE_LOCK();
	for (FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
	{
		if (!AbilitySpec.Ability || !AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			continue;
		}

		bHandledAnyAbility = true;
		AbilitySpec.InputPressed = true;
		if (AbilitySpec.IsActive())
		{
			if (AbilitySpec.Ability->bReplicateInputDirectly && !IsOwnerActorAuthoritative())
			{
				ServerSetInputPressed(AbilitySpec.Handle);
			}

			AbilitySpecInputPressed(AbilitySpec);

PRAGMA_DISABLE_DEPRECATION_WARNINGS
			TArray<UGameplayAbility*> Instances = AbilitySpec.GetAbilityInstances();
			const FGameplayAbilityActivationInfo& ActivationInfo = Instances.IsEmpty() ? AbilitySpec.ActivationInfo : Instances.Last()->GetCurrentActivationInfoRef();
PRAGMA_ENABLE_DEPRECATION_WARNINGS
			InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputPressed, AbilitySpec.Handle, ActivationInfo.GetActivationPredictionKey());
		}
		else
		{
			TryActivateAbility(AbilitySpec.Handle);
		}
	}

	return bHandledAnyAbility;
}

bool UProject_JAbilitySystemComponent::AbilityInputTagReleased(const FGameplayTag& InputTag)
{
	if (!InputTag.IsValid())
	{
		return false;
	}

	bool bHandledAnyAbility = false;

	ABILITYLIST_SCOPE_LOCK();
	for (FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
	{
		if (!AbilitySpec.Ability || !AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			continue;
		}

		bHandledAnyAbility = true;
		AbilitySpec.InputPressed = false;
		if (AbilitySpec.IsActive())
		{
			if (AbilitySpec.Ability->bReplicateInputDirectly && !IsOwnerActorAuthoritative())
			{
				ServerSetInputReleased(AbilitySpec.Handle);
			}

			AbilitySpecInputReleased(AbilitySpec);

PRAGMA_DISABLE_DEPRECATION_WARNINGS
			TArray<UGameplayAbility*> Instances = AbilitySpec.GetAbilityInstances();
			const FGameplayAbilityActivationInfo& ActivationInfo = Instances.IsEmpty() ? AbilitySpec.ActivationInfo : Instances.Last()->GetCurrentActivationInfoRef();
PRAGMA_ENABLE_DEPRECATION_WARNINGS
			InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputReleased, AbilitySpec.Handle, ActivationInfo.GetActivationPredictionKey());
		}
	}

	return bHandledAnyAbility;
}

bool UProject_JAbilitySystemComponent::HasGameplayTagReplicationAuthority() const
{
	const AActor* Avatar = GetAvatarActor();
	if (Avatar && Avatar->HasAuthority())
	{
		return true;
	}

	const AActor* Owner = GetOwnerActor();
	return Owner && Owner->HasAuthority();
}

bool UProject_JAbilitySystemComponent::ReserveAbilityGrantSource(const FName SourceId, uint64& OutGeneration)
{
	OutGeneration = 0;
	if (AbilityGrantRecords.Contains(SourceId)) { return false; }
	bool bNeedsGrant = false;
	return AcquireAbilityGrantSource(SourceId, bNeedsGrant, OutGeneration);
}

bool UProject_JAbilitySystemComponent::AcquireAbilityGrantSource(FName SourceId, bool& bOutNeedsGrant, uint64& OutGeneration)
{
	check(IsInGameThread());
	bOutNeedsGrant = false;
	OutGeneration = 0;
	if (!IsOwnerActorAuthoritative() || SourceId.IsNone()) return false;
	if (auto* Record = AbilityGrantRecords.Find(SourceId))
	{
		if (Record->LeaseCount == MAX_int32) return false;
		++Record->LeaseCount;
		OutGeneration = Record->Generation;
		return true;
	}
	if (NextAbilityGrantGeneration == MAX_uint64) { return false; }
	OutGeneration = ++NextAbilityGrantGeneration;
	AbilityGrantRecords.Add(SourceId).Generation = OutGeneration;
	bOutNeedsGrant = true;
	return true;
}

bool UProject_JAbilitySystemComponent::RegisterGrantedAbility(const FName SourceId, uint64 Generation, const FGameplayAbilitySpecHandle Handle)
{
	if (FProject_JAbilityGrantRecord* Record = AbilityGrantRecords.Find(SourceId); Record && Record->Generation == Generation && Handle.IsValid())
	{
		Record->AbilityHandles.Add(Handle);
		return true;
	}
	return false;
}

bool UProject_JAbilitySystemComponent::RegisterGrantedEffect(const FName SourceId, uint64 Generation, const FActiveGameplayEffectHandle Handle)
{
	if (FProject_JAbilityGrantRecord* Record = AbilityGrantRecords.Find(SourceId); Record && Record->Generation == Generation && Handle.IsValid())
	{
		Record->EffectHandles.Add(Handle);
		return true;
	}
	return false;
}

bool UProject_JAbilitySystemComponent::RemoveAbilityGrantSource(const FName SourceId, uint64 Generation)
{
	check(IsInGameThread());
	if (!IsOwnerActorAuthoritative() || !IsAbilityGrantSourceCurrent(SourceId, Generation)) return false;
	if (auto* Existing = AbilityGrantRecords.Find(SourceId); Existing && Existing->LeaseCount > 1)
	{
		--Existing->LeaseCount;
		return true;
	}
	FProject_JAbilityGrantRecord Record;
	if (!IsOwnerActorAuthoritative() || SourceId.IsNone() || !AbilityGrantRecords.RemoveAndCopyValue(SourceId, Record))
	{
		return false;
	}

	for (const FGameplayAbilitySpecHandle Handle : Record.AbilityHandles)
	{
		if (Handle.IsValid())
		{
			CancelAbilityHandle(Handle);
			ClearAbility(Handle);
		}
	}
	for (const FActiveGameplayEffectHandle Handle : Record.EffectHandles)
	{
		if (Handle.IsValid())
		{
			RemoveActiveGameplayEffect(Handle);
		}
	}
	return true;
}

bool UProject_JAbilitySystemComponent::HasAbilityGrantSource(const FName SourceId) const
{
	return !SourceId.IsNone() && AbilityGrantRecords.Contains(SourceId);
}
bool UProject_JAbilitySystemComponent::IsAbilityGrantSourceCurrent(FName SourceId, uint64 Generation) const
{
	const auto* Record = AbilityGrantRecords.Find(SourceId);
	return Generation != 0 && Record && Record->Generation == Generation;
}
