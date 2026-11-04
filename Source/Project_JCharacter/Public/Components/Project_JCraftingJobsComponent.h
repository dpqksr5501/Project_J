#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Crafting/Project_JCraftingJobValues.h"
#include "Inventory/Project_JPersistentEdit.h"
#include "Project_JCraftingJobsComponent.generated.h"

/** Identity and generation travel as one owner-only value, independently of PS identity delivery. */
USTRUCT()
struct FProject_JCraftingOwnerScope
{
 GENERATED_BODY()
 UPROPERTY() FGuid CharacterId;
 UPROPERTY() uint64 Generation = 0;
 bool NetSerialize(FArchive& Ar, UPackageMap*, bool& bOutSuccess)
 { Ar << CharacterId; Ar << Generation; bOutSuccess = !Ar.IsError(); return true; }
};
template<> struct TStructOpsTypeTraits<FProject_JCraftingOwnerScope> : TStructOpsTypeTraitsBase2<FProject_JCraftingOwnerScope>
{ enum { WithNetSerializer = true }; };

UENUM(BlueprintType)
enum class EProject_JCraftingJobAction : uint8 { Collect, Cancel };
UENUM(BlueprintType)
enum class EProject_JCraftingRequestStage : uint8 { Idle, Pending, Rejected, Confirmed, Unknown };
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JCraftingRequestState
{
 GENERATED_BODY()
 /** Client observation correlation only, never a durable command or asset identity. */
 UPROPERTY(BlueprintReadOnly) FGuid RequestId;
 UPROPERTY(BlueprintReadOnly) FGuid JobId;
 UPROPERTY(BlueprintReadOnly) EProject_JCraftingJobAction Action = EProject_JCraftingJobAction::Collect;
 UPROPERTY(BlueprintReadOnly) EProject_JCraftingRequestStage Stage = EProject_JCraftingRequestStage::Idle;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JCraftingJobSummary
{
 GENERATED_BODY()
 /** Derived presentation identity; old rows cannot become another character's work. */
 UPROPERTY(BlueprintReadOnly) FGuid OwnerCharacterId;
 UPROPERTY() uint64 OwnerGeneration = 0;
 UPROPERTY(BlueprintReadOnly) FGuid JobId;
 UPROPERTY(BlueprintReadOnly) FName RecipeId;
 UPROPERTY(BlueprintReadOnly) int32 RecipeVersion = 1;
 UPROPERTY(BlueprintReadOnly) int32 Batches = 1;
 /** Remaining real seconds when published. Client display starts at receipt, conservatively late by transport delay. */
 UPROPERTY(BlueprintReadOnly) double RemainingSecondsAtPublish = 0;
 bool operator==(const FProject_JCraftingJobSummary& Other) const
 { return OwnerCharacterId == Other.OwnerCharacterId && OwnerGeneration == Other.OwnerGeneration && JobId == Other.JobId && RecipeId == Other.RecipeId && RecipeVersion == Other.RecipeVersion && Batches == Other.Batches && RemainingSecondsAtPublish == Other.RemainingSecondsAtPublish; }
};

/** Character-owned durable jobs. Escrow stays authority-local; only bounded summaries replicate to the owner. */
UCLASS(ClassGroup=(Crafting), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JCraftingJobsComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UProject_JCraftingJobsComponent();
 DECLARE_MULTICAST_DELEGATE(FOnChanged);
 FOnChanged OnChanged;
 FOnChanged OnRequestChanged;
 /** Authority lifecycle notification from PlayerState. Clients consume only the replicated atomic scope. */
 void NotifyOwnerIdentity(FGuid CharacterId);
 uint64 GetPresentationGeneration() const
 { return CanPublish() && OwnerIdentity.IsValid() && OwnerScope.CharacterId == OwnerIdentity ? OwnerScope.Generation : 0; }
 FGuid GetPresentationCharacterId() const { return GetPresentationGeneration() ? OwnerScope.CharacterId : FGuid(); }
 /** Pending observation expires to Unknown after real10s without holding any timer/thread. */
 UFUNCTION(BlueprintPure, Category="Crafting") FProject_JCraftingRequestState GetRequestState() const;
 TArray<ProjectJ::Crafting::FJobValues> CaptureJobs() const;
 bool ValidateCommittedJobs(const TArray<ProjectJ::Crafting::FJobValues>& Values) const;
 void InstallCommittedJobs(const TArray<ProjectJ::Crafting::FJobValues>& Values, bool bBroadcast = false);
 void BroadcastCommittedJobs();
 UFUNCTION(BlueprintPure, Category="Crafting") TArray<FProject_JCraftingJobSummary> GetJobs() const;
 /** UTC on authority; publish remaining minus local monotonic elapsed on client. -1 means unknown/missing/ended, 0 is advisory ready only. */
 UFUNCTION(BlueprintPure, Category="Crafting") double GetRemainingSeconds(FGuid JobId) const;
 /** Synchronous server value only; never retain this pointer across a callback/installation. */
 const ProjectJ::Crafting::FJobValues* FindJob(const FGuid& JobId) const;
 FGuid FindRecipeJob(FName RecipeId, int32 Version) const;
 bool IsJobDue(const FGuid& JobId) const;
 FProject_JPersistentSubmit SubmitPersistentEdit;
 TFunction<FGuid()> CreatePersistentRequest;
 TFunction<bool()> PersistentAdmission;
 UFUNCTION(BlueprintCallable, Category="Crafting") void RequestCollectJob(FGuid JobId);
 UFUNCTION(BlueprintCallable, Category="Crafting") void RequestCancelJob(FGuid JobId);
 bool TryCollectJob(FGuid JobId, FProject_JPersistentCompletion Complete = {});
 bool TryCancelJob(FGuid JobId, FProject_JPersistentCompletion Complete = {});
 virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
private:
 UFUNCTION(Server, Reliable) void ServerCollectJob(FGuid RequestId, FGuid JobId, uint64 ExpectedGeneration);
 UFUNCTION(Server, Reliable) void ServerCancelJob(FGuid RequestId, FGuid JobId, uint64 ExpectedGeneration);
 UFUNCTION(Client, Reliable) void ClientJobRequestResult(FGuid RequestId, FGuid JobId, uint64 ExpectedGeneration, EProject_JCraftingJobAction Action, EProject_JCraftingRequestStage Stage);
 UFUNCTION() void OnRep_OwnerScope();
 void BeginClientRequest(FGuid JobId, EProject_JCraftingJobAction Action);
 void DispatchClientRequest(FGuid RequestId, FGuid JobId, uint64 ExpectedGeneration, EProject_JCraftingJobAction Action);
 void PublishRequestState();
 UFUNCTION() void OnRep_Jobs();
 bool CanPublish() const;
 bool SubmitJob(EProject_JPersistentEdit Kind, FGuid JobId, FProject_JPersistentCompletion Complete);
 void Stop();
 FGuid OwnerIdentity, InstalledIdentity, RequestIdentity;
 FProject_JCraftingRequestState CurrentRequest;
 UPROPERTY(ReplicatedUsing=OnRep_OwnerScope) FProject_JCraftingOwnerScope OwnerScope;
 uint64 InstalledGeneration = 0, RequestGeneration = 0, ObservedGeneration = 0;
 double RequestStartedAt = -1;
 static constexpr double RequestTimeoutSeconds = 10;
 TArray<ProjectJ::Crafting::FJobValues> Jobs;
 TWeakObjectPtr<UWorld> SummaryWorld;
 UPROPERTY(ReplicatedUsing=OnRep_Jobs) TArray<FProject_JCraftingJobSummary> Summaries;
 double ReceivedAtSeconds = -1;
 bool bEnding = false, bPendingNotification = false;
};
