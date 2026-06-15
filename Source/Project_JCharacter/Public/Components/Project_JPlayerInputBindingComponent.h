#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Project_JPlayerInputBindingComponent.generated.h"

class AProject_JPlayerCharacter;
class UInputAction;
class UInputComponent;
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

private:
	void HandleMove(const FInputActionValue& Value);
	void HandleLook(const FInputActionValue& Value);
	void HandleMoveStopped();
	void HandleJumpStarted();
	void HandleJumpStopped();
	void HandlePrimarySkillPressed();
	void HandlePrimarySkillReleased();
	void HandleSecondarySkillPressed();
	void HandleSecondarySkillReleased();
	void HandleSkillModifierPressed();
	void HandleSkillModifierReleased();

	UPROPERTY(Transient)
	TObjectPtr<AProject_JPlayerCharacter> BoundPlayerCharacter = nullptr;
};
