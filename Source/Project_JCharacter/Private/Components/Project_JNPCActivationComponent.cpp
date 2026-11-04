#include "Components/Project_JNPCActivationComponent.h"
#include "Components/Project_JNPCActionComponent.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "Project_JNPCCharacter.h"
#include "AbilitySystemComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "AbilitySystemGlobals.h"
#include "System/Project_JNPCDecisionSubsystem.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Abilities/GameplayAbility.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Combat/Project_JGameplayAbility_NPCAttack.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJNPCActivation, Log, All);

#if WITH_EDITOR
EDataValidationResult UProject_JNPCActivationDefinition::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult Result = Super::IsDataValid(Context);
	if (!AttackDefinitionOverride) return Result;
	if (!IsValid(AttackDefinitionOverride.Get()) || !AttackAbility || !AttackAbility->IsChildOf(UProject_JGameplayAbility_NPCAttack::StaticClass()))
	{
		Context.AddError(NSLOCTEXT("ProjectJNPCActivation", "InvalidAttackOverride", "An attack definition override requires an NPCAttack ability class."));
		return EDataValidationResult::Invalid;
	}
	return Result == EDataValidationResult::Invalid ? Result : EDataValidationResult::Valid;
}
#endif

void UProject_JNPCActivationComponent::BeginPlay()
{
	bEnding = false;
	Super::BeginPlay();
}

bool UProject_JNPCActivationComponent::ActivateNPC(const UProject_JNPCActivationDefinition* Definition,
	UProject_JTargetScoringComponent* Scoring, UProject_JNPCActionComponent* Action, int32 TeamId)
{
	check(IsInGameThread());
	auto* NPC = Cast<AProject_JNPCCharacter>(GetOwner());
	if (bChanging || bEnding || IsBeingDestroyed() || ActionToken || ActivationSerial == MAX_uint64 || !IsRegistered() || !GetWorld() || GetWorld()->bIsTearingDown || !IsValid(Definition) || Definition->AbilityLevel < 1 ||
		!IsValid(NPC) || !NPC->HasAuthority() || NPC->IsActorBeingDestroyed() || !IsValid(Scoring) || !IsValid(Action) ||
		Scoring->GetOwner() != NPC || Action->GetOwner() != NPC || !Scoring->IsRegistered() || !Action->IsRegistered() ||
		Scoring->IsBatchedNPCDecisionRegistered() || Action->GetActionState() != EProjectJNPCActionState::Disabled || Action->HasPendingResume()) { return false; }
	const TStrongObjectPtr<ThisClass> PinnedThis(this);
	const TStrongObjectPtr<const UProject_JNPCActivationDefinition> PinnedDefinition(Definition);
	const TStrongObjectPtr<AProject_JNPCCharacter> PinnedNPC(NPC);
	const TStrongObjectPtr<UProject_JTargetScoringComponent> PinnedScoring(Scoring);
	const TStrongObjectPtr<UProject_JNPCActionComponent> PinnedAction(Action);
	const TStrongObjectPtr<UWorld> PinnedWorld(GetWorld());
	TGuardValue<bool> Guard(bChanging, true); // Reserve before the virtual ASC getter can reenter.
	++ActivationSerial;
	auto* ASC = NPC->GetAbilitySystemComponent();
	if (!IsValid(ASC) || ASC->IsBeingDestroyed() || ASC->GetAvatarActor() != NPC) { return false; }
	const TStrongObjectPtr<UAbilitySystemComponent> PinnedASC(ASC);
	if (bEnding || IsBeingDestroyed() || !IsRegistered() || GetWorld() != PinnedWorld.Get() || PinnedWorld->bIsTearingDown ||
		!IsValid(NPC) || NPC->IsActorBeingDestroyed() || GetOwner() != NPC || !NPC->HasAuthority() ||
		!IsValid(Scoring) || Scoring->IsBeingDestroyed() || !Scoring->IsRegistered() || Scoring->GetOwner() != NPC ||
		!IsValid(Action) || Action->IsBeingDestroyed() || !Action->IsRegistered() || Action->GetOwner() != NPC || ActionToken) return false;
	auto* ProjectASC = Cast<UProject_JAbilitySystemComponent>(ASC);
	if (ProjectASC && ProjectASC->IsActorInfoChanging()) return false;
	const uint64 ActorInfoRevision = ProjectASC ? ProjectASC->GetActorInfoRevision() : 0;
	const bool bThreat = Definition->bUseDamageThreat;
	const int32 SourceLimit = Definition->MaxDamageThreatSources;
	const double ValueLimit = Definition->MaxDamageThreatPerSource;
	auto* Attributes = bThreat ? NPC->GetAttributeSet() : nullptr;
	if (bThreat && (!ProjectASC || !IsValid(Attributes) || !Attributes->CanBeginHealthGEObservation()
		|| Attributes->GetOwningAbilitySystemComponent() != ASC || SourceLimit < 1 || SourceLimit > UProject_JNPCDecisionSubsystem::MaxTargets
		|| !FMath::IsFinite(ValueLimit) || ValueLimit < 1 || ValueLimit > 1.e15)) return false;
	const auto AttackClass = Definition->AttackAbility;
	const int32 AbilityLevel = Definition->AbilityLevel;
	const auto* Ability = Definition->AttackAbility ? Definition->AttackAbility->GetDefaultObject<UGameplayAbility>() : nullptr;
	if (Ability && !UProject_JNPCActionComponent::SupportsAttackAbility(Ability)) { return false; }
	auto* AttackOverride = Definition->AttackDefinitionOverride.Get();
	if (AttackOverride && (!IsValid(AttackOverride) || !Ability || !Ability->IsA<UProject_JGameplayAbility_NPCAttack>())) return false;
	const TStrongObjectPtr<UProject_JAttackDefinition> PinnedAttackOverride(AttackOverride);
	UObject* const GrantSource = AttackOverride ? static_cast<UObject*>(AttackOverride) : this;
	if (!Scoring->StartBatchedNPCDecisions(TeamId)) { return false; }
	OwnedScoring = Scoring; ScoringRevision = Scoring->GetBatchRegistrationRevision();
	OwnedASC = ASC; OwnedAction = Action;
	OwnedAttackDefinition = PinnedAttackOverride.Get();
	if (ProjectASC) ActorInfoChangingHandle = ProjectASC->OnActorInfoChanging.AddUObject(this, &ThisClass::OnActorInfoChanging);
	const auto IsCurrent = [&]
	{
		return !bEnding && !IsBeingDestroyed() && IsRegistered() && IsValid(NPC) && !NPC->IsActorBeingDestroyed()
			&& GetOwner() == NPC && NPC->HasAuthority() && GetWorld() && !GetWorld()->bIsTearingDown
			&& IsValid(ASC) && !ASC->IsBeingDestroyed() && NPC->GetAbilitySystemComponent() == ASC && ASC->GetAvatarActor() == NPC
			// A same-pointer Init/Clear replaces the runtime recipient epoch too.
			&& (!ProjectASC || ProjectASC->GetActorInfoRevision() == ActorInfoRevision)
			&& IsValid(Scoring) && !Scoring->IsBeingDestroyed() && Scoring->GetOwner() == NPC && Scoring->IsRegistered()
			&& Scoring->IsBatchedNPCDecisionRegistered() && Scoring->GetBatchRegistrationRevision() == ScoringRevision
			&& IsValid(Action) && !Action->IsBeingDestroyed() && Action->GetOwner() == NPC && Action->IsRegistered();
	};
	if (IsCurrent() && Ability) { OwnedGrant = ASC->GiveAbility(FGameplayAbilitySpec(AttackClass, AbilityLevel, INDEX_NONE, GrantSource)); }
	if (!IsCurrent() || (Ability && !OwnedGrant.IsValid()) || !Action->StartActions(Scoring, OwnedGrant))
	{
		ReleaseOwnedActivation(); return false;
	}
	ActionToken = Action->GetOwnershipToken();
	if (!IsCurrent() || !ActionToken) { ReleaseOwnedActivation(); return false; }
	ThreatStats = {};
	if (bThreat)
	{
		if (!IsValid(Attributes) || NPC->GetAttributeSet() != Attributes || !Attributes->CanBeginHealthGEObservation()
			|| !Scoring->BindDamageThreatProvider(this, ScoringRevision)) { ReleaseOwnedActivation(); return false; }
		ThreatAttributes = Attributes; ThreatRecipientEpoch = ActorInfoRevision;
		ThreatSourceLimit = SourceLimit; ThreatValueLimit = ValueLimit;
		ThreatObservationBaseline = Attributes->GetHealthGEObservationFailures(); ThreatStats.bEnabled = true;
		HealthLossHandle = Attributes->OnConfirmedHealthGELoss.AddUObject(this, &ThisClass::OnConfirmedHealthGELoss);
		HealthObservationFailureHandle = Attributes->OnHealthGEObservationFailed.AddUObject(this, &ThisClass::OnHealthGEObservationFailed);
	}
	return true;
}

void UProject_JNPCActivationComponent::ReleaseOwnedActivation()
{
	const TStrongObjectPtr<ThisClass> PinnedThis(this);
	// Detach first. Ability/scoring callbacks cannot release a subsequent activation.
	const TStrongObjectPtr<UProject_JNPCActionComponent> Action(OwnedAction.Get());
	const TStrongObjectPtr<UProject_JTargetScoringComponent> Scoring(OwnedScoring.Get());
	const TStrongObjectPtr<UAbilitySystemComponent> ASC(OwnedASC.Get());
	const TStrongObjectPtr<AActor> PinnedOwner(GetOwner());
	const TStrongObjectPtr<AActor> PinnedASCOwner(ASC ? ASC->GetOwnerActor() : nullptr);
	const auto Grant = OwnedGrant; const auto Token = ActionToken; const auto Revision = ScoringRevision;
	const TStrongObjectPtr<UProject_JAttackDefinition> PreviousAttackDefinition(OwnedAttackDefinition.Get());
	StopDamageThreatObservation(); ThreatAttributes.Reset();
	DamageThreat.Empty(); ThreatStats.bEnabled = false; ThreatStats.Sources = 0;
	ThreatRecipientEpoch = ThreatObservationBaseline = 0; ++DamageThreatRevision;
	if (auto* ProjectASC = Cast<UProject_JAbilitySystemComponent>(ASC.Get()))
		ProjectASC->OnActorInfoChanging.Remove(ActorInfoChangingHandle);
	ActorInfoChangingHandle.Reset();
	OwnedAction.Reset(); OwnedScoring.Reset(); OwnedASC.Reset(); OwnedGrant = {}; OwnedAttackDefinition = nullptr; ActionToken = ScoringRevision = 0;
	if (IsValid(Scoring.Get()) && !Scoring->IsBeingDestroyed()) Scoring->UnbindDamageThreatProvider(this, Revision);
	if (IsValid(Action.Get()) && !Action->IsBeingDestroyed()) { Action->StopActionsIfOwned(Token); }
	if (IsValid(ASC.Get()) && !ASC->IsBeingDestroyed() && Grant.IsValid())
	{
		ASC->CancelAbilityHandle(Grant);
		// Unregister/DestroyActiveState already retires a destroyed ASC's grants.
		if (IsValid(ASC.Get()) && !ASC->IsBeingDestroyed()) { ASC->ClearAbility(Grant); }
	}
	if (IsValid(Scoring.Get()) && !Scoring->IsBeingDestroyed() && Scoring->GetBatchRegistrationRevision() == Revision) { Scoring->StopBatchedNPCDecisions(); }
}

void UProject_JNPCActivationComponent::OnActorInfoChanging(AActor*, AActor*)
{
	// A grant-time transition is rolled back by ActivateNPC's epoch check.
	// An established lease must cancel while the old GAS actor info is still installed.
	DeactivateNPC();
}

void UProject_JNPCActivationComponent::DeactivateNPC()
{
	if (bChanging) { return; }
	const TStrongObjectPtr<ThisClass> PinnedThis(this);
	TGuardValue<bool> Guard(bChanging, true); ReleaseOwnedActivation();
}
bool UProject_JNPCActivationComponent::DeactivateNPCIfOwned(uint64 Token)
{
    check(IsInGameThread());
    if (!Token || bChanging || GetOwnershipToken() != Token) return false;
    DeactivateNPC();
    return GetOwnershipToken() != Token;
}

void UProject_JNPCActivationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	bEnding = true; DeactivateNPC(); Super::EndPlay(Reason);
}
void UProject_JNPCActivationComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	bEnding = true; DeactivateNPC(); Super::OnComponentDestroyed(bDestroyingHierarchy);
}

bool UProject_JNPCActivationComponent::IsCurrentDamageThreatLease() const
{
    auto* NPC = Cast<AProject_JNPCCharacter>(GetOwner());
    auto* ASC = Cast<UProject_JAbilitySystemComponent>(OwnedASC.Get());
    auto* Scoring = OwnedScoring.Get();
    return ThreatStats.bEnabled && !bEnding && !IsBeingDestroyed() && IsRegistered() && ActionToken
        && GetWorld() && !GetWorld()->bIsTearingDown && IsValid(NPC) && NPC->HasAuthority() && !NPC->IsActorBeingDestroyed()
        && IsValid(ASC) && ASC->IsRegistered() && !ASC->IsBeingDestroyed() && NPC->GetAbilitySystemComponent() == ASC
        && ASC->GetOwnerActor() == NPC && ASC->GetAvatarActor() == NPC && !ASC->IsActorInfoChanging()
        && ASC->GetActorInfoRevision() == ThreatRecipientEpoch
        && IsValid(Scoring) && !Scoring->IsBeingDestroyed() && Scoring->IsRegistered() && Scoring->GetOwner() == NPC
        && Scoring->IsBatchedNPCDecisionRegistered() && Scoring->GetBatchRegistrationRevision() == ScoringRevision
        && ThreatAttributes.IsValid() && NPC->GetAttributeSet() == ThreatAttributes.Get();
}

bool UProject_JNPCActivationComponent::IsDamageThreatActive() const
{ return IsCurrentDamageThreatLease() && !ThreatStats.bDegraded; }

bool UProject_JNPCActivationComponent::IsDamageThreatSourceCurrent(AActor* Actor, const FProject_JNPCTargetThreat Source) const
{
    const TStrongObjectPtr<AActor> KeepActor(Actor), KeepOwner(Source.Owner.Get());
    const TStrongObjectPtr<UAbilitySystemComponent> KeepASC(Source.ASC.Get());
    const auto* ASC = Cast<UProject_JAbilitySystemComponent>(KeepASC.Get());
    const auto Current = [&]
    {
        return IsDamageThreatActive() && IsValid(Actor) && !Actor->IsActorBeingDestroyed() && Actor != GetOwner()
            && Actor->HasAuthority() && Actor->GetWorld() == GetWorld() && IsValid(KeepOwner.Get()) && !KeepOwner->IsActorBeingDestroyed()
            && KeepOwner->HasAuthority() && KeepOwner->GetWorld() == GetWorld()
            && IsValid(ASC) && ASC->IsRegistered() && !ASC->IsBeingDestroyed() && !ASC->IsActorInfoChanging() && ASC->GetWorld() == GetWorld()
            && ASC->GetOwnerActor() == KeepOwner.Get() && ASC->GetAvatarActor() == Actor && ASC->GetActorInfoRevision() == Source.Epoch
            && ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()) > 0;
    };
    // The provider getter is extensible: no map entry/iterator survives this call.
    return Current() && UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor) == ASC && Current();
}

void UProject_JNPCActivationComponent::DegradeDamageThreat(EProject_JNPCThreatDegradation Reason)
{
    if (!ThreatStats.bEnabled || ThreatStats.bDegraded) return;
    ThreatStats.bDegraded = true; ThreatStats.Reason = Reason;
    StopDamageThreatObservation();
    ThreatStats.Sources = 0; DamageThreat.Empty(); ++DamageThreatRevision;
    UE_LOG(LogProjectJNPCActivation, Display, TEXT("NPCDamageThreatDegraded owner=%s reason=%d; distance/direction fallback until a new activation lease"), *GetNameSafe(GetOwner()), int32(Reason));
}

void UProject_JNPCActivationComponent::StopDamageThreatObservation()
{
    const TStrongObjectPtr<UProject_JAttributeSet> Attributes(ThreatAttributes.Get());
    if (IsValid(Attributes.Get()))
    {
        Attributes->OnConfirmedHealthGELoss.Remove(HealthLossHandle);
        Attributes->OnHealthGEObservationFailed.Remove(HealthObservationFailureHandle);
        Attributes->ReleaseUnusedHealthGEObservationStorage();
    }
    HealthLossHandle.Reset(); HealthObservationFailureHandle.Reset();
}

void UProject_JNPCActivationComponent::OnConfirmedHealthGELoss(const FProject_JConfirmedHealthGELoss& Loss)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_NPCDamageThreat_Record);
    check(IsInGameThread());
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    const TStrongObjectPtr<AActor> KeepOwner(GetOwner());
    const auto& Proof = Loss.Proof;
    if (!IsCurrentDamageThreatLease() || Proof.TargetASC != OwnedASC || Proof.TargetOwner.Get() != GetOwner()
        || Proof.TargetAvatar.Get() != GetOwner() || Proof.TargetEpoch != ThreatRecipientEpoch) return;
    ++ThreatStats.ObservedLosses;
    if (ThreatStats.bDegraded) return;
    const double Delta = double(Loss.OldValue) - Loss.NewValue;
    if (!FMath::IsFinite(Delta) || Delta <= 0 || Loss.NewValue <= 0) return;
    auto* SourceASC = Cast<UProject_JAbilitySystemComponent>(Proof.SourceASC.Get());
    auto* Source = Proof.SourceAvatar.Get();
    FProject_JNPCTargetThreat SourceProof;
    SourceProof.ASC = SourceASC; SourceProof.Owner = Proof.SourceOwner; SourceProof.Epoch = Proof.SourceEpoch;
    // Existing melee identifies its Avatar directly. Standard MakeEffectContext identifies Owner + Avatar causer.
    if (!Source || (Proof.Instigator.Get() != Source
        && !(Proof.Instigator == Proof.SourceOwner && Proof.EffectCauser.Get() == Source))
        || !IsDamageThreatSourceCurrent(Source, SourceProof)) { ++ThreatStats.RejectedSources; return; }
    const TStrongObjectPtr<AActor> KeepSource(Source), KeepSourceOwner(Proof.SourceOwner.Get());
    const TStrongObjectPtr<UAbilitySystemComponent> KeepSourceASC(SourceASC);
    const TStrongObjectPtr<UProject_JTargetScoringComponent> KeepScoring(OwnedScoring.Get());
    const uint64 Registration = ScoringRevision, Epoch = ThreatRecipientEpoch;
    auto* Scheduler = GetWorld()->GetSubsystem<UProject_JNPCDecisionSubsystem>();
    if (!Scheduler || !Scheduler->CanActOnTarget(KeepScoring.Get(), Source)
        || !IsDamageThreatActive() || ScoringRevision != Registration || ThreatRecipientEpoch != Epoch
        || OwnedScoring.Get() != KeepScoring.Get() || !IsDamageThreatSourceCurrent(Source, SourceProof))
    { if (IsCurrentDamageThreatLease()) ++ThreatStats.RejectedSources; return; }
    // Read current state only after callbacks; nested valid GE losses accumulate rather than being dropped.
    auto* Existing = DamageThreat.Find(Source);
    double Previous = 0;
    if (Existing)
    {
        if (Existing->ASC == SourceProof.ASC && Existing->Owner == SourceProof.Owner && Existing->Epoch == SourceProof.Epoch) Previous = Existing->Value;
        else ++ThreatStats.SourceEpochResets;
    }
    else if (DamageThreat.Num() >= ThreatSourceLimit)
    { ++ThreatStats.SourceLimitFailures; DegradeDamageThreat(EProject_JNPCThreatDegradation::SourceLimit); return; }
    if (!FMath::IsFinite(Previous) || Previous < 0 || Delta > ThreatValueLimit - Previous)
    { ++ThreatStats.ValueLimitFailures; DegradeDamageThreat(EProject_JNPCThreatDegradation::ValueLimit); return; }
    SourceProof.Value = Previous + Delta;
    DamageThreat.Add(Source, SourceProof); ++DamageThreatRevision; ++ThreatStats.AcceptedLosses;
    ThreatStats.Sources = DamageThreat.Num(); ThreatStats.PeakSources = FMath::Max(ThreatStats.PeakSources, ThreatStats.Sources);
}

void UProject_JNPCActivationComponent::OnHealthGEObservationFailed(const FProject_JHealthGEExecutionProof& Proof, EProject_JHealthGEObservationFailure)
{
    if (!IsCurrentDamageThreatLease() || Proof.TargetASC != OwnedASC || Proof.TargetOwner.Get() != GetOwner()
        || Proof.TargetAvatar.Get() != GetOwner() || Proof.TargetEpoch != ThreatRecipientEpoch) return;
    if (!ThreatStats.bDegraded) ++ThreatStats.ObservationFailures;
    DegradeDamageThreat(EProject_JNPCThreatDegradation::ObservationFailure);
}

bool UProject_JNPCActivationComponent::PrepareDamageThreatSelection()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_NPCDamageThreat_Prepare);
    check(IsInGameThread());
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    const TStrongObjectPtr<AActor> KeepOwner(GetOwner());
    if (!IsDamageThreatActive()) return false;
    if (ThreatAttributes->GetHealthGEObservationFailures() != ThreatObservationBaseline)
    { ++ThreatStats.ObservationFailures; DegradeDamageThreat(EProject_JNPCThreatDegradation::ObservationFailure); return false; }
    // Existing cadence owns cleanup. A rejected new damage source never scans every record.
    TArray<TWeakObjectPtr<AActor>> Keys; DamageThreat.GenerateKeyArray(Keys);
    for (const auto& Key : Keys)
    {
        const auto* Found = DamageThreat.Find(Key);
        if (!Found) continue;
        const FProject_JNPCTargetThreat Before = *Found;
        const bool bValid = IsDamageThreatSourceCurrent(Key.Get(), Before);
        if (!IsDamageThreatActive()) return false;
        Found = DamageThreat.Find(Key);
        if (!bValid && Found && Found->ASC == Before.ASC && Found->Owner == Before.Owner && Found->Epoch == Before.Epoch)
        { DamageThreat.Remove(Key); ++DamageThreatRevision; }
    }
    ThreatStats.Sources = DamageThreat.Num();
    return IsDamageThreatActive();
}

bool UProject_JNPCActivationComponent::ReadDamageThreat(AActor* Candidate, FProject_JNPCTargetThreat& Out) const
{
    check(IsInGameThread());
    if (!IsDamageThreatActive()) return false;
    const auto* Found = DamageThreat.Find(Candidate);
    if (!Found) return false;
    const FProject_JNPCTargetThreat Value = *Found;
    if (!FMath::IsFinite(Value.Value) || Value.Value <= 0 || !IsDamageThreatSourceCurrent(Candidate, Value)) return false;
    Out = Value; return IsDamageThreatActive();
}
