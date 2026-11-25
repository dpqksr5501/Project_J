#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Project_JCCResistanceComponent.generated.h"

/** Crowd-control category tracked for Diminishing Returns (BP04). */
UENUM(BlueprintType)
enum class EProject_JCCCategory : uint8
{
	Stun,
	Knockdown,
	Root,
	Silence,
	Slow
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FProjectJOnBossStaggered, float, GroggyDurationSeconds);

/**
 * Server-authoritative CC Diminishing Returns (100% -> 50% -> 25% -> Immune over 15s window),
 * SuperArmor/Invulnerable gating, and Giant Boss Stagger Bar (BP04).
 */
UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class PROJECT_JGAS_API UProject_JCCResistanceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JCCResistanceComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|CC", meta = (ClampMin = "1.0"))
	float DRWindowSeconds = 15.0f;

	/** Bosses ignore hard CC and instead accumulate Stagger damage into a Groggy window. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Boss")
	bool bIsBossCCImmune = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Boss", meta = (ClampMin = "0.0"))
	float MaxStagger = 1000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Boss")
	float CurrentStagger = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Boss", meta = (ClampMin = "1.0"))
	float GroggyDurationSeconds = 5.0f;

	UPROPERTY(BlueprintAssignable, Category = "Combat|Boss")
	FProjectJOnBossStaggered OnBossStaggered;

	/** Evaluates DR and immunity; returns effective CC duration (0.0f if immune/blocked). */
	UFUNCTION(BlueprintCallable, Category = "Combat|CC")
	float TryApplyCC(EProject_JCCCategory Category, float BaseDurationSeconds, double NowSeconds, bool bHasSuperArmor = false, bool bIsInvulnerable = false);

	/** Applies stagger damage to a boss; returns true when stagger breaks into Groggy. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Boss")
	bool ApplyStaggerDamage(float StaggerDamage, double NowSeconds);

	UFUNCTION(BlueprintPure, Category = "Combat|Boss")
	bool IsGroggy(double NowSeconds) const { return NowSeconds < GroggyEndTime; }

	void ResetAll();

private:
	struct FDRBucket
	{
		int32 Applications = 0;
		double LastEndOrApplyTime = -1000.0;
	};
	TMap<EProject_JCCCategory, FDRBucket> DRState;
	double GroggyEndTime = -1000.0;
};
