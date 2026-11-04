#include "Testing/Project_JRenderedCrowdFixture.h"
#include "Project_JNPCCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Project_JCombatInterface.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JPlayerAnimInstance.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimTypes.h"
#include "Animation/Project_JAnimNotifyState_MeleeHit.h"
#include "Combat/Project_JGameplayAbility_NPCAttack.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Components/Project_JNPCActivationComponent.h"
#include "Components/Project_JNPCActionComponent.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Components/Project_JCombatPresentationComponent.h"
#include "Combat/Project_JCombatPresentationSet.h"
#include "Components/CapsuleComponent.h"
#include "System/Project_JNPCDecisionSubsystem.h"
#include "System/Project_JCharacterAnimationBudgetSubsystem.h"
#include "Movement/Project_JMovementQueries.h"
#include "AIController.h"
#include "GameplayEffect.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/SkeletalMesh.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

// All methods are defined in every target. Only the existing non-shipping
// fixture admission can set NPCCombatCount; zero is a no-op, including Shipping.
bool UProject_JRenderedCrowdFixture::IsNPCCombatRowCurrent(int32 Index) const
{
    if (!bNPCCombatSetup || bNPCCombatCleaning || bNPCCombatCleaned || !NPCCombatRows.IsValidIndex(Index) ||
        !GetWorld() || GetWorld()->bIsTearingDown) return false;
    const auto& R=NPCCombatRows[Index];
    auto* NPC=R.NPC.Get(); auto* Target=R.Recipient.Get(); auto* ASC=R.ASC.Get(); auto* TargetASC=R.RecipientASC.Get();
    const bool bPostCurrent=R.Post.IsExplicitlyNull() ? R.Mesh.IsValid() && !R.Mesh->GetPostProcessInstance() :
        R.Post.IsValid() && R.Mesh.IsValid() && R.Mesh->GetPostProcessInstance()==R.Post.Get();
    return IsValid(NPC) && !NPC->IsActorBeingDestroyed() && NPC->HasActorBegunPlay() && NPC->HasAuthority() && NPC->GetWorld()==GetWorld() &&
        R.Mesh.IsValid() && R.Mesh->IsRegistered() && !R.Mesh->IsBeingDestroyed() && NPC->GetMesh()==R.Mesh.Get() &&
        R.Mesh->GetSkeletalMeshAsset()==R.MeshAsset.Get() && R.Anim.IsValid() && R.Mesh->GetAnimInstance()==R.Anim.Get() &&
        !R.Anim->HasAnyFlags(RF_BeginDestroyed|RF_FinishDestroyed) && R.Anim->GetClass()==R.AnimClass.Get() && bPostCurrent &&
        IsValid(ASC) && !ASC->IsBeingDestroyed() && ASC->IsRegistered() && ASC->GetOwner()==NPC && ASC->GetWorld()==GetWorld() &&
        NPC->GetAbilitySystemComponent()==ASC && ASC->GetOwnerActor()==NPC && ASC->GetAvatarActor()==NPC && ASC->GetActorInfoRevision()==R.SourceEpoch &&
        IsValid(Target) && !Target->IsActorBeingDestroyed() && Target->HasActorBegunPlay() && Target->GetWorld()==GetWorld() &&
        IsValid(TargetASC) && !TargetASC->IsBeingDestroyed() && TargetASC->IsRegistered() && TargetASC->GetOwner()==Target &&
        Target->GetAbilitySystemComponent()==TargetASC && TargetASC->GetOwnerActor()==Target && TargetASC->GetAvatarActor()==Target &&
        TargetASC->GetActorInfoRevision()==R.RecipientEpoch &&
        (R.Controller.IsExplicitlyNull() ? !R.bAdmitted && !NPC->GetController() : R.Controller.IsValid() && NPC->GetController()==R.Controller.Get() && R.Controller->GetPawn()==NPC &&
            !R.Controller->IsActorBeingDestroyed() && R.Controller->GetWorld()==GetWorld()) &&
        (!R.bAdmitted || (R.Action.IsValid() && R.Action->IsRegistered() && !R.Action->IsBeingDestroyed() && R.Action->GetOwner()==NPC &&
            R.Scoring.IsValid() && R.Scoring->IsRegistered() && !R.Scoring->IsBeingDestroyed() && R.Scoring->GetOwner()==NPC &&
            R.Hit.IsValid() && R.Hit->IsRegistered() && !R.Hit->IsBeingDestroyed() && R.Hit->GetOwner()==NPC));
}

bool UProject_JRenderedCrowdFixture::SetupNPCCombatCohort(FString& Failure)
{
    check(IsInGameThread());
    if (!NPCCombatCount) return true;
    if (NPCCombatCount<1 || NPCCombatCount>32 || bNPCCombatSetup || !NPCCombatRows.IsEmpty() ||
        !GetWorld() || GetWorld()->GetNetMode()!=NM_Standalone || GetWorld()->bIsTearingDown)
    { Failure=TEXT("Actual NPC cohort requires fresh standalone Count1..32 state"); return false; }
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    auto* Registry=GetWorld()->GetSubsystem<UProject_JNPCDecisionSubsystem>();
    auto* Budget=GetWorld()->GetSubsystem<UProject_JCharacterAnimationBudgetSubsystem>();
    if (!Registry || !Budget) { Failure=TEXT("Actual NPC registry/budget subsystem unavailable"); return false; }
    NPCCombatBaseAgents=Registry->GetAgentCount(); NPCCombatBaseActions=Registry->GetActionCount();
    NPCCombatBaseTargets=Registry->GetTargetCount(); NPCCombatBaseTracked=Budget->GetTrackedCount();
    if (NPCCombatBaseAgents || NPCCombatBaseActions)
    { Failure=TEXT("Actual NPC opt-in rejects pre-existing NPC agents/actions; target baseline is preserved"); return false; }
    const TCHAR* DefinitionPath=bNPCAuthoredPath?TEXT("/Game/DataAssetSets/NPC/DA_NPC_PrototypeCleaveCanonicalActivation.DA_NPC_PrototypeCleaveCanonicalActivation"):TEXT("/Game/DataAssetSets/NPC/DA_NPC_PrototypeCleaveActivation.DA_NPC_PrototypeCleaveActivation");
    NPCCombatDefinition=LoadObject<UProject_JNPCActivationDefinition>(nullptr,DefinitionPath);
    NPCCombatAttack=NPCCombatDefinition && NPCCombatDefinition->AttackDefinitionOverride?NPCCombatDefinition->AttackDefinitionOverride.Get():
        LoadObject<UProject_JAttackDefinition>(nullptr,TEXT("/Game/DataAssetSets/AttackDefinition/NPC/DA_Attack_NPC_PrototypeCleave.DA_Attack_NPC_PrototypeCleave"));
    if (bNPCAuthoredPath && (!NPCCombatDefinition || !NPCCombatDefinition->AttackDefinitionOverride || !NPCCombatAttack || !NPCCombatAttack->MeleeTrajectory.bEnabled))
    { Failure=TEXT("Saved selected canonical NPC definition/path is unavailable"); return false; }
    UClass* NPCClass=LoadClass<AProject_JNPCCharacter>(nullptr,TEXT("/Game/NPCs/BP_NPC_Test.BP_NPC_Test_C"));
    UClass* AnimClass=LoadClass<UProject_JCharacterAnimInstance>(nullptr,TEXT("/Game/NPCs/Animation/ABP_NPC_Mover_Unarmed.ABP_NPC_Mover_Unarmed_C"));
    const auto* Ability=NPCCombatDefinition && NPCCombatDefinition->AttackAbility && NPCCombatDefinition->AttackAbility->IsChildOf(UProject_JGameplayAbility_NPCAttack::StaticClass()) ?
        NPCCombatDefinition->AttackAbility->GetDefaultObject<UProject_JGameplayAbility_NPCAttack>() : nullptr;
    const auto* AttackField=Ability ? FindFProperty<FObjectPropertyBase>(Ability->GetClass(),TEXT("AttackDefinition")) : nullptr;
    const auto* TagField=Ability ? FindFProperty<FStructProperty>(Ability->GetClass(),TEXT("HitEventTag")) : nullptr;
    if (!NPCClass || !AnimClass || AnimClass->IsChildOf(UProject_JPlayerAnimInstance::StaticClass()) || !NPCCombatDefinition ||
        NPCCombatDefinition->bUseDamageThreat || !NPCCombatAttack || !NPCCombatAttack->Montage || !NPCCombatAttack->DamageEffect ||
        !Ability || !UProject_JNPCActionComponent::SupportsAttackAbility(Ability) || !AttackField || !TagField ||
        (!NPCCombatDefinition->AttackDefinitionOverride && AttackField->GetObjectPropertyValue_InContainer(Ability)!=NPCCombatAttack) || NPCCombatAttack->NPCMaxMeleeTargets!=2)
    { Failure=TEXT("Authored non-MM NPC/cleave contracts unavailable"); return false; }
    NPCCombatHitTag=*TagField->ContainerPtrToValuePtr<FGameplayTag>(Ability);
    if (!NPCCombatHitTag.IsValid()) { Failure=TEXT("Authored NPC hit event tag missing"); return false; }
    // Observe authored data without changing notify/asset/eligibility semantics.
    NPCCombatObservedTraceRadius=NPCCombatAttack->HitSpec.TraceRadius;
    NPCCombatObservedTraceDistance=NPCCombatAttack->HitSpec.TraceDistance;
    bNPCCombatObservedUseTipOffset=NPCCombatAttack->HitSpec.bUseCanonicalBladeTipOffset;
    NPCCombatObservedTipOffset=NPCCombatAttack->HitSpec.CanonicalBladeTipOffset;
    NPCCombatObservedPlayRate=NPCCombatAttack->PlayRate;
    const int32 NotifyLimit=FMath::Min(NPCCombatAttack->Montage->Notifies.Num(),64);
    bNPCCombatNotifyMetadataTruncated=NPCCombatAttack->Montage->Notifies.Num()>NotifyLimit;
    for (int32 N=0;N<NotifyLimit;++N)
    {
        const auto& Notify=NPCCombatAttack->Montage->Notifies[N];
        const auto* Melee=Cast<UProject_JAnimNotifyState_MeleeHit>(Notify.NotifyStateClass.Get());
        if (!Melee) continue;
        FNPCCombatNotifyObservation Observation;
        Observation.ClassPath=Melee->GetClass()->GetPathName(); Observation.Start=Notify.GetTriggerTime(); Observation.Duration=Notify.GetDuration();
        Observation.MontageTickType=int32(Notify.MontageTickType);
        Observation.TriggerWeightThreshold=Notify.TriggerWeightThreshold; Observation.NotifyTriggerChance=Notify.NotifyTriggerChance;
        Observation.NotifyFilterType=int32(Notify.NotifyFilterType); Observation.NotifyFilterLOD=Notify.NotifyFilterLOD;
        Observation.bCanBeFilteredViaRequest=Notify.bCanBeFilteredViaRequest; Observation.bTriggerOnDedicatedServer=Notify.bTriggerOnDedicatedServer;
        Observation.bTriggerOnFollower=Notify.bTriggerOnFollower;
        Observation.bNativeBranchingPoint=Melee->bIsNativeBranchingPoint; Observation.bEffectiveBranchingPoint=Notify.IsBranchingPoint();
        if (const auto* Socket=FindFProperty<FNameProperty>(Melee->GetClass(),TEXT("SocketName")))
        { Observation.Socket=Socket->GetPropertyValue_InContainer(Melee).ToString(); Observation.bSocketResolved=true; }
        NPCCombatNotifyObservations.Add(MoveTemp(Observation));
    }
    const auto Pin=[this](UObject* Object)
    {
        if (Object && !NPCCombatPins.Contains(Object))
        { NPCCombatPins.Add(Object); NPCCombatOriginalDirty.Add(Object->GetPackage()->IsDirty()); }
    };
    for (UObject* Object : TArray<UObject*>{NPCClass,AnimClass,NPCCombatDefinition.Get(),NPCCombatAttack.Get(),
        Ability->GetClass(),NPCCombatAttack->Montage.Get(),NPCCombatAttack->DamageEffect.Get()}) Pin(Object);
    ++NPCCombatEpoch; bNPCCombatSetup=true;
    const uint64 Epoch=NPCCombatEpoch;
    NPCCombatRows.SetNum(NPCCombatCount);
    NPCCombatCSV=TEXT("phase,frame_id,phase_game_s,stage,authored_npcs,passive_recipients,current_identities,managed,eligible,combat_critical,update_required,montages,hit_active,grants,quiet_seen,protected_seen,returned_seen,natural_ends,raw_contacts,effects,health_loss,bone_finalizations,attack_finalizations,registry_agents,registry_actions,registry_targets,budget_tracked,budget_managed\n");
    FActorSpawnParameters Params; Params.ObjectFlags|=RF_Transient;
    Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const TWeakObjectPtr<ThisClass> WeakSelf(this);
    for (int32 I=0;I<NPCCombatCount;++I)
    {
        const FVector Location(-390+(I%8)*600,2300+(I/8)*450,100);
        const TStrongObjectPtr<AProject_JNPCCharacter> NPC(GetWorld()->SpawnActor<AProject_JNPCCharacter>(NPCClass,Location,FRotator::ZeroRotator,Params));
        NPCCombatRows[I].NPC=NPC.Get(); // Publish partial ownership before any following setup can fail.
        if (!NPC || NPC->IsActorBeingDestroyed()) { Failure=TEXT("Actual NPC spawn failed"); return false; }
        const TStrongObjectPtr<AProject_JNPCCharacter> Recipient(GetWorld()->SpawnActor<AProject_JNPCCharacter>(AProject_JNPCCharacter::StaticClass(),Location+FVector(120,0,0),FRotator::ZeroRotator,Params));
        NPCCombatRows[I].Recipient=Recipient.Get();
        if (!Recipient || Recipient->IsActorBeingDestroyed()) { Failure=TEXT("Passive recipient spawn failed"); return false; }
        auto* Mesh=Cast<UProject_JBudgetedSkeletalMeshComponent>(NPC->GetMesh());
        auto* ASC=Cast<UProject_JAbilitySystemComponent>(NPC->GetAbilitySystemComponent());
        auto* TargetASC=Cast<UProject_JAbilitySystemComponent>(Recipient->GetAbilitySystemComponent());
        if (!Mesh || !Mesh->GetSkeletalMeshAsset() || !Mesh->GetAnimInstance() || Mesh->GetAnimInstance()->GetClass()!=AnimClass ||
            !ASC || !TargetASC || ASC->GetAvatarActor()!=NPC.Get() || TargetASC->GetAvatarActor()!=Recipient.Get() ||
            !ASC->GetActorInfoRevision() || !TargetASC->GetActorInfoRevision())
        { Failure=TEXT("Actual non-MM NPC mesh/animation/ASC setup invalid"); return false; }
        const TStrongObjectPtr<UProject_JAbilitySystemComponent> SourcePin(ASC), TargetPin(TargetASC);
        {
            auto& R=NPCCombatRows[I];
            R.Name=NPC->GetName(); R.RecipientName=Recipient->GetName(); R.Mesh=Mesh; R.RecipientMesh=Recipient->GetMesh();
            R.MeshAsset=Mesh->GetSkeletalMeshAsset(); R.Anim=Mesh->GetAnimInstance(); R.AnimClass=AnimClass; R.Post=Mesh->GetPostProcessInstance();
            R.AnimationPath=AnimClass->GetPathName(); R.ASC=ASC; R.RecipientASC=TargetASC;
            R.Controller=Cast<AAIController>(NPC->GetController());
            R.bPresentationComponent=NPC->FindComponentByClass<UProject_JCombatPresentationComponent>()!=nullptr;
        }
        Pin(Mesh->GetSkeletalMeshAsset());
        const uint64 SourceEpoch=ASC->GetActorInfoRevision(), TargetEpoch=TargetASC->GetActorInfoRevision();
        const auto SetupCurrent=[&]
        {
            return bNPCCombatSetup && !bNPCCombatCleaning && !bNPCCombatCleaned && NPCCombatEpoch==Epoch && GetWorld() && !GetWorld()->bIsTearingDown &&
                IsValid(NPC.Get()) && !NPC->IsActorBeingDestroyed() && NPC->GetWorld()==GetWorld() && NPC->GetAbilitySystemComponent()==ASC &&
                IsValid(ASC) && !ASC->IsBeingDestroyed() && ASC->IsRegistered() && ASC->GetOwnerActor()==NPC.Get() && ASC->GetAvatarActor()==NPC.Get() && ASC->GetActorInfoRevision()==SourceEpoch &&
                IsValid(Recipient.Get()) && !Recipient->IsActorBeingDestroyed() && Recipient->GetWorld()==GetWorld() && Recipient->GetAbilitySystemComponent()==TargetASC &&
                IsValid(TargetASC) && !TargetASC->IsBeingDestroyed() && TargetASC->IsRegistered() && TargetASC->GetOwnerActor()==Recipient.Get() && TargetASC->GetAvatarActor()==Recipient.Get() && TargetASC->GetActorInfoRevision()==TargetEpoch;
        };
        // Begun BaseCharacter already initializes authored defaults. Never revive
        // or replace source gameplay values to make this fixture pass.
        const float SourceMax=ASC->GetNumericAttribute(UProject_JAttributeSet::GetMaxHealthAttribute());
        const float SourceHealth=ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute());
        if (!FMath::IsFinite(SourceMax) || !FMath::IsFinite(SourceHealth) || SourceMax<=0 || SourceHealth<=0)
        { Failure=TEXT("NPC authored native Health/MaxHealth must already be positive finite"); return false; }
        const bool bDead=IProject_JCombatInterface::Execute_IsDead(NPC.Get());
        if (!SetupCurrent() || bDead) { Failure=TEXT("NPC source is not an unchanged live combat recipient"); return false; }
        // Existing native fanout/138 recipient setup; no profile/asset mutation.
        TargetASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(),1000.f);
        if (!SetupCurrent()) { Failure=TEXT("Recipient MaxHealth initialization changed identity"); return false; }
        TargetASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(),1000.f);
        if (!SetupCurrent()) { Failure=TEXT("Health setup callbacks changed NPC/recipient identity"); return false; }
        NPCCombatRows[I].SourceEpoch=ASC->GetActorInfoRevision();
        NPCCombatRows[I].RecipientEpoch=TargetASC->GetActorInfoRevision();
        if (!IsNPCCombatRowCurrent(I)) { Failure=TEXT("NPC setup callbacks changed the recipient tuple"); return false; }
        const bool Registered=Registry->RegisterTarget(Recipient.Get(),2);
        NPCCombatRows[I].bTargetRegistered=Registered;
        if (!Registered || !IsNPCCombatRowCurrent(I)) { Failure=TEXT("Passive recipient registration failed or changed identity"); return false; }
        NPCCombatRows[I].BoneHandle=Mesh->RegisterOnBoneTransformsFinalizedDelegate(FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateLambda([WeakSelf,Epoch,I]
        {
            auto* Self=WeakSelf.Get();
            if (!Self || Self->NPCCombatEpoch!=Epoch || !Self->IsNPCCombatRowCurrent(I)) return;
            auto& R=Self->NPCCombatRows[I]; ++R.Finalizations;
            if (R.bAdmitted && !R.bEnded && R.Hit.IsValid() && R.Hit->GetActiveAttackDefinition()==Self->NPCCombatAttack) ++R.AttackFinalizations;
        }));
        NPCCombatRows[I].ContactHandle=ASC->AddGameplayEventTagContainerDelegate(FGameplayTagContainer(NPCCombatHitTag),
            FGameplayEventTagMulticastDelegate::FDelegate::CreateLambda([WeakSelf,Epoch,I](FGameplayTag Tag,const FGameplayEventData* Event)
        {
            auto* Self=WeakSelf.Get();
            if (!Self || Self->NPCCombatEpoch!=Epoch || Self->bNPCCombatCleaning || Self->bNPCCombatCleaned) return;
            if (!Self->IsNPCCombatRowCurrent(I)) { if (!Self->bNPCCombatViolation) Self->NPCCombatFailure=TEXT("Contact recipient identity changed"); Self->bNPCCombatViolation=true; return; }
            const auto& Source=Self->NPCCombatRows[I];
            if (!Event || Tag!=Self->NPCCombatHitTag || Event->Instigator.Get()!=Source.NPC.Get()) return;
            bool OwnedTarget=false;
            for (const auto& Row:Self->NPCCombatRows) OwnedTarget|=Event->Target.Get()==Row.Recipient.Get();
            if (!Self->bNPCCombatStarted || Self->Phase!=5 || Source.bEnded || !OwnedTarget)
            { if (!Self->bNPCCombatViolation) Self->NPCCombatFailure=TEXT("Raw contact outside owned finite attack/recipient scope"); Self->bNPCCombatViolation=true; return; }
            ++Self->NPCCombatRows[I].Contacts; // Repeated physical contacts are intentionally valid.
        }));
        // The bound ASC is the recipient; UE supplies the applying SOURCE ASC.
        NPCCombatRows[I].EffectHandle=TargetASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda([WeakSelf,Epoch,I](UAbilitySystemComponent* EffectSource,const FGameplayEffectSpec& Spec,FActiveGameplayEffectHandle)
        {
            auto* Self=WeakSelf.Get();
            if (!Self || Self->NPCCombatEpoch!=Epoch || Self->bNPCCombatCleaning || Self->bNPCCombatCleaned || !Self->NPCCombatAttack ||
                Spec.Def!=Self->NPCCombatAttack->DamageEffect->GetDefaultObject<UGameplayEffect>()) return;
            int32 SourceIndex=INDEX_NONE;
            for (int32 J=0;J<Self->NPCCombatRows.Num();++J)
                if (Spec.GetContext().GetOriginalInstigator()==Self->NPCCombatRows[J].NPC.Get()) { SourceIndex=J; break; }
            if (SourceIndex==INDEX_NONE || !Self->IsNPCCombatRowCurrent(I) || !Self->IsNPCCombatRowCurrent(SourceIndex) ||
                EffectSource!=Self->NPCCombatRows[SourceIndex].ASC.Get() || !Self->bNPCCombatStarted || Self->Phase!=5 || Self->NPCCombatRows[SourceIndex].bEnded)
            {
                const auto* Instigator=Spec.GetContext().GetOriginalInstigator();
                if (!Self->bNPCCombatViolation) Self->NPCCombatFailure=FString::Printf(TEXT("Authored effect outside exact owned attack/ASC epoch: recipient=%d source=%d instigator=%s class=%s phase=%d recipient_current=%d source_current=%d source_ended=%d exact_asc=%d"),
                    I,SourceIndex,*GetNameSafe(Instigator),*GetPathNameSafe(Instigator?Instigator->GetClass():nullptr),Self->Phase,Self->IsNPCCombatRowCurrent(I),
                    SourceIndex!=INDEX_NONE && Self->IsNPCCombatRowCurrent(SourceIndex),SourceIndex!=INDEX_NONE && Self->NPCCombatRows[SourceIndex].bEnded,SourceIndex!=INDEX_NONE && EffectSource==Self->NPCCombatRows[SourceIndex].ASC.Get());
                Self->bNPCCombatViolation=true; return;
            }
            const FObjectKey TargetKey(Self->NPCCombatRows[I].Recipient.Get());
            auto& Source=Self->NPCCombatRows[SourceIndex];
            if (Source.EffectTargets.Contains(TargetKey) || Source.EffectTargets.Num()>=Self->NPCCombatAttack->NPCMaxMeleeTargets)
            { if (!Self->bNPCCombatViolation) Self->NPCCombatFailure=TEXT("Duplicate effect or authored target-limit violation"); Self->bNPCCombatViolation=true; return; }
            const float Health=Self->NPCCombatRows[I].RecipientASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute());
            auto& Target=Self->NPCCombatRows[I];
            if (!FMath::IsFinite(Health) || Health<0 || Health>=Target.LastHealth)
            { if (!Self->bNPCCombatViolation) Self->NPCCombatFailure=TEXT("Authored effect did not reduce real recipient Health"); Self->bNPCCombatViolation=true; return; }
            Source.EffectTargets.Add(TargetKey); ++Source.Effects; ++Target.RecipientEffects;
            Target.HealthLoss+=Target.LastHealth-Health; Target.LastHealth=Health;
        });
        NPCCombatRows[I].EndHandle=ASC->OnAbilityEnded.AddLambda([WeakSelf,Epoch,I](const FAbilityEndedData& End)
        {
            const TStrongObjectPtr<ThisClass> Self(WeakSelf.Get());
            if (!Self || Self->NPCCombatEpoch!=Epoch || Self->bNPCCombatCleaning || Self->bNPCCombatCleaned || !Self->NPCCombatRows.IsValidIndex(I)) return;
            const auto Grant=Self->NPCCombatRows[I].Grant;
            if (!Grant.IsValid() || End.AbilitySpecHandle!=Grant) return;
            if (!Self->IsNPCCombatRowCurrent(I) || !End.AbilityThatEnded || End.AbilityThatEnded->GetClass()!=Self->NPCCombatDefinition->AttackAbility ||
                Self->Phase!=5 || End.bWasCancelled || Self->NPCCombatRows[I].bEnded)
            {
                const auto* Caster=Self->NPCCombatRows[I].NPC.Get();
                const auto Physical=Project_J::Movement::ReadPhysicalFrame(Caster);
                if (!Self->bNPCCombatViolation) Self->NPCCombatFailure=FString::Printf(TEXT("Owned NPC attack ended unexpectedly/cancelled: index=%d cancelled=%d phase=%d current=%d ended=%d physical_valid=%d grounded=%d source_health=%.9g"),
                    I,End.bWasCancelled,Self->Phase,Self->IsNPCCombatRowCurrent(I),Self->NPCCombatRows[I].bEnded,Physical.bValid,Physical.bGrounded,Caster && Caster->GetAttributeSet()?Caster->GetAttributeSet()->GetHealth():-1.f);
                Self->bNPCCombatViolation=true; return;
            }
            const TStrongObjectPtr<AProject_JNPCCharacter> Caster(Self->NPCCombatRows[I].NPC.Get());
            {
                auto& R=Self->NPCCombatRows[I]; R.bEnded=true; ++R.NaturalEnds;
                R.ContactsAtEnd=R.Contacts; R.EffectsAtEnd=R.Effects;
            }
            // No row reference crosses Stop/Cancel/Clear callbacks. ASC's supported
            // NotifyAbilityEnded list lock defers grant removal through engine completion.
            Caster->StopCombatActions();
        });
    }
    return true;
}

bool UProject_JRenderedCrowdFixture::BeginNPCCombatCohort(FString& Failure)
{
    check(IsInGameThread());
    if (!NPCCombatCount) return true;
    if (!bNPCCombatSetup || bNPCCombatStarted || bNPCCombatCleaned || Phase!=5)
    { Failure=TEXT("NPC combat phase may begin exactly once at phase5"); return false; }
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    bNPCCombatStarted=true;
    const uint64 AdmissionEpoch=NPCCombatEpoch;
    for (int32 I=0;I<NPCCombatRows.Num();++I)
    {
        if (!IsNPCCombatRowCurrent(I) || !NPCCombatRows[I].bQuietManaged)
        { Failure=TEXT("NPC has not naturally entered animation budget before attack"); return false; }
        const TStrongObjectPtr<AProject_JNPCCharacter> NPC(NPCCombatRows[I].NPC.Get());
        const TStrongObjectPtr<UProject_JBudgetedSkeletalMeshComponent> Mesh(NPCCombatRows[I].Mesh.Get());
        const auto Physical=Project_J::Movement::ReadPhysicalFrame(NPC.Get());
        if (!Physical.bValid || !Physical.bGrounded || Mesh->IsCombatCritical() || Mesh->HasAnimationUpdateRequirement())
        { Failure=TEXT("NPC not naturally settled/quiet before actual attack admission"); return false; }
        {
            auto& R=NPCCombatRows[I]; R.QuietVisibility=uint8(Mesh->VisibilityBasedAnimTickOption);
            R.bQuietURO=Mesh->bEnableUpdateRateOptimizations; R.bQuietSuppress=Mesh->bSuppressNotifyEventDispatch;
            R.bQuietDemand=Mesh->HasAnimationUpdateRequirement(); R.bHelperController=NPC->GetController()==nullptr;
        }
        // Capture any authored components; only the production helper may assemble missing ones.
        const TStrongObjectPtr<UProject_JCombatHitValidationComponent> PreviousHit(NPC->FindComponentByClass<UProject_JCombatHitValidationComponent>());
        const TStrongObjectPtr<UProject_JCombatPresentationComponent> PreviousPresentation(NPC->FindComponentByClass<UProject_JCombatPresentationComponent>());
        const TArray<FName> HitTags=PreviousHit.IsValid()?PreviousHit->ComponentTags:TArray<FName>();
        const TArray<FName> PresentationTags=PreviousPresentation.IsValid()?PreviousPresentation->ComponentTags:TArray<FName>();
        const bool HitCanTick=PreviousHit.IsValid() && PreviousHit->PrimaryComponentTick.bCanEverTick;
        const bool PresentationCanTick=PreviousPresentation.IsValid() && PreviousPresentation->PrimaryComponentTick.bCanEverTick;
        const TStrongObjectPtr<UProject_JCombatPresentationSet> BaseSet(PreviousPresentation.IsValid()?PreviousPresentation->BasePresentationSet.Get():nullptr);
        if (!NPC->StartCombatActions(NPCCombatDefinition.Get(),1))
        { Failure=TEXT("Actual NPC StartCombatActions rejected"); return false; }
        if (!bNPCCombatSetup || bNPCCombatCleaning || bNPCCombatCleaned || NPCCombatEpoch!=AdmissionEpoch ||
            !NPCCombatRows.IsValidIndex(I) || NPCCombatRows[I].NPC.Get()!=NPC.Get())
        { Failure=TEXT("NPC helper callbacks changed the cohort before assembly observation"); return false; }
        auto* AssembledHit=NPC->FindComponentByClass<UProject_JCombatHitValidationComponent>();
        auto* AssembledPresentation=NPC->FindComponentByClass<UProject_JCombatPresentationComponent>();
        TInlineComponentArray<UProject_JCombatHitValidationComponent*> Hits(NPC.Get());
        TInlineComponentArray<UProject_JCombatPresentationComponent*> Presentations(NPC.Get());
        int32 HitCount=0,PresentationCount=0;
        for (auto* Entry:Hits) if (IsValid(Entry) && !Entry->IsBeingDestroyed()) ++HitCount;
        for (auto* Entry:Presentations) if (IsValid(Entry) && !Entry->IsBeingDestroyed()) ++PresentationCount;
        if (HitCount!=1 || PresentationCount!=1 || !IsValid(AssembledHit) || AssembledHit->IsBeingDestroyed() || !AssembledHit->IsRegistered() ||
            !AssembledHit->HasBegunPlay() || AssembledHit->GetOwner()!=NPC.Get() || AssembledHit->GetWorld()!=GetWorld() || !AssembledHit->GetIsReplicated() ||
            !IsValid(AssembledPresentation) || AssembledPresentation->IsBeingDestroyed() || !AssembledPresentation->IsRegistered() ||
            !AssembledPresentation->HasBegunPlay() || AssembledPresentation->GetOwner()!=NPC.Get() || AssembledPresentation->GetWorld()!=GetWorld() || !AssembledPresentation->GetIsReplicated())
        { Failure=TEXT("Production helper did not assemble one actual live begun replicated HitValidation/Presentation pair"); return false; }
        if ((PreviousHit.IsValid() && AssembledHit!=PreviousHit.Get()) || (PreviousPresentation.IsValid() && AssembledPresentation!=PreviousPresentation.Get()) ||
            AssembledHit->ComponentTags!=HitTags || AssembledPresentation->ComponentTags!=PresentationTags ||
            AssembledHit->PrimaryComponentTick.bCanEverTick!=HitCanTick || AssembledPresentation->PrimaryComponentTick.bCanEverTick!=PresentationCanTick ||
            AssembledPresentation->BasePresentationSet.Get()!=BaseSet.Get())
        { Failure=TEXT("Production assembly replaced an authored component or changed its tags/tick/profile settings"); return false; }
        {
            auto& R=NPCCombatRows[I]; R.bAdmitted=true; R.Controller=Cast<AAIController>(NPC->GetController());
            R.Action=NPC->FindComponentByClass<UProject_JNPCActionComponent>();
            R.Scoring=NPC->FindComponentByClass<UProject_JTargetScoringComponent>();
            R.Hit=AssembledHit; R.bPresentationComponent=true;
            if (R.Action.IsValid()) { R.Grant=R.Action->GetAttackAbilityHandle(); R.ActionToken=R.Action->GetOwnershipToken(); }
        }
        const auto* Granted=NPCCombatRows[I].ASC.IsValid()?NPCCombatRows[I].ASC->FindAbilitySpecFromHandle(NPCCombatRows[I].Grant):nullptr;
        if (bNPCAuthoredPath && (!Granted || Granted->SourceObject.Get()!=NPCCombatAttack))
        { Failure=TEXT("NPC helper did not grant its selected authored attack source"); return false; }
        if (!IsNPCCombatRowCurrent(I) || !NPCCombatRows[I].Grant.IsValid() || !NPCCombatRows[I].ActionToken)
        { Failure=TEXT("NPC admission did not publish exact action/grant/controller tuple"); return false; }
    }
    return true;
}

bool UProject_JRenderedCrowdFixture::ObserveNPCCombatCohort(FString& Failure)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_RenderedCrowd_NPCObserver);
    check(IsInGameThread());
    if (!NPCCombatCount || bNPCCombatCleaned) return true;
    if (bNPCCombatViolation) { Failure=NPCCombatFailure; return false; }
    if (++NPCCombatSamples>65536) { Failure=TEXT("Bounded NPC observation capacity exceeded"); return false; }
    int32 Current=0,Managed=0,Eligible=0,Critical=0,Demand=0,Montages=0,Hits=0,Grants=0,Quiet=0,Protected=0,Returned=0,Ends=0,Contacts=0,Effects=0,Bones=0,AttackBones=0;
    double Loss=0;
    for (int32 I=0;I<NPCCombatRows.Num();++I)
    {
        if (!IsNPCCombatRowCurrent(I)) { Failure=FString::Printf(TEXT("Actual NPC tuple changed at index%d"),I); return false; }
        auto& R=NPCCombatRows[I]; ++Current;
        auto* Mesh=R.Mesh.Get(); auto* Anim=R.Anim.Get();
        const bool bPlaying=Anim->Montage_IsPlaying(NPCCombatAttack->Montage);
        Managed+=Mesh->IsManagedByBudget(); Eligible+=Mesh->CanUseBudget(); Critical+=Mesh->IsCombatCritical(); Demand+=Mesh->HasAnimationUpdateRequirement();
        Montages+=bPlaying; Hits+=R.Hit.IsValid() && R.Hit->GetActiveAttackDefinition()==NPCCombatAttack;
        Grants+=R.Grant.IsValid() && R.ASC->FindAbilitySpecFromHandle(R.Grant)!=nullptr;
        if (!bNPCCombatStarted && Mesh->IsManagedByBudget() && Mesh->CanUseBudget() && !Mesh->IsCombatCritical() && !Mesh->HasAnimationUpdateRequirement()) R.bQuietManaged=true;
        if (R.bAdmitted && !R.bEnded)
        {
            auto* Target=R.Action->GetIntentTarget();
            if (R.TraceObservedFrame!=GFrameCounter)
            {
                R.TraceObservedFrame=GFrameCounter;
                if (Mesh->bSuppressNotifyEventDispatch) ++R.NotifySuppressedObservations; else ++R.NotifyUnsuppressedObservations;
                if (const auto* ActiveMontage=Anim->GetActiveInstanceForMontage(NPCCombatAttack->Montage))
                {
                    const double Weight=ActiveMontage->GetWeight();
                    if (FMath::IsFinite(Weight))
                    { ++R.MontageWeightObservations; R.MontageWeightMin=FMath::Min(R.MontageWeightMin,Weight); R.MontageWeightMax=FMath::Max(R.MontageWeightMax,Weight); }
                }
                const int32 LOD=Mesh->GetPredictedLODLevel();
                R.PredictedLODMin=FMath::Min(R.PredictedLODMin,LOD); R.PredictedLODMax=FMath::Max(R.PredictedLODMax,LOD);
                const double Position=Anim->Montage_GetPosition(NPCCombatAttack->Montage);
                if (bPlaying && FMath::IsFinite(Position))
                {
                    if (R.MontagePositionObservations)
                    {
                        R.MontagePreviousPosition=R.MontageLastPosition;
                        R.MontageMaxForwardAdvance=FMath::Max(R.MontageMaxForwardAdvance,Position-R.MontageLastPosition);
                    }
                    else R.MontageFirstPosition=Position;
                    R.MontageLastPosition=Position; ++R.MontagePositionObservations;
                }
                const auto* GameState=GetWorld()->GetGameState();
                const double QueryTime=GameState?GameState->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
                FProject_JAuthoritativeSweepRecord Packet;
                ++R.HistoryAttempts; R.LastHistoryQueryTime=QueryTime;
                if (R.Hit->FindAuthoritativeTracePacketAtTime(QueryTime,0,NPCCombatAttack->AttackTag,Packet))
                {
                    ++R.HistoryFound;
                    R.LastMatchedQueryTime=QueryTime; R.LastMatchedMontagePosition=Position; R.bLastMatchedWindowOpen=Packet.bHitWindowOpen;
                    R.LastMatchedTraceSegments=Packet.Segments.Num();
                    if (Packet.bHitWindowOpen)
                    { ++R.OpenHistoryObservations; R.LastOpenQueryTime=QueryTime; R.LastOpenMontagePosition=Position; }
                    R.LastMatchedTraceStart=R.LastMatchedTraceEnd=FVector::ZeroVector;
                    R.LastMatchedActorPosition=R.NPC->GetActorLocation();
                    if (!Packet.Segments.IsEmpty())
                    {
                        R.LastMatchedTraceStart=Packet.Segments[0].TraceStart; R.LastMatchedTraceEnd=Packet.Segments[0].TraceEnd;
                        const auto* GeometryTarget=Target?Cast<APawn>(Target):R.Recipient.Get();
                        const auto* Capsule=GeometryTarget?GeometryTarget->FindComponentByClass<UCapsuleComponent>():nullptr;
                        if (Capsule && IsValid(GeometryTarget))
                        {
                            const FVector Center=Capsule->GetComponentLocation();
                            const double Radius=Capsule->GetScaledCapsuleRadius(), HalfHeight=Capsule->GetScaledCapsuleHalfHeight();
                            const FVector Axis=Capsule->GetUpVector()*FMath::Max(0.0,HalfHeight-Radius);
                            double Closest=TNumericLimits<double>::Max();
                            for (const auto& Segment:Packet.Segments)
                            {
                                FVector SweepPoint,CapsulePoint;
                                FMath::SegmentDistToSegmentSafe(Segment.TraceStart,Segment.TraceEnd,Center-Axis,Center+Axis,SweepPoint,CapsulePoint);
                                const double Distance=FVector::Distance(SweepPoint,CapsulePoint);
                                R.MinSweepCapsuleAxisDistance=FMath::Min(R.MinSweepCapsuleAxisDistance,Distance);
                                R.MinSweepCapsuleClearance=FMath::Min(R.MinSweepCapsuleClearance,Distance-Radius-double(NPCCombatObservedTraceRadius));
                                if (Distance<Closest)
                                { Closest=Distance; R.LastMatchedTraceStart=Segment.TraceStart; R.LastMatchedTraceEnd=Segment.TraceEnd; }
                            }
                            R.ObservedCapsuleRadius=Radius; R.ObservedCapsuleHalfHeight=HalfHeight;
                            R.LastMatchedRecipientPosition=Center; R.LastGeometryTarget=GeometryTarget->GetName();
                        }
                    }
                }
            }
            if (Target)
            {
                bool bOwned=false; for (const auto& T:NPCCombatRows) bOwned|=T.Recipient.Get()==Target;
                const double Distance=FVector::DistSquared(R.NPC->GetActorLocation(),Target->GetActorLocation());
                if (!bOwned || !FMath::IsFinite(Distance) || Distance>FMath::Square(R.Action->AttackRange))
                { Failure=TEXT("Actual selected NPC target is not owned/in attack range"); return false; }
            }
            R.bMontage|=bPlaying && R.Hit->GetActiveAttackDefinition()==NPCCombatAttack;
            R.bProtected|=bPlaying && R.Hit->GetActiveAttackDefinition()==NPCCombatAttack && Mesh->IsCombatCritical() && Mesh->HasAnimationUpdateRequirement() && !Mesh->IsManagedByBudget();
        }
        if (R.bEnded)
        {
            if (R.Contacts!=R.ContactsAtEnd || R.Effects!=R.EffectsAtEnd)
            { Failure=TEXT("Late contact/effect after finite natural attack end"); return false; }
            const bool bPolicyRestored=uint8(Mesh->VisibilityBasedAnimTickOption)==R.QuietVisibility &&
                bool(Mesh->bEnableUpdateRateOptimizations)==R.bQuietURO && bool(Mesh->bSuppressNotifyEventDispatch)==R.bQuietSuppress;
            R.bReturned|=!Anim->IsAnyMontagePlaying() && !Mesh->IsCombatCritical() && Mesh->HasAnimationUpdateRequirement()==R.bQuietDemand &&
                Mesh->CanUseBudget() && Mesh->IsManagedByBudget() && bPolicyRestored;
        }
        Quiet+=R.bQuietManaged; Protected+=R.bProtected; Returned+=R.bReturned; Ends+=R.NaturalEnds;
        Contacts+=R.Contacts; Effects+=R.Effects; Loss+=R.HealthLoss; Bones+=R.Finalizations; AttackBones+=R.AttackFinalizations;
    }
    NPCCombatPeakMontages=FMath::Max(NPCCombatPeakMontages,Montages);
    const auto* Registry=GetWorld()->GetSubsystem<UProject_JNPCDecisionSubsystem>();
    const auto* Budget=GetWorld()->GetSubsystem<UProject_JCharacterAnimationBudgetSubsystem>();
    if (!Registry || !Budget) { Failure=TEXT("NPC registry/budget retired while cohort was live"); return false; }
    NPCCombatCSV+=FString::Printf(TEXT("%d,%llu,%.6f,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.6f,%d,%d,%d,%d,%d,%d,%d\n"),
        Phase,GFrameCounter,GetWorld()->GetTimeSeconds()-PhaseAt,bNPCCombatStarted?TEXT("finite_attack_or_drain"):TEXT("quiet"),NPCCombatCount,NPCCombatCount,
        Current,Managed,Eligible,Critical,Demand,Montages,Hits,Grants,Quiet,Protected,Returned,Ends,Contacts,Effects,Loss,Bones,AttackBones,
        Registry->GetAgentCount(),Registry->GetActionCount(),Registry->GetTargetCount(),Budget->GetTrackedCount(),Budget->GetManagedCount());
    return true;
}

bool UProject_JRenderedCrowdFixture::AdvanceNPCCombatRetirement(bool& Ready,FString& Failure)
{
    check(IsInGameThread());
    Ready=!NPCCombatCount;
    if (!NPCCombatCount) return true;
    if (bNPCCombatRetired) { Ready=true; return true; }
    if (!NPCCombatRetireTailAt) NPCCombatRetireTailAt=FPlatformTime::Seconds();
    if (bNPCCombatViolation) { Failure=NPCCombatFailure; return false; }
    if (!bNPCCombatStarted) { Failure=TEXT("Actual NPC attack phase was never admitted"); return false; }
    for (int32 I=0;I<NPCCombatRows.Num();++I)
    {
        if (!IsNPCCombatRowCurrent(I)) { Failure=TEXT("NPC retirement tuple changed"); return false; }
        const auto& R=NPCCombatRows[I];
        if (!R.bEnded) return true; // Existing whole-run wall deadline remains authoritative.
        if (R.NaturalEnds!=1 || R.Contacts<1 || R.Effects<1 || !R.bMontage || !R.bProtected || !R.Finalizations || !R.AttackFinalizations)
        { Failure=TEXT("An actual NPC lacks natural montage/contact/GE/protected-pose evidence"); return false; }
        if (R.NPC->HasCombatActions() || R.ASC->FindAbilitySpecFromHandle(R.Grant) || R.Hit->GetActiveAttackDefinition() ||
            R.Action->GetActionState()!=EProjectJNPCActionState::Disabled || R.Action->GetOwnershipToken() || R.Scoring->IsBatchedNPCDecisionRegistered())
        { Failure=TEXT("Naturally ended NPC retained grant/action/scoring ownership"); return false; }
        const auto* Presentation=R.NPC->FindComponentByClass<UProject_JCombatPresentationComponent>();
        if (!IsValid(Presentation) || Presentation->IsBeingDestroyed() || !Presentation->IsRegistered() || !Presentation->HasBegunPlay() ||
            !Presentation->GetIsReplicated() || Presentation->GetOwner()!=R.NPC.Get() || Presentation->GetWorld()!=GetWorld())
        { Failure=TEXT("Normal capability Stop did not retain the actor-lifetime baseline presentation component"); return false; }
        if (R.Anim->IsAnyMontagePlaying() || !R.bReturned || R.Mesh->IsCombatCritical() ||
            R.Mesh->HasAnimationUpdateRequirement()!=R.bQuietDemand || !R.Mesh->CanUseBudget() || !R.Mesh->IsManagedByBudget()) return true;
    }
    CleanupNPCCombatCohort();
    if (!bNPCCombatCleanupPassed) { Failure=NPCCombatFailure; return false; }
    NPCCombatRetireTailSeconds=FPlatformTime::Seconds()-NPCCombatRetireTailAt;
    bNPCCombatRetired=true; Ready=true;
    return true;
}

void UProject_JRenderedCrowdFixture::CleanupNPCCombatCohort()
{
    check(IsInGameThread());
    if (!NPCCombatCount || bNPCCombatCleaned || bNPCCombatCleaning) return;
    bNPCCombatCleaning=true; ++NPCCombatEpoch;
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    const TStrongObjectPtr<UWorld> World(GetWorld());
    const TStrongObjectPtr<UProject_JNPCDecisionSubsystem> Registry(World ? World->GetSubsystem<UProject_JNPCDecisionSubsystem>() : nullptr);
    const TStrongObjectPtr<UProject_JCharacterAnimationBudgetSubsystem> Budget(World ? World->GetSubsystem<UProject_JCharacterAnimationBudgetSubsystem>() : nullptr);
    TArray<TStrongObjectPtr<AActor>,TInlineAllocator<96>> Pins;
    for (const auto& R:NPCCombatRows)
    {
        if (R.NPC.IsValid()) Pins.Emplace(R.NPC.Get());
        if (R.Recipient.IsValid()) Pins.Emplace(R.Recipient.Get());
        if (R.Controller.IsValid()) Pins.Emplace(R.Controller.Get());
    }
    // Detach every callback before Stop/Cancel/Destroy; old sources cannot write a later cohort.
    for (auto& R:NPCCombatRows)
    {
        if (R.ASC.IsValid()) { R.ASC->RemoveGameplayEventTagContainerDelegate(FGameplayTagContainer(NPCCombatHitTag),R.ContactHandle); R.ASC->OnAbilityEnded.Remove(R.EndHandle); }
        if (R.RecipientASC.IsValid()) R.RecipientASC->OnGameplayEffectAppliedDelegateToSelf.Remove(R.EffectHandle);
        if (R.Mesh.IsValid()) R.Mesh->UnregisterOnBoneTransformsFinalizedDelegate(R.BoneHandle);
        R.ContactHandle.Reset(); R.EndHandle.Reset(); R.EffectHandle.Reset(); R.BoneHandle.Reset();
    }
    bool bReleased=true;
    for (auto& R:NPCCombatRows)
    {
        const TStrongObjectPtr<AProject_JNPCCharacter> NPC(R.NPC.Get());
        if (NPC && !NPC->IsActorBeingDestroyed()) { NPC->StopNavigation(); NPC->StopCombatActions(); }
        if (Registry && R.bTargetRegistered && R.Recipient.IsValid()) Registry->UnregisterTarget(R.Recipient.Get());
        R.bTargetRegistered=false;
    }
    for (auto& R:NPCCombatRows)
    {
        const TStrongObjectPtr<AProject_JNPCCharacter> NPC(R.NPC.Get()), Target(R.Recipient.Get());
        const TStrongObjectPtr<AAIController> Controller(R.Controller.Get());
        if (NPC && !NPC->IsActorBeingDestroyed()) NPC->Destroy();
        if (Target && !Target->IsActorBeingDestroyed()) Target->Destroy();
        // Spawned-with-NPC controllers and helper-created controllers both belong
        // to this fixture, but never destroy one now possessing another actor.
        if (Controller && !Controller->IsActorBeingDestroyed() && !Controller->GetPawn()) Controller->Destroy();
        R.bDestroyed=(!NPC || NPC->IsActorBeingDestroyed()) && (!Target || Target->IsActorBeingDestroyed()) &&
            (!Controller || Controller->IsActorBeingDestroyed());
        R.bComponentsUnregistered=(!R.Mesh.IsValid() || !R.Mesh->IsRegistered()) &&
            (!R.RecipientMesh.IsValid() || !R.RecipientMesh->IsRegistered());
        NPCCombatDestroyed+=!R.NPC.IsExplicitlyNull() && (!NPC || NPC->IsActorBeingDestroyed());
        NPCCombatDestroyed+=!R.Recipient.IsExplicitlyNull() && (!Target || Target->IsActorBeingDestroyed());
        NPCCombatUnregistered+=!R.NPC.IsExplicitlyNull() && (!NPC || !NPC->GetMesh() || !NPC->GetMesh()->IsRegistered());
        NPCCombatUnregistered+=!R.Recipient.IsExplicitlyNull() && (!Target || !Target->GetMesh() || !Target->GetMesh()->IsRegistered());
        bReleased&=R.bDestroyed && R.bComponentsUnregistered && (!R.ASC.IsValid() || !R.Grant.IsValid() || !R.ASC->FindAbilitySpecFromHandle(R.Grant));
    }
    bReleased&=Registry && Budget && World && GetWorld()==World.Get() &&
        Registry->GetAgentCount()==NPCCombatBaseAgents && Registry->GetActionCount()==NPCCombatBaseActions && Registry->GetTargetCount()==NPCCombatBaseTargets &&
        Budget->GetTrackedCount()==NPCCombatBaseTracked;
    bNPCCombatCleanupPassed=bReleased; bNPCCombatCleaned=true; bNPCCombatCleaning=false;
    if (!bReleased) NPCCombatFailure=TEXT("Actual NPC normal destruction did not restore owned registrations/components/grants to baseline");
}

bool UProject_JRenderedCrowdFixture::WriteNPCCombatCohort(bool bSuccess,const FString& Reason)
{
    check(IsInGameThread());
    if (!NPCCombatCount) return true;
    bool bDirtyUnchanged=NPCCombatPins.Num()==NPCCombatOriginalDirty.Num();
    for (int32 I=0;I<NPCCombatPins.Num() && I<NPCCombatOriginalDirty.Num();++I)
        bDirtyUnchanged&=IsValid(NPCCombatPins[I]) && NPCCombatPins[I]->GetPackage()->IsDirty()==NPCCombatOriginalDirty[I];
    auto Root=MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("schema"),1); Root->SetStringField(TEXT("scope"),TEXT("standalone_actual_non_mm_npc_finite_combat_animation_budget"));
    Root->SetBoolField(TEXT("success"),bSuccess && bNPCCombatRetired && bNPCCombatCleanupPassed && !bNPCCombatViolation && bDirtyUnchanged);
    Root->SetStringField(TEXT("reason"),Reason); Root->SetStringField(TEXT("npc_failure"),NPCCombatFailure);
    Root->SetNumberField(TEXT("authored_npcs"),NPCCombatCount); Root->SetNumberField(TEXT("passive_native_recipients"),NPCCombatCount);
    Root->SetBoolField(TEXT("authored_melee_path"),bNPCAuthoredPath);
    Root->SetStringField(TEXT("activation_definition"),GetPathNameSafe(NPCCombatDefinition.Get())); Root->SetStringField(TEXT("attack_definition"),GetPathNameSafe(NPCCombatAttack.Get()));
    Root->SetNumberField(TEXT("authored_path_keys"),NPCCombatAttack?NPCCombatAttack->MeleeTrajectory.Keys.Num():0);
    Root->SetStringField(TEXT("authored_path_stamp"),NPCCombatAttack?NPCCombatAttack->MeleeTrajectory.SourceStamp:FString());
    Root->SetNumberField(TEXT("peak_simultaneous_montages"),NPCCombatPeakMontages); Root->SetNumberField(TEXT("samples"),NPCCombatSamples);
    Root->SetBoolField(TEXT("retired"),bNPCCombatRetired); Root->SetBoolField(TEXT("cleanup_passed"),bNPCCombatCleanupPassed);
    Root->SetBoolField(TEXT("dirty_unchanged"),bDirtyUnchanged); Root->SetNumberField(TEXT("destroyed_owned_actors"),NPCCombatDestroyed);
    Root->SetNumberField(TEXT("unregistered_owned_meshes"),NPCCombatUnregistered); Root->SetNumberField(TEXT("retirement_tail_seconds"),NPCCombatRetireTailSeconds);
    Root->SetStringField(TEXT("not_qualified"),TEXT("No Nav routing, network audience, NPC VFX/mandatory-warning, full quality, population capacity or FPS claim; raw contacts may repeat; 32 passive recipients are not 32 additional authored animated NPCs."));
    auto TraceSetup=MakeShared<FJsonObject>();
    TraceSetup->SetStringField(TEXT("montage"),GetPathNameSafe(NPCCombatAttack?NPCCombatAttack->Montage.Get():nullptr));
    TraceSetup->SetNumberField(TEXT("play_rate"),NPCCombatObservedPlayRate);
    TraceSetup->SetNumberField(TEXT("trace_radius"),NPCCombatObservedTraceRadius); TraceSetup->SetNumberField(TEXT("trace_distance"),NPCCombatObservedTraceDistance);
    TraceSetup->SetBoolField(TEXT("uses_canonical_tip_offset"),bNPCCombatObservedUseTipOffset);
    TraceSetup->SetStringField(TEXT("canonical_tip_offset"),NPCCombatObservedTipOffset.ToString());
    TraceSetup->SetBoolField(TEXT("notify_metadata_truncated"),bNPCCombatNotifyMetadataTruncated);
    TraceSetup->SetStringField(TEXT("history_count_scope"),TEXT("At most one public now/key0/tag history lookup per admitted live actor per game frame. Counts are successful GT readback/open observations, not distinct native ring records; public lookup returns no record timestamp/ID and may reuse a record within native tolerance. No query success does not prove notify was not executed."));
    TraceSetup->SetStringField(TEXT("geometry_scope"),TEXT("Euclidean distance between matched history sweep segment (possibly closed/interpolated) and current actual-intent capsule axis; signed clearance subtracts capsule radius and authored trace radius. This diagnostic is not a collision scene query or a guaranteed contact/visibility test. Negative clearance is geometric overlap only."));
    TArray<TSharedPtr<FJsonValue>> NotifyValues;
    for (const auto& N:NPCCombatNotifyObservations)
    {
        auto Value=MakeShared<FJsonObject>(); Value->SetStringField(TEXT("class"),N.ClassPath);
        Value->SetNumberField(TEXT("start"),N.Start); Value->SetNumberField(TEXT("duration"),N.Duration); Value->SetNumberField(TEXT("end"),N.Start+N.Duration);
        Value->SetStringField(TEXT("socket"),N.Socket); Value->SetBoolField(TEXT("socket_resolved"),N.bSocketResolved);
        Value->SetNumberField(TEXT("montage_tick_type"),N.MontageTickType); Value->SetBoolField(TEXT("state_native_branching_point"),N.bNativeBranchingPoint);
        Value->SetBoolField(TEXT("effective_branching_point"),N.bEffectiveBranchingPoint);
        Value->SetNumberField(TEXT("trigger_weight_threshold"),N.TriggerWeightThreshold); Value->SetNumberField(TEXT("notify_trigger_chance"),N.NotifyTriggerChance);
        Value->SetNumberField(TEXT("notify_filter_type"),N.NotifyFilterType); Value->SetNumberField(TEXT("notify_filter_lod"),N.NotifyFilterLOD);
        Value->SetBoolField(TEXT("can_be_filtered_via_request"),N.bCanBeFilteredViaRequest); Value->SetBoolField(TEXT("trigger_on_dedicated_server"),N.bTriggerOnDedicatedServer);
        Value->SetBoolField(TEXT("trigger_on_follower"),N.bTriggerOnFollower);
        NotifyValues.Add(MakeShared<FJsonValueObject>(Value));
    }
    TraceSetup->SetArrayField(TEXT("melee_notifies"),NotifyValues); Root->SetObjectField(TEXT("trace_observation_setup"),TraceSetup);
    const auto VectorJSON=[](const FVector& V)
    {
        auto Value=MakeShared<FJsonObject>(); Value->SetNumberField(TEXT("x"),V.X); Value->SetNumberField(TEXT("y"),V.Y); Value->SetNumberField(TEXT("z"),V.Z); return Value;
    };
    TArray<TSharedPtr<FJsonValue>> Values;
    for (const auto& R:NPCCombatRows)
    {
        auto Value=MakeShared<FJsonObject>();
        Value->SetStringField(TEXT("npc"),R.Name); Value->SetStringField(TEXT("paired_recipient"),R.RecipientName);
        Value->SetStringField(TEXT("anim_class"),R.AnimationPath);
        Value->SetStringField(TEXT("source_epoch"),LexToString(R.SourceEpoch)); Value->SetStringField(TEXT("recipient_epoch"),LexToString(R.RecipientEpoch));
        Value->SetBoolField(TEXT("admitted"),R.bAdmitted); Value->SetBoolField(TEXT("quiet_managed"),R.bQuietManaged);
        Value->SetBoolField(TEXT("native_protected_pose"),R.bProtected); Value->SetBoolField(TEXT("actual_montage"),R.bMontage);
        Value->SetBoolField(TEXT("budget_returned"),R.bReturned); Value->SetNumberField(TEXT("natural_ends"),R.NaturalEnds);
        Value->SetNumberField(TEXT("raw_contacts"),R.Contacts); Value->SetNumberField(TEXT("effects"),R.Effects);
        Value->SetNumberField(TEXT("distinct_effect_recipients"),R.EffectTargets.Num()); Value->SetNumberField(TEXT("received_effects"),R.RecipientEffects);
        Value->SetNumberField(TEXT("actual_health_loss"),R.HealthLoss); Value->SetNumberField(TEXT("last_health"),R.LastHealth);
        Value->SetNumberField(TEXT("bone_finalizations"),R.Finalizations); Value->SetNumberField(TEXT("attack_finalizations"),R.AttackFinalizations);
        Value->SetBoolField(TEXT("presentation_component_present"),R.bPresentationComponent);
        Value->SetBoolField(TEXT("helper_created_controller"),R.bHelperController); Value->SetBoolField(TEXT("destroyed"),R.bDestroyed);
        Value->SetBoolField(TEXT("components_unregistered"),R.bComponentsUnregistered);
        Value->SetNumberField(TEXT("history_lookup_attempts"),R.HistoryAttempts); Value->SetNumberField(TEXT("history_lookup_found"),R.HistoryFound);
        Value->SetNumberField(TEXT("open_history_observations"),R.OpenHistoryObservations);
        Value->SetNumberField(TEXT("notify_suppressed_observations"),R.NotifySuppressedObservations); Value->SetNumberField(TEXT("notify_unsuppressed_observations"),R.NotifyUnsuppressedObservations);
        Value->SetNumberField(TEXT("montage_weight_observations"),R.MontageWeightObservations);
        if (R.MontageWeightObservations)
        { Value->SetNumberField(TEXT("montage_weight_min"),R.MontageWeightMin); Value->SetNumberField(TEXT("montage_weight_max"),R.MontageWeightMax); }
        Value->SetNumberField(TEXT("last_history_query_time"),R.LastHistoryQueryTime); Value->SetNumberField(TEXT("last_open_query_time"),R.LastOpenQueryTime);
        Value->SetNumberField(TEXT("last_matched_query_time"),R.LastMatchedQueryTime); Value->SetNumberField(TEXT("last_matched_montage_position"),R.LastMatchedMontagePosition);
        Value->SetBoolField(TEXT("last_matched_window_open"),R.bLastMatchedWindowOpen);
        Value->SetNumberField(TEXT("last_matched_trace_segments"),R.LastMatchedTraceSegments);
        Value->SetStringField(TEXT("last_matched_trace_role"),R.LastMatchedTraceSegments?TEXT("closest_actual_packet_piece_to_current_capsule"):TEXT("no_geometry"));
        Value->SetNumberField(TEXT("predicted_lod_min"),R.PredictedLODMin==MAX_int32?-1:R.PredictedLODMin); Value->SetNumberField(TEXT("predicted_lod_max"),R.PredictedLODMax);
        Value->SetNumberField(TEXT("montage_position_observations"),R.MontagePositionObservations);
        Value->SetNumberField(TEXT("montage_first_position"),R.MontageFirstPosition); Value->SetNumberField(TEXT("montage_previous_position"),R.MontagePreviousPosition);
        Value->SetNumberField(TEXT("montage_last_position"),R.MontageLastPosition); Value->SetNumberField(TEXT("montage_max_forward_advance"),R.MontageMaxForwardAdvance);
        Value->SetNumberField(TEXT("last_open_montage_position"),R.LastOpenMontagePosition); Value->SetStringField(TEXT("geometry_target"),R.LastGeometryTarget);
        Value->SetNumberField(TEXT("observed_capsule_radius"),R.ObservedCapsuleRadius); Value->SetNumberField(TEXT("observed_capsule_half_height"),R.ObservedCapsuleHalfHeight);
        Value->SetBoolField(TEXT("sweep_capsule_geometry_observed"),!R.LastGeometryTarget.IsEmpty());
        if (!R.LastGeometryTarget.IsEmpty())
        {
            Value->SetNumberField(TEXT("min_sweep_capsule_axis_distance"),R.MinSweepCapsuleAxisDistance);
            Value->SetNumberField(TEXT("min_sweep_capsule_clearance"),R.MinSweepCapsuleClearance);
        }
        Value->SetObjectField(TEXT("last_matched_trace_start"),VectorJSON(R.LastMatchedTraceStart)); Value->SetObjectField(TEXT("last_matched_trace_end"),VectorJSON(R.LastMatchedTraceEnd));
        Value->SetObjectField(TEXT("last_matched_actor_position"),VectorJSON(R.LastMatchedActorPosition)); Value->SetObjectField(TEXT("last_matched_recipient_position"),VectorJSON(R.LastMatchedRecipientPosition));
        Values.Add(MakeShared<FJsonValueObject>(Value));
    }
    Root->SetArrayField(TEXT("npcs"),Values);
    FString Json;
    const bool bSerialized=FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json));
    const bool bCSV=FFileHelper::SaveStringToFile(NPCCombatCSV,*(Output/TEXT("npc-combat.csv")));
    const bool bJSON=bSerialized && FFileHelper::SaveStringToFile(Json,*(Output/TEXT("npc-combat.json")));
    return bCSV && bJSON && bDirtyUnchanged && (!bSuccess || (bNPCCombatRetired && bNPCCombatCleanupPassed && !bNPCCombatViolation));
}
