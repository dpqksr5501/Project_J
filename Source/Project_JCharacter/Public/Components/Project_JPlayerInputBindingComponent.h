#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Input/Project_JMoveIntentState.h"
#include "Project_JPlayerInputBindingComponent.generated.h"

class AProject_JPlayerCharacter;
class UInputAction;
class UInputComponent;
class UProject_JSkillInputMappingData;
struct FInputActionValue;

USTRUCT(BlueprintType)
struct FProject_JPlayerInputActionSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> JumpAction = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction = nullptr;

	/** Optional semantic direction actions. They do not drive CharacterMovement. */
	TObjectPtr<UInputAction> MoveIntentForwardAction = nullptr;
	TObjectPtr<UInputAction> MoveIntentBackwardAction = nullptr;
	TObjectPtr<UInputAction> MoveIntentLeftAction = nullptr;
	TObjectPtr<UInputAction> MoveIntentRightAction = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MouseLookAction = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> SprintAction = nullptr;

 UPROPERTY() TObjectPtr<UInputAction> WalkAction = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> ToggleCombatAction = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> AttackAction = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> HeavyAttackAction = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> SkillModifierAction = nullptr;

	/** Shared class/job mapping asset for direct skill actions such as Q/R/T. */
	TObjectPtr<UProject_JSkillInputMappingData> SkillInputMappingData = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> InteractAction = nullptr;
};

/**
 * Owns EnhancedInput bindings for a player character.
 *
 * The component intentionally calls the character's existing public gameplay methods so
 * locomotion, motion matching, and server RPC timing stay unchanged.
 */
class UEnhancedInputComponent;

UCLASS(ClassGroup=(Input), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JPlayerInputBindingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JPlayerInputBindingComponent();
	void UnbindInput();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

	bool BindInput(UInputComponent* PlayerInputComponent, AProject_JPlayerCharacter* PlayerCharacter, const FProject_JPlayerInputActionSet& ActionSet);
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	friend class FProjectJActiveMoveReentryTest;
	friend class FProjectJAudit2SharedSkillHoldTest;
	bool bEnding = false;
	uint64 BindingRevision = 0;
	TWeakObjectPtr<UEnhancedInputComponent> BoundEnhancedInputComponent;
	TArray<uint32> OwnedBindingHandles;
	void HandleMove(const FInputActionValue& Value);
	void HandleLook(const FInputActionValue& Value);
	void HandleMoveStopped();
	void HandleMoveIntentDirectionStarted(EProject_JMoveIntentDirection Direction);
	void HandleMoveIntentDirectionStopped(EProject_JMoveIntentDirection Direction);
	void FinalizeMoveStopped();
	void FinalizeMoveStoppedForCharacter(AProject_JPlayerCharacter* PlayerCharacter);
	bool MayRetirePlayerState(AProject_JPlayerCharacter* PlayerCharacter, uint64 ReleaseRevision) const;
	bool IsCurrentBinding(AProject_JPlayerCharacter* PlayerCharacter, uint64 Revision) const;
	void CancelPendingMoveStopReconciliation();
	void QueueSemanticMoveIntentRefresh();
	void RefreshSemanticMoveIntent();
	void HandleJumpStarted();
	void HandleJumpStopped();
	void HandlePrimarySkillPressed();
	void HandlePrimarySkillReleased();
	void HandleSecondarySkillPressed();
	void HandleSecondarySkillReleased();
	void HandleSkillModifierPressed();
	void HandleSkillModifierReleased();
	void HandleDirectSkillActionPressed(UInputAction* InputAction);
	void HandleDirectSkillActionReleased(UInputAction* InputAction);
	void HandleInteract();

	UPROPERTY(Transient)
	TObjectPtr<AProject_JPlayerCharacter> BoundPlayerCharacter = nullptr;

	// Enhanced Input can report a Completed/Canceled edge while another mapping of
	// the same 2D Move Action becomes Triggered in the same input update (A+D,
	// W+S, gamepad direction changes). Defer semantic Stop until that update has
	// settled; gameplay movement remains immediate in HandleMove.
	bool bPendingMoveStopReconciliation = false;

	// Enhanced Input can dispatch several Boolean direction edges for one physical
	// chord change. Coalesce only this cosmetic semantic snapshot at the end of
	// the input update; IA_Move and CharacterMovement remain immediate.
	bool bPendingSemanticMoveIntentRefresh = false;
	/**
	 * Semantic direction input is an all-or-nothing optional presentation
	 * contract. A class/profile with only some of the four actions configured
	 * keeps the existing IA_Move Axis2D fallback instead of publishing a partial
	 * (and therefore misleading) semantic intent.
	 */
	bool bSemanticMoveIntentActionsBound = false;
	FProject_JMoveIntentState SemanticMoveIntent;

	UPROPERTY(Transient)
	TObjectPtr<UProject_JSkillInputMappingData> ActiveSkillInputMappingData = nullptr;
	struct FDirectInputPress { FGameplayTag InputTag; uint64 Receipt = 0; };
	TMap<UInputAction*, FDirectInputPress> ActiveDirectInputTags;
	uint64 NextDirectInputReceipt = 0;
};
