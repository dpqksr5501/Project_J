#pragma once
#include "Async/Future.h"
#include "CoreMinimal.h"
#include "Inventory/Project_JPersistentEdit.h"
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

/** Standalone developer checkpoints, explicitly selected by a local launch flag.
 * Not account authentication, network persistence, or a durable economy service.
 * Existing progression and quest components remain the only live gameplay writers.
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
	/** Trusted local command; bool is admission, completion is durable confirmation. */
	FGuid NewRequestId(AProject_JPlayerState *Player);
	bool SubmitEdit(AProject_JPlayerState *Player, const FProject_JPersistentEdit &Edit,
					FProject_JPersistentCompletion Complete = {});
#if WITH_DEV_AUTOMATION_TESTS
	int32 GetNativeJobCountForTest() const
	{
		return Jobs.Num();
	}
	void LoseNextCommitAcknowledgementForTest(AProject_JPlayerState *Player);
	void HoldCommandCompletionsForTest(bool Hold);
	int32 GetHeldCompletionCountForTest() const
	{
		return HeldCompletionsForTest.Num();
	}
	void FailCommandWritesForTest(AProject_JPlayerState *Player);
#endif
  private:
	friend class FProjectJLocalProfileLifecycleTest;
	friend class FProjectJLocalProfileFailureTest;
	friend class FProjectJProfileMigrationTest;
	friend class FProjectJGameRetentionTest;
	friend class FProjectJProfileRuntimeCoverageTest;
	void CompleteEdit(const TSharedPtr<FProject_JLocalProfileBinding, ESPMode::ThreadSafe> &Binding, bool Confirmed,
					  bool FirstApplication, ProjectJ::MMO::ECommitStatus Status, TArray<uint8> Payload);
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
