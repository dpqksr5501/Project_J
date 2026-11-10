#pragma once
struct FProject_JQuestProgressInput;
#include "Async/Future.h"
#include "CoreMinimal.h"
#include "Inventory/Project_JPersistentEdit.h"
#include "MMO/Repository.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Project_JLocalProfileSubsystem.generated.h"

class AProject_JPlayerState;
struct FProject_JLocalProfileSnapshot;
struct FProject_JLocalProfileBinding;
namespace ProjectJ::MMO
{
class FAggregateGate;
enum class ECommitStatus : uint8;
} // namespace ProjectJ::MMO

/** Storage evidence only. ProvenAbsent describes the exact current-session/intent lookup instant,
 * not a permanent rejection receipt. A later resubmission of that key may commit.
 * Committed includes replay of the exact command; it never means a new grant was issued.
 * An exclusive-resource consumer must fence its old operation before releasing its reservation. */
enum class EProject_JProfileDurableOutcome : uint8 { Committed, ProvenAbsent, Unknown };
struct FProject_JProfileDurableResult
{
	FGuid Request;
	EProject_JProfileDurableOutcome Outcome = EProject_JProfileDurableOutcome::Unknown;
	ProjectJ::MMO::ECommitStatus StoreStatus = ProjectJ::MMO::ECommitStatus::Invalid;
	ProjectJ::MMO::ECommitStatus ReceiptStatus = ProjectJ::MMO::ECommitStatus::Invalid;
	bool bReceiptQueried = false;
};
using FProject_JProfileDurableCompletion = TFunction<void(const FProject_JProfileDurableResult&)>;

/** Standalone developer checkpoints, explicitly selected by a local launch flag.
 * Not account authentication, network persistence, or a durable economy service.
 * Existing progression, quest, inventory/equipment and crafting-job components remain the live gameplay writers.
 */
UCLASS()
class PROJECT_J_API UProject_JLocalProfileSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
  public:
	virtual void Initialize(FSubsystemCollectionBase &Collection) override;
	virtual void Deinitialize() override;
	void OpenProfile(AProject_JPlayerState *Player, const FString &Profile, TFunction<void(bool)> OnReady);
	void PlayerBegunPlay(AProject_JPlayerState *Player);
	void Detach(AProject_JPlayerState *Player);
	void RequestCheckpoint(AProject_JPlayerState *Player);
	bool IsReady(const AProject_JPlayerState *Player) const;
	bool IsCheckpointPending(const AProject_JPlayerState *Player) const;
	int64 GetCommittedRevision(const AProject_JPlayerState *Player) const;
	/** HUD projection only; never a second gameplay state owner. */
	FText GetStatusText(const AProject_JPlayerState *Player) const;
	static bool ResolveLocalIdentity(const FString &Profile, FGuid &Account, FGuid &Character);
	/** Offline migration uses the exact normal restore admission without installing state. */
	static bool ValidateMigrationSnapshot(AProject_JPlayerState *Player, const FProject_JLocalProfileSnapshot &Snapshot,
										  FString &Error);
	void RequestBackup(TFunction<void(bool, FString)> OnComplete);
	/** Session-issued identity for one prepared logical intent; invalid if the profile cannot issue a request. */
	FGuid NewRequestId(AProject_JPlayerState *Player);
	/** Optional value observer runs exactly once on GT per accepted job, before live projection/Complete.
	 * It survives owner/binding retirement, independently of this subsystem's weak lifetime.
	 * Receivers retain native values or weak UObject references and copy results they need later.
	 * World shutdown may retire a receiver: never mutate an ended/replacement world.
	 * Existing bool Complete still means successful live installation/publication and remains owner-fenced. */
	bool SubmitEdit(AProject_JPlayerState *Player, const FProject_JPersistentEdit &Edit,
					FProject_JPersistentCompletion Complete = {}, FProject_JProfileDurableCompletion OnDurable = {});
#if WITH_DEV_AUTOMATION_TESTS
	int32 GetNativeJobCountForTest() const
	{
		return Jobs.Num();
	}
	void LoseNextCommitAcknowledgementForTest(AProject_JPlayerState *Player);
	void SetCommandStageHookForTest(AProject_JPlayerState* Player, TFunction<void(const TCHAR*)> Hook);
	void HoldCommandCompletionsForTest(bool Hold);
	int32 GetHeldCompletionCountForTest() const
	{
		return HeldCompletionsForTest.Num();
	}
	void FailCommandWritesForTest(AProject_JPlayerState *Player);
#endif
  private:
	friend struct FProjectJQuestRewardProfileTestAccess;
	friend class FProjectJLocalProfileLifecycleTest;
	friend class FProjectJJobProfileRestoreTest;
	friend class FProjectJProfileUnknownShapeTest;
	friend class FProjectJProfileProgressCapacityTest;
	friend class FProjectJProfileProgressAdmissionTest;
	friend class FProjectJLocalProfileFailureTest;
	friend class FProjectJProfileMigrationTest;
	friend class FProjectJGameRetentionTest;
	friend class FProjectJProfileRuntimeCoverageTest;
	friend class FProjectJCraftingExecutionTest;
	friend class FProjectJGatheringExecutionTest;
	void CompleteEdit(const TSharedPtr<FProject_JLocalProfileBinding, ESPMode::ThreadSafe> &Binding, bool Confirmed,
					  bool FirstApplication, ProjectJ::MMO::ECommitStatus Status,
					  TSharedPtr<const FProject_JLocalProfileSnapshot, ESPMode::ThreadSafe> Values, int64 ProjectionRevision);
	bool ApplyConfirmedProjection(const TSharedPtr<FProject_JLocalProfileBinding, ESPMode::ThreadSafe>& Binding,
		TSharedPtr<const FProject_JLocalProfileSnapshot, ESPMode::ThreadSafe> Values, int64 ProjectionRevision);
	void CompleteProgress(const TSharedPtr<FProject_JLocalProfileBinding, ESPMode::ThreadSafe>& Binding,
		TSharedPtr<const FProject_JLocalProfileSnapshot, ESPMode::ThreadSafe> Values, int64 ProjectionRevision);
	bool QueueQuestProgress(const TSharedPtr<FProject_JLocalProfileBinding, ESPMode::ThreadSafe>& Binding,
		const TArray<FProject_JQuestProgressInput>& Inputs);
	void CompleteOpen(const TSharedPtr<FProject_JLocalProfileBinding, ESPMode::ThreadSafe> &Binding);
	void BindPersistentCommands(const TSharedPtr<FProject_JLocalProfileBinding, ESPMode::ThreadSafe> &Binding);
	bool IsCurrent(const TSharedPtr<FProject_JLocalProfileBinding, ESPMode::ThreadSafe> &Binding) const;
	TMap<TWeakObjectPtr<AProject_JPlayerState>, TSharedPtr<FProject_JLocalProfileBinding, ESPMode::ThreadSafe>>
		Bindings;
	TSharedPtr<ProjectJ::MMO::FAggregateGate, ESPMode::ThreadSafe> IOGate;
	FString DatabasePath;
	bool bClosing = false;
	TArray<TFuture<void>> Jobs;
#if WITH_DEV_AUTOMATION_TESTS
	bool bHoldCompletionsForTest = false;
	TArray<TFunction<void()>> HeldCompletionsForTest;
#endif
};
