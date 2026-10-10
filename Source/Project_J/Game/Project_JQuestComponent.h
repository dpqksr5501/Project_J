#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "Project_JQuestComponent.generated.h"

UCLASS(BlueprintType)
class PROJECT_J_API UProject_JQuestDefinition : public UDataAsset
{
	GENERATED_BODY()
  public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FName QuestId;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FText Title;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FText Description;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FName ObjectiveEvent;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "1")) int32 RequiredCount = 1;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "0")) int64 RewardExperience = 0;
	/** Enables bounded server-side travel sampling; one count = one metre. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) bool bTravelObjective = false;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) bool bHasMapTarget = false;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FVector MapTarget = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FProject_JQuestState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FName QuestId;
	UPROPERTY(BlueprintReadOnly) int32 Count = 0;
	UPROPERTY(BlueprintReadOnly) bool bClaimed = false;
	UPROPERTY(BlueprintReadOnly) bool bTracked = true;
};
USTRUCT()
struct FProject_JQuestSnapshot
{
	GENERATED_BODY()
	UPROPERTY() int32 Version = 1;
	UPROPERTY() TArray<FProject_JQuestState> States;
};

/** PlayerState-owned, owner-only quest progress. Reward amounts never come from clients. */
UCLASS(ClassGroup = (Gameplay), meta = (BlueprintSpawnableComponent))
class PROJECT_J_API UProject_JQuestComponent : public UActorComponent
{
	GENERATED_BODY()
  public:
	UProject_JQuestComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty> &Out) const override;
	UPROPERTY(EditDefaultsOnly) TArray<TObjectPtr<UProject_JQuestDefinition>> Definitions;
	UPROPERTY(ReplicatedUsing = OnRep_States, BlueprintReadOnly) TArray<FProject_JQuestState> States;
	UFUNCTION(BlueprintPure) UProject_JQuestDefinition *FindDefinition(FName Id) const;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly) bool Accept(FName Id);
	/** Trusted gameplay events only: kills/interactions/objectives report to the server here. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly) void RecordObjective(FName Event, int32 Count = 1);
	UFUNCTION(BlueprintCallable) void RequestClaim(FName Id);
	UFUNCTION(BlueprintCallable) void RequestTracking(FName Id, bool bTracked);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly) bool Claim(FName Id);
	FProject_JQuestSnapshot CaptureSnapshot() const;
	bool RestoreSnapshot(const FProject_JQuestSnapshot &Snapshot);
	DECLARE_MULTICAST_DELEGATE(FChanged);
	FChanged OnChanged;

  private:
	UFUNCTION() void OnRep_States();
	UFUNCTION(Server, Reliable) void ServerClaim(FName Id);
	UFUNCTION(Server, Reliable) void ServerTracking(FName Id, bool bTracked);
	bool AllowRequest();
	void Publish();
	void SampleTravel();
	FTimerHandle TravelTimer;
	TWeakObjectPtr<APawn> TravelPawn;
	FVector LastLocation = FVector::ZeroVector;
	double TravelRemainder = 0;
	double RequestWindow = -1;
	int32 RequestCount = 0;
	bool bChanging = false;
};
