#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MouseLookAction = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> SprintAction = nullptr;

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
UCLASS(ClassGroup=(Input), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JPlayerInputBindingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JPlayerInputBindingComponent();

	bool BindInput(UInputComponent* PlayerInputComponent, AProject_JPlayerCharacter* PlayerCharacter, const FProject_JPlayerInputActionSet& ActionSet);
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void HandleMove(const FInputActionValue& Value);
	void HandleLook(const FInputActionValue& Value);
	void HandleMoveStopped();
	void FinalizeMoveStopped();
	void CancelPendingMoveStopReconciliation();
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

	TObjectPtr<UProject_JSkillInputMappingData> ActiveSkillInputMappingData = nullptr;
	TMap<UInputAction*, FGameplayTag> ActiveDirectInputTags;
};
