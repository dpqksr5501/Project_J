#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Project_JMultiPartHitComponent.generated.h"

/** Hit volume attached to a giant boss bone/socket (BP04: Head, Body, Tail, Limbs). */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JBossHitPartSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|HitPart")
	FName PartName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|HitPart")
	FName SocketOrBoneName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|HitPart")
	FVector LocalOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|HitPart", meta = (ClampMin = "10.0"))
	float SphereRadius = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|HitPart", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float DamageMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|HitPart")
	bool bIsWeakpoint = false;
};

class USkeletalMeshComponent;

struct FProject_JBossPartSample
{
	double ServerTime = 0.0;
	TArray<FVector, TInlineAllocator<8>> WorldCenters;
};

/**
 * Opt-in multi-part hit volume & 30Hz ring-buffer SSR component for giant field/dungeon bosses (BP04).
 * Resolves single-capsule hit limitations on 15–30m bosses and reports exact part + weakpoint multiplier.
 */
UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JMultiPartHitComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	static constexpr int32 MaxParts = 8;
	static constexpr int32 MaxHistoryFrames = 18; // ~0.6s at 30Hz

	UProject_JMultiPartHitComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|HitParts")
	TArray<FProject_JBossHitPartSpec> HitParts;

	virtual void BeginPlay() override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void SetComponentTickEnabled(bool bEnabled) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Records current world-space part centers into the ring buffer (also callable directly by tests). */
	void RecordSample(double ServerTimeSeconds);

	/** Clears recorded history across spatial discontinuities (teleports / respawn). */
	void ResetHistory();

	/** Verifies a segment against rewound multi-part volumes at QueryTimeSeconds, preferring the highest multiplier hit. */
	bool VerifyRewoundSegmentHit(
		double QueryTimeSeconds,
		const FVector& SegmentStart,
		const FVector& SegmentEnd,
		float AttackRadius,
		FName& OutPartName,
		float& OutDamageMultiplier,
		FVector& OutHitCenter) const;

private:
	friend class UProject_JCombatSpatialHistorySubsystem;
	void StartHistoryCapture();
	void StopHistoryCapture();
	bool bUsingBatchCapture = false;
	bool bCaptureRequested = true;
	USkeletalMeshComponent* ResolveOwnerMesh() const;
	FVector ComputeLivePartCenter(const FProject_JBossHitPartSpec& Part, const USkeletalMeshComponent* Mesh) const;
	const FProject_JBossPartSample& GetHistoryAt(int32 ChronologicalIndex) const;

	mutable TWeakObjectPtr<USkeletalMeshComponent> CachedMesh;
	mutable bool bMeshLookupAttempted = false;
	FProject_JBossPartSample HistoryRing[MaxHistoryFrames];
	int32 HistoryStart = 0;
	int32 HistoryCount = 0;
};
