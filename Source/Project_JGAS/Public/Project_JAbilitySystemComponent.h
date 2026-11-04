// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "Project_JAbilitySystemComponent.generated.h"

struct FProject_JAbilityGrantRecord
{
	uint64 Generation = 0;
	int32 LeaseCount = 1;
	TArray<FGameplayAbilitySpecHandle> AbilityHandles;
	TArray<FActiveGameplayEffectHandle> EffectHandles;
};

/** GT notification before GAS replaces its recipient. Null arguments denote ClearActorInfo. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FProject_JActorInfoChanging, AActor*, AActor*);
class UProject_JAbilitySystemComponent;
/** GT notification after the outermost Init/Clear settles. Re-read the current pair; listeners may reenter. */
DECLARE_MULTICAST_DELEGATE_OneParam(FProject_JActorInfoSettled, UProject_JAbilitySystemComponent*);
DECLARE_DELEGATE_RetVal(bool, FProject_JGASClientSessionAuthority);

/**
 * Custom Ability System Component for Project J.
 * Can be extended with custom logic for the MMORPG structure.
 */
UCLASS()
class PROJECT_JGAS_API UProject_JAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()
	
public:
	/** Refresh cached controller/component metadata only for the current owner/avatar. */
	bool RefreshActorInfoForCurrentActor(AActor* Requester);
	UProject_JAbilitySystemComponent();
	virtual void InitAbilityActorInfo(AActor* InOwnerActor,AActor* InAvatarActor) override;
	virtual void ClearActorInfo() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void NotifyAbilityActivated(const FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability) override;
	virtual void NotifyAbilityEnded(FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability, bool bWasCancelled) override;
	virtual void OnRemoveAbility(FGameplayAbilitySpec& AbilitySpec) override;
 virtual float PlayMontage(UGameplayAbility* Ability,FGameplayAbilityActivationInfo Activation,UAnimMontage* Montage,float Rate,FName Section=NAME_None,float Start=0.f) override;
 virtual void CurrentMontageStop(float BlendOut=-1.f) override;
 virtual void CurrentMontageJumpToSection(FName Section) override;
 virtual void CurrentMontageSetPlayRate(float Rate) override;
 virtual void ServerCurrentMontageJumpToSectionName_Implementation(UAnimSequenceBase* ClientAnimation,FName SectionName) override;
 virtual void ServerCurrentMontageSetPlayRate_Implementation(UAnimSequenceBase* ClientAnimation,float Rate) override;
	virtual void ServerCurrentMontageSetNextSectionName_Implementation(UAnimSequenceBase* ClientAnimation, float ClientPosition, FName SectionName, FName NextSectionName) override;
	virtual void ServerAbilityRPCBatch_Internal(FServerAbilityRPCBatch& BatchInfo) override;
	virtual void ServerSetReplicatedTargetData_Implementation(FGameplayAbilitySpecHandle AbilityHandle,
		FPredictionKey AbilityOriginalPredictionKey, const FGameplayAbilityTargetDataHandle& TargetData,
		FGameplayTag ApplicationTag, FPredictionKey CurrentPredictionKey) override;
	virtual void ServerSetReplicatedTargetDataCancelled_Implementation(FGameplayAbilitySpecHandle AbilityHandle,
		FPredictionKey AbilityOriginalPredictionKey, FPredictionKey CurrentPredictionKey) override;
	virtual void ServerSetReplicatedEvent_Implementation(EAbilityGenericReplicatedEvent::Type EventType,
		FGameplayAbilitySpecHandle AbilityHandle, FPredictionKey AbilityOriginalPredictionKey, FPredictionKey CurrentPredictionKey) override;
	virtual void ServerSetReplicatedEventWithPayload_Implementation(EAbilityGenericReplicatedEvent::Type EventType,
		FGameplayAbilitySpecHandle AbilityHandle, FPredictionKey AbilityOriginalPredictionKey, FPredictionKey CurrentPredictionKey,
		FVector_NetQuantize100 VectorPayload) override;
	virtual void ServerEndAbility_Implementation(FGameplayAbilitySpecHandle AbilityToEnd,
		FGameplayAbilityActivationInfo ActivationInfo, FPredictionKey PredictionKey) override;
	/** Weak native PlayerState owner fence. Unbound NPC/prototype behavior is unchanged. */
	FProject_JGASClientSessionAuthority ClientSessionAuthority;
	/** Tracked execution/reservation headroom for NEW client activations. Actual trusted activation remains unconstrained. */
	UPROPERTY(EditDefaultsOnly, Category="Network|GAS", meta=(ClampMin="1", ClampMax="128"))
	int32 ClientPayloadExecutionLimit = 128;
	/** Runtime recipient epoch, separate from ability grant leases. Not persisted. */
	uint64 GetActorInfoRevision() const { return ActorInfoRevision; }
	bool IsActorInfoChanging() const { return bActorInfoChanging; }
	FProject_JActorInfoChanging OnActorInfoChanging;
	FProject_JActorInfoSettled OnActorInfoSettled;

	void AddProjectJLooseGameplayTag(const FGameplayTag& GameplayTag, bool bReplicateOnAuthority = true);
	void RemoveProjectJLooseGameplayTag(const FGameplayTag& GameplayTag, bool bReplicateOnAuthority = true);
	bool AbilityInputTagPressed(const FGameplayTag& InputTag);
	bool AbilityInputTagReleased(const FGameplayTag& InputTag);
	/** Discrete intent uses the same reliable actor channel as GAS activation. */
	UFUNCTION(Server, Reliable)
	void ServerSendCombatInputEvent(AActor* ExpectedAvatar, FGameplayTag InputTag, double ClientTimestamp, uint32 InputSequence);
	bool ReserveAbilityGrantSource(FName SourceId, uint64& OutGeneration);
	/** Acquire one provider lease. Only the first provider creates specs/effects. */
	bool AcquireAbilityGrantSource(FName SourceId, bool& bOutNeedsGrant, uint64& OutGeneration);
	bool RegisterGrantedAbility(FName SourceId, uint64 Generation, FGameplayAbilitySpecHandle Handle);
	bool RegisterGrantedEffect(FName SourceId, uint64 Generation, FActiveGameplayEffectHandle Handle);
	bool RemoveAbilityGrantSource(FName SourceId, uint64 Generation);
	bool HasAbilityGrantSource(FName SourceId) const;
	bool IsAbilityGrantSourceCurrent(FName SourceId, uint64 Generation) const;

protected:
	virtual void InternalServerTryActivateAbility(FGameplayAbilitySpecHandle Handle, bool InputPressed,
		const FPredictionKey& PredictionKey, const FGameplayEventData* TriggerEventData) override;
	virtual void CancelAbilitySpec(FGameplayAbilitySpec& Spec, UGameplayAbility* Ignore) override;

private:
	struct FClientCommandContext
	{
		TWeakObjectPtr<AActor> Owner, Avatar;
		uint64 ActorInfoRevision = 0;
	};
	struct FBatchCommandAdmission
	{
		FGameplayAbilitySpecHandle Handle;
		FPredictionKey PredictionKey;
		FClientCommandContext Context;
		bool bAvailable = false;
		bool bTargetDataAllowed = false;
	};
	FClientCommandContext CaptureClientCommandContext() const;
	bool IsClientCommandContextCurrent(const FClientCommandContext& Context) const;
	bool IsClientCommandContextAuthorized(const FClientCommandContext& Context);
	bool AdmitClientGameplayCommand(const FClientCommandContext& Context);
	// Stack-local, exact and single-use. Nested/reentrant requests never inherit a free batch window.
	FBatchCommandAdmission* ActiveBatchCommandAdmission = nullptr;
	enum class EClientPayloadPhase : uint8 { Reserved, Active, Ending };
	struct FClientPayloadExecution
	{
		uint64 Serial = 0;
		FGameplayAbilitySpecHandle Handle;
		FPredictionKey PredictionKey;
		FClientCommandContext Context;
		TWeakObjectPtr<UGameplayAbility> Instance;
		FDelegateHandle EndHandle;
		EClientPayloadPhase Phase = EClientPayloadPhase::Reserved;
	};
	struct FClientActivationReservation
	{
		uint64 Serial = 0;
		FGameplayAbilitySpecHandle Handle;
		FPredictionKey RequestedKey;
		bool bAvailable = true;
	};
	uint64 ReserveClientPayloadExecution(FGameplayAbilitySpecHandle Handle, const FPredictionKey& Key, const FClientCommandContext& Context);
	void ReleaseClientPayloadReservation(uint64 Serial);
	void PublishClientPayloadExecution(FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability);
	void BeginClientPayloadExecutionEnding(uint64 Serial);
	void RetireClientPayloadExecution(uint64 Serial, bool bClearNative);
	void RetireClientPayloadExecutions(FGameplayAbilitySpecHandle Handle = FGameplayAbilitySpecHandle());
	UGameplayAbility* AuthorizeClientPayload(FGameplayAbilitySpecHandle Handle, const FPredictionKey& Key);
	bool HasClientPayloadAlias(FGameplayAbilitySpecHandle Handle, int16 Current, uint64 ExceptSerial = 0, bool bIncludeReservations = true) const;
	TArray<FClientPayloadExecution> ClientPayloadExecutions;
	FClientActivationReservation* ActiveClientActivationReservation = nullptr;
	uint64 NextClientPayloadSerial = 0;
	bool bRetiringClientPayloads = false;
	bool CanAcceptClientSessionCommand();
	void RejectSessionActivation(FGameplayAbilitySpecHandle Handle, const FPredictionKey& PredictionKey);
	void PublishSettledActorInfo();
 void RefreshMoverMontage();
 TWeakObjectPtr<class UProject_JMoverComponent> MontageMover;
	bool HasGameplayTagReplicationAuthority() const;
	TMap<FName, FProject_JAbilityGrantRecord> AbilityGrantRecords;
	uint64 NextAbilityGrantGeneration = 0;
	uint64 ActorInfoRevision=0;
	bool bActorInfoChanging = false;
};
