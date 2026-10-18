#pragma once
#include "CoreMinimal.h"
#include "Game/Project_JGameMode.h"
#include "GameFramework/PlayerController.h"
#include "Project_JGreatswordCharacter.h"
#include "Project_JNPCCharacter.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Components/Project_JCombatPresentationComponent.h"
#include "GameplayEffect.h"
#include "Project_JAuditNetworkFixture.generated.h"

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
 virtual void ServerRequestSSRHit_Implementation(AActor* Target,float Time,FVector Start,FVector End,FGameplayTag Node,int32 Sequence,int32 Key) override;
};
UCLASS(NotBlueprintable)
class AProject_JAuditNetworkPlayer : public AProject_JGreatswordCharacter
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
UCLASS(NotBlueprintable)
class AProject_JAuditNetworkController : public APlayerController
{
 GENERATED_BODY()
public:
 virtual void PlayerTick(float Delta) override;
 UFUNCTION(Client,Reliable) void ClientPrepare(int32 InCase,AProject_JAuditNetworkPlayer* InAttacker,AProject_JAuditNetworkTarget* InTarget,AProject_JAuditCueProbe* InProbe);
 UFUNCTION(Client,Reliable) void ClientSendHit(int32 InCase,float Time);
 UFUNCTION(Client,Reliable) void ClientCheckHealth(int32 InCase,float Expected);
 UFUNCTION(Client,Reliable) void ClientObserveCue(int32 InCase,int32 EventOrder,bool bInExpectA,bool bInExpectB,bool bInRequireRevision);
 UFUNCTION(Client,Reliable) void ClientObserveMovement(int32 InCase,bool bInFalling);
 UFUNCTION(Client,Reliable) void ClientRate(int32 InCase,bool bInAdvance);
 bool ConfigureRate(AProject_JAuditNetworkPlayer* InPlayer);
 bool AdvanceRate(AProject_JAuditNetworkPlayer* InPlayer);
 float ReadRate(AProject_JAuditNetworkPlayer* InPlayer) const;
 UFUNCTION(Client,Reliable) void ClientFinish(bool Success);
 UFUNCTION(Server,Reliable) void ServerObserve(int32 InCase,int32 Step,bool Success,const FString& Detail);
 void SendCueDelta(AProject_JAuditCueProbe* Probe,bool bStart,int32 Order,uint32 Instance,const FProject_JReplicatedCombatPresentationState& Snapshot);
 void PublishCueState(AProject_JAuditCueProbe* Probe,const FProject_JReplicatedCombatPresentationState& State);
 int32 Slot=INDEX_NONE,ConfirmedCase=INDEX_NONE,ConfirmedStep=INDEX_NONE;
private:
 UPROPERTY() TObjectPtr<AProject_JAuditNetworkPlayer> Attacker;
 UPROPERTY() TObjectPtr<AProject_JAuditNetworkTarget> Target;
 UPROPERTY() TObjectPtr<AProject_JAuditCueProbe> Probe;
 UPROPERTY() TObjectPtr<class UProject_JCombatStyleDefinition> RateStyle;
 int32 MovementExpectation=INDEX_NONE,RatePhase=0;
 bool bRateStarted=false,bRateAdvanced=false;
 int32 Case=INDEX_NONE,Remaining=0,Sequence=0,CueOrder=0;
 float HitTime=0,ExpectedHealth=0;
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
 virtual void PostLogin(APlayerController* PC) override;
 virtual void Tick(float Delta) override;
 void Observe(AProject_JAuditNetworkController* PC,int32 InCase,int32 Step,bool Success,const FString& Detail);
 void RecordHit(int32 Sequence,float ClientTime,float Before,float After,FGameplayTag Node,FGameplayTag Current);
private:
 void StartCase(int32 Next); void Finish(bool Success,const FString& Why);
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
 float SampleTime=0;
 bool bSent=false,bFinished=false,bAuthorityHit=false;
 FString Rows=TEXT("case,sequence,client_time,arrival_time,age,before,after,node,current_node\n");
};
