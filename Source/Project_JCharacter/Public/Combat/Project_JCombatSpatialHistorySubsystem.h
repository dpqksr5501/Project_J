#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Project_JCombatSpatialHistorySubsystem.generated.h"

class AActor;
class UCapsuleComponent;
class USkeletalMeshComponent;
class UProject_JMultiPartHitComponent;
class UProject_JServerSideRewindComponent;
class UProject_JCombatSpatialHistorySubsystem;

/** Single TG_PostPhysics tick function driving all authority SSR & MultiPart history captures in one contiguous pass. */
struct FProjectJCombatSpatialHistoryTickFunction : public FTickFunction
{
	TWeakObjectPtr<UProject_JCombatSpatialHistorySubsystem> Target;

	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override { return TEXT("FProjectJCombatSpatialHistoryTickFunction"); }
	virtual FName DiagnosticContext(bool bDetailed) override { return FName(TEXT("ProjectJCombatSpatialHistory")); }
};

/**
 * Data-oriented Tick Aggregation subsystem for authority combat spatial history (Lyra / AAA MMO pattern).
 * Replaces hundreds of scattered per-actor TG_PostPhysics component ticks with one linear cache-friendly pass.
 */
UCLASS()
class PROJECT_JCHARACTER_API UProject_JCombatSpatialHistorySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	void RegisterSSR(UProject_JServerSideRewindComponent* Component, AActor* Owner, UCapsuleComponent* Capsule, float IntervalSeconds);
	void UnregisterSSR(const UProject_JServerSideRewindComponent* Component);

	void RegisterMultiPart(UProject_JMultiPartHitComponent* Component, AActor* Owner, float IntervalSeconds);
	void UnregisterMultiPart(const UProject_JMultiPartHitComponent* Component);

	int32 GetRegisteredSSRCount() const { return SSRSlots.Num(); }
	int32 GetRegisteredMultiPartCount() const { return MultiPartSlots.Num(); }

	void TickBatch(float DeltaTime);

private:
	struct FSSRSlot
	{
		TWeakObjectPtr<UProject_JServerSideRewindComponent> Component;
		TWeakObjectPtr<AActor> Owner;
		TWeakObjectPtr<UCapsuleComponent> Capsule;
		double Interval = 1.0 / 30.0;
		double NextSampleTime = -1.0;
	};

	struct FMultiPartSlot
	{
		TWeakObjectPtr<UProject_JMultiPartHitComponent> Component;
		TWeakObjectPtr<AActor> Owner;
		TWeakObjectPtr<USkeletalMeshComponent> PosePrerequisite;
		double Interval = 1.0 / 30.0;
		double NextSampleTime = -1.0;
	};

	void EnsureTickRegistered();
	void RefreshTickEnabled();
	void RemoveMultiPartSlot(int32 Index);

	FProjectJCombatSpatialHistoryTickFunction PostPhysicsTick;
	TArray<FSSRSlot> SSRSlots;
	TArray<FMultiPartSlot> MultiPartSlots;
};
