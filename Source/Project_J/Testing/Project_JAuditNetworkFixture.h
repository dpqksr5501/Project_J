#pragma once
#include "CoreMinimal.h"
#include "Game/Project_JGameMode.h"
#include "Game/Project_JQuestComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraActor.h"
#include "Project_JSpellbladeCharacter.h"
#include "Project_JNPCCharacter.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Components/Project_JCombatPresentationComponent.h"
#include "GameplayEffect.h"
#include "Project_JAbilitySystemComponent.h"
#include "Iris/ReplicationSystem/NetRefHandle.h"
#include "Project_JAuditNetworkFixture.generated.h"

/** Fixture control camera: native CDO flags select ordinary always-relevant Iris routing. */
UCLASS(NotBlueprintable)
class AProject_JAuditNetworkCamera : public ACameraActor
{
 GENERATED_BODY()
public: AProject_JAuditNetworkCamera();
};

/** Explicit CLI diagnostics. No production map or asset is changed. */
UCLASS(NotBlueprintable)
class UProject_JAuditNetworkDamage : public UGameplayEffect
{
 GENERATED_BODY()
public: UProject_JAuditNetworkDamage();
};
UCLASS(NotBlueprintable)
class UProject_JAuditNetworkHit : public UProject_JCombatHitValidationComponent
{
 GENERATED_BODY()
public:
 virtual void ServerRequestSSRHit_Implementation(AActor* Target,double Time,FVector Start,FVector End,FGameplayTag Node,int32 Sequence,int32 Key) override;
};
UCLASS(NotBlueprintable)
class AProject_JAuditNetworkPlayer : public AProject_JSpellbladeCharacter
{
 GENERATED_BODY()
public:
 AProject_JAuditNetworkPlayer();
 virtual void BeginPlay() override;
 UPROPERTY() TObjectPtr<UProject_JAuditNetworkHit> AuditHit;
};
UCLASS(NotBlueprintable)
class AProject_JAuditNetworkTarget : public AProject_JNPCCharacter
{
 GENERATED_BODY()
public: AProject_JAuditNetworkTarget(); virtual void BeginPlay() override;
};
UCLASS(NotBlueprintable)
class AProject_JAuditCueProbe : public AProject_JNPCCharacter
{
 GENERATED_BODY()
public:
 AProject_JAuditCueProbe(); virtual void BeginPlay() override;
 UPROPERTY() TObjectPtr<UProject_JCombatPresentationComponent> Presentation;
};
class AProject_JPlayerState;
class UProject_JAbilitySystemComponent;
class UProject_JAttributeSet;
class AProject_JMountCharacter;
class ACameraActor;
class AProject_JAuditNetworkController;
class UProject_JNPCActivationDefinition;
/** Bounded CLI-only observations; the production session gate owns authority. */
struct FProjectJAuditSessionOwner
{
 TWeakObjectPtr<AProject_JAuditNetworkController> Controller;
 TWeakObjectPtr<AProject_JPlayerState> State;
 TWeakObjectPtr<AProject_JAuditNetworkPlayer> Pawn;
 TWeakObjectPtr<UProject_JAbilitySystemComponent> ASC;
 ProjectJ::MMO::FServerSessionLease Lease;
 FGameplayAbilitySpecHandle Grant;
 FProject_JGASClientSessionAuthority SavedPermission;
 FDelegateHandle PermissionHandle;
 uint64 AvatarRevision=0;
 int32 Calls=0,Allowed=0,Denied=0;
 int32 PredictionKey=0;
};
/** Local diagnostic identity only. Never replicated or persisted. */
struct FProjectJAuditMountIdentity
{
 TWeakObjectPtr<AProject_JAuditNetworkPlayer> Rider;
 TWeakObjectPtr<AProject_JPlayerState> State;
 TWeakObjectPtr<UProject_JAbilitySystemComponent> ASC;
 TWeakObjectPtr<UProject_JAttributeSet> Attributes;
 uint64 Epoch = 0, TestReplacementEpoch = 0;
 float Health = 0, MaxHealth = 0;
};
/** Opt-in observer state only: native identity/lifetime evidence, never replicated. */
struct FProjectJAuditMountSpatial
{
 TWeakObjectPtr<AProject_JMountCharacter> OldMount;
 TWeakObjectPtr<AProject_JAuditNetworkPlayer> OldRider;
 UE::Net::FNetRefHandle MountHandle,RiderHandle;
 FVector Position=FVector::ZeroVector;
 float MountCull=0,RiderCull=0;
 uint32 OldMountObject=0,OldRiderObject=0;
 bool Flags[6]={};
 bool bWarm=false,bMountDestroyed=false,bRiderDestroyed=false,bRetired=false,bReentryBound=false;
 uint64 Group=0;uint32 GroupsCreated=0;
};

UCLASS(NotBlueprintable)
class AProject_JAuditNetworkController : public APlayerController
{
 GENERATED_BODY()
public:
 virtual void PlayerTick(float Delta) override;
 virtual void UpdateCameraManager(float DeltaSeconds) override;
 virtual void EndPlay(const EEndPlayReason::Type InReason) override;
 UFUNCTION(Client,Reliable) void ClientPrepare(int32 InCase,AProject_JAuditNetworkPlayer* InAttacker,AProject_JAuditNetworkTarget* InTarget,AProject_JAuditCueProbe* InProbe);
 UFUNCTION(Client,Reliable) void ClientSendHit(int32 InCase,double Time);
 UFUNCTION(Client,Reliable) void ClientCheckHealth(int32 InCase,float Expected);
 UFUNCTION(Client,Reliable) void ClientObserveCue(int32 InCase,int32 EventOrder,bool bInExpectA,bool bInExpectB,bool bInRequireRevision);
 UFUNCTION(Client,Reliable) void ClientObserveMovement(int32 InCase,bool bInFalling);
 UFUNCTION(Client,Reliable) void ClientRate(int32 InCase,bool bInAdvance);
 UFUNCTION(Client,Reliable) void ClientMountStep(int32 InCase,int32 InStep,const TArray<uint64>& InPublicIds,const TArray<float>& InPublicResources);
 bool ConfigureRate(AProject_JAuditNetworkPlayer* InPlayer);
 bool AdvanceRate(AProject_JAuditNetworkPlayer* InPlayer);
 float ReadRate(AProject_JAuditNetworkPlayer* InPlayer) const;
 UFUNCTION(Client,Reliable) void ClientWarningStep(int32 InStep,AProject_JAuditCueProbe* InProbe,uint32 InInstance,int32 InOrder);
 UFUNCTION(Client,Reliable) void ClientQuestHistoryStep(int32 InStep,AProject_JPlayerState* OwnState,AProject_JPlayerState* ForeignState);
 UFUNCTION(Client,Reliable) void ClientNPCAssemblyStep(int32 InStep,AProject_JNPCCharacter* InNPC,uint32 InInstance,int32 InOrder);
 FProject_JReplicatedCombatPresentationState ReadNPCAssemblyState(AProject_JNPCCharacter* InNPC,int32& OutPending,int32& OutNiagara) const;
 // Read-only native authority witness; never writes presentation/property state.
 FProject_JReplicatedCombatPresentationState ReadWarningState(AProject_JAuditCueProbe* InProbe) const;
 UFUNCTION(Client,Reliable) void ClientFinish(bool Success);
 UFUNCTION(Client,Reliable) void ClientSessionStep(int32 InStep,AProject_JPlayerState* InState,AProject_JAuditNetworkPlayer* InPawn,FGameplayAbilitySpecHandle InGrant,FGuid InAccount,FGuid InCharacter);
 UFUNCTION(Server,Reliable) void ServerObserve(int32 InCase,int32 Step,bool Success,const FString& Detail);
 void SendCueDelta(AProject_JAuditCueProbe* Probe,bool bStart,int32 Order,uint32 Instance,const FProject_JReplicatedCombatPresentationState& Snapshot);
 void PublishCueState(AProject_JAuditCueProbe* Probe,const FProject_JReplicatedCombatPresentationState& State);
 int32 Slot=INDEX_NONE,ConfirmedCase=INDEX_NONE,ConfirmedStep=INDEX_NONE;
private:
 void ObserveQuestHistoryAudit();
 int32 QuestHistoryStep=INDEX_NONE;
 TWeakObjectPtr<AProject_JPlayerState> QuestHistoryOwnState,QuestHistoryForeignState;
 FGuid QuestHistoryInitialRevision,QuestHistoryReturnedNonce;
 uint64 QuestHistoryStableFrame=MAX_uint64;
 bool bQuestHistoryRecorded=false,bQuestHistoryQuerySent=false,bQuestHistoryPacketsLogged=false;
 FString QuestHistoryRows;
 void ObserveNPCAssemblyAudit();
 int32 NPCAssemblyStep=INDEX_NONE,NPCAssemblyOrder=0;
 uint32 NPCAssemblyInstance=0,NPCAssemblyHitId=0,NPCAssemblyPresentationId=0;
 uint64 NPCAssemblyActorId=0,NPCAssemblyStableFrame=MAX_uint64;
 bool bNPCAssemblyRecorded=false,bNPCAssemblyPacketsLogged=false;
 TWeakObjectPtr<AProject_JNPCCharacter> NPCAssemblyIdentity;
 TWeakObjectPtr<UProject_JCombatHitValidationComponent> NPCAssemblyHit;
 TWeakObjectPtr<UProject_JCombatPresentationComponent> NPCAssemblyPresentation;
 TWeakObjectPtr<ULineBatchComponent> NPCAssemblyMarker;
 FString NPCAssemblyOwner,NPCAssemblyRows;
 void ObserveSessionAudit();
 int32 SessionStep=INDEX_NONE;
 TWeakObjectPtr<AProject_JPlayerState> SessionState;
 TWeakObjectPtr<AProject_JAuditNetworkPlayer> SessionPawn;
 FGameplayAbilitySpecHandle SessionGrant;
 FGuid SessionAccount,SessionCharacter;
 bool bSessionSent=false,bSessionRecorded=false,bSessionPacketsLogged=false;
 int32 SessionPredictionKey=0;
 FPredictionKey SessionActivationKey;
 FString SessionRows,SessionLastDetail;
 UPROPERTY() TObjectPtr<AProject_JAuditNetworkPlayer> Attacker;
 UPROPERTY() TObjectPtr<AProject_JAuditNetworkTarget> Target;
 UPROPERTY() TObjectPtr<AProject_JAuditCueProbe> Probe;
 UPROPERTY() TObjectPtr<class UProject_JCombatStyleDefinition> RateStyle;
 int32 MovementExpectation=INDEX_NONE,RatePhase=0;
 bool bRateStarted=false,bRateAdvanced=false;
 void ObserveWarningAudit();
 int32 WarningStep=INDEX_NONE,WarningRequiredOrder=0;
 uint32 WarningExpectedInstance=0;
 uint64 WarningStableFrame=MAX_uint64,WarningProbeId=0;
 TWeakObjectPtr<AProject_JAuditCueProbe> WarningIdentity;
 bool bWarningRecorded=false;
 FString WarningRows;
 void ObserveMountAudit();
 void ObserveMountSpatial();
 UFUNCTION() void OnMountSpatialEnd(AActor* Actor,EEndPlayReason::Type Reason);
 FProjectJAuditMountSpatial MountSpatial;
 FString MountSpatialRows;
 FProjectJAuditMountIdentity MountIdentities[2];
 TArray<uint64> MountExpectedIds;
 TArray<float> MountPublicResources;
 int32 MountStep=INDEX_NONE,MountRecordedCase=INDEX_NONE,MountRecordedStep=INDEX_NONE;
 double NextMountObserve=0;
 bool bMountDismountRequested=false;
 FString MountLastDetail;
 FString MountRows=TEXT("case,step,observer_slot,rider_slot,rider_id,state_id,mount_id,rider_role,mount_role,rider_controller,mount_controller,getter_accepted,attribute_accepted,canonical_same,expected_avatar_id,actual_avatar_id,epoch,epoch_rule,health,max_health,private_items,private_equipment,private_expected,mounted,tuple_rejected,request_sent,passed\n");
 int32 Case=INDEX_NONE,Remaining=0,Sequence=0,CueOrder=0;
 double HitTime=0;
 float ExpectedHealth=0;
 double NextSend=0,ObserveStarted=0;
 bool bPrepared=false,bCheckHealth=false,bObserveCue=false,bExpectA=false,bExpectB=false,bRequireRevision=false;
};
UCLASS(NotBlueprintable)
class AProject_JAuditNetworkGameMode : public AProject_JGameMode
{
 GENERATED_BODY()
public:
 AProject_JAuditNetworkGameMode();
 virtual void BeginPlay() override;
 virtual FString InitNewPlayer(APlayerController* PC,const FUniqueNetIdRepl& UniqueId,const FString& Options,const FString& Portal=TEXT("")) override;
 virtual void PostLogin(APlayerController* PC) override;
 virtual void EndPlay(EEndPlayReason::Type InReason) override;
 virtual void Tick(float Delta) override;
 void Observe(AProject_JAuditNetworkController* PC,int32 InCase,int32 Step,bool Success,const FString& Detail);
 void RecordHit(int32 Sequence,double ClientTime,float Before,float After,FGameplayTag Node,FGameplayTag Current);
private:
 void UpdateQuestHistoryAudit();
 bool PrepareQuestHistoryAudit();
 bool QuestHistoryCurrent() const;
 bool RecordQuestHistoryAuthority();
 void SendQuestHistoryStep();
 void CleanupQuestHistoryAudit();
 TWeakObjectPtr<AProject_JPlayerState> QuestHistoryStates[2];
 TWeakObjectPtr<UProject_JQuestComponent> QuestHistoryComponents[2];
 UPROPERTY(Transient) TObjectPtr<UProject_JQuestDefinition> QuestHistoryDefinition;
 ProjectJ::QuestHistory::FViewPtr QuestHistoryInitialValues;
 FName QuestHistoryNextId;
 FGuid QuestHistoryInitialRevision;
 double QuestHistoryAckAt=-1;
 bool bQuestHistoryCleaned=false;
 FString QuestHistoryRows;
 void UpdateNPCAssemblyAudit();
 bool NPCAssemblyCurrent() const;
 bool BeginNPCAssemblyWarning();
 bool EndNPCAssemblyWarning();
 bool RecordNPCAssemblyAuthority();
 void SendNPCAssemblyStep();
 void CleanupNPCAssemblyAudit();
 UPROPERTY(Transient) TObjectPtr<AProject_JNPCCharacter> NPCAssemblyActor;
 UPROPERTY(Transient) TObjectPtr<UProject_JNPCActivationDefinition> NPCAssemblyDefinition;
 TWeakObjectPtr<UProject_JAbilitySystemComponent> NPCAssemblyASC;
 TWeakObjectPtr<UProject_JCombatHitValidationComponent> NPCAssemblyHit;
 TWeakObjectPtr<UProject_JCombatPresentationComponent> NPCAssemblyPresentation;
 TWeakObjectPtr<AController> NPCAssemblyController;
 uint64 NPCAssemblyActorId=0,NPCAssemblyEpoch=0,NPCAssemblyLease=0;
 uint32 NPCAssemblyHitId=0,NPCAssemblyPresentationId=0;
 bool bNPCAssemblyLateJoinReady=false,bNPCAssemblyLateJoinSent=false,bNPCAssemblyOwnsController=false;
 FProject_JReplicatedCombatPresentationState NPCAssemblyExpected;
 FString NPCAssemblyOwner,NPCAssemblyRows;
 void UpdateSessionAudit();
 bool PrepareSessionOwner(int32 ExplicitSlot,FString& Failure);
 bool RecordSessionPhase(FString& Failure);
 void SendSessionStep(int32 InStep);
 void RecordSessionPermission(int32 ExplicitSlot,UProject_JAbilitySystemComponent* ASC,AProject_JPlayerState* State,bool Allowed);
 bool CleanupSessionAudit();
 FProjectJAuditSessionOwner SessionOwners[2]; // Indexed only by validated LoginOptions, never connection arrival order.
 FProject_JAccountId SessionAccount;
 FProject_JCharacterId SessionCharacter;
 ProjectJ::MMO::FServerSessionAttempt SessionSlowAttempt;
 bool bSessionCleaning=false,bSessionCleaned=false,bSessionCleanupPassed=false;
 bool bSessionStaleComplete=false,bSessionStaleRevoke=false,bSessionAppQueued=false,bSessionAppAttempted=false,bSessionAppApplied=false;
 uint64 SessionAppQueuedFrame=0;
 TWeakObjectPtr<AProject_JPlayerState> SessionAppRecipient;
 ProjectJ::MMO::FServerSessionLease SessionAppLease;
 FName SessionAppClass;
 int32 SessionAppBeforeLevel=0,SessionAppRequestedLevel=0;
 FString SessionRows;
 void StartCase(int32 Next); void Finish(bool Success,const FString& Why);
 void UpdateWarningAudit();
 bool RecordWarningAuthority(const TCHAR* Transition,uint64 Lease,bool bExpectedActive);
 void CleanupWarningAudit();
 uint64 WarningLease=0;
 FString WarningRows;
 bool PrepareMountAudit();
 void StartMountStep(int32 InCase,int32 InStep);
 void UpdateMountAudit();
 void UpdateMountSpatial();
 FProjectJAuditMountSpatial MountSpatial;
 FString MountSpatialRows;
 UPROPERTY(Transient) TObjectPtr<ACameraActor> MountSpatialCamera;
 void CleanupMountAudit();
 bool InspectMountScopeRegistration(const TCHAR* PhaseName, bool bStopped, bool bDismounted, bool bRecord);
 void UpdateMountRegistrationRestart();
 void ObserveMountRegistrationActorPhase(UWorld* World, ELevelTick TickType, float DeltaTime);
 uint8 MountRegistrationPhase=0;
 uint64 MountRegistrationGroup=0;
 uint32 MountRegistrationGroupsCreated=0;
 bool bMountRegistrationActorPhaseObserved=false;
 FDelegateHandle MountRegistrationPhaseObserver;
 TWeakObjectPtr<AProject_JAuditNetworkController> MountRegistrationController;
 TWeakObjectPtr<AProject_JMountCharacter> MountRegistrationPawn;
 int32 MountRegistrationLoggedStates=0;
 FString MountRegistrationLastDetail;
 FString MountRegistrationRows=TEXT("phase,observer_slot,owner_slot,group_token,groups_created,member_id,current_id,full_match,ready,identity_matches,group_valid,member_present,allow_requested,pending,admission_failed,mount_replicated,pc_pawn_id,mount_id,rider_id\n");
 UPROPERTY(Transient) TObjectPtr<AProject_JMountCharacter> MountActor;
 FProjectJAuditMountIdentity MountIdentities[2];
 int32 MountRecordedCase=INDEX_NONE,MountRecordedStep=INDEX_NONE;
 uint64 MountRevision=0;
 bool bMountCleaning=false;
 FString MountLastDetail;
 FString MountRows=TEXT("case,step,observer_slot,rider_slot,rider_id,state_id,mount_id,rider_role,mount_role,rider_controller,mount_controller,getter_accepted,attribute_accepted,canonical_same,expected_avatar_id,actual_avatar_id,epoch,epoch_rule,health,max_health,private_items,private_equipment,private_expected,mounted,tuple_rejected,request_sent,passed\n");
 bool AllConfirmed(int32 Step) const;
 UPROPERTY() TArray<TObjectPtr<AProject_JAuditNetworkController>> Clients;
 UPROPERTY() TObjectPtr<AProject_JAuditNetworkPlayer> Attacker;
 UPROPERTY() TObjectPtr<AProject_JAuditNetworkTarget> Target;
 UPROPERTY() TObjectPtr<AProject_JAuditCueProbe> Probe;
 UPROPERTY() TObjectPtr<class UProject_JAttackDefinition> Attack;
 FProject_JReplicatedCombatPresentationState Pending;
 TMap<int32,int32> Arrivals,DamageApplications;
 int32 Case=INDEX_NONE,Step=0;
 double Started=0,CaseStarted=0,StepStarted=0;
 double SampleTime=0;
 bool bSent=false,bFinished=false,bAuthorityHit=false;
 FString Rows=TEXT("case,sequence,client_time,arrival_time,age,before,after,node,current_node\n");
};
