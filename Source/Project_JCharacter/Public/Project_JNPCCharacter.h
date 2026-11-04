// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Optimization/Project_JNPCUpdateBudget.h"
#include "Project_JBaseCharacter.h"
#include "Project_JNPCCharacter.generated.h"

class AAIController;
class UProject_JNPCActionComponent;
class UProject_JNPCActivationComponent;
class UProject_JNPCActivationDefinition;
class UProject_JCombatHitValidationComponent;
class UProject_JCombatPresentationComponent;

/**
 * Base character class for NPCs and Monsters in Project J.
 * Does NOT inherit from MotionMatchingCharacter, as they do not use motion matching.
 */
UCLASS()
class PROJECT_JCHARACTER_API AProject_JNPCCharacter : public AProject_JBaseCharacter
{
	GENERATED_BODY()

public:
	AProject_JNPCCharacter();

	/** Authority-only finite command, using the existing path scheduler/NavMover writer.
	 * Destination is a fixed world-space navigation-feet location (abs <= 1e9), radius 1..2000.
	 * False is admission rejection (no completion); true is not proof of movement/arrival.
	 * An admitted completion reports actual arrival or cancellation/failure after exact action retirement. */
	bool StartNavigationTo(FVector Destination, double AcceptanceRadius, TFunction<void(bool)> OnStopped = {});
	/** Retires only this helper's action token. Keeps its controller for same-actor combat promotion. */
	void StopNavigation();

	/** Explicit hostile activation. Requires loaded definition and an idle action context;
	 * navigation must be retired explicitly. Lazily prepares missing native hit/presentation capabilities,
	 * retained until actor destruction; existing authored components/settings are preserved.
	 * No automatic BeginPlay activation or client RPC. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="NPC|Combat")
	bool StartCombatActions(const UProject_JNPCActivationDefinition* Definition, int32 TeamId);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="NPC|Combat")
	void StopCombatActions();
	/** Owns this exact activation lease, including its suspended action. */
	UFUNCTION(BlueprintPure, Category="NPC|Combat")
	bool HasCombatActions() const;

	/** Returns policy data only; no AI is throttled until its owner opts in. */
	float GetRecommendedAIUpdateInterval() const;
	/** Read-only policy access for consumer-local distance tiers; does not change global significance. */
	float GetRecommendedAIUpdateIntervalForTier(EProject_JNPCUpdateBudgetTier Tier) const;
	EProject_JNPCUpdateBudgetTier GetDecisionTierForDistance(double Distance, EProject_JNPCUpdateBudgetTier PreviousTier) const;
	EProject_JNPCUpdateBudgetTier GetNPCUpdateBudgetTier() const;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type Reason) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Optimization")
	bool bApplyDefaultNPCOptimizationPolicy = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Optimization", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float NPCNetCullDistance = 8000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Optimization", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float NPCNetUpdateFrequency = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Optimization", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float NPCMinNetUpdateFrequency = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Optimization")
	FProject_JNPCUpdateBudgetSettings NPCUpdateBudget;

	void ApplyDefaultNPCOptimizationPolicy();
private:
	friend class FNPCSpatialImportanceTest;
	bool HasValidNPCNetworkPolicy() const;
	bool HasUnclaimedRuntimeActions() const;
	bool AreCombatCapabilitiesReady(const UProject_JCombatHitValidationComponent* Hit, const UProject_JCombatPresentationComponent* Presentation) const;
	bool IsRuntimeControllerIdle(const AAIController* RuntimeAI) const;
	AAIController* PrepareRuntimeController(AController*& ExpectedController, TFunctionRef<bool()> IsCurrent);
	void OnNavigationStopped(uint64 Request, UProject_JNPCActionComponent* Source, bool bArrived);
	TWeakObjectPtr<AAIController> CreatedActionController;
	TWeakObjectPtr<UProject_JNPCActionComponent> NavigationAction;
	TFunction<void(bool)> NavigationStopped;
	TWeakObjectPtr<UProject_JNPCActivationComponent> CombatActivation;
	uint64 CombatRequest = 0, CombatToken = 0;
	uint64 NavigationRequest = 0, NavigationToken = 0;
	bool bPreparingRuntimeAction = false, bRuntimeActionEnding = false;
};
