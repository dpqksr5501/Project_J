#include "Movement/Project_JMoverComponent.h"
#include "DefaultMovementSet/CharacterMoverSimulationTypes.h"

// Explicit authority-only diagnostic: actual authored NPC leases, physical montage notifies and GE recipients.
namespace
{
uint64 FanoutASCEpoch(UAbilitySystemComponent* ASC)
{
 const auto* Typed=Cast<UProject_JAbilitySystemComponent>(ASC);
 return Typed ? Typed->GetActorInfoRevision() : 0;
}
}
bool AProject_JNetworkLoadGameMode::IsCombatFanoutCurrent(int32 Index, uint64 Epoch) const
{
 if (bFinished || FanoutEpoch!=Epoch || !GetWorld() || GetWorld()->bIsTearingDown || !FanoutGroups.IsValidIndex(Index) || !CleaveAttack || !CleaveDefinition) return false;
 const auto& G=FanoutGroups[Index];
 auto* Caster=G.Caster.Get(); auto* Source=G.Source.Get();
 if (!IsValid(Caster) || Caster->IsActorBeingDestroyed() || !Caster->GetMover() || !CleaveNoPlayerConnection(Caster) || Caster->GetWorld()!=GetWorld() ||
  !IsValid(Source) || Source->IsBeingDestroyed() || Caster->GetAbilitySystemComponent()!=Source || Source->GetOwnerActor()!=Caster || Source->GetAvatarActor()!=Caster || FanoutASCEpoch(Source)!=G.SourceEpoch ||
  !G.Activation.IsValid() || G.Activation->GetOwner()!=Caster || !G.Scoring.IsValid() || G.Scoring->GetOwner()!=Caster ||
  !G.Action.IsValid() || G.Action->GetOwner()!=Caster || !G.Hit.IsValid() || G.Hit->GetOwner()!=Caster) return false;
 for (int32 S=0;S<3;++S)
 {
  auto* Victim=G.Victims[S].Get(); auto* ASC=G.Recipients[S].Get();
  if (!IsValid(Victim) || Victim->IsActorBeingDestroyed() || Victim->GetWorld()!=GetWorld() || !IsValid(ASC) || ASC->IsBeingDestroyed() ||
   Victim->GetAbilitySystemComponent()!=ASC || ASC->GetOwnerActor()!=Victim || ASC->GetAvatarActor()!=Victim || FanoutASCEpoch(ASC)!=G.TargetEpoch[S]) return false;
 }
 return true;
}
void AProject_JNetworkLoadGameMode::BeginCombatFanout()
{
 const TStrongObjectPtr<ThisClass> KeepSelf(this);
 Stage=50; StageStarted=FPlatformTime::Seconds();
 auto* Registry=GetWorld()->GetSubsystem<UProject_JNPCDecisionSubsystem>();
 if (!CombatFanoutEnabled() || !Registry || !FanoutGroups.IsEmpty() || FTraceAuxiliary::IsConnected() || FParse::Param(FCommandLine::Get(),TEXT("tracenocache")))
 { Finish(false,TEXT("Fanout requires fresh authority records and native important-event cache without an existing trace")); return; }
 FanoutBaseAgents=Registry->GetAgentCount(); FanoutBaseActions=Registry->GetActionCount(); FanoutBaseTargets=Registry->GetTargetCount();
 CleaveDefinition=LoadObject<UProject_JNPCActivationDefinition>(nullptr,TEXT("/Game/DataAssetSets/NPC/DA_NPC_PrototypeCleaveActivation.DA_NPC_PrototypeCleaveActivation"));
 CleaveAttack=LoadObject<UProject_JAttackDefinition>(nullptr,TEXT("/Game/DataAssetSets/AttackDefinition/NPC/DA_Attack_NPC_PrototypeCleave.DA_Attack_NPC_PrototypeCleave"));
 const TStrongObjectPtr<UClass> Class(LoadClass<AProject_JNPCCharacter>(nullptr,TEXT("/Game/NPCs/BP_NPC_Test.BP_NPC_Test_C")));
 const auto* Ability=CleaveDefinition && CleaveDefinition->AttackAbility && CleaveDefinition->AttackAbility->IsChildOf(UProject_JGameplayAbility_NPCAttack::StaticClass())
  ? CleaveDefinition->AttackAbility->GetDefaultObject<UProject_JGameplayAbility_NPCAttack>() : nullptr;
 const auto* AttackField=Ability ? FindFProperty<FObjectPropertyBase>(Ability->GetClass(),TEXT("AttackDefinition")) : nullptr;
 const auto* TagField=Ability ? FindFProperty<FStructProperty>(Ability->GetClass(),TEXT("HitEventTag")) : nullptr;
 if (!CleaveAttack || !CleaveAttack->Montage || !CleaveAttack->DamageEffect || CleaveAttack->NPCMaxMeleeTargets!=2 || !Class.IsValid() || !Ability ||
  !UProject_JNPCActionComponent::SupportsAttackAbility(Ability) || !AttackField || !TagField || AttackField->GetObjectPropertyValue_InContainer(Ability)!=CleaveAttack || CleaveDefinition->bUseDamageThreat)
 { Finish(false,TEXT("Fanout requires the unchanged authored NPC cleave contracts")); return; }
 CleaveHitTag=*TagField->ContainerPtrToValuePtr<FGameplayTag>(Ability);
 int32 Notifies=0;
 if (CleaveAttack->Montage->Notifies.Num()>64) { Finish(false,TEXT("Fanout authored notify bound exceeded")); return; }
 for (const auto& N:CleaveAttack->Montage->Notifies) if (N.NotifyStateClass && N.NotifyStateClass->IsA<UProject_JAnimNotifyState_MeleeHit>())
 {
  ++Notifies; const auto* EventField=FindFProperty<FStructProperty>(N.NotifyStateClass->GetClass(),TEXT("HitEventTag"));
  if (!EventField || *EventField->ContainerPtrToValuePtr<FGameplayTag>(N.NotifyStateClass.Get())!=CleaveHitTag) { Finish(false,TEXT("Fanout authored hit tag mismatch")); return; }
  CleaveFirstImpact=N.GetTriggerTime();
 }
 if (Notifies!=1 || !CleaveHitTag.IsValid() || !FMath::IsFinite(CleaveFirstImpact) || CleaveFirstImpact<=0)
 { Finish(false,TEXT("Fanout requires one actual timed melee notify")); return; }
 FanoutGroups.SetNum(32); FanoutOwnedActors.Reserve(160); FanoutRegisteredTargets.Reserve(96);
 const uint64 Epoch=FanoutEpoch; const TWeakObjectPtr<ThisClass> WeakThis(this);
 const TStrongObjectPtr<UWorld> SetupWorld(GetWorld());
 const auto SetupCurrent=[this,Epoch,&SetupWorld]
 { return !bFinished && !IsActorBeingDestroyed() && FanoutEpoch==Epoch && FanoutGroups.Num()==32 && GetWorld()==SetupWorld.Get() && SetupWorld.IsValid() && !SetupWorld->bIsTearingDown; };
 const auto Track=[this,&SetupCurrent](AActor* Actor)
 {
  const TStrongObjectPtr<AActor> Pin(Actor);
  if (!SetupCurrent() || !IsValid(Actor) || Actor->IsActorBeingDestroyed())
  { if (IsValid(Actor) && !Actor->IsActorBeingDestroyed()) Actor->Destroy(); return false; }
  FanoutOwnedActors.AddUnique(Actor); return true;
 };
 const TStrongObjectPtr<UProject_JNPCDecisionSubsystem> SetupRegistry(Registry);
 FActorSpawnParameters P; P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn; P.ObjectFlags|=RF_Transient;
 for (int32 I=0;I<32;++I)
 {
  const FVector Origin(-17500+(I%8)*5000,-7500+(I/8)*5000,100);
  auto& G=FanoutGroups[I];
  const TStrongObjectPtr<AProject_JNPCCharacter> CasterPin(GetWorld()->SpawnActor<AProject_JNPCCharacter>(Class.Get(),Origin,FRotator::ZeroRotator,P));
  auto* Caster=CasterPin.Get();
  if (!Track(Caster)) { Finish(false,TEXT("Fanout caster allocation/lifetime failed")); return; }
  G.Caster=Caster;
  auto* AI=Cast<AAIController>(Caster->GetController());
  if (!AI)
  {
   AI=GetWorld()->SpawnActor<AAIController>(P);
   const TStrongObjectPtr<AAIController> AIPin(AI);
   if (!Track(AI)) { Finish(false,TEXT("Fanout controller allocation/lifetime failed")); return; }
   AI->Possess(Caster);
  }
  else if (!Track(AI)) { Finish(false,TEXT("Fanout authored controller lifetime failed")); return; }
  if (!SetupCurrent() || !IsValid(Caster) || Caster->IsActorBeingDestroyed() || !IsValid(AI) || AI->IsActorBeingDestroyed() ||
   Caster->GetController()!=AI || (AI->GetBrainComponent() && AI->GetBrainComponent()->IsRunning()) || !CleaveNoPlayerConnection(Caster) ||
   !Caster->GetMesh() || !Caster->GetMesh()->GetAnimInstance()) { Finish(false,TEXT("Fanout requires actual idle NPC AI/ASC/montage owners")); return; }
  G.Controller=AI;
  const auto ComponentCurrent=[&SetupCurrent,Caster] { return SetupCurrent() && IsValid(Caster) && !Caster->IsActorBeingDestroyed(); };
  const TStrongObjectPtr<UProject_JNPCActivationComponent> Activation(CleaveComponent<UProject_JNPCActivationComponent>(Caster));
  if (!ComponentCurrent() || !Activation.IsValid()) { Finish(false,TEXT("Fanout activation component registration changed owner")); return; } G.Activation=Activation.Get();
  const TStrongObjectPtr<UProject_JTargetScoringComponent> Scoring(CleaveComponent<UProject_JTargetScoringComponent>(Caster));
  if (!ComponentCurrent() || !Scoring.IsValid()) { Finish(false,TEXT("Fanout scoring component registration changed owner")); return; } G.Scoring=Scoring.Get();
  const TStrongObjectPtr<UProject_JNPCActionComponent> Action(CleaveComponent<UProject_JNPCActionComponent>(Caster));
  if (!ComponentCurrent() || !Action.IsValid()) { Finish(false,TEXT("Fanout action component registration changed owner")); return; } G.Action=Action.Get();
  const TStrongObjectPtr<UProject_JCombatHitValidationComponent> Hit(CleaveComponent<UProject_JCombatHitValidationComponent>(Caster));
  if (!ComponentCurrent() || !Hit.IsValid()) { Finish(false,TEXT("Fanout hit component registration changed owner")); return; } G.Hit=Hit.Get();
  G.Source=Caster->GetAbilitySystemComponent(); G.SourceEpoch=FanoutASCEpoch(G.Source.Get());
  if (!G.Activation.IsValid() || !G.Scoring.IsValid() || !G.Action.IsValid() || !G.Hit.IsValid() || !G.SourceEpoch ||
   G.Activation->HasOwnedActivation() || G.Scoring->IsBatchedNPCDecisionRegistered() || G.Action->GetActionState()!=EProjectJNPCActionState::Disabled ||
   G.Hit->GetActiveAttackDefinition() || G.Scoring->Range>2000 || G.Action->AttackRange>2000)
  { Finish(false,TEXT("Fanout initial ownership/range exceeds isolated geometry")); return; }
  const TStrongObjectPtr<UAbilitySystemComponent> SourceASC(G.Source.Get()); const uint64 SourceEpoch=G.SourceEpoch;
  const auto SourceCurrent=[&ComponentCurrent,&SourceASC,Caster,SourceEpoch]
  { return ComponentCurrent() && SourceASC.IsValid() && !SourceASC->IsBeingDestroyed() && Caster->GetAbilitySystemComponent()==SourceASC.Get() &&
    SourceASC->GetOwnerActor()==Caster && SourceASC->GetAvatarActor()==Caster && FanoutASCEpoch(SourceASC.Get())==SourceEpoch; };
  Caster->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
  if (!SourceCurrent()) { Finish(false,TEXT("Fanout source Pawn response callback retired owner")); return; }
  Caster->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Ignore);
  if (!SourceCurrent()) { Finish(false,TEXT("Fanout source Visibility response callback retired owner")); return; }
  SourceASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(),CleaveInitialHealth);
  if (!SourceCurrent()) { Finish(false,TEXT("Fanout source initial MaxHealth callback retired owner")); return; }
  SourceASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(),CleaveInitialHealth);
  if (!SourceCurrent()) { Finish(false,TEXT("Fanout source initial Health callback retired owner")); return; }
  for (int32 S=0;S<3;++S)
  {
   const FVector Location=Origin+FVector(110,(S-1)*24,0);
   const TStrongObjectPtr<AProject_JNPCCharacter> VictimPin(GetWorld()->SpawnActor<AProject_JNPCCharacter>(AProject_JNPCCharacter::StaticClass(),Location,FRotator::ZeroRotator,P));
   auto* Victim=VictimPin.Get();
   if (!Track(Victim)) { Finish(false,TEXT("Fanout victim allocation/lifetime failed")); return; }
   G.Victims[S]=Victim; G.Recipients[S]=Victim->GetAbilitySystemComponent(); G.TargetEpoch[S]=FanoutASCEpoch(G.Recipients[S].Get());
   if (Victim->GetController() || !CleaveNoPlayerConnection(Victim) || !G.TargetEpoch[S])
   { Finish(false,TEXT("Fanout victim ownership or registry admission failed")); return; }
   const bool Registered=SetupRegistry->RegisterTarget(Victim,2);
   if (!ComponentCurrent() || !IsValid(Victim) || Victim->IsActorBeingDestroyed())
   { if (Registered) SetupRegistry->UnregisterTarget(Victim); Finish(false,TEXT("Fanout target registration callback retired owner")); return; }
   if (!Registered) { Finish(false,TEXT("Fanout target registry admission failed")); return; }
   FanoutRegisteredTargets.Add(Victim);
   const TStrongObjectPtr<UAbilitySystemComponent> Recipient(G.Recipients[S].Get()); const uint64 RecipientEpoch=G.TargetEpoch[S];
   const auto RecipientCurrent=[&SourceCurrent,&Recipient,Victim,RecipientEpoch]
   { return SourceCurrent() && IsValid(Victim) && !Victim->IsActorBeingDestroyed() && Recipient.IsValid() && !Recipient->IsBeingDestroyed() &&
     Victim->GetAbilitySystemComponent()==Recipient.Get() && Recipient->GetOwnerActor()==Victim && Recipient->GetAvatarActor()==Victim && FanoutASCEpoch(Recipient.Get())==RecipientEpoch; };
   Victim->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
   if (!RecipientCurrent()) { Finish(false,TEXT("Fanout victim Pawn response callback retired owner")); return; }
   Victim->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Ignore);
   if (!RecipientCurrent()) { Finish(false,TEXT("Fanout victim Visibility response callback retired owner")); return; }
   Recipient->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(),CleaveInitialHealth);
   if (!RecipientCurrent()) { Finish(false,TEXT("Fanout victim MaxHealth callback retired owner")); return; }
   Recipient->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(),CleaveInitialHealth);
   if (!RecipientCurrent()) { Finish(false,TEXT("Fanout victim Health callback retired owner")); return; }
   G.EffectHandles[S]=G.Recipients[S]->OnGameplayEffectAppliedDelegateToSelf.AddLambda([WeakThis,Epoch,I,S](UAbilitySystemComponent*,const FGameplayEffectSpec& Spec,FActiveGameplayEffectHandle)
   {
    const TStrongObjectPtr<ThisClass> Self(WeakThis.Get());
    if (!Self.IsValid() || !Self->IsCombatFanoutCurrent(I,Epoch)) return;
    const auto& Current=Self->FanoutGroups[I];
    const TStrongObjectPtr<AProject_JNPCCharacter> Source(Current.Caster.Get()),Target(Current.Victims[S].Get());
    const TStrongObjectPtr<UAbilitySystemComponent> SourceASC(Current.Source.Get()),TargetASC(Current.Recipients[S].Get());
    if (Spec.Def!=Self->CleaveAttack->DamageEffect->GetDefaultObject<UGameplayEffect>()) return;
    if (Spec.GetContext().GetOriginalInstigator()!=Source.Get() || Self->Stage!=51 || ++Self->FanoutGroups[I].Effects[S]>1) Self->bFanoutViolation=true;
   });
  }
  if (!IsCombatFanoutCurrent(I,Epoch)) { Finish(false,TEXT("Fanout setup recipient epoch changed")); return; }
  G.ContactHandle=G.Source->AddGameplayEventTagContainerDelegate(FGameplayTagContainer(CleaveHitTag),FGameplayEventTagMulticastDelegate::FDelegate::CreateLambda(
   [WeakThis,Epoch,I](FGameplayTag Tag,const FGameplayEventData* Payload)
   {
    const TStrongObjectPtr<ThisClass> Self(WeakThis.Get());
    if (!Self.IsValid() || !Self->IsCombatFanoutCurrent(I,Epoch)) return;
    const TStrongObjectPtr<AProject_JNPCCharacter> Caster(Self->FanoutGroups[I].Caster.Get());
    const TStrongObjectPtr<UAbilitySystemComponent> ASC(Self->FanoutGroups[I].Source.Get());
    if (!Payload || Tag!=Self->CleaveHitTag || Payload->Instigator.Get()!=Caster.Get() || Payload->TargetData.Num()!=1)
    { Self->bFanoutViolation=true; return; }
    const auto* Data=Payload->TargetData.Get(0); const auto* Contact=Data ? Data->GetHitResult() : nullptr;
    if (!Contact || Contact->GetActor()!=Payload->Target.Get() || Self->Stage!=51) { Self->bFanoutViolation=true; return; }
    for (int32 S=0;S<3;++S) if (Contact->GetActor()==Self->FanoutGroups[I].Victims[S].Get()) { Self->FanoutGroups[I].ContactMask|=1<<S; return; }
    Self->bFanoutViolation=true;
   }));
  G.EndHandle=G.Source->OnAbilityEnded.AddLambda([WeakThis,Epoch,I](const FAbilityEndedData& Data)
  {
   const TStrongObjectPtr<ThisClass> Self(WeakThis.Get());
   if (!Self.IsValid() || !Self->IsCombatFanoutCurrent(I,Epoch) || !Data.AbilityThatEnded || Data.AbilityThatEnded->GetClass()!=Self->CleaveDefinition->AttackAbility) return;
   const auto& Current=Self->FanoutGroups[I];
   const TStrongObjectPtr<AProject_JNPCCharacter> Caster(Current.Caster.Get());
   const TStrongObjectPtr<UAbilitySystemComponent> ASC(Current.Source.Get());
   const TStrongObjectPtr<UProject_JNPCActivationComponent> Activation(Current.Activation.Get());
   if (Data.AbilitySpecHandle!=Current.Grant && Data.AbilitySpecHandle!=Current.Action->GetAttackAbilityHandle()) return;
   if (Current.bEnded || (Self->Stage==51 ? Data.bWasCancelled : Self->Stage!=52 || !Current.bCancelRequested || !Data.bWasCancelled))
   { Self->bFanoutViolation=true; return; }
   Self->FanoutGroups[I].bEnded=true;
   // Retire this exact lease before the scheduler can repeat the natural attack.
   if (Self->Stage==51 && Activation.IsValid()) Activation->DeactivateNPC();
  });
 }
 if (FanoutOwnedActors.Num()!=160 || FanoutRegisteredTargets.Num()!=96 || Registry->GetTargetCount()!=FanoutBaseTargets+96)
 { Finish(false,TEXT("Fanout setup bound mismatch")); return; }
 for (const auto& PC:Clients) PC->ClientConfigure(50,PC->FixtureSlot,ExpectedIds(PC->FixtureSlot),Items[PC->FixtureSlot],false,RequiredPulse);
}
bool AProject_JNetworkLoadGameMode::StartCombatFanoutBurst(int32 Phase)
{
 Stage=Phase; StageStarted=FPlatformTime::Seconds(); bFanoutBurstActivated=true;
 TRACE_BOOKMARK(TEXT("ProjectJ.CombatFanout Phase=%d Casters=32 AuthorityOnly=1"),Phase);
 const uint64 Epoch=FanoutEpoch; const TStrongObjectPtr<ThisClass> KeepSelf(this);
 for (int32 I=0;I<32;++I)
 {
  if (!IsCombatFanoutCurrent(I,Epoch)) return false;
  auto& G=FanoutGroups[I]; G.bStarted=G.bEnded=G.bCancelRequested=false; G.ContactMask=0;
  for (int32 S=0;S<3;++S) G.Effects[S]=0;
  const TStrongObjectPtr<AProject_JNPCCharacter> Caster(G.Caster.Get());
  const TStrongObjectPtr<UProject_JNPCActivationComponent> Activation(G.Activation.Get());
  const TStrongObjectPtr<UProject_JTargetScoringComponent> Scoring(G.Scoring.Get());
  const TStrongObjectPtr<UProject_JNPCActionComponent> Action(G.Action.Get());
  const TStrongObjectPtr<UProject_JNPCActivationDefinition> Definition(CleaveDefinition);
  if (!Activation->ActivateNPC(Definition.Get(),Scoring.Get(),Action.Get(),1) || !IsCombatFanoutCurrent(I,Epoch)) return false;
  FanoutGroups[I].Grant=Action->GetAttackAbilityHandle();
  if (!FanoutGroups[I].Grant.IsValid()) return false;
 }
 return true;
}
void AProject_JNetworkLoadGameMode::UpdateCombatFanout()
{
 const uint64 Epoch=FanoutEpoch; const TStrongObjectPtr<ThisClass> KeepSelf(this);
 if (bFanoutViolation || FanoutGroups.Num()!=32) { Finish(false,TEXT("Fanout actual contact/effect/end contract violated")); return; }
 for (int32 I=0;I<32;++I)
 {
  if (!IsCombatFanoutCurrent(I,Epoch)) { Finish(false,TEXT("Fanout owner/recipient/epoch changed")); return; }
  const auto& G=FanoutGroups[I];
  if (Stage==50 || (Stage==52 && !bFanoutBurstActivated))
  {
   const auto CasterFrame=Project_J::Movement::ReadPhysicalFrame(G.Caster.Get());
   if (!CasterFrame.bValid || !CasterFrame.bGrounded) return;
   if (Stage==52 && (G.Caster->GetMover()->GetLastTimeStep().ServerFrame<=G.PreparationFrame ||
    FVector::DistSquared2D(G.Caster->GetActorLocation(),FVector(-17500+(I%8)*5000,-7500+(I/8)*5000,100))>1)) return;
   for (int32 S=0;S<3;++S)
   { const auto Frame=Project_J::Movement::ReadPhysicalFrame(G.Victims[S].Get()); if (!Frame.bValid || !Frame.bGrounded) return; }
  }
 }
 if (Stage==52 && !bFanoutBurstActivated)
 {
  if (!StartCombatFanoutBurst(52)) Finish(false,TEXT("Fanout cancellation activation failed"));
  return;
 }
 if (Stage==50)
 {
  if (!bFanoutTraceStarted)
  {
   const FString File=OutputRoot()/TEXT("combat-fanout.utrace");
   if (FTraceAuxiliary::IsConnected() || IFileManager::Get().FileExists(*File)) { Finish(false,TEXT("Fanout trace target already exists/connected")); return; }
   // One settled pre-trace observation, outside the measured attack/animation loop.
   // These are actual caster values, not admission overrides or predicted state.
   const FString ObservationFile=OutputRoot()/TEXT("combat-fanout-prediction-inputs.csv");
   if (IFileManager::Get().FileExists(*ObservationFile)) { Finish(false,TEXT("Fanout prediction observation target already exists")); return; }
   const auto Csv=[](const FString& Value) { return TEXT("\"")+Value.Replace(TEXT("\""),TEXT("\"\""))+TEXT("\""); };
   const auto Path=[](const UObject* Object) { return Object ? Object->GetPathName() : FString(); };
   FString Observations=TEXT("caster_index,caster_id,caster_name,mover_class,actual_mode_class,nearest_native_parent,generate_move_owner,generate_walk_move_owner,has_sync,sync_base_class,sync_base_bone,sync_base_mobility,input_type,has_input,using_input_base,active_modifier_count,layered_move_count,has_root_motion\n");
   for (int32 I=0;I<32;++I)
   {
    if (!IsCombatFanoutCurrent(I,Epoch)) { Finish(false,TEXT("Fanout prediction observation owner retired")); return; }
    const TStrongObjectPtr<AProject_JNPCCharacter> Caster(FanoutGroups[I].Caster.Get());
    const auto* Mover=Caster->GetMover();
    const UBaseMovementMode* Mode=Mover->GetMovementMode();
    const UClass* NativeParent=Mode ? Mode->GetClass() : nullptr;
    while (NativeParent && !NativeParent->HasAnyClassFlags(CLASS_Native)) NativeParent=NativeParent->GetSuperClass();
    const UFunction* Generate=Mode ? Mode->FindFunction(TEXT("GenerateMove")) : nullptr;
    const UFunction* Walk=Mode ? Mode->FindFunction(TEXT("GenerateWalkMove")) : nullptr;
    const FMoverSyncState& Physical=Mover->GetSyncState();
    const auto* Sync=Physical.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
    const auto* Base=Sync ? Sync->GetMovementBase() : nullptr;
    const auto* Input=Mover->GetLastInputCmd().InputCollection.FindDataByType<FCharacterDefaultInputs>();
    int32 Modifiers=0;for (auto It=Physical.MovementModifiers.GetActiveModifiersIterator();It;++It) ++Modifiers;
    Observations+=FString::Printf(TEXT("%d,%u,%s,%s,%s,%s,%s,%s,%d,%s,%s,%d,%d,%d,%d,%d,%d,%d\n"),I,Caster->GetUniqueID(),
     *Csv(Caster->GetName()),*Csv(Path(Mover->GetClass())),*Csv(Path(Mode ? Mode->GetClass() : nullptr)),*Csv(Path(NativeParent)),
     *Csv(Path(Generate ? Generate->GetOwnerClass() : nullptr)),*Csv(Path(Walk ? Walk->GetOwnerClass() : nullptr)),Sync!=nullptr,
     *Csv(Path(Base ? Base->GetClass() : nullptr)),*Csv(Sync ? Sync->GetMovementBaseBoneName().ToString() : FString()),
     Base ? int32(Base->GetMobility()) : -1,Input ? int32(Input->GetMoveInputType()) : -1,Input!=nullptr,Input && Input->bUsingMovementBase,
     Modifiers,Physical.LayeredMoves.GetActiveMoves().Num(),Mover->HasGameplayTag(Mover_AnimRootMotion,false));
   }
   IFileManager::Get().MakeDirectory(*OutputRoot(),true);
   if (!FFileHelper::SaveStringToFile(Observations,*ObservationFile)) { Finish(false,TEXT("Fanout prediction observation save failed")); return; }
   // The native writer signals this after schemas/important specs are sent.
   // Its callback may run on a writer thread: capture plain thread-safe data only, never a UObject.
   FanoutTraceReady=MakeShared<TAtomic<bool>,ESPMode::ThreadSafe>(false);
   const auto Ready=FanoutTraceReady.ToSharedRef();
   FanoutTraceConnectionHandle=FTraceAuxiliary::OnConnection.AddLambda([Ready] { Ready->Store(true); });
   FTraceAuxiliary::FOptions Options; Options.bExcludeTail=true;
   if (!FTraceAuxiliary::Start(FTraceAuxiliary::EConnectionType::File,*File,TEXT("cpu,frame,bookmark,region,counters"),&Options))
   { Finish(false,TEXT("Fanout late server CPU trace could not start")); return; }
   bFanoutTraceStarted=true;
   return;
  }
  if (!FTraceAuxiliary::IsConnected() || !FanoutTraceReady.IsValid() || !FanoutTraceReady->Load()) return;
  if (!StartCombatFanoutBurst(51)) Finish(false,TEXT("Fanout natural activation failed"));
  return;
 }
 int32 ActiveMontages=0,ActiveAttacks=0,Concurrent=0,Ended=0;
 bool DrainingMontage=false;
 for (int32 I=0;I<32;++I)
 {
  if (!IsCombatFanoutCurrent(I,Epoch)) { Finish(false,TEXT("Fanout callback replaced a group")); return; }
  const auto& G=FanoutGroups[I];
  const TStrongObjectPtr<AProject_JNPCCharacter> Caster(G.Caster.Get());
  const TStrongObjectPtr<UAbilitySystemComponent> ASC(G.Source.Get());
  const TStrongObjectPtr<UProject_JNPCActionComponent> Action(G.Action.Get());
  const TStrongObjectPtr<UProject_JNPCActivationComponent> Activation(G.Activation.Get());
  const TStrongObjectPtr<UProject_JCombatHitValidationComponent> Hit(G.Hit.Get());
  auto* Anim=Caster->GetMesh()->GetAnimInstance();
  const auto* Spec=ASC->FindAbilitySpecFromHandle(G.Grant); const auto* Instance=Spec ? Spec->GetPrimaryInstance() : nullptr;
  const bool Montage=Anim && Anim->Montage_IsPlaying(CleaveAttack->Montage);
  const bool Attack=Instance && Instance->IsActive() && Hit->GetActiveAttackDefinition()==CleaveAttack;
  ActiveMontages+=Montage; ActiveAttacks+=Attack;
  if (Montage && Attack)
  {
   ++Concurrent;
   auto* Target=Action->GetIntentTarget(); bool Local=false; for (int32 S=0;S<3;++S) Local|=Target==G.Victims[S].Get();
   if (!Local) { Finish(false,TEXT("Fanout actual primary selected another cluster")); return; }
   FanoutGroups[I].bStarted=true;
   if (Stage==52 && !G.bCancelRequested)
   {
    if (Anim->Montage_GetPosition(CleaveAttack->Montage)>=CleaveFirstImpact || G.ContactMask)
    { Finish(false,TEXT("Fanout actual pre-impact cancellation window was missed")); return; }
    FanoutGroups[I].bCancelRequested=true;
    Activation->DeactivateNPC();
    if (!IsCombatFanoutCurrent(I,Epoch)) { Finish(false,TEXT("Fanout cancel callback replaced its owner")); return; }
   }
  }
  const auto& After=FanoutGroups[I];
  Ended+=After.bEnded;
  DrainingMontage|=After.bEnded && Montage;
  if (After.bEnded && (Activation->HasOwnedActivation() || Hit->GetActiveAttackDefinition() || Action->GetActionState()!=EProjectJNPCActionState::Disabled || ASC->FindAbilitySpecFromHandle(After.Grant)))
  { Finish(false,TEXT("Fanout ended attack retained its owned execution or grant")); return; }
 }
 if (Stage==51) { FanoutPeakMontages=FMath::Max(FanoutPeakMontages,ActiveMontages); FanoutPeakAttacks=FMath::Max(FanoutPeakAttacks,ActiveAttacks); FanoutPeakConcurrent=FMath::Max(FanoutPeakConcurrent,Concurrent); }
 if (Ended!=32 || DrainingMontage) return;
 int32 StartedCount=0,Contacts=0,Effects=0,Losses=0;
 for (int32 I=0;I<32;++I)
 {
  if (!IsCombatFanoutCurrent(I,Epoch)) { Finish(false,TEXT("Fanout final group was replaced")); return; }
  auto& G=FanoutGroups[I]; StartedCount+=G.bStarted;
  if (!G.bStarted || (Stage==51 ? G.ContactMask!=7 : !G.bCancelRequested || G.ContactMask!=0))
  { Finish(false,TEXT("Fanout real montage/contact/cancel observation incomplete")); return; }
  int32 GroupEffects=0,GroupLosses=0;
  for (int32 S=0;S<3;++S)
  {
   const float Before=Stage==51 ? CleaveInitialHealth : G.NaturalHealth[S];
   const float Value=G.Recipients[S]->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute());
   const bool Lost=Value<Before-KINDA_SMALL_NUMBER;
   if (!FMath::IsFinite(Value) || Value<=0 || Value>Before || G.Effects[S]<0 || G.Effects[S]>1 ||
    (Stage==51 ? (G.Effects[S]==1)!=Lost : G.Effects[S]!=0 || !FMath::IsNearlyEqual(Value,Before)))
   { Finish(false,TEXT("Fanout actual Health and matching effect observations disagree")); return; }
   GroupEffects+=G.Effects[S]; GroupLosses+=Lost;
   const int32 Contact=(G.ContactMask&(1<<S))!=0; Contacts+=Contact;
   FanoutTargets+=FString::Printf(TEXT("%d,%d,%d,%u,%u,%.9g,%.9g,%d,%d,%d,%d,%d,1,authority_actual_combat_not_client_damage\n"),
    Stage,I,S,G.Caster->GetUniqueID(),G.Victims[S]->GetUniqueID(),Before,Value,G.Effects[S],Contact,G.bStarted,G.bEnded,G.bCancelRequested);
   if (Stage==51) G.NaturalHealth[S]=Value;
  }
  if (GroupEffects!=(Stage==51 ? 2 : 0) || GroupLosses!=(Stage==51 ? 2 : 0))
  { Finish(false,TEXT("Fanout requires each group's exact two accepted recipients")); return; }
  Effects+=GroupEffects; Losses+=GroupLosses;
 }
 if (bFanoutViolation || StartedCount!=32) { Finish(false,TEXT("Fanout callback/start proof failed")); return; }
 if (Stage==51)
 {
  if (FanoutPeakConcurrent<2) { Finish(false,TEXT("Fanout failed to observe multiple actual simultaneous casters")); return; }
  FanoutNaturalStarted=StartedCount; FanoutNaturalEnded=Ended; FanoutNaturalContacts=Contacts; FanoutNaturalEffects=Effects; FanoutNaturalLosses=Losses;
  // Reuse the single-caster fixture's supported Mover teleport preparation after root motion.
  // The next burst waits for the actual new physical frame; no pose/trace/Health is fabricated.
  for (int32 I=0;I<32;++I)
  {
   if (!IsCombatFanoutCurrent(I,Epoch) || !FanoutGroups[I].Caster->GetMover()) { Finish(false,TEXT("Fanout post-attack Mover missing")); return; }
   FanoutGroups[I].PreparationFrame=FanoutGroups[I].Caster->GetMover()->GetLastTimeStep().ServerFrame;
   auto Teleport=MakeShared<FTeleportEffect>(); Teleport->TargetLocation=FVector(-17500+(I%8)*5000,-7500+(I/8)*5000,100);
   Teleport->bUseActorRotation=false; Teleport->TargetRotation=FRotator::ZeroRotator;
   FanoutGroups[I].Caster->GetMover()->QueueInstantMovementEffect(Teleport);
  }
  Stage=52; StageStarted=FPlatformTime::Seconds(); bFanoutBurstActivated=false;
  return;
 }
 const bool Cleaned=CleanupCombatFanout();
 FanoutSummary=TEXT("groups,natural_started,natural_ended,natural_contacts,natural_effects,natural_health_losses,cancel_started,cancel_ended,cancel_contacts,cancel_effects,peak_montages,peak_attack_contexts,peak_concurrent,cleanup,trace_started,trace_stopped,passed,scope\n");
 FanoutSummary+=FString::Printf(TEXT("32,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,authority_actual_combat_not_client_damage\n"),
  FanoutNaturalStarted,FanoutNaturalEnded,FanoutNaturalContacts,FanoutNaturalEffects,FanoutNaturalLosses,StartedCount,Ended,Contacts,Effects,
  FanoutPeakMontages,FanoutPeakAttacks,FanoutPeakConcurrent,Cleaned,bFanoutTraceStarted,bFanoutTraceStopped,Cleaned && !bFanoutViolation);
 if (!Cleaned) { Finish(false,TEXT("Fanout failed to return registry/trace resources to baseline")); return; }
 Stage=6; StageStarted=FPlatformTime::Seconds(); Clients[1]->ClientFinish(true);
}
bool AProject_JNetworkLoadGameMode::CleanupCombatFanout()
{
 if (FanoutGroups.IsEmpty() && FanoutOwnedActors.IsEmpty())
 {
  FTraceAuxiliary::OnConnection.Remove(FanoutTraceConnectionHandle); FanoutTraceConnectionHandle.Reset(); FanoutTraceReady.Reset();
  return bFanoutCleanupPassed;
 }
 ++FanoutEpoch;
 const TStrongObjectPtr<ThisClass> KeepSelf(this);
 const TStrongObjectPtr<UWorld> KeepWorld(GetWorld());
 const TStrongObjectPtr<UProject_JNPCDecisionSubsystem> Registry(KeepWorld.IsValid() ? KeepWorld->GetSubsystem<UProject_JNPCDecisionSubsystem>() : nullptr);
 const auto RegistryCurrent=[this,&KeepWorld,&Registry]
 { return KeepWorld.IsValid() && GetWorld()==KeepWorld.Get() && !KeepWorld->bIsTearingDown && Registry.IsValid() && !Registry->HasAnyFlags(RF_BeginDestroyed|RF_FinishDestroyed) &&
   Registry->GetWorld()==KeepWorld.Get() && KeepWorld->GetSubsystem<UProject_JNPCDecisionSubsystem>()==Registry.Get(); };
 FTraceAuxiliary::OnConnection.Remove(FanoutTraceConnectionHandle); FanoutTraceConnectionHandle.Reset(); FanoutTraceReady.Reset();
 auto Groups=MoveTemp(FanoutGroups); auto Actors=MoveTemp(FanoutOwnedActors); auto Targets=MoveTemp(FanoutRegisteredTargets);
 TArray<TStrongObjectPtr<AActor>,TInlineAllocator<160>> Pins; for (const auto& A:Actors) if (IsValid(A)) Pins.Emplace(A.Get());
 for (auto& G:Groups)
 {
  if (auto* ASC=G.Source.Get()) { ASC->RemoveGameplayEventTagContainerDelegate(FGameplayTagContainer(CleaveHitTag),G.ContactHandle); ASC->OnAbilityEnded.Remove(G.EndHandle); }
  for (int32 S=0;S<3;++S) if (auto* ASC=G.Recipients[S].Get()) ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(G.EffectHandles[S]);
 }
 bool Released=true;
 for (auto& G:Groups)
 {
  const TStrongObjectPtr<UProject_JNPCActivationComponent> Activation(G.Activation.Get());
  const TStrongObjectPtr<UAbilitySystemComponent> ASC(G.Source.Get());
  if (Activation.IsValid()) Activation->DeactivateNPC();
  if (Activation.IsValid() && Activation->HasOwnedActivation()) Released=false;
  if (G.Hit.IsValid() && G.Hit->GetActiveAttackDefinition()) Released=false;
  if (G.Action.IsValid() && G.Action->GetActionState()!=EProjectJNPCActionState::Disabled) Released=false;
  if (ASC.IsValid() && G.Grant.IsValid() && ASC->FindAbilitySpecFromHandle(G.Grant)) Released=false;
 }
 for (const auto& Target:Targets)
 {
  if (!RegistryCurrent()) { Released=false; break; }
  Registry->UnregisterTarget(Target.Get());
 }
 for (const auto& A:Pins) if (IsValid(A.Get()) && !A->IsActorBeingDestroyed()) A->Destroy();
 Released &= RegistryCurrent() && Registry->GetAgentCount()==FanoutBaseAgents && Registry->GetActionCount()==FanoutBaseActions && Registry->GetTargetCount()==FanoutBaseTargets;
 if (bFanoutTraceStarted && !bFanoutTraceStopped) bFanoutTraceStopped=FTraceAuxiliary::Stop();
 Released &= !bFanoutTraceStarted || bFanoutTraceStopped;
 bFanoutCleanupPassed=Released;
 CleaveDefinition=nullptr; CleaveAttack=nullptr;
 return Released;
}
