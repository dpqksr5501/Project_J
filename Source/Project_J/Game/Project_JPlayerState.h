// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "Project_JMMOTypes.h"
#include "Project_JSocialStateInterface.h"
#include "AbilitySystemInterface.h"
#include "Social/Project_JPartyRoster.h"
#include "TimerManager.h"
#include "MMO/Coordination.h"
#include "Project_JAbilitySystemOwnerInterface.h"
#include "Project_JPlayerState.generated.h"

class UProject_JCraftingJobsComponent;
class UProject_JSocialSubsystem;
struct FOnAttributeChangeData;
class UProject_JQuestComponent;
class UProject_JCompanionComponent;
class UProject_JProgressionComponent;
class UProject_JInventoryComponent;
class UProject_JEquipmentManagerComponent;
class AProject_JGameMode;

/**
 * Public, replicated player metadata.
 *
 * Keep private account data, inventory, currencies, and long-lived service data
 * out of PlayerState. Those belong to backend-owned session data or owner-only
 * replicated components.
 */
UCLASS()
class PROJECT_J_API AProject_JPlayerState
	: public APlayerState
	, public IAbilitySystemInterface
	, public IProject_JAbilitySystemOwnerInterface
	, public IProject_JSocialStateInterface
{
	GENERATED_BODY()

public:
	AProject_JPlayerState();
	DECLARE_MULTICAST_DELEGATE(FOnCharacterIdentityChanged);
	FOnCharacterIdentityChanged OnCharacterIdentityChanged;
	DECLARE_MULTICAST_DELEGATE(FOnPartyRosterChanged);
	FOnPartyRosterChanged OnPartyRosterChanged;
	/** Complete member-visible values; never resolves a distant Pawn or remote ASC. */
	UFUNCTION(BlueprintPure, Category="MMO|Party") TArray<FProject_JPartyMemberSummary> GetPartyRoster() const;
	uint64 GetPartyRosterRevision() const { return PartyRosterRevision; }
	virtual void SetPlayerName(const FString& Name) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** Narrow only the four native private streams; keep default UObjects and public components. */
	virtual ELifetimeCondition AllowActorComponentToReplicate(const UActorComponent* ComponentToReplicate) const override;
	virtual void BeginPlay() override;
	virtual void PostInitializeComponents() override;
	virtual void OnRep_Owner() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Implement IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	// Implement IProject_JAbilitySystemOwnerInterface
	virtual UProject_JAbilitySystemComponent* GetProjectJAbilitySystemComponent() const override;
	virtual UProject_JAttributeSet* GetProjectJAttributeSet() const override;
	virtual bool HasGrantedDefaultAbilities() const override { return bDefaultAbilitiesGranted; }
	virtual void SetHasGrantedDefaultAbilities(bool bGranted) override { bDefaultAbilitiesGranted = bGranted; }
	virtual FName GetPartyId_Implementation() const override { return PartyId; }
	virtual bool TryGetAuthoritativePartyId(FName& OutPartyId) const override final
	{ OutPartyId = PartyId; return HasAuthority() && !IsActorBeingDestroyed(); }
	virtual FName GetGuildId_Implementation() const override { return GuildId; }

	UFUNCTION(BlueprintPure, Category = "MMO|Identity")
	FProject_JAccountId GetAccountId() const { return AccountId; }

	UFUNCTION(BlueprintPure, Category = "MMO|Identity")
	FProject_JCharacterId GetCharacterId() const { return CharacterId; }

	UFUNCTION(BlueprintPure, Category = "MMO|Character")
	FName GetPublicClassId() const { return PublicClassId; }

	UFUNCTION(BlueprintPure, Category = "MMO|Character")
	int32 GetPublicCharacterLevel() const { return PublicCharacterLevel; }

	UFUNCTION(BlueprintPure, Category = "MMO|Social")
	FGuid GetPartyLeaderCharacterId() const { return PartyLeaderCharacterId; }

	UFUNCTION(BlueprintPure, Category = "MMO|Social")
	FGuid GetGuildLeaderCharacterId() const { return GuildLeaderCharacterId; }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MMO|Identity")
	void SetIdentity(const FProject_JAccountId& InAccountId, const FProject_JCharacterId& InCharacterId);

	/** Server runtime fence, not a replicated credential. Empty captures preserve
	 * legacy callers only until this state enters the trusted-session path. */
	ProjectJ::MMO::FServerSessionLease CaptureServerSession() const;
	bool IsServerSessionCurrent(const ProjectJ::MMO::FServerSessionLease& Captured) const;
	bool CanAcceptSessionCommand() const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MMO|Character")
	void SetPublicCharacterSnapshot(FName InClassId, int32 InLevel);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MMO|Social")
	void SetSocialState(
		FName InPartyId,
		FName InGuildId,
		const FGuid& InPartyLeaderCharacterId,
		const FGuid& InGuildLeaderCharacterId);

	UFUNCTION(BlueprintPure, Category="Companion") UProject_JCompanionComponent* GetCompanionComponent() const { return CompanionComponent; }
	UFUNCTION(BlueprintPure, Category="Crafting") UProject_JCraftingJobsComponent* GetCraftingJobsComponent() const { return CraftingJobsComponent; }
	FORCEINLINE UProject_JInventoryComponent* GetInventoryComponent() const { return InventoryComponent; }
	FORCEINLINE UProject_JEquipmentManagerComponent* GetEquipmentManagerComponent() const { return EquipmentManagerComponent; }

protected:
	virtual void OnReplicationStartedForIris(const FOnReplicationStartedParams& Params) override;
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "MMO|Identity")
	FProject_JAccountId AccountId;

	UPROPERTY(ReplicatedUsing=OnRep_CharacterId, VisibleAnywhere, BlueprintReadOnly, Category = "MMO|Identity")
	FProject_JCharacterId CharacterId;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "MMO|Character")
	FName PublicClassId = NAME_None;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "MMO|Character")
	int32 PublicCharacterLevel = 1;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "MMO|Social")
	FName PartyId = NAME_None;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "MMO|Social")
	FName GuildId = NAME_None;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "MMO|Social")
	FGuid PartyLeaderCharacterId;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "MMO|Social")
	FGuid GuildLeaderCharacterId;

private:
	friend class AProject_JGameMode;
	void RequireServerSession(AProject_JGameMode* Authority);
	void InstallServerSession(AProject_JGameMode* Authority, const ProjectJ::MMO::FServerSessionLease& Lease);
	bool OwnsServerSession(const AProject_JGameMode* Authority, const ProjectJ::MMO::FServerSessionLease& Lease) const;
	bool bServerSessionRequired = false;
	bool bServerSessionRevoked = false;
	TWeakObjectPtr<AProject_JGameMode> ServerSessionAuthority;
	ProjectJ::MMO::FServerSessionLease ServerSessionLease;
	friend class UProject_JSocialSubsystem;
	friend struct FProject_JPartyRosterArray;
	bool ApplyPartyRoster(const TArray<FProject_JPartyMemberSummary>& Desired);
	void NotifyPartyRosterChanged();
	void SetPartySummaryObservation(UProject_JSocialSubsystem* Authority, bool bEnable);
	void StopPartySummaryObservation();
	void QueuePartySummary();
	void RetirePartyRoster();
	void OnPartyHealthChanged(const FOnAttributeChangeData& Data, uint64 Generation);
	void OnPartyDefaultsInitialized(uint64 Generation);
	UFUNCTION() void OnPartySummaryPawnSet(APlayerState* State, APawn* NewPawn, APawn* OldPawn);
	UPROPERTY(Replicated) FProject_JPartyRosterArray PartyRoster;
	TWeakObjectPtr<UProject_JSocialSubsystem> PartySummaryAuthority;
	TWeakObjectPtr<UProject_JAbilitySystemComponent> PartySummaryASC;
	TWeakObjectPtr<UProject_JAttributeSet> PartySummaryAttributes;
	TWeakObjectPtr<UWorld> PartySummaryTimerWorld;
	FDelegateHandle PartyHealthHandle, PartyMaxHealthHandle, PartyDefaultsHandle;
	FTimerHandle PartySummaryTimer;
	uint64 PartySummaryGeneration = 0, PartyRosterRevision = 0;
	bool bPartySummaryObserving = false, bPartyRosterEnding = false;
	// Server-only admission fence follows this PlayerState lifetime. Explicit
	// subsystem binding is the only path that reopens a superseded session.
	bool bSocialSessionRevoked = false;

	UPROPERTY(VisibleAnywhere) TObjectPtr<UProject_JCraftingJobsComponent> CraftingJobsComponent;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProject_JQuestComponent> QuestComponent;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProject_JCompanionComponent> CompanionComponent;
	UFUNCTION() void OnRep_CharacterId();
	void RefreshPublicProgressionSnapshot();
	UPROPERTY(VisibleAnywhere, Category="Character")
	TObjectPtr<UProject_JProgressionComponent> ProgressionComponent;
	UPROPERTY(Transient)
	bool bDefaultAbilitiesGranted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities", meta = (AllowPrivateAccess = "true"))
	class UProject_JAbilitySystemComponent* AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities", meta = (AllowPrivateAccess = "true"))
	class UProject_JAttributeSet* AttributeSet;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UProject_JInventoryComponent* InventoryComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UProject_JEquipmentManagerComponent* EquipmentManagerComponent;
};
