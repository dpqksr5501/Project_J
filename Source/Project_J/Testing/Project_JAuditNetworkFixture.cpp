#include "Testing/Project_JAuditNetworkFixture.h"
#include "AbilitySystemComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Game/Project_JPlayerState.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Combat/Project_JCombatPresentationSet.h"
#include "Combat/Project_JServerSideRewindComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "NativeGameplayTags.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "TimerManager.h"
#include "AbilitySystemInterface.h"
#include "EngineUtils.h"
#include "Project_JGameplayTags.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Components/Project_JReplicatedAnimEventComponent.h"
#include "Components/Project_JSkillInputExecutionComponent.h"
#include "Combat/Project_JGameplayAbility_Melee.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Combat/Project_JComboDefinition.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/PackageName.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(AuditNodeA,"ProjectJ.Tests.AuditNetwork.NodeA");
UE_DEFINE_GAMEPLAY_TAG_STATIC(AuditNodeB,"ProjectJ.Tests.AuditNetwork.NodeB");
UE_DEFINE_GAMEPLAY_TAG_STATIC(AuditCueA,"ProjectJ.Tests.AuditNetwork.CueA");
UE_DEFINE_GAMEPLAY_TAG_STATIC(AuditCueB,"ProjectJ.Tests.AuditNetwork.CueB");
UE_DEFINE_GAMEPLAY_TAG_STATIC(AuditRateInput,"InputTag.SourceAudit.NetworkRate");
DEFINE_LOG_CATEGORY_STATIC(LogProjectJAuditNetwork,Log,All);
namespace {
bool AuditEnabled() {
#if UE_BUILD_SHIPPING
 return false;
#else
 return FParse::Param(FCommandLine::Get(),TEXT("ProjectJAuditNetwork"));
#endif
}
FString AuditOutput() { FString P;FParse::Value(FCommandLine::Get(),TEXT("ProjectJAuditOutput="),P);return P; }
void WriteAudit(const FString& Name,const FString& Body) {
 const auto Root=AuditOutput();if(Root.IsEmpty())return;
 IFileManager::Get().MakeDirectory(*Root,true);FFileHelper::SaveStringToFile(Body,*(Root/Name));
}
float Health(AActor* Actor) { const auto* I=Cast<IAbilitySystemInterface>(Actor);const auto* ASC=I?I->GetAbilitySystemComponent():nullptr;return ASC?ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()):-999.f; }
FVector TraceStart(AActor* A){return A->GetActorLocation()+FVector(20,0,0);}
FVector TraceEnd(AActor* A){return A->GetActorLocation()+FVector(140,0,0);}
void FreezeAuditKinematics(ACharacter* C){C->GetCharacterMovement()->SetMovementMode(MOVE_Flying);C->GetCharacterMovement()->SetComponentTickEnabled(false);}
}
UProject_JAuditNetworkDamage::UProject_JAuditNetworkDamage() {
 DurationPolicy=EGameplayEffectDurationType::Instant;
 auto& M=Modifiers.AddDefaulted_GetRef();M.Attribute=UProject_JAttributeSet::GetHealthAttribute();M.ModifierOp=EGameplayModOp::Additive;M.ModifierMagnitude=FScalableFloat(-10.f);
}
void UProject_JAuditNetworkHit::ServerRequestSSRHit_Implementation(AActor* T,float Time,FVector Start,FVector End,FGameplayTag Node,int32 Sequence,int32 Key) {
 if(!AuditEnabled())return;
 const float Before=Health(T);const auto Current=GetActiveAttackNodeTag();
 // This is the actual inherited Server RPC validation/apply path, not a direct
 // ValidateServerHitRequest/history-query substitute for network damage.
 Super::ServerRequestSSRHit_Implementation(T,Time,Start,End,Node,Sequence,Key);
 if(auto* M=GetWorld()->GetAuthGameMode<AProject_JAuditNetworkGameMode>())M->RecordHit(Sequence,Time,Before,Health(T),Node,Current);
}
AProject_JAuditNetworkPlayer::AProject_JAuditNetworkPlayer(){AuditHit=CreateDefaultSubobject<UProject_JAuditNetworkHit>(TEXT("AuditSSR"));bAlwaysRelevant=true;}
void AProject_JAuditNetworkPlayer::BeginPlay(){
 Super::BeginPlay();if(!AuditEnabled())return;FreezeAuditKinematics(this);SetActorTickEnabled(false);
 GetMesh()->SetSkeletalMeshAsset(LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple")));
 GetMesh()->SetAnimInstanceClass(UAnimInstance::StaticClass());
 GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
 GetMesh()->bEnableUpdateRateOptimizations=false;
}
AProject_JAuditNetworkTarget::AProject_JAuditNetworkTarget(){CreateDefaultSubobject<UProject_JServerSideRewindComponent>(TEXT("AuditRewind"));bAlwaysRelevant=true;}
void AProject_JAuditNetworkTarget::BeginPlay(){Super::BeginPlay();if(AuditEnabled()){FreezeAuditKinematics(this);SetNetUpdateFrequency(30.f);}}
AProject_JAuditCueProbe::AProject_JAuditCueProbe(){Presentation=CreateDefaultSubobject<UProject_JCombatPresentationComponent>(TEXT("AuditPresentation"));bAlwaysRelevant=true;}
void AProject_JAuditCueProbe::BeginPlay(){
 Super::BeginPlay();if(!AuditEnabled())return;FreezeAuditKinematics(this);SetNetUpdateFrequency(30.f);
 auto* Set=NewObject<UProject_JCombatPresentationSet>(this);auto* Profile=NewObject<UProject_JAttackPresentationProfile>(Set);
 auto* System=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/SlashTrail_SoftTofu/Niagara/Basic/NS_SlashTrail_Basic.NS_SlashTrail_Basic"));
 for(const FGameplayTag Tag:{FGameplayTag(AuditCueA),FGameplayTag(AuditCueB)}){auto& Cue=Profile->Cues.AddDefaulted_GetRef();Cue.CueTag=Tag;Cue.NiagaraSystem=System;Cue.bLooping=true;Cue.bDestroyImmediatelyOnStop=true;Cue.AttachmentTarget=EProject_JCombatVFXAttachmentTarget::World;}
 auto& Entry=Set->AttackPresentations.AddDefaulted_GetRef();Entry.AttackTag=AuditNodeA;Entry.Profile=Profile;Presentation->BasePresentationSet=Set;
}
void AProject_JAuditNetworkController::ClientPrepare_Implementation(int32 InCase,AProject_JAuditNetworkPlayer* A,AProject_JAuditNetworkTarget* T,AProject_JAuditCueProbe* P){
 if(!AuditEnabled())return;Case=InCase;Attacker=A;Target=T;Probe=P;bPrepared=false;bCheckHealth=false;bObserveCue=false;Remaining=0;MovementExpectation=INDEX_NONE;RatePhase=0;
 FParse::Value(FCommandLine::Get(),TEXT("ProjectJAuditSlot="),Slot);
 UE_LOG(LogProjectJAuditNetwork,Display,TEXT("CLIENT_PREPARE case=%d A=%s T=%s P=%s"),Case,*GetNameSafe(A),*GetNameSafe(T),*GetNameSafe(P));
}
void AProject_JAuditNetworkController::ClientSendHit_Implementation(int32 InCase,float Time){if(Case!=InCase)return;HitTime=Time;Sequence=InCase*100;Remaining=6;NextSend=0;}
void AProject_JAuditNetworkController::ClientCheckHealth_Implementation(int32 InCase,float Expected){if(Case!=InCase)return;ExpectedHealth=Expected;bCheckHealth=true;ObserveStarted=FPlatformTime::Seconds();}
void AProject_JAuditNetworkController::ClientObserveCue_Implementation(int32 InCase,int32 Order,bool A,bool B,bool Revision){if(Case!=InCase)return;CueOrder=Order;bExpectA=A;bExpectB=B;bRequireRevision=Revision;bObserveCue=true;ObserveStarted=FPlatformTime::Seconds();}
void AProject_JAuditNetworkController::PlayerTick(float Delta){
 Super::PlayerTick(Delta);if(!AuditEnabled()||HasAuthority()||Case==INDEX_NONE)return;
 // Reliable control references can precede the corresponding actor channel.
 // Resolve the fixture's unique probe types once their real replicas exist.
 if(!Target){for(TActorIterator<AProject_JAuditNetworkTarget> It(GetWorld());It;++It){Target=*It;break;}}
 if(!Probe){for(TActorIterator<AProject_JAuditCueProbe> It(GetWorld());It;++It){Probe=*It;break;}}
 if(!bPrepared&&FMath::Fmod(GetWorld()->GetTimeSeconds(),1.f)<Delta){
  UE_LOG(LogProjectJAuditNetwork,Display,TEXT("CLIENT_WAIT case=%d A=%s T=%s P=%s ASC=%d TargetASC=%d Set=%d TBegin=%d PBegin=%d"),Case,*GetNameSafe(Attacker),*GetNameSafe(Target),*GetNameSafe(Probe),Attacker&&Attacker->GetAbilitySystemComponent()!=nullptr,Target&&Target->GetAbilitySystemComponent()!=nullptr,Probe&&Probe->Presentation->BasePresentationSet!=nullptr,Target&&Target->HasActorBegunPlay(),Probe&&Probe->HasActorBegunPlay());
 }
 if(!bPrepared&&Attacker&&Target&&Probe&&Attacker->GetAbilitySystemComponent()&&Target->GetAbilitySystemComponent()&&Probe->Presentation->BasePresentationSet){
  bPrepared=true;ServerObserve(Case,0,true,FString::Printf(TEXT("netmode=%d ownrole=%d attackerrole=%d owner=%d"),int32(GetNetMode()),int32(GetPawn()?GetPawn()->GetLocalRole():ROLE_None),int32(Attacker->GetLocalRole()),GetPawn()==Attacker));
 }
 const double Now=FPlatformTime::Seconds();
 if(Remaining>0&&Attacker&&Now>=NextSend){
  if(GetPawn()!=Attacker){Remaining=0;return;}
  Attacker->AuditHit->ServerRequestSSRHit(Target,HitTime,TraceStart(Attacker),TraceEnd(Attacker),AuditNodeA,++Sequence,17);
  --Remaining;NextSend=Now+.035;
 }
 if(bCheckHealth&&Target&&(FMath::IsNearlyEqual(Health(Target),ExpectedHealth)||Now-ObserveStarted>8)){
  bCheckHealth=false;ServerObserve(Case,2,FMath::IsNearlyEqual(Health(Target),ExpectedHealth),FString::Printf(TEXT("health=%.1f expected=%.1f attackerrole=%d"),Health(Target),ExpectedHealth,int32(Attacker->GetLocalRole())));
 }
 if(bObserveCue&&Probe){auto* C=Probe->Presentation.Get();
  const bool Event=C->LastAppliedPresentationEventOrder>=CueOrder;
  const bool A=C->ActiveLoopingCues.Contains(AuditCueA),B=C->ActiveLoopingCues.Contains(AuditCueB);
  const bool Complete=Event&&(!bRequireRevision||(A==bExpectA&&B==bExpectB&&C->LastAppliedPresentationRevision>=Case*10+3));
  if(Complete||Now-ObserveStarted>8){bObserveCue=false;
   ServerObserve(Case,bRequireRevision?2:1,Complete,FString::Printf(TEXT("event=%d revision=%d recovery=%d A=%d B=%d mode=%d"),C->LastAppliedPresentationEventOrder,C->LastAppliedPresentationRevision,C->LastAppliedRecoveryEventOrder,A,B,int32(GetNetMode())));
  }
 }
 if(MovementExpectation!=INDEX_NONE&&Attacker){
  auto* ASC=Attacker->GetAbilitySystemComponent();const int32 Count=ASC?ASC->GetTagCount(FProject_JGameplayTags::Get().State_Movement_InAir):-1;
  auto* State=Attacker->GetLocomotionAnimStateComponent();
  const bool Semantic=GetPawn()==Attacker||(State&&(MovementExpectation?State->bIsInAir:State->LastConfirmedRemoteLandingSequence>=1&&!State->bInAirGameplayTagRequested));
  const bool Match=Semantic&&(MovementExpectation?Count>0:Count==0);
  if(Match||Now-ObserveStarted>8){
   const int32 Expected=MovementExpectation;MovementExpectation=INDEX_NONE;
   ServerObserve(Case,Expected?1:2,Match,FString::Printf(TEXT("movement role=%d InAir=%d expected_falling=%d"),int32(Attacker->GetLocalRole()),Count,Expected));
  }
 }
 if(RatePhase>0&&Attacker){
  if(!RateStyle&&!ConfigureRate(Attacker))return;
  if(GetPawn()==Attacker){
   auto* ASC=Cast<UProject_JAbilitySystemComponent>(Attacker->GetAbilitySystemComponent());
   if(RatePhase==1&&!bRateStarted&&ASC&&ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_CombatMode)){
    for(auto& Spec:ASC->GetActivatableAbilities())if(Spec.GetDynamicSpecSourceTags().HasTagExact(AuditRateInput)){
     if(auto* Input=Attacker->FindComponentByClass<UProject_JSkillInputExecutionComponent>()){Input->HandleInputTagPressed(AuditRateInput);bRateStarted=true;}break;
    }
   }
   if(RatePhase==2&&!bRateAdvanced){bRateAdvanced=AdvanceRate(Attacker);}
  }
  const float Rate=ReadRate(Attacker);const bool Match=FMath::IsNearlyEqual(Rate,float(RatePhase));
  if(Match||Now-ObserveStarted>8){const int32 Phase=RatePhase;RatePhase=0;
   ServerObserve(Case,Phase,Match,FString::Printf(TEXT("montage role=%d owner=%d rate=%.3f expected=%d advanced=%d"),int32(Attacker->GetLocalRole()),GetPawn()==Attacker,Rate,Phase,bRateAdvanced));
  }
 }
}
void AProject_JAuditNetworkController::ClientObserveMovement_Implementation(int32 InCase,bool bInFalling){
 if(Case!=InCase)return;MovementExpectation=bInFalling?1:0;ObserveStarted=FPlatformTime::Seconds();
}
void AProject_JAuditNetworkController::ClientRate_Implementation(int32 InCase,bool bInAdvance){
 if(Case!=InCase)return;RatePhase=bInAdvance?2:1;ObserveStarted=FPlatformTime::Seconds();
}
bool AProject_JAuditNetworkController::ConfigureRate(AProject_JAuditNetworkPlayer* InPlayer){
 if(!InPlayer||!InPlayer->GetAbilitySystemComponent())return false;
 FString MontagePath;FParse::Value(FCommandLine::Get(),TEXT("ProjectJAuditMontage="),MontagePath);
 if(MontagePath.IsEmpty())return false;
 const FString StylePackage=FPackageName::GetLongPackagePath(MontagePath)/TEXT("DA_AuditStyle");
 RateStyle=LoadObject<UProject_JCombatStyleDefinition>(nullptr,*(StylePackage+TEXT(".DA_AuditStyle")));
 if(!RateStyle||!RateStyle->ComboDefinition||RateStyle->ComboDefinition->Nodes.Num()!=2)return false;
 InPlayer->SetCurrentEquipmentConfiguration(RateStyle,InPlayer->GetCurrentWeaponPresentationProfile());
 auto* ASC=Cast<UProject_JAbilitySystemComponent>(InPlayer->GetAbilitySystemComponent());
 if(InPlayer->HasAuthority()){
  ASC->CancelAllAbilities();
  ASC->AddProjectJLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
  FGameplayAbilitySpec Spec(UProject_JGameplayAbility_Melee::StaticClass());Spec.GetDynamicSpecSourceTags().AddTag(AuditRateInput);ASC->GiveAbility(Spec);
 }
 return true;
}
float AProject_JAuditNetworkController::ReadRate(AProject_JAuditNetworkPlayer* InPlayer)const{
 auto* Anim=InPlayer?InPlayer->GetMesh()->GetAnimInstance():nullptr;
 auto* Montage=RateStyle&&RateStyle->ComboDefinition?RateStyle->ComboDefinition->Nodes[0].AttackDefinition->Montage.Get():nullptr;
 return Anim&&Montage&&Anim->Montage_IsPlaying(Montage)?Anim->Montage_GetPlayRate(Montage):-1.f;
}
bool AProject_JAuditNetworkController::AdvanceRate(AProject_JAuditNetworkPlayer* InPlayer){
 auto* ASC=InPlayer?InPlayer->GetAbilitySystemComponent():nullptr;if(!ASC||!RateStyle)return false;
 for(auto& Spec:ASC->GetActivatableAbilities()){
  if(!Spec.GetDynamicSpecSourceTags().HasTagExact(AuditRateInput))continue;
  auto* Ability=Cast<UProject_JGameplayAbility_Melee>(Spec.GetPrimaryInstance());if(!Ability||!Ability->IsActive())return false;
  auto* Task=Ability->MontageTask;Ability->StartComboNode(RateStyle->ComboDefinition->Nodes[1]);
  const bool Preserved=Task&&Ability->MontageTask==Task;
  UE_LOG(LogProjectJAuditNetwork,Display,TEXT("RATE_ADVANCE role=%d authority=%d task_preserved=%d rate=%.3f"),int32(InPlayer->GetLocalRole()),InPlayer->HasAuthority(),Preserved,ReadRate(InPlayer));
  return Preserved&&FMath::IsNearlyEqual(ReadRate(InPlayer),2.f);
 }return false;
}
void AProject_JAuditNetworkController::ServerObserve_Implementation(int32 InCase,int32 Step,bool Success,const FString& Detail){if(auto* M=GetWorld()->GetAuthGameMode<AProject_JAuditNetworkGameMode>())M->Observe(this,InCase,Step,Success,Detail);}
void AProject_JAuditNetworkController::ClientFinish_Implementation(bool Success){
 WriteAudit(FString::Printf(TEXT("client_%d.txt"),Slot),FString::Printf(TEXT("success=%d netmode=%d local_role=%d\n"),Success,int32(GetNetMode()),int32(GetPawn()?GetPawn()->GetLocalRole():ROLE_None)));
 FPlatformMisc::RequestExitWithStatus(false,Success?0:1);
}
void AProject_JAuditNetworkController::SendCueDelta(AProject_JAuditCueProbe* P,bool bStart,int32 Order,uint32 Instance,const FProject_JReplicatedCombatPresentationState& Snapshot){
 // Deliberately send the real multicast before the full
 // replicated property. The omitted A delta models one lost cue independently
 // of the packet simulator's additional actual lag/loss/reordering.
 P->Presentation->MulticastPlayPresentationCue(AuditNodeA,AuditCueB,bStart,Order,Instance,Snapshot);
}
void AProject_JAuditNetworkController::PublishCueState(AProject_JAuditCueProbe* P,const FProject_JReplicatedCombatPresentationState& State){P->Presentation->ReplicatedPresentationState=State;P->ForceNetUpdate();}
AProject_JAuditNetworkGameMode::AProject_JAuditNetworkGameMode(){PrimaryActorTick.bCanEverTick=true;PlayerControllerClass=AProject_JAuditNetworkController::StaticClass();DefaultPawnClass=nullptr;}
void AProject_JAuditNetworkGameMode::BeginPlay(){
 Super::BeginPlay();Started=FPlatformTime::Seconds();if(!AuditEnabled()){Finish(false,TEXT("Explicit fixture flag required"));return;}
 FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;P.ObjectFlags|=RF_Transient;
 Target=GetWorld()->SpawnActor<AProject_JAuditNetworkTarget>(AProject_JAuditNetworkTarget::StaticClass(),FVector(120,0,100),FRotator::ZeroRotator,P);
 Probe=GetWorld()->SpawnActor<AProject_JAuditCueProbe>(AProject_JAuditCueProbe::StaticClass(),FVector(500,0,100),FRotator::ZeroRotator,P);
 Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(),100.f);
 Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(),100.f);
 Attack=NewObject<UProject_JAttackDefinition>(this);Attack->AttackTag=AuditNodeA;Attack->DamageEffect=UProject_JAuditNetworkDamage::StaticClass();Attack->HitSpec.TraceRadius=20.f;
 auto* Driver=GetWorld()->GetNetDriver();
#if DO_ENABLE_NET_TEST
 const auto& Sim=Driver->PacketSimulationSettings;
 UE_LOG(LogProjectJAuditNetwork,Display,TEXT("PACKETS lag=%d variance=%d loss=%d order=%d duplicate=%d netmode=%d"),Sim.PktLag,Sim.PktLagVariance,Sim.PktLoss,Sim.PktOrder,Sim.PktDup,int32(GetNetMode()));
#endif
}
void AProject_JAuditNetworkGameMode::PostLogin(APlayerController* New){
 Super::PostLogin(New);if(!AuditEnabled())return;
 auto* PC=CastChecked<AProject_JAuditNetworkController>(New);PC->Slot=Clients.Num();Clients.Add(PC);
 if(Clients.Num()>2){Finish(false,TEXT("Unexpected third client"));return;}
 FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;P.Owner=PC;P.ObjectFlags|=RF_Transient;
 auto* Player=GetWorld()->SpawnActor<AProject_JAuditNetworkPlayer>(AProject_JAuditNetworkPlayer::StaticClass(),FVector(0,PC->Slot*2000,100),FRotator::ZeroRotator,P);
 PC->Possess(Player);if(PC->Slot==0)Attacker=Player;
 auto* PS=PC->GetPlayerState<AProject_JPlayerState>();
 auto* Item=LoadObject<UProject_JEquipmentItemDefinition>(nullptr,TEXT("/Game/DataAssetSets/Animation_Profiles/Equip/DA_Greatsword_Equip.DA_Greatsword_Equip"));
 if(!PS||!Item){Finish(false,TEXT("Production persistent equipment fixture missing"));return;}
 const auto Added=PS->GetInventoryComponent()->AddItemDefinition(Item);
 if(!PS->GetEquipmentManagerComponent()->TryEquipItemInstanceById(Added.InstanceId).bSucceeded){Finish(false,TEXT("Weapon equip failed"));}
}
bool AProject_JAuditNetworkGameMode::AllConfirmed(int32 InStep)const{for(AProject_JAuditNetworkController* PC:Clients)if(PC->ConfirmedCase!=Case||PC->ConfirmedStep!=InStep)return false;return Clients.Num()==2;}
void AProject_JAuditNetworkGameMode::StartCase(int32 Next){
 Case=Next;CaseStarted=StepStarted=FPlatformTime::Seconds();Step=0;bSent=false;bAuthorityHit=false;
 Attacker->AuditHit->EndAttack();Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(),100.f);
 // Reset the live presentation through production APIs and let both real
 // clients acknowledge the empty boundary before starting another trial.
 Probe->Presentation->EndAttackPresentation();
 for(AProject_JAuditNetworkController* PC:Clients){PC->ConfirmedCase=PC->ConfirmedStep=INDEX_NONE;PC->ClientPrepare(Case,Attacker,Target,Probe);}
 UE_LOG(LogProjectJAuditNetwork,Display,TEXT("CASE_BEGIN %d Attacker=%s pos=%s Target=%s begun=%d pos=%s Probe=%s begun=%d pos=%s"),Case,*GetNameSafe(Attacker),*Attacker->GetActorLocation().ToString(),*GetNameSafe(Target),Target->HasActorBegunPlay(),*Target->GetActorLocation().ToString(),*GetNameSafe(Probe),Probe->HasActorBegunPlay(),*Probe->GetActorLocation().ToString());
}
void AProject_JAuditNetworkGameMode::Tick(float Delta){
 Super::Tick(Delta);if(!AuditEnabled()||bFinished)return;const double Now=FPlatformTime::Seconds();
 if(Now-Started>140){Finish(false,TEXT("Fixture deadline; no forced process termination"));return;}
 if(Case==INDEX_NONE){if(Clients.Num()==2&&Attacker&&Attacker->FindComponentByClass<UProject_JEquipmentRuntimeComponent>()->GetWeaponRevision()!=0&&Now-Started>3)StartCase(0);return;}
 if(Now-CaseStarted>20){Finish(false,FString::Printf(TEXT("Case %d step %d timed out"),Case,Step));return;}
 if(Step==0&&Now-StepStarted>1){
  for(AProject_JAuditNetworkController* PC:Clients)if(PC->ConfirmedCase!=Case||PC->ConfirmedStep!=0)PC->ClientPrepare(Case,Attacker,Target,Probe);
  StepStarted=Now;
 }
 auto* Hit=Attacker->AuditHit.Get();
 if(Case<6){
  if(Step==0&&AllConfirmed(0)){Hit->BeginAttackNode(AuditNodeA,Attack,17);Hit->SetHitWindowOpen(true);Hit->RecordAuthoritativeTrace(TraceStart(Attacker),TraceEnd(Attacker));Step=1;StepStarted=Now;}
  if(Hit->GetActiveAttackNodeTag().IsValid())Hit->RecordAuthoritativeTrace(TraceStart(Attacker),TraceEnd(Attacker));
  if(Step==1&&Now-StepStarted>.15){
   SampleTime=GetWorld()->GetTimeSeconds()-.03f;
   if(Case==1){bAuthorityHit=Hit->ProcessAuthorityHit(Target);if(!bAuthorityHit||!FMath::IsNearlyEqual(Health(Target),90.f)){Finish(false,TEXT("Authority damage before transition did not apply exactly once"));return;}}
   Clients[0]->ClientSendHit(Case,SampleTime);
   if(Case==1||Case==2||Case==3){Hit->BeginAttackNode(AuditNodeB,Attack,17);Hit->SetHitWindowOpen(true);Hit->RecordAuthoritativeTrace(TraceStart(Attacker),TraceEnd(Attacker));}
   if(Case==3){Hit->BeginAttackNode(AuditNodeA,Attack,17);Hit->SetHitWindowOpen(true);Hit->RecordAuthoritativeTrace(TraceStart(Attacker),TraceEnd(Attacker));}
   if(Case==4)Hit->EndAttack();
   Step=2;StepStarted=Now;
  }
  if(Step==2&&Now-StepStarted>1.2){
   const float Expected=(Case==0||Case==1||Case==5)?90.f:100.f;
   const int32 ExpectedApplications=(Case==0||Case==5)?1:0;
   if(Arrivals.FindRef(Case)==0||DamageApplications.FindRef(Case)!=ExpectedApplications||!FMath::IsNearlyEqual(Health(Target),Expected)){
    Finish(false,FString::Printf(TEXT("SSR case=%d arrivals=%d applications=%d expected=%d health=%.1f expected=%.1f"),Case,Arrivals.FindRef(Case),DamageApplications.FindRef(Case),ExpectedApplications,Health(Target),Expected));return;
   }
   for(AProject_JAuditNetworkController* PC:Clients)PC->ClientCheckHealth(Case,Expected);Step=3;
  }
  if(Step==3&&AllConfirmed(2)){StartCase(Case==5?10:Case+1);}return;
 }
 if(Case==10||Case==11){
  const int32 Base=Case*10;
  if(Case==11&&Step==0&&AllConfirmed(0)){
   FProject_JReplicatedCombatPresentationState Baseline;Baseline.ActiveAttackTag=AuditNodeA;Baseline.AttackInstance=Case+1;Baseline.Revision=Base+3;Baseline.EventOrder=Base+2;
   Baseline.ActiveLoopingCueTags.AddTag(AuditCueA);Baseline.ActiveLoopingCueTags.AddTag(AuditCueB);
   Clients[0]->PublishCueState(Probe,Baseline);
   for(AProject_JAuditNetworkController* PC:Clients)PC->ClientObserveCue(Case,Base+2,true,true,true);
   Step=4;
  }
  if((Case==10&&Step==0&&AllConfirmed(0))||(Case==11&&Step==4&&AllConfirmed(2))){
   Pending={};Pending.ActiveAttackTag=AuditNodeA;Pending.AttackInstance=Case+1;Pending.Revision=Base+(Case==10?3:4);Pending.EventOrder=Base+4;
   Pending.ActiveLoopingCueTags.AddTag(AuditCueB);
   if(Case==10)Pending.ActiveLoopingCueTags.AddTag(AuditCueA);
   Clients[0]->SendCueDelta(Probe,true,Base+5,Pending.AttackInstance,Pending);
   for(AProject_JAuditNetworkController* PC:Clients)PC->ClientObserveCue(Case,Base+5,Case==10,true,false);
   Step=1;StepStarted=Now;
  }
  // Retransmit the same semantic event/order. A dropped RPC must be observed
  // before the property is published; receipts prove this actual wire order.
  if(Step==1&&!AllConfirmed(1)&&Now-StepStarted>.3){Clients[0]->SendCueDelta(Probe,true,Base+5,Pending.AttackInstance,Pending);StepStarted=Now;}
  if(Step==1&&AllConfirmed(1)){
   Clients[0]->PublishCueState(Probe,Pending);
   for(AProject_JAuditNetworkController* PC:Clients)PC->ClientObserveCue(Case,Base+5,Case==10,true,true);
   Step=2;
  }
  if(Step==2&&AllConfirmed(2)){if(Case==10)StartCase(11);else StartCase(20);}
 }
 if(Case==20){
  auto* State=Attacker->GetLocomotionAnimStateComponent();auto* Events=Attacker->FindComponentByClass<UProject_JReplicatedAnimEventComponent>();
  if(Step==0&&AllConfirmed(0)){
   State->HandleReplicatedFallOffStarted();Events->DispatchFallOffStarted();
   for(AProject_JAuditNetworkController* PC:Clients)PC->ClientObserveMovement(Case,true);Step=1;
  }
  if(Step==1&&AllConfirmed(1)){
   State->HandleReplicatedLandingStarted(1,0.f,600.f,false,false,false);Events->DispatchLandingStarted(600.f,false,false,false);
   for(AProject_JAuditNetworkController* PC:Clients)PC->ClientObserveMovement(Case,false);Step=2;
  }
  if(Step==2&&AllConfirmed(2)){
   const int32 Count=Attacker->GetAbilitySystemComponent()->GetTagCount(FProject_JGameplayTags::Get().State_Movement_InAir);
   if(Count!=0){Finish(false,TEXT("Authority InAir contribution survived remote landing"));return;}StartCase(30);
  }
 }
 if(Case==30){
  if(Step==0&&AllConfirmed(0)){
   if(!Clients[0]->ConfigureRate(Attacker)){Finish(false,TEXT("Isolated saved rate style/montage missing"));return;}
   for(AProject_JAuditNetworkController* PC:Clients)PC->ClientRate(Case,false);Step=1;
  }
  if(Step==1&&AllConfirmed(1)){
   if(!FMath::IsNearlyEqual(Clients[0]->ReadRate(Attacker),1.f)||!Clients[0]->AdvanceRate(Attacker)){Finish(false,TEXT("Authority rate/task transition failed"));return;}
   for(AProject_JAuditNetworkController* PC:Clients)PC->ClientRate(Case,true);Step=2;
  }
  if(Step==2&&AllConfirmed(2))Finish(true,TEXT("SSR damage, cue recovery, remote landing and montage rates observed on authority/owner/simulated roles"));
 }
}
void AProject_JAuditNetworkGameMode::Observe(AProject_JAuditNetworkController* PC,int32 InCase,int32 InStep,bool Success,const FString& Detail){
 UE_LOG(LogProjectJAuditNetwork,Display,TEXT("OBSERVE slot=%d case=%d step=%d success=%d %s"),PC->Slot,InCase,InStep,Success,*Detail);
 if(bFinished||InCase!=Case||!Clients.Contains(PC))return;
 if(!Success){Finish(false,FString::Printf(TEXT("Client %d case %d step %d: %s"),PC->Slot,InCase,InStep,*Detail));return;}
 PC->ConfirmedCase=InCase;PC->ConfirmedStep=InStep;
}
void AProject_JAuditNetworkGameMode::RecordHit(int32 Seq,float Time,float Before,float After,FGameplayTag Node,FGameplayTag Current){
 const int32 RequestCase=Seq/100;++Arrivals.FindOrAdd(RequestCase);if(After<Before)++DamageApplications.FindOrAdd(RequestCase);
 Rows+=FString::Printf(TEXT("%d,%d,%.6f,%.6f,%.6f,%.1f,%.1f,%s,%s\n"),RequestCase,Seq,Time,GetWorld()->GetTimeSeconds(),GetWorld()->GetTimeSeconds()-Time,Before,After,*Node.ToString(),*Current.ToString());
 UE_LOG(LogProjectJAuditNetwork,Display,TEXT("HIT case=%d seq=%d age=%.4f before=%.1f after=%.1f current=%s"),RequestCase,Seq,GetWorld()->GetTimeSeconds()-Time,Before,After,*Current.ToString());
}
void AProject_JAuditNetworkGameMode::Finish(bool Success,const FString& Why){
 if(bFinished)return;bFinished=true;WriteAudit(TEXT("hits.csv"),Rows);WriteAudit(TEXT("server.txt"),FString::Printf(TEXT("success=%d case=%d step=%d mode=%d reason=%s\n"),Success,Case,Step,int32(GetNetMode()),*Why));
 UE_LOG(LogProjectJAuditNetwork,Display,TEXT("FINISH success=%d %s"),Success,*Why);
 for(AProject_JAuditNetworkController* PC:Clients)PC->ClientFinish(Success);
 // Let reliable finish receipts drain under lag/loss. The timeout subsystem
 // independently bounds processes whose connection never succeeds.
 FTimerHandle Handle;GetWorldTimerManager().SetTimer(Handle,[Success](){FPlatformMisc::RequestExitWithStatus(false,Success?0:1);},3.f,false);
}
