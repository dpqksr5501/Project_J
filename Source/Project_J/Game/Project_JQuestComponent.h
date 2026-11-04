#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "Inventory/Project_JPersistentEdit.h"
#include "Crafting/Project_JCraftingRecipeValues.h"
#include "Game/Project_JQuestHistory.h"
#include "Project_JQuestComponent.generated.h"

UENUM(BlueprintType)
enum class EProject_JQuestObjectiveKind : uint8 { Event, Travel, GatherItem, CraftRecipe };

USTRUCT(BlueprintType)
struct FProject_JQuestObjectiveDefinition
{
	GENERATED_BODY()
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FName ObjectiveId;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) EProject_JQuestObjectiveKind Kind = EProject_JQuestObjectiveKind::Event;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FName Event;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FName TargetId;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) int32 TargetVersion = 1;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="1")) int32 RequiredCount = 1;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FText Title;
};
USTRUCT(BlueprintType)
struct FProject_JQuestObjectiveState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FName ObjectiveId;
	UPROPERTY(BlueprintReadOnly) int32 Count = 0;
	bool operator==(const FProject_JQuestObjectiveState& Other) const { return ObjectiveId == Other.ObjectiveId && Count == Other.Count; }
};
struct FProject_JQuestState;
class UProject_JCharacterDataSubsystem;

/** Canonical item terms; stackability stays owned by the registered item definition. */
USTRUCT(BlueprintType)
struct FProject_JQuestRewardItem
{
    GENERATED_BODY()
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FName Definition;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="1")) int32 DefinitionVersion = 1;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="1", ClampMax="1000000")) int32 Count = 1;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="1", ClampMax="10000")) int32 Level = 1;
    bool operator==(const FProject_JQuestRewardItem& Other) const
    { return Definition == Other.Definition && DefinitionVersion == Other.DefinitionVersion && Count == Other.Count && Level == Other.Level; }
};

UCLASS(BlueprintType)
class PROJECT_J_API UProject_JQuestDefinition : public UDataAsset
{
	GENERATED_BODY()
  public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FName QuestId;
	/** Bump when changing the meaning of persisted progress or rewards. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "1")) int32 SchemaVersion = 1;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FText Title;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FText Description;
	/** Empty preserves the existing single-objective fields and exact v1 save contract. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(TitleProperty="ObjectiveId")) TArray<FProject_JQuestObjectiveDefinition> Objectives;
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	bool IsRuntimeValid() const;
    /** GT-only canonical capture. No loading; failure preserves Out. Empty bundles require no registry. */
    bool CaptureRewardItems(const UProject_JCharacterDataSubsystem* Data, TArray<ProjectJ::Crafting::FResolvedLine>& Out) const;
	bool IsStateValid(const FProject_JQuestState& State) const;
	bool IsComplete(const FProject_JQuestState& State) const;
	bool HasIncompleteTravel(const FProject_JQuestState& State) const;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FName ObjectiveEvent;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "1")) int32 RequiredCount = 1;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "0")) int64 RewardExperience = 0;
    /** All-or-nothing with XP and completion. At most8 unique item IDs; empty keeps existing reward rules. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest|Reward", meta=(TitleProperty="Definition")) TArray<FProject_JQuestRewardItem> RewardItems;
	/** Enables bounded server-side travel sampling; one count = one metre. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) bool bTravelObjective = false;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) bool bHasMapTarget = false;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FVector MapTarget = FVector::ZeroVector;
};

/** GT-scoped view. Rebuilt per operation so edited/replaced definitions cannot leave stale ID caches. */
using FProject_JQuestDefinitionIndex = TMap<FName, UProject_JQuestDefinition*>;

USTRUCT(BlueprintType)
struct FProject_JQuestState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FName QuestId;
	/** Used only by legacy single objectives; compound states keep this zero. */
	UPROPERTY(BlueprintReadOnly) int32 Count = 0;
	int32 GetObjectiveCount(FName Id) const
	{ const auto* Found = Objectives.FindByPredicate([Id](const auto& S) { return S.ObjectiveId == Id; }); return Found ? Found->Count : 0; }
	UPROPERTY(BlueprintReadOnly) bool bClaimed = false;
	UPROPERTY(BlueprintReadOnly) bool bTracked = true;
	// Append new storage after the original four fields to preserve native aggregate callers.
	UPROPERTY(BlueprintReadOnly) TArray<FProject_JQuestObjectiveState> Objectives;
	bool operator==(const FProject_JQuestState& Other) const
	{ return QuestId == Other.QuestId && Count == Other.Count && Objectives == Other.Objectives && bClaimed == Other.bClaimed && bTracked == Other.bTracked; }
};
USTRUCT()
struct FProject_JQuestSnapshot
{
	GENERATED_BODY()
	UPROPERTY() int32 Version = 1;
	UPROPERTY() TArray<FProject_JQuestState> States;
    /** Hot metadata only; a decoded positive head must be hydrated before runtime installation. */
    UPROPERTY() int32 HistoryCount = 0;
    UPROPERTY() FString HistoryRootDigest;
    ProjectJ::QuestHistory::FViewPtr History; // Native immutable values; intentionally not a reflected flat history.
    bool HasHydratedHistory() const
    {
        return HistoryCount >= 0 && HistoryCount <= ProjectJ::QuestHistory::MaxIds
            && HistoryCount == ProjectJ::QuestHistory::Count(History)
            && HistoryRootDigest == ProjectJ::QuestHistory::RootDigest(History);
    }
};

UENUM(BlueprintType)
enum class EProject_JQuestCompletionStatus : uint8 { Unknown, NotCompleted, Completed };
USTRUCT(BlueprintType)
struct FProject_JQuestHistorySummary
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) int32 Count = 0;
    UPROPERTY(BlueprintReadOnly) FGuid Revision;
    bool operator==(const FProject_JQuestHistorySummary& Other) const { return Count == Other.Count && Revision == Other.Revision; }
};
UENUM(BlueprintType)
enum class EProject_JQuestHistoryPageStatus : uint8 { Empty, Pending, Fresh, Stale, Rejected, Unknown };
USTRUCT(BlueprintType)
struct FProject_JQuestHistoryPageView
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FProject_JQuestHistorySummary Summary;
    UPROPERTY(BlueprintReadOnly) int32 Ordinal = 0;
    UPROPERTY(BlueprintReadOnly) TArray<FName> Ids;
    UPROPERTY(BlueprintReadOnly) bool bHasPage = false;
    UPROPERTY(BlueprintReadOnly) FGuid Request;
    UPROPERTY(BlueprintReadOnly) int32 RequestedOrdinal = 0;
    UPROPERTY(BlueprintReadOnly) EProject_JQuestHistoryPageStatus Status = EProject_JQuestHistoryPageStatus::Empty;
};

/** Trusted, bounded native input; mapped from an already accepted live objective on GT. */
struct FProject_JQuestProgressInput
{
	FName Quest, Objective;
	int32 Current = 0, Required = 0, Amount = 0;
};

/** PlayerState-owned, owner-only quest progress. Reward amounts never come from clients. */
UCLASS(Config=Game, DefaultConfig, ClassGroup = (Gameplay), meta = (BlueprintSpawnableComponent))
class PROJECT_J_API UProject_JQuestComponent : public UActorComponent
{
	GENERATED_BODY()
  public:
	UProject_JQuestComponent();
	/** Explicit GT bootstrap shared by BeginPlay and pre-BeginPlay profile open. */
	bool PrepareForProfile();
	bool EnsureDefinitionsResolved();
	/** Mutates only a captured Proposed value, before the existing native commit. */
	bool PlanActivityProgress(FProject_JQuestSnapshot& Proposed, EProject_JQuestObjectiveKind Kind,
		FName TargetId, int32 TargetVersion, int64 Amount, const FProject_JQuestDefinitionIndex& Index) const;
	bool PlanAcceptance(FName Id, FProject_JQuestSnapshot& Proposed) const;
	static bool PlanTracking(FName Id, bool bTracked, FProject_JQuestSnapshot& Proposed);
	static constexpr int32 MaxObjectivesPerQuest = 8;
    static constexpr int32 MaxCurrentQuestStates = 128;
    /** Failure preserves the supplied value. Legacy definitions must be validated by the caller first. */
    static bool NormalizeClaimed(FProject_JQuestSnapshot& Snapshot);
    /** Caller verifies completion/reward eligibility; this reserves the completion and removes its current detail. */
    static bool PlanClaimCompletion(FName Id, FProject_JQuestSnapshot& Snapshot);
    UFUNCTION(BlueprintPure) EProject_JQuestCompletionStatus GetCompletionStatus(FName Id) const;
    /** Known-positive convenience only: Unknown also returns false. Use the enum for decisions on clients. */
    UFUNCTION(BlueprintPure) bool HasCompleted(FName Id) const { return GetCompletionStatus(Id) == EProject_JQuestCompletionStatus::Completed; }
    UFUNCTION(BlueprintPure) int32 GetCompletedCount() const;
    FProject_JQuestHistorySummary GetHistorySummary() const { return HistorySummary; }
    FProject_JQuestHistoryPageView GetHistoryPage() const;
    FGuid RequestCompletedPage(int32 Ordinal);
    /** Exact outstanding request only; no broadcast during an old UI's source cleanup. */
    void CancelHistoryPageRequest(FGuid ExpectedNonce);
	FProject_JPersistentSubmit SubmitPersistentEdit;
	TFunction<bool(const TArray<FProject_JQuestProgressInput>&)> SubmitPersistentProgress;
	TFunction<FGuid()> CreatePersistentRequest;
	TFunction<bool()> PersistentAdmission;
	bool ValidateCommittedQuests(const FProject_JQuestSnapshot &Snapshot, bool bTrustedApply = false) const;
	bool InstallCommittedQuests(const FProject_JQuestSnapshot &Snapshot);
	void PublishCommittedQuests()
	{
		Publish();
	}
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty> &Out) const override;
	UPROPERTY(EditDefaultsOnly) TArray<TObjectPtr<UProject_JQuestDefinition>> Definitions;
	/** Current details only after schema3 promotion; full completion history is queried separately. */
	UPROPERTY(ReplicatedUsing = OnRep_States, BlueprintReadOnly) TArray<FProject_JQuestState> States;
	UFUNCTION(BlueprintPure) UProject_JQuestDefinition *FindDefinition(FName Id) const;
	void BuildDefinitionIndex(FProject_JQuestDefinitionIndex& Out) const;
	/** Profile acceptance returns admission only; observe OnChanged/committed States before consuming progress. Without persistence it remains synchronous. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly) bool Accept(FName Id);
	/** Compatible void entry; shares TryRecordObjective's implementation but cannot report admission. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly) void RecordObjective(FName Event, int32 Count = 1);
	/** GT trusted server input. True means accepted or a valid no-op (no matching/incomplete objective).
	 * False means invalid/retired/unavailable; accepted pending progress is not a durable acknowledgement. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly) bool TryRecordObjective(FName Event, int32 Count = 1);
	UFUNCTION(BlueprintCallable) void RequestClaim(FName Id);
	/** Persistent tracking is visible only after commit; a rejected/pending request does not toggle live UI state. */
	UFUNCTION(BlueprintCallable) void RequestTracking(FName Id, bool bTracked);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly) bool Claim(FName Id);
	/** Full native capture is authority-only. Clients receive an explicitly unhydrated value; UI uses States/Summary/Page. */
	FProject_JQuestSnapshot CaptureSnapshot() const;
	bool CanRestoreSnapshot(const FProject_JQuestSnapshot &Snapshot, bool bReplacePristineDefaults = false) const;
	/** Replacement is restricted to fresh, unpossessed owners with untouched automatic defaults. */
	bool RestoreSnapshot(const FProject_JQuestSnapshot &Snapshot, bool bReplacePristineDefaults = false);
	DECLARE_MULTICAST_DELEGATE(FChanged);
	FChanged OnChanged;
    /** Local query/presentation updates never wake gameplay/checkpoint OnChanged subscribers. */
    FChanged OnHistoryPageChanged;

  private:
	friend struct FProject_JNativeQuestRewardCommit;
	friend class FProjectJQuestLifetimeTest;
	friend class FProjectJQuestExecutionTest;
	UFUNCTION() void OnRep_States();
    UFUNCTION() void OnRep_HistorySummary();
    UFUNCTION(Server, Reliable) void ServerRequestCompletedPage(FGuid Request, int32 Ordinal, FGuid ExpectedRevision);
    UFUNCTION(Client, Reliable) void ClientCompletedPage(FGuid Request, int32 Ordinal, FProject_JQuestHistorySummary Summary, const TArray<FName>& Ids, bool bAccepted);
    void RefreshHistorySummary();
    bool GetCompletionPageValues(int32 Ordinal, TArray<FName>& Out) const;
    ProjectJ::QuestHistory::FViewPtr History;
    int32 SnapshotVersion = 1;
    UPROPERTY(ReplicatedUsing=OnRep_HistorySummary) FProject_JQuestHistorySummary HistorySummary;
    FProject_JQuestHistoryPageView HistoryPage;
    FString SummaryRootDigest;
    TArray<FName> SummaryLegacyIds;
    FGuid PendingHistoryRequest;
    FGuid PendingHistoryRevision;
    FGuid PendingHistoryCharacter;
    int32 PendingHistoryOrdinal = 0;
    double HistoryRequestStarted = -1, NextHistoryRequestTime = 0;
    bool bRequestingHistoryPage = false;
	UFUNCTION(Server, Reliable) void ServerClaim(FName Id);
	UFUNCTION(Server, Reliable) void ServerTracking(FName Id, bool bTracked);
	bool AllowRequest();
	void Publish(const FProject_JQuestDefinitionIndex* Index = nullptr);
	void CollectProgressInputs(FName Event, int32 Count, const FProject_JQuestDefinitionIndex& Index,
		const TSet<FName>* TravelEvents, TArray<FProject_JQuestProgressInput>& Out) const;
	void SampleTravel();
	void SampleTravelAt(double Now);
	bool CanMutate() const;
	bool CanPublish() const;
	void Retire();
	void ReconcileTravelSampling(const FProject_JQuestDefinitionIndex* Index = nullptr);
	FTimerHandle TravelTimer;
	TWeakObjectPtr<APawn> TravelPawn;
	FVector LastLocation = FVector::ZeroVector;
	double TravelRemainder = 0;
	double LastTravelSampleTime = -1, LastTravelSpeed = 0;
	double RequestWindow = -1;
	int32 RequestCount = 0;
	UPROPERTY(Config) TArray<TSoftObjectPtr<UProject_JQuestDefinition>> DefaultDefinitionPaths;
	bool bDefinitionsAttempted = false, bDefaultsInitialized = false;
	bool bChanging = false;
	bool bEnding = false;
};
