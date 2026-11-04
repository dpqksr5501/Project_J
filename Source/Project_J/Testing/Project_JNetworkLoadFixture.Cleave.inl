// Opt-in authored NPC attack on the existing server + two real socket clients.
// The 100 AOI probes remain static counters; only this one NPC executes combat.
namespace
{
constexpr float CleaveInitialHealth = 1000.f;
const FVector CleaveOrigin(3090, 0, 100);
FVector CleaveThreatPlayerLocation(int32 Phase, int32 Slot)
{
    const int32 ExpectedPrimary = Phase == 33 ? 1 : 0;
    return FVector(Slot == ExpectedPrimary ? 3210 : 3200, Slot == 0 ? -24 : 24, 100);
}
uint64 CleavePublicId(const AActor* Actor)
{
    const auto* Bridge = Actor ? UE::Net::FReplicationSystemUtil::GetActorReplicationBridge(Actor) : nullptr;
    return Bridge ? Bridge->GetReplicatedRefHandle(Actor).GetId() : 0;
}
template <typename T> T* CleaveComponent(AProject_JNPCCharacter* NPC)
{
    if (auto* Existing = NPC->FindComponentByClass<T>()) return Existing;
    auto* Added = NewObject<T>(NPC); NPC->AddInstanceComponent(Added); Added->RegisterComponent(); return Added;
}
bool CleaveNoPlayerConnection(AProject_JNPCCharacter* NPC)
{
    const auto* ASC = NPC ? NPC->GetAbilitySystemComponent() : nullptr;
    return NPC && !NPC->GetNetConnection() && !NPC->GetNetOwningPlayer() && !Cast<APlayerController>(NPC->GetController())
        && ASC && ASC->AbilityActorInfo.IsValid() && ASC->GetOwnerActor() == NPC && ASC->GetAvatarActor() == NPC
        && !ASC->AbilityActorInfo->PlayerController.IsValid();
}
}

bool AProject_JNetworkLoadController::ObserveCleave()
{
    if (!CleaveEnabled() || !((CurrentStage >= 30 && CurrentStage <= 32) || CleaveThreatStage(CurrentStage))) return true;
    // The same explicit close-packed geometry must not make the autonomous
    // client's ordinary Mover depenetrate while authority ignores Pawn contacts.
    for (TActorIterator<AProject_JBaseCharacter> It(GetWorld()); It; ++It)
        if (It->IsA<AProject_JPlayerCharacter>() || It->GetClass() == AProject_JNPCCharacter::StaticClass()
            || It->GetClass()->GetPathName() == TEXT("/Game/NPCs/BP_NPC_Test.BP_NPC_Test_C"))
        {
            auto* Capsule = It->GetCapsuleComponent();
            if (Capsule->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Ignore) Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
            if (Capsule->GetCollisionResponseToChannel(ECC_Visibility) != ECR_Ignore) Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
        }
    AProject_JNPCCharacter* Caster = nullptr; AProject_JNPCCharacter* NPCVictim = nullptr;
    int32 Casters = 0, NPCVictims = 0;
    for (TActorIterator<AProject_JNPCCharacter> It(GetWorld()); It; ++It)
    {
        if (It->IsActorBeingDestroyed()) continue;
        if (It->GetClass()->GetPathName() == TEXT("/Game/NPCs/BP_NPC_Test.BP_NPC_Test_C")) { Caster = *It; ++Casters; }
        else if (It->GetClass() == AProject_JNPCCharacter::StaticClass()) { NPCVictim = *It; ++NPCVictims; }
    }
    if (Casters != 1 || NPCVictims != 1 || !CleaveNoPlayerConnection(Caster) || !CleaveNoPlayerConnection(NPCVictim)) return false;
    CleaveObservedCaster = Caster;
    if (CleaveThreatEnabled() && CurrentStage == 32)
    {
        auto* Anim = Caster->GetMesh() ? Caster->GetMesh()->GetAnimInstance() : nullptr;
        auto* Montage = Anim ? Anim->GetCurrentActiveMontage() : nullptr;
        if (!Anim || (Montage && Anim->Montage_IsPlaying(Montage))) return false;
    }
    if (CleaveThreatStage(CurrentStage)) ObserveCleaveThreatMontage();
    TArray<AActor*> Victims; Victims.SetNumZeroed(3); Victims[2] = NPCVictim;
    for (TActorIterator<AProject_JPlayerCharacter> It(GetWorld()); It; ++It)
    {
        const auto* PS = It->GetPlayerState<AProject_JPlayerState>();
        if (!PS) continue;
        const FName Id = PS->GetPublicClassId();
        const int32 Slot = Id == TEXT("DClient0") ? 0 : Id == TEXT("DClient1") ? 1 : INDEX_NONE;
        if (Slot != INDEX_NONE) { if (Victims[Slot]) return false; Victims[Slot] = *It; }
    }
    int32 Damaged = 0; TSet<uint64> Seen; FString Pending; TMap<uint64, float> Health;
    const uint64 CasterId = CleavePublicId(Caster);
    if (!CasterId) return false;
    for (int32 Slot = 0; Slot < 3; ++Slot)
    {
        auto* Victim = Victims[Slot]; const uint64 Id = CleavePublicId(Victim);
        auto* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Victim);
        if (!IsValid(Victim) || !Id || Id == CasterId || Seen.Contains(Id) || !ASC) return false;
        Seen.Add(Id); const float Value = ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute());
        if (!FMath::IsFinite(Value) || Value <= 0 || Value > CleaveInitialHealth) return false;
        Damaged += Value < CleaveInitialHealth - KINDA_SMALL_NUMBER;
        if (CurrentStage == 30 && !FMath::IsNearlyEqual(Value, CleaveInitialHealth)) return false;
        if (CleaveCancellationStage(CurrentStage) && (!CleaveObservedHealth.Contains(Id) || !FMath::IsNearlyEqual(CleaveObservedHealth[Id], Value))) return false;
        Health.Add(Id, Value);
        if (CleaveThreatStage(CurrentStage))
            Pending += FString::Printf(TEXT("%d,%d,%d,%llu,%llu,%.9g,%d,%s,1\n"), CurrentStage, LocalSlot, Slot, Id, CasterId, Value,
                bCleaveThreatObservedAttack, *CleaveThreatObservedMontage);
        else Pending += FString::Printf(TEXT("%d,%d,%d,%llu,%llu,%.9g,1\n"), CurrentStage, LocalSlot, Slot, Id, CasterId, Value);
    }
    if (CurrentStage >= 31 && Damaged != 2) { CleaveStableSince = 0; return false; }
    if (CurrentStage == 33 && !bCleaveThreatObservedAttack) return false;
    const double Now = FPlatformTime::Seconds();
    if (!CleaveStableSince) CleaveStableSince = Now;
    if (Now - CleaveStableSince < .5) return false; // Replication convergence, not a hit/physics readiness shortcut.
    if (CleaveRecordedStage != CurrentStage)
    {
        CleaveRecordedStage = CurrentStage;
        if (CleaveThreatStage(CurrentStage)) CleaveThreatRows += Pending; else CleaveRows += Pending;
        if (CleaveNaturalStage(CurrentStage)) CleaveObservedHealth = MoveTemp(Health);
    }
    return true;
}

bool AProject_JNetworkLoadGameMode::PrepareCleave()
{
    CleaveDefinition = LoadObject<UProject_JNPCActivationDefinition>(nullptr, TEXT("/Game/DataAssetSets/NPC/DA_NPC_PrototypeCleaveActivation.DA_NPC_PrototypeCleaveActivation"));
    CleaveAttack = LoadObject<UProject_JAttackDefinition>(nullptr, TEXT("/Game/DataAssetSets/AttackDefinition/NPC/DA_Attack_NPC_PrototypeCleave.DA_Attack_NPC_PrototypeCleave"));
    auto* NPCClass = LoadClass<AProject_JNPCCharacter>(nullptr, TEXT("/Game/NPCs/BP_NPC_Test.BP_NPC_Test_C"));
    const auto* Ability = CleaveDefinition && CleaveDefinition->AttackAbility && CleaveDefinition->AttackAbility->IsChildOf(UProject_JGameplayAbility_NPCAttack::StaticClass())
        ? CleaveDefinition->AttackAbility->GetDefaultObject<UProject_JGameplayAbility_NPCAttack>() : nullptr;
    const auto* AttackProperty = Ability ? FindFProperty<FObjectPropertyBase>(Ability->GetClass(), TEXT("AttackDefinition")) : nullptr;
    const auto* TagProperty = Ability ? FindFProperty<FStructProperty>(Ability->GetClass(), TEXT("HitEventTag")) : nullptr;
    if (!CleaveAttack || CleaveAttack->NPCMaxMeleeTargets != 2 || !CleaveAttack->Montage || !CleaveAttack->DamageEffect || !NPCClass || !Ability
        || !UProject_JNPCActionComponent::SupportsAttackAbility(Ability) || !AttackProperty || !TagProperty
        || AttackProperty->GetObjectPropertyValue_InContainer(Ability) != CleaveAttack) return false;
    CleaveHitTag = *TagProperty->ContainerPtrToValuePtr<FGameplayTag>(Ability);
    int32 MeleeNotifies = 0; CleaveFirstImpact = 0;
    if (CleaveAttack->Montage->Notifies.Num() > 64) return false;
    for (const auto& Notify : CleaveAttack->Montage->Notifies)
        if (Notify.NotifyStateClass && Notify.NotifyStateClass->IsA<UProject_JAnimNotifyState_MeleeHit>())
        {
            ++MeleeNotifies; const auto* State = Notify.NotifyStateClass.Get();
            const auto* Event = FindFProperty<FStructProperty>(State->GetClass(), TEXT("HitEventTag"));
            if (!Event || *Event->ContainerPtrToValuePtr<FGameplayTag>(State) != CleaveHitTag) return false;
            CleaveFirstImpact = Notify.GetTriggerTime();
        }
    if (MeleeNotifies != 1 || !CleaveHitTag.IsValid() || !FMath::IsFinite(CleaveFirstImpact) || CleaveFirstImpact <= 0) return false;
    FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    CleaveCaster = GetWorld()->SpawnActor<AProject_JNPCCharacter>(NPCClass, CleaveOrigin, FRotator::ZeroRotator, P);
    CleaveNPCVictim = GetWorld()->SpawnActor<AProject_JNPCCharacter>(AProject_JNPCCharacter::StaticClass(), FVector(3200, 0, 100), FRotator::ZeroRotator, P);
    if (!CleaveCaster || !CleaveNPCVictim || !CleaveCaster->GetMesh() || !CleaveCaster->GetMesh()->GetAnimInstance()) return false;
    auto* AI = Cast<AAIController>(CleaveCaster->GetController());
    if (!AI)
    {
        AI = GetWorld()->SpawnActor<AAIController>(P); if (!AI) return false;
        CleaveController = AI; AI->Possess(CleaveCaster);
    }
    if (CleaveCaster->GetController() != AI || (AI->GetBrainComponent() && AI->GetBrainComponent()->IsRunning())
        || !CleaveNoPlayerConnection(CleaveCaster) || !CleaveNoPlayerConnection(CleaveNPCVictim)) return false;
    auto* Scoring = CleaveComponent<UProject_JTargetScoringComponent>(CleaveCaster);
    auto* Action = CleaveComponent<UProject_JNPCActionComponent>(CleaveCaster);
    auto* Activation = CleaveComponent<UProject_JNPCActivationComponent>(CleaveCaster);
    auto* Hit = CleaveComponent<UProject_JCombatHitValidationComponent>(CleaveCaster);
    if (!Scoring || !Action || !Activation || !Hit || Scoring->IsBatchedNPCDecisionRegistered() || Activation->HasOwnedActivation()
        || Action->GetActionState() != EProjectJNPCActionState::Disabled || Hit->GetActiveAttackDefinition()) return false;
    CleaveVictims.Reset(); CleavePreparationFrames.Init(INDEX_NONE, 3);
    for (int32 Slot = 0; Slot < 2; ++Slot)
    {
        auto* Player = Cast<AProject_JPlayerCharacter>(Clients[Slot]->GetPawn());
        if (!Player || !Player->GetMover()) return false;
        CleavePreparationFrames[Slot] = Player->GetMover()->GetLastTimeStep().ServerFrame;
        auto Teleport = MakeShared<FTeleportEffect>(); Teleport->TargetLocation = FVector(3200, Slot == 0 ? -24 : 24, 100);
        Teleport->bUseActorRotation = false; Teleport->TargetRotation = FRotator::ZeroRotator;
        Player->GetMover()->QueueInstantMovementEffect(Teleport);
        CleaveVictims.Add(Player);
    }
    CleaveVictims.Add(CleaveNPCVictim); CleaveEffects.Init(0, 3); CleaveHealthAfterAttack.Reset();
    auto* Scheduler = GetWorld()->GetSubsystem<UProject_JNPCDecisionSubsystem>();
    if (!Scheduler || Scheduler->GetTargetCount() != 0 || Scheduler->GetAgentCount() != 0) return false;
    for (int32 Slot = 0; Slot < 3; ++Slot)
    {
        auto* Victim = CleaveVictims[Slot].Get();
        auto* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Victim);
        if (!Victim || !ASC || ASC->GetAvatarActor() != Victim || !Scheduler->RegisterTarget(Victim, 2)) return false;
        ++CleaveRegisteredTargets;
        ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(), CleaveInitialHealth);
        ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), CleaveInitialHealth);
        auto* Base = CastChecked<AProject_JBaseCharacter>(Victim);
        Base->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
        Base->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
        Victim->ForceNetUpdate();
    }
    CleaveCaster->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
    CleaveCaster->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
    CleaveCaster->GetAbilitySystemComponent()->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(), CleaveInitialHealth);
    CleaveCaster->GetAbilitySystemComponent()->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), CleaveInitialHealth);
    const TWeakObjectPtr<AProject_JNetworkLoadGameMode> WeakThis(this); const uint64 Revision = CleaveRevision;
    auto* SourceASC = CleaveCaster->GetAbilitySystemComponent();
    CleaveEventHandle = SourceASC->AddGameplayEventTagContainerDelegate(FGameplayTagContainer(CleaveHitTag), FGameplayEventTagMulticastDelegate::FDelegate::CreateLambda(
        [WeakThis, Revision](FGameplayTag Tag, const FGameplayEventData* Payload)
        {
            const TStrongObjectPtr<AProject_JNetworkLoadGameMode> Self(WeakThis.Get());
            if (!Self || Self->bFinished || Self->CleaveRevision != Revision || !Payload || Payload->Instigator.Get() != Self->CleaveCaster) return;
            if (CleaveCancellationStage(Self->Stage))
            {
                UE_CLOG(!Self->bCleaveViolation, LogProjectJDNetwork, Display,
                    TEXT("CleaveViolation reason=ContactDuringCancel stage=%d tag=%s target=%s data=%d"), Self->Stage, *Tag.ToString(), *GetNameSafe(Payload->Target.Get()), Payload->TargetData.Num());
                Self->bCleaveViolation = true; return;
            }
            if (!CleaveNaturalStage(Self->Stage) || Tag != Self->CleaveHitTag || Payload->TargetData.Num() != 1) return;
            const auto* Data = Payload->TargetData.Get(0); const auto* Contact = Data ? Data->GetHitResult() : nullptr;
            if (!Contact || Contact->GetActor() != Payload->Target.Get())
            {
                UE_CLOG(!Self->bCleaveViolation, LogProjectJDNetwork, Display,
                    TEXT("CleaveViolation reason=ContactPayloadMismatch stage=%d target=%s hitActor=%s hitResult=%d"),
                    Self->Stage, *GetNameSafe(Payload->Target.Get()), *GetNameSafe(Contact ? Contact->GetActor() : nullptr), Contact != nullptr);
                Self->bCleaveViolation = true; return;
            }
            for (const auto& Victim : Self->CleaveVictims) if (Victim.Get() == Contact->GetActor()) Self->CleaveContactIds.Add(CleavePublicId(Victim.Get()));
        }));
    CleaveEndHandle = SourceASC->OnAbilityEnded.AddLambda([WeakThis, Revision](const FAbilityEndedData& Data)
    {
        const TStrongObjectPtr<AProject_JNetworkLoadGameMode> Self(WeakThis.Get());
        if (!Self || Self->bFinished || Self->CleaveRevision != Revision || !Data.AbilityThatEnded
            || Data.AbilityThatEnded->GetClass() != Self->CleaveDefinition->AttackAbility) return;
        if (CleaveNaturalStage(Self->Stage) && !Data.bWasCancelled && !Self->bCleaveNaturalEnd)
        {
            Self->bCleaveNaturalEnd = true;
            const TStrongObjectPtr<UProject_JNPCActivationComponent> Activation(Self->CleaveCaster->FindComponentByClass<UProject_JNPCActivationComponent>());
            if (Activation) Activation->DeactivateNPC(); // Stop the owned lease before another scheduled attack.
        }
        else if (CleaveCancellationStage(Self->Stage) && Self->bCleaveCancelling && Data.bWasCancelled) Self->bCleaveCancelled = true;
        else
        {
            const auto* Action = IsValid(Self->CleaveCaster) ? Self->CleaveCaster->FindComponentByClass<UProject_JNPCActionComponent>() : nullptr;
            const auto* Scoring = IsValid(Self->CleaveCaster) ? Self->CleaveCaster->FindComponentByClass<UProject_JTargetScoringComponent>() : nullptr;
            auto* Caster = IsValid(Self->CleaveCaster) ? Self->CleaveCaster.Get() : nullptr;
            auto* Intent = Action ? Action->GetIntentTarget() : nullptr;
            auto* Scored = Scoring ? Scoring->GetLastScoredTarget() : nullptr;
            const auto Physical = Project_J::Movement::ReadPhysicalFrame(Caster);
            const auto* Mover = Caster ? Caster->GetMover() : nullptr;
            const auto* ASC = Caster ? Caster->GetAbilitySystemComponent() : nullptr;
            const auto HealthAt = [&](int32 Slot)
            {
                const auto* VictimASC = Self->CleaveObservedASCs.IsValidIndex(Slot) ? Self->CleaveObservedASCs[Slot].Get() : nullptr;
                return VictimASC ? VictimASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()) : -1.f;
            };
            UE_CLOG(!Self->bCleaveViolation, LogProjectJDNetwork, Display,
                TEXT("CleaveViolation reason=UnexpectedAbilityEnd stage=%d cancelled=%d naturalEnd=%d cancelling=%d expectedCancelled=%d intent=%s intentId=%llu scored=%s scoredId=%llu actionState=%d physicalValid=%d grounded=%d mode=%d simFrame=%d velocity=%s location=%s sourceHealth=%.9g targetHealth=%.9g/%.9g/%.9g"),
                Self->Stage, Data.bWasCancelled, Self->bCleaveNaturalEnd, Self->bCleaveCancelling, Self->bCleaveCancelled,
                *GetNameSafe(Intent), CleavePublicId(Intent), *GetNameSafe(Scored), CleavePublicId(Scored), Action ? int32(Action->GetActionState()) : INDEX_NONE,
                Physical.bValid, Physical.bGrounded, int32(Physical.Mode), Mover ? Mover->GetLastTimeStep().ServerFrame : INDEX_NONE, *Physical.Velocity.ToCompactString(),
                *(Caster ? Caster->GetActorLocation().ToCompactString() : FString(TEXT("Unavailable"))),
                ASC ? ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()) : -1.f, HealthAt(0), HealthAt(1), HealthAt(2));
            Self->bCleaveViolation = true;
        }
    });
    CleaveObservedASCs.Reset(); CleaveEffectHandles.Reset();
    for (int32 Slot = 0; Slot < 3; ++Slot)
    {
        auto* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(CleaveVictims[Slot].Get()); CleaveObservedASCs.Add(ASC);
        CleaveEffectHandles.Add(ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda([WeakThis, Revision, Slot](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
        {
            const TStrongObjectPtr<AProject_JNetworkLoadGameMode> Self(WeakThis.Get());
            if (!Self || Self->bFinished || Self->CleaveRevision != Revision || Spec.Def != Self->CleaveAttack->DamageEffect->GetDefaultObject<UGameplayEffect>()
                || Spec.GetContext().GetOriginalInstigator() != Self->CleaveCaster) return;
            if (!CleaveNaturalStage(Self->Stage) || ++Self->CleaveEffects[Slot] > 1)
            {
                UE_CLOG(!Self->bCleaveViolation, LogProjectJDNetwork, Display,
                    TEXT("CleaveViolation reason=UnexpectedEffect stage=%d slot=%d count=%d target=%s effect=%s instigator=%s"),
                    Self->Stage, Slot, Self->CleaveEffects[Slot], *GetNameSafe(Self->CleaveVictims[Slot].Get()), *GetNameSafe(Spec.Def.Get()), *GetNameSafe(Spec.GetContext().GetOriginalInstigator()));
                Self->bCleaveViolation = true;
            }
        }));
    }
    return true;
}

void AProject_JNetworkLoadController::ObserveCleaveThreatMontage()
{
    if (!CleaveThreatEnabled() || CurrentStage != 33 || bCleaveThreatObservedAttack) return;
    auto* Caster = CleaveObservedCaster.Get();
    auto* Anim = IsValid(Caster) && Caster->GetMesh() ? Caster->GetMesh()->GetAnimInstance() : nullptr;
    auto* Montage = Anim ? Anim->GetCurrentActiveMontage() : nullptr;
    if (Montage && Anim->Montage_IsPlaying(Montage))
    {
        bCleaveThreatObservedAttack = true;
        CleaveThreatObservedMontage = Montage->GetPathName();
    }
}

bool AProject_JNetworkLoadGameMode::InjectCleaveThreat()
{
    const TStrongObjectPtr<AProject_JNetworkLoadGameMode> KeepSelf(this);
    const TStrongObjectPtr<AProject_JNPCCharacter> Caster(CleaveCaster);
    const TStrongObjectPtr<UProject_JNPCActivationComponent> Activation(Caster ? Caster->FindComponentByClass<UProject_JNPCActivationComponent>() : nullptr);
    const TStrongObjectPtr<UProject_JTargetScoringComponent> Scoring(Caster ? Caster->FindComponentByClass<UProject_JTargetScoringComponent>() : nullptr);
    const TStrongObjectPtr<UAbilitySystemComponent> TargetASC(Caster ? Caster->GetAbilitySystemComponent() : nullptr);
    const uint64 Revision = CleaveRevision;
    const int32 Phase = Stage, ExpectedSlot = Phase == 33 ? 1 : 0;
    const auto IsCurrent = [&]()
    {
        return !bFinished && Stage == Phase && CleaveRevision == Revision && CleaveThreatStage(Phase)
            && IsValid(Caster.Get()) && !Caster->IsActorBeingDestroyed() && CleaveCaster == Caster.Get()
            && IsValid(Activation.Get()) && Activation->IsDamageThreatActive() && IsValid(TargetASC.Get())
            && Caster->GetAbilitySystemComponent() == TargetASC.Get() && GetWorld() && !GetWorld()->bIsTearingDown;
    };
    if (!IsCurrent() || !Scoring || !CleaveAttack || !CleaveAttack->DamageEffect
        || Activation->GetDamageThreatStats().Sources || Activation->GetDamageThreatStats().AcceptedLosses) return false;
    CleaveThreatCasterBefore = TargetASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute());
    ProjectJ::TargetScoring::FSnapshot Stock;
    Stock.Origin = Caster->GetActorLocation(); Stock.Forward = Caster->GetActorForwardVector();
    Stock.Range = Scoring->Range; Stock.DistanceWeight = Scoring->DistanceWeight; Stock.DirectionWeight = Scoring->DirectionWeight;
    for (int32 Slot = 0; Slot < 3; ++Slot)
    {
        auto* Victim = CleaveVictims[Slot].Get();
        if (!IsValid(Victim) || Victim->IsActorBeingDestroyed()) return false;
        Stock.Candidates.Add({Slot, Victim->GetActorLocation()});
        CleaveThreatDistances[Slot] = FVector::Distance(Stock.Origin, Victim->GetActorLocation());
    }
    const std::atomic<bool> Cancelled(false);
    const auto StockResult = ProjectJ::TargetScoring::Evaluate(Stock, false, Cancelled);
    if (StockResult.Status != ProjectJ::TargetScoring::EStatus::Completed || StockResult.EligibleCandidates != 3
        || StockResult.BestId < 0 || StockResult.BestId > 2 || StockResult.BestId == ExpectedSlot
        || CleaveThreatDistances[ExpectedSlot] <= CleaveThreatDistances[1 - ExpectedSlot] + 5) return false;
    CleaveThreatStockId = CleavePublicId(CleaveVictims[StockResult.BestId].Get());
    // These are ordinary outgoing authored GE executions. Each accepted credit
    // is cross-checked against actual clamped Health loss, never an apply bool.
    for (int32 Slot = 0; Slot < 2; ++Slot)
    {
        const TStrongObjectPtr<AActor> Source(CleaveVictims[Slot].Get());
        const TStrongObjectPtr<UProject_JAbilitySystemComponent> ASC(Cast<UProject_JAbilitySystemComponent>(CleaveObservedASCs[Slot].Get()));
        if (!IsCurrent() || !IsValid(Source.Get()) || !ASC || ASC->GetAvatarActor() != Source.Get()
            || UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Source.Get()) != ASC.Get()) return false;
        CleaveThreatSourceOwners[Slot] = ASC->GetOwnerActor();
        CleaveThreatSourceEpochs[Slot] = ASC->GetActorInfoRevision();
        CleaveThreatTotals[Slot] = 0;
        const int32 Count = Slot == ExpectedSlot ? 3 : 2;
        for (int32 Hit = 0; Hit < Count; ++Hit)
        {
            const auto BeforeCount = Activation->GetDamageThreatStats().AcceptedLosses;
            const float Before = TargetASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute());
            auto Context = ASC->MakeEffectContext(); Context.AddInstigator(Source.Get(), Source.Get());
            const auto Spec = ASC->MakeOutgoingSpec(CleaveAttack->DamageEffect, 1.f, Context);
            if (!Spec.IsValid()) return false;
            ASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC.Get());
            if (!IsCurrent() || ASC->GetOwnerActor() != CleaveThreatSourceOwners[Slot].Get()
                || ASC->GetAvatarActor() != Source.Get() || ASC->GetActorInfoRevision() != CleaveThreatSourceEpochs[Slot]) return false;
            const float After = TargetASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute());
            const double Loss = double(Before) - After;
            if (!FMath::IsFinite(Loss) || Loss <= 0 || After <= 0
                || Activation->GetDamageThreatStats().AcceptedLosses != BeforeCount + 1) return false;
            CleaveThreatTotals[Slot] += Loss;
        }
        FProject_JNPCTargetThreat Recorded;
        if (!Activation->ReadDamageThreat(Source.Get(), Recorded) || Recorded.ASC.Get() != ASC.Get()
            || Recorded.Owner != CleaveThreatSourceOwners[Slot] || Recorded.Epoch != CleaveThreatSourceEpochs[Slot]
            || !FMath::IsNearlyEqual(Recorded.Value, CleaveThreatTotals[Slot], 0.001)) return false;
    }
    CleaveThreatCasterAfter = TargetASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute());
    const auto& Stats = Activation->GetDamageThreatStats();
    return IsCurrent() && CleaveThreatStockId && CleaveThreatTotals[ExpectedSlot] > CleaveThreatTotals[1 - ExpectedSlot]
        && !Stats.bDegraded && Stats.Sources == 2 && Stats.PeakSources == 2 && Stats.ObservedLosses == 5 && Stats.AcceptedLosses == 5
        && !Stats.RejectedSources && !Stats.SourceEpochResets
        && FMath::IsNearlyEqual(double(CleaveThreatCasterBefore) - CleaveThreatCasterAfter,
            CleaveThreatTotals[0] + CleaveThreatTotals[1], 0.001);
}

bool AProject_JNetworkLoadGameMode::CleaveThreatSourcesCurrent() const
{
    for (int32 Slot = 0; Slot < 2; ++Slot)
    {
        const auto* ASC = Cast<UProject_JAbilitySystemComponent>(CleaveObservedASCs[Slot].Get());
        if (!IsValid(ASC) || ASC->IsActorInfoChanging() || ASC->GetOwnerActor() != CleaveThreatSourceOwners[Slot].Get()
            || ASC->GetAvatarActor() != CleaveVictims[Slot].Get() || ASC->GetActorInfoRevision() != CleaveThreatSourceEpochs[Slot]
            || UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(CleaveVictims[Slot].Get()) != ASC) return false;
    }
    return true;
}

void AProject_JNetworkLoadGameMode::StartCleaveStage(int32 Phase)
{
    Stage = Phase; StageStarted = FPlatformTime::Seconds(); RequiredPulse = Pulse + 1;
    bCleavePublished = bCleaveStageReady = bCleaveStartedAttack = false;
    for (auto& PC : Clients) PC->ConfirmedStage = INDEX_NONE;
    if (Phase == 30 && !PrepareCleave()) { Finish(false, TEXT("Authored cleave preparation/contract failed")); return; }
    if (CleaveThreatStage(Phase))
    {
        if (!CleaveCaster || !CleaveCaster->GetMover() || !CleaveDefinition || CleaveDefinition->bUseDamageThreat)
        { Finish(false, TEXT("Threat consumer requires the unchanged default-OFF authored activation")); return; }
        if (Phase == 33)
        {
            CleaveThreatDefinition = DuplicateObject<UProject_JNPCActivationDefinition>(CleaveDefinition, this);
            if (!CleaveThreatDefinition) { Finish(false, TEXT("Transient activation definition duplication failed")); return; }
            CleaveThreatDefinition->SetFlags(RF_Transient);
            CleaveThreatDefinition->bUseDamageThreat = true;
            bCleaveNaturalEnd = false;
            for (const auto& Recipient : CleaveObservedASCs)
            {
                if (!Recipient.IsValid()) { Finish(false, TEXT("Threat recipient preparation disappeared")); return; }
                Recipient->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), CleaveInitialHealth);
            }
        }
        bCleaveCancelling = bCleaveCancelled = false;
        CleaveContactIds.Reset(); CleaveEffects.Init(0, 3); CleaveThreatPrimaryId = CleaveThreatStockId = 0; CleaveThreatMontage.Reset();
        CleaveThreatCasterBefore = CleaveThreatCasterAfter = 0;
        for (int32 Slot = 0; Slot < 2; ++Slot) { CleaveThreatTotals[Slot] = 0; CleaveThreatSourceEpochs[Slot] = 0; CleaveThreatSourceOwners[Slot].Reset(); }
        for (int32 Slot = 0; Slot < 2; ++Slot)
        {
            auto* Player = Cast<AProject_JPlayerCharacter>(CleaveVictims[Slot].Get());
            if (!Player || !Player->GetMover()) { Finish(false, TEXT("Threat source Mover missing")); return; }
            CleavePreparationFrames[Slot] = Player->GetMover()->GetLastTimeStep().ServerFrame;
            auto Teleport = MakeShared<FTeleportEffect>(); Teleport->TargetLocation = CleaveThreatPlayerLocation(Phase, Slot);
            Teleport->bUseActorRotation = false; Teleport->TargetRotation = FRotator::ZeroRotator;
            Player->GetMover()->QueueInstantMovementEffect(Teleport);
        }
        auto* ASC = CleaveCaster->GetAbilitySystemComponent();
        ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(), 10000.f);
        ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 10000.f);
    }
    if (CleaveCancellationStage(Phase) || CleaveThreatStage(Phase))
    {
        CleaveContactIds.Reset(); CleaveEffects.Init(0, 3);
        if (!CleaveCaster || !CleaveCaster->GetMover()) { Finish(false, TEXT("Cleave cancellation preparation Mover missing")); return; }
        CleavePreparationFrames[2] = CleaveCaster->GetMover()->GetLastTimeStep().ServerFrame;
        auto Teleport = MakeShared<FTeleportEffect>(); Teleport->TargetLocation = CleaveOrigin;
        Teleport->bUseActorRotation = false; Teleport->TargetRotation = FRotator::ZeroRotator;
        CleaveCaster->GetMover()->QueueInstantMovementEffect(Teleport);
    }
}

void AProject_JNetworkLoadGameMode::UpdateCleave()
{
    if (!IsValid(CleaveCaster) || CleaveCaster->IsActorBeingDestroyed() || !IsValid(CleaveNPCVictim) || CleaveNPCVictim->IsActorBeingDestroyed()
        || CleaveVictims.Num() != 3 || CleaveObservedASCs.Num() != 3 || !CleaveNoPlayerConnection(CleaveCaster)
        || bCleaveViolation || !GetWorld() || GetWorld()->bIsTearingDown)
    {
        UE_LOG(LogProjectJDNetwork, Display,
            TEXT("CleaveFailure stage=%d casterValid=%d casterDestroying=%d victimValid=%d victimDestroying=%d victims=%d ASCs=%d noPlayerConnection=%d violation=%d world=%d tearingDown=%d effects=%d/%d/%d contacts=%d naturalEnd=%d cancelling=%d cancelled=%d"),
            Stage, IsValid(CleaveCaster), IsValid(CleaveCaster) && CleaveCaster->IsActorBeingDestroyed(),
            IsValid(CleaveNPCVictim), IsValid(CleaveNPCVictim) && CleaveNPCVictim->IsActorBeingDestroyed(), CleaveVictims.Num(), CleaveObservedASCs.Num(),
            IsValid(CleaveCaster) && CleaveNoPlayerConnection(CleaveCaster), bCleaveViolation, GetWorld() != nullptr, GetWorld() && GetWorld()->bIsTearingDown,
            CleaveEffects.IsValidIndex(0) ? CleaveEffects[0] : INDEX_NONE, CleaveEffects.IsValidIndex(1) ? CleaveEffects[1] : INDEX_NONE,
            CleaveEffects.IsValidIndex(2) ? CleaveEffects[2] : INDEX_NONE, CleaveContactIds.Num(), bCleaveNaturalEnd, bCleaveCancelling, bCleaveCancelled);
        Finish(false, TEXT("Cleave authority/lifetime/effect-count violation")); return;
    }
    auto* ASC = CleaveCaster->GetAbilitySystemComponent();
    auto* Activation = CleaveCaster->FindComponentByClass<UProject_JNPCActivationComponent>();
    auto* Scoring = CleaveCaster->FindComponentByClass<UProject_JTargetScoringComponent>();
    auto* Action = CleaveCaster->FindComponentByClass<UProject_JNPCActionComponent>();
    auto* Hit = CleaveCaster->FindComponentByClass<UProject_JCombatHitValidationComponent>();
    if (!ASC || !Activation || !Scoring || !Action || !Hit) { Finish(false, TEXT("Cleave consumer disappeared")); return; }
    for (int32 Slot = 0; Slot < 3; ++Slot)
        if (!CleaveVictims[Slot].IsValid() || CleaveVictims[Slot]->IsActorBeingDestroyed() || !CleaveObservedASCs[Slot].IsValid()
            || UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(CleaveVictims[Slot].Get()) != CleaveObservedASCs[Slot].Get()
            || CleaveObservedASCs[Slot]->GetAvatarActor() != CleaveVictims[Slot].Get())
        { Finish(false, TEXT("Cleave recipient/Avatar replaced")); return; }
    if (!bCleavePublished)
    {
        if (!Project_J::Movement::ReadPhysicalFrame(CleaveCaster).bGrounded) return;
        if (Stage == 30)
            for (int32 Slot = 0; Slot < 2; ++Slot)
            {
                const auto* Player = Cast<AProject_JPlayerCharacter>(CleaveVictims[Slot].Get());
                if (Player->GetMover()->GetLastTimeStep().ServerFrame <= CleavePreparationFrames[Slot]
                    || FVector::DistSquared2D(Player->GetActorLocation(), FVector(3200, Slot == 0 ? -24 : 24, 100)) > 1) return;
            }
        if (CleaveThreatStage(Stage))
            for (int32 Slot = 0; Slot < 2; ++Slot)
            {
                const auto* Player = Cast<AProject_JPlayerCharacter>(CleaveVictims[Slot].Get());
                if (!Player || !Player->GetMover() || Player->GetMover()->GetLastTimeStep().ServerFrame <= CleavePreparationFrames[Slot]
                    || FVector::DistSquared2D(Player->GetActorLocation(), CleaveThreatPlayerLocation(Stage, Slot)) > 1) return;
            }
        if ((CleaveCancellationStage(Stage) || CleaveThreatStage(Stage)) && (CleaveCaster->GetMover()->GetLastTimeStep().ServerFrame <= CleavePreparationFrames[2]
            || FVector::DistSquared2D(CleaveCaster->GetActorLocation(), CleaveOrigin) > 1)) return;
        for (const auto& Victim : CleaveVictims)
        {
            const auto Physical = Project_J::Movement::ReadPhysicalFrame(Cast<AProject_JBaseCharacter>(Victim.Get()));
            if (!Physical.bValid || !Physical.bGrounded || !CleavePublicId(Victim.Get())) return;
            if (const auto* Player = Cast<AProject_JPlayerCharacter>(Victim.Get())) if (Player->GetLocomotionAnimStateComponent()->bIsLanding) return;
        }
        if (!CleavePublicId(CleaveCaster)) return;
        TArray<AProject_JPlayerCharacter*> Players; TArray<FProject_JReplicatedAnimEventState> States;
        for (auto& PC : Clients)
        {
            auto* Player = CastChecked<AProject_JPlayerCharacter>(PC->GetPawn()); Players.Add(Player); States.Add(ReadAnimationEvents(Player));
            PC->ClientConfigure(Stage, PC->FixtureSlot, ExpectedIds(PC->FixtureSlot), Items[PC->FixtureSlot], false, RequiredPulse);
        }
        for (auto& PC : Clients) PC->ClientAnimationExpect(Stage, Players, States);
        bCleavePublished = true;
        if (Stage != 30)
        {
            const auto* Definition = CleaveThreatStage(Stage) ? CleaveThreatDefinition.Get() : CleaveDefinition.Get();
            if (!Activation->ActivateNPC(Definition, Scoring, Action, 1)) { Finish(false, TEXT("Actual owned NPC activation failed")); return; }
            if (CleaveThreatStage(Stage))
            {
                // Keep the diagnostic/cleanup participants alive if an actual
                // GE callback retires the fixture while injection is on-stack.
                const TStrongObjectPtr<AProject_JNetworkLoadGameMode> KeepSelf(this);
                const TStrongObjectPtr<AProject_JNPCCharacter> KeepCaster(CleaveCaster);
                const TStrongObjectPtr<UProject_JNPCActivationComponent> KeepActivation(Activation);
                const TStrongObjectPtr<UAbilitySystemComponent> KeepASC(ASC);
                if (!InjectCleaveThreat())
                {
                    if (bFinished) return;
                    const auto& Stats = Activation->GetDamageThreatStats();
                    UE_LOG(LogProjectJDNetwork, Display,
                        TEXT("ThreatInjectionFailure stage=%d enabled=%d degraded=%d reason=%d sources=%d observed=%llu accepted=%llu rejected=%llu epochResets=%llu casterBefore=%.9g casterCurrent=%.9g totals=%.9g/%.9g distances=%.9g/%.9g/%.9g stockId=%llu sourceEpochs=%llu/%llu"),
                        Stage, Stats.bEnabled, Stats.bDegraded, int32(Stats.Reason), Stats.Sources, Stats.ObservedLosses, Stats.AcceptedLosses,
                        Stats.RejectedSources, Stats.SourceEpochResets, CleaveThreatCasterBefore, ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()),
                        CleaveThreatTotals[0], CleaveThreatTotals[1], CleaveThreatDistances[0], CleaveThreatDistances[1], CleaveThreatDistances[2],
                        CleaveThreatStockId, CleaveThreatSourceEpochs[0], CleaveThreatSourceEpochs[1]);
                    Finish(false, TEXT("Actual outgoing GE/Health/threat ordering proof failed")); return;
                }
            }
            bCleaveStartedAttack = true;
        }
    }
    if (Stage == 30) bCleaveStageReady = true;
    if (CleaveThreatStage(Stage) && bCleaveStartedAttack && !CleaveThreatSourcesCurrent())
    { Finish(false, TEXT("Threat source owner/Avatar/epoch replaced during the real consumer")); return; }
    if (CleaveThreatStage(Stage) && !CleaveThreatPrimaryId)
    {
        const auto Handle = Action->GetAttackAbilityHandle(); const auto* Spec = ASC->FindAbilitySpecFromHandle(Handle);
        const auto* Instance = Spec ? Spec->GetPrimaryInstance() : nullptr;
        auto* Anim = CleaveCaster->GetMesh()->GetAnimInstance();
        if (Instance && Instance->IsActive() && Anim && Anim->Montage_IsPlaying(CleaveAttack->Montage)
            && Hit->GetActiveAttackDefinition() == CleaveAttack)
        {
            const int32 ExpectedSlot = Stage == 33 ? 1 : 0;
            auto* Primary = Action->GetIntentTarget();
            if (Primary != CleaveVictims[ExpectedSlot].Get() || !Action->CanCommitToTarget(Primary))
            { Finish(false, TEXT("Actual NPCAttack primary disagrees with cumulative threat")); return; }
            CleaveThreatPrimaryId = CleavePublicId(Primary); CleaveThreatMontage = CleaveAttack->Montage->GetPathName();
        }
    }
    if (CleaveCancellationStage(Stage) && bCleaveStartedAttack && !bCleaveCancelling)
    {
        const auto Handle = Action->GetAttackAbilityHandle(); const auto* Spec = ASC->FindAbilitySpecFromHandle(Handle);
        auto* Instance = Spec ? Spec->GetPrimaryInstance() : nullptr;
        auto* Anim = CleaveCaster->GetMesh()->GetAnimInstance();
        if (Instance && Instance->IsActive() && Anim && Anim->Montage_IsPlaying(CleaveAttack->Montage)
            && Hit->GetActiveAttackDefinition() == CleaveAttack)
        {
            const float Position = Anim->Montage_GetPosition(CleaveAttack->Montage);
            if (Position >= CleaveFirstImpact || !CleaveContactIds.IsEmpty()) { Finish(false, TEXT("Pre-impact cancellation boundary was not observed")); return; }
            bCleaveCancelling = true; Activation->DeactivateNPC();
            bCleaveStageReady = bCleaveCancelled && !Activation->HasOwnedActivation() && !Hit->GetActiveAttackDefinition()
                && !Anim->Montage_IsPlaying(CleaveAttack->Montage) && !ASC->FindAbilitySpecFromHandle(Handle);
            if (!bCleaveStageReady) { Finish(false, TEXT("Actual cancellation did not release the owned attack/grant/montage")); return; }
        }
    }
    if (CleaveNaturalStage(Stage) && bCleaveNaturalEnd)
    {
        int32 Effects = 0, Damaged = 0; TArray<float> Health;
        for (int32 Slot = 0; Slot < 3; ++Slot)
        {
            auto* VictimASC = CleaveObservedASCs[Slot].Get();
            if (!VictimASC) { Finish(false, TEXT("Cleave recipient ASC retired")); return; }
            const float Value = VictimASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute());
            if (!FMath::IsFinite(Value) || Value <= 0 || Value > CleaveInitialHealth || (CleaveEffects[Slot] == 1) != (Value < CleaveInitialHealth))
            { Finish(false, TEXT("Actual damage and effect observations disagree")); return; }
            Effects += CleaveEffects[Slot]; Damaged += Value < CleaveInitialHealth; Health.Add(Value);
        }
        bCleaveStageReady = Effects == 2 && Damaged == 2 && CleaveContactIds.Num() == 3 && !CleaveContactIds.Contains(0)
            && !Activation->HasOwnedActivation() && !Hit->GetActiveAttackDefinition() && Action->GetActionState() == EProjectJNPCActionState::Disabled;
        if (!bCleaveStageReady) { Finish(false, TEXT("Natural authored sweep must contact all3 victims and damage exactly2 once")); return; }
        if (Stage == 33 && CleaveEffects[1] != 1)
        { Finish(false, TEXT("The selected higher-threat primary did not receive its ordinary authored hit")); return; }
        CleaveHealthAfterAttack = MoveTemp(Health);
    }
    if (CleaveCancellationStage(Stage) && bCleaveStageReady)
        for (int32 Slot = 0; Slot < 3; ++Slot)
            if (CleaveEffects[Slot] || !FMath::IsNearlyEqual(CleaveObservedASCs[Slot]->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()), CleaveHealthAfterAttack[Slot]))
            { Finish(false, TEXT("Cancelled owned attack changed Health")); return; }
    if (CleaveThreatStage(Stage) && bCleaveStageReady)
    {
        const auto& Stats = Activation->GetDamageThreatStats();
        if (!CleaveThreatPrimaryId || Stats.bDegraded || Stats.Sources || Stats.PeakSources != 2 || Stats.AcceptedLosses != 5
            || Activation->IsDamageThreatActive() || Activation->HasOwnedActivation() || Scoring->IsBatchedNPCDecisionRegistered()
            || Action->GetActionState() != EProjectJNPCActionState::Disabled)
        { Finish(false, TEXT("Actual threat attack/retirement proof incomplete")); return; }
    }
    if (bCleaveStageReady && FPlatformTime::Seconds() - StageStarted >= 3
        && !Clients.ContainsByPredicate([this](const auto& PC) { return !IsValid(PC) || PC->ConfirmedStage != Stage; }))
    {
        for (int32 Slot = 0; Slot < 3; ++Slot)
        {
            auto* Victim = CleaveVictims[Slot].Get(); auto* Recipient = CleaveObservedASCs[Slot].Get();
            if (CleaveThreatStage(Stage))
            {
                const auto& Stats = Activation->GetDamageThreatStats(); const int32 ExpectedSlot = Stage == 33 ? 1 : 0;
                CleaveThreatAuthorityRows += FString::Printf(TEXT("%d,%d,%llu,%llu,%llu,%llu,%llu,%llu,%.9g,%.9g,%.9g,%d,%d,%d,%d,%d,%llu,1,1,%.9g,%.9g,%s,1\n"),
                    Stage, Slot, CleavePublicId(Victim), CleavePublicId(CleaveCaster), CleavePublicId(CleaveVictims[ExpectedSlot].Get()),
                    CleaveThreatPrimaryId, CleaveThreatStockId, Slot < 2 ? CleaveThreatSourceEpochs[Slot] : uint64(0),
                    Slot < 2 ? CleaveThreatTotals[Slot] : 0., CleaveThreatDistances[Slot], Recipient->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()),
                    CleaveEffects[Slot], CleaveContactIds.Contains(CleavePublicId(Victim)), bCleaveNaturalEnd, bCleaveCancelled,
                    Stats.PeakSources, Stats.AcceptedLosses, CleaveThreatCasterBefore, CleaveThreatCasterAfter, *CleaveThreatMontage);
                continue;
            }
            CleaveAuthorityRows += FString::Printf(TEXT("%d,%d,%llu,%llu,%.9g,%d,%d,%d,%d,1\n"), Stage, Slot, CleavePublicId(Victim), CleavePublicId(CleaveCaster),
                Recipient->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()), CleaveEffects[Slot], CleaveContactIds.Contains(CleavePublicId(Victim)), bCleaveNaturalEnd, bCleaveCancelled);
        }
        if (Stage < 32 || (CleaveThreatEnabled() && Stage < 34)) StartCleaveStage(Stage + 1);
        else { CleanupCleave(); Stage = 6; StageStarted = FPlatformTime::Seconds(); Clients[1]->ClientFinish(true); }
    }
}

void AProject_JNetworkLoadGameMode::CleanupCleave()
{
    ++CleaveRevision;
    const TStrongObjectPtr<AProject_JNPCCharacter> Caster(CleaveCaster); const TStrongObjectPtr<AProject_JNPCCharacter> NPCVictim(CleaveNPCVictim);
    const TStrongObjectPtr<AAIController> Controller(CleaveController.Get());
    const TStrongObjectPtr<UAbilitySystemComponent> SourceASC(Caster ? Caster->GetAbilitySystemComponent() : nullptr);
    if (SourceASC)
    {
        SourceASC->RemoveGameplayEventTagContainerDelegate(FGameplayTagContainer(CleaveHitTag), CleaveEventHandle);
        SourceASC->OnAbilityEnded.Remove(CleaveEndHandle);
    }
    for (int32 Slot = 0; Slot < CleaveObservedASCs.Num(); ++Slot)
        if (auto* ASC = CleaveObservedASCs[Slot].Get()) ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(CleaveEffectHandles[Slot]);
    CleaveObservedASCs.Reset(); CleaveEffectHandles.Reset(); CleaveEventHandle.Reset(); CleaveEndHandle.Reset();
    if (auto* Registry = GetWorld() ? GetWorld()->GetSubsystem<UProject_JNPCDecisionSubsystem>() : nullptr)
        for (int32 Slot = 0; Slot < CleaveRegisteredTargets; ++Slot) Registry->UnregisterTarget(CleaveVictims[Slot].Get());
    CleaveRegisteredTargets = 0;
    CleaveVictims.Reset(); CleaveCaster = nullptr; CleaveNPCVictim = nullptr; CleaveController.Reset();
    if (IsValid(Caster.Get()) && !Caster->IsActorBeingDestroyed())
    {
        const TStrongObjectPtr<UProject_JNPCActivationComponent> Activation(Caster->FindComponentByClass<UProject_JNPCActivationComponent>());
        if (Activation) Activation->DeactivateNPC(); Caster->Destroy();
    }
    if (IsValid(NPCVictim.Get()) && !NPCVictim->IsActorBeingDestroyed()) NPCVictim->Destroy();
    if (IsValid(Controller.Get()) && !Controller->IsActorBeingDestroyed()) Controller->Destroy();
    CleaveThreatDefinition = nullptr; CleaveDefinition = nullptr; CleaveAttack = nullptr;
}
