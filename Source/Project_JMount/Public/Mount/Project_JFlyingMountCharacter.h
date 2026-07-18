#pragma once

#include "CoreMinimal.h"
#include "Mount/Project_JMountCharacter.h"
#include "InputAction.h"
#include "Project_JFlyingMountCharacter.generated.h"

/** Shared aerial movement rules for wyverns, griffins, and dragons. */
UCLASS(Abstract, Blueprintable)
class PROJECT_JMOUNT_API AProject_JFlyingMountCharacter : public AProject_JMountCharacter
{
	GENERATED_BODY()
public:
	AProject_JFlyingMountCharacter();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Mount|Flight") bool BeginFlight();
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Mount|Flight") bool EndFlight();
	UFUNCTION(BlueprintCallable, Category="Mount|Flight") void SetGliding(bool bNewGliding);
	UFUNCTION(BlueprintPure, Category="Mount|Flight") bool IsGliding() const { return bIsGliding; }
	UFUNCTION(BlueprintPure, Category="Mount|Flight") bool IsFlyingMount() const;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
protected:
	void HandleMove(const struct FInputActionValue& Value);
	void HandleLook(const struct FInputActionValue& Value);
	void HandleAscend(const struct FInputActionValue& Value);
	void HandleDescend(const struct FInputActionValue& Value);
	void HandleTakeOff();
	void HandleDismount();
	UFUNCTION(Server, Reliable) void ServerRequestBeginFlight();
	UPROPERTY(EditDefaultsOnly, Category="Input") TObjectPtr<class UInputAction> MoveAction = nullptr;
	UPROPERTY(EditDefaultsOnly, Category="Input") TObjectPtr<class UInputAction> LookAction = nullptr;
	UPROPERTY(EditDefaultsOnly, Category="Input") TObjectPtr<class UInputAction> AscendAction = nullptr;
	UPROPERTY(EditDefaultsOnly, Category="Input") TObjectPtr<class UInputAction> DescendAction = nullptr;
	UPROPERTY(EditDefaultsOnly, Category="Input") TObjectPtr<class UInputAction> InteractAction = nullptr;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mount|Flight") float FlightSpeed = 1200.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mount|Flight") float GlideSpeed = 900.0f;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Mount|Flight") bool bIsGliding = false;
};
