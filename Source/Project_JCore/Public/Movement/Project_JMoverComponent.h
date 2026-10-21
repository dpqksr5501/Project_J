#pragma once
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "MovementMode.h"
#include "Engine/EngineTypes.h"
#include "DefaultMovementSet/InstantMovementEffects/BasicInstantMovementEffects.h"
#include "DefaultMovementSet/Modes/FallingMode.h"
#include "DefaultMovementSet/Modes/WalkingMode.h"
#include "Movement/Project_JGroundMovement.h"
#include "Project_JMoverComponent.generated.h"

UCLASS()
class PROJECT_JCORE_API UProject_JFallingMode : public UFallingMode
{
 GENERATED_BODY()
public:
 UProject_JFallingMode(const FObjectInitializer& ObjectInitializer);
 virtual void GenerateMove_Implementation(const FMoverSimContext& SimContext,const FMoverTickStartData& Start,const FMoverTimeStep& Step,FProposedMove& Move) const override;
 /** GASP's falling rotation limit, applied in simulation time (including rollback). */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Mover",meta=(ClampMin="0",ForceUnits="deg/s")) float AirTurningRate=300.f;
 virtual void Activate(const FMoverEventContext& Context,FName Previous,const FMoverSimContext& SimContext,const FMoverTickStartData& Start,FMoverSyncState* Output,FMoverAuxStateContext* Aux) override;
 const FHitResult& GetLastLandingHit() const { return LastLandingHit; }
protected:
 virtual void ProcessLanded(const FFloorCheckResult& Floor,FVector Location,FVector& Velocity,FRelativeBaseInfo& Base,FMoverTickEndData& Output) const override;
private:
 mutable FHitResult LastLandingHit;
};

USTRUCT()
struct PROJECT_JCORE_API FProject_JFacingEffect : public FInstantMovementEffect
{
 GENERATED_BODY()
 UPROPERTY() FRotator Facing=FRotator::ZeroRotator;
 virtual bool ApplyMovementEffect(FApplyMovementEffectParams& Params, FMoverSyncState& Output) override;
 virtual void NetSerialize(FArchive& Ar) override { Super::NetSerialize(Ar); Ar<<Facing; }
 virtual FInstantMovementEffect* Clone() const override { return new FProject_JFacingEffect(*this); }
 virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
};

/** Records the confirmed physical jump in rollback-owned simulation state. */
USTRUCT()
struct PROJECT_JCORE_API FProject_JJumpImpulseEffect : public FJumpImpulseEffect
{
 GENERATED_BODY()
 virtual bool ApplyMovementEffect(FApplyMovementEffectParams& Params, FMoverSyncState& Output) override;
 virtual FInstantMovementEffect* Clone() const override { return new FProject_JJumpImpulseEffect(*this); }
 virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
};

/** Mounted/externally driven actors keep a stationary simulation instead of falling while attached. */
UCLASS()
class PROJECT_JCORE_API UProject_JAttachedMovementMode : public UBaseMovementMode
{
	GENERATED_BODY()
public:
	virtual void GenerateMove_Implementation(const FMoverSimContext&, const FMoverTickStartData&, const FMoverTimeStep&, FProposedMove& Move) const override;
	virtual void SimulationTick_Implementation(const FSimulationTickParams& Params, FMoverTickEndData& Output) override;
};

UCLASS(meta=(BlueprintSpawnableComponent))
class PROJECT_JCORE_API UProject_JMoverComponent : public UCharacterMoverComponent
{
	GENERATED_BODY()
public:
	UProject_JMoverComponent();
	virtual void PostInitProperties() override;
 virtual void OnRegister() override;
 /** Authored node adapter around the native solver. Native commands are the only simulation input. */
 UPROPERTY(EditDefaultsOnly,Category="Movement|Player Locomotion") TSoftClassPtr<UBaseMovementMode> GroundWalkingMode;
	UPROPERTY(EditDefaultsOnly,Category="Movement|Player Locomotion") FProject_JGroundMovementSettings GroundMovementSettings;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual bool Jump() override;
 virtual void OnPreSimulate(const FMoverTimeStep& Step,const FMoverTickStartData& Start) override;
 virtual void OnPostSimulate(const FMoverTimeStep& Step,const FMoverTickStartData& Start,FMoverTickEndData& End) override;
	void SetGameplayMovementMode(EMovementMode Mode);
 void QueueGameplayFacing(FRotator Facing);
 /** Attach movement extraction to an already playing montage; never starts a second montage. */
 void FollowGameplayMontage(class UAnimMontage* Montage);
 void StopGameplayMontageRootMotion(class UAnimMontage* ExpectedMontage=nullptr);
 /** Synchronous authority handover: writes the backend's pending state before acknowledging a pose. */
 bool ApplyAuthorityHandoverTransform(const FTransform& Transform);
private:
 UFUNCTION() void GameplayMontageSectionChanged(class UAnimMontage* Montage,FName Section,bool bLooped);
 UFUNCTION() void GameplayMontageEnded(class UAnimMontage* Montage,bool bInterrupted);
	UFUNCTION() void FinishExternalMovement(const FMoverSyncState& State, const FMoverAuxStateContext& Aux);
	FName PendingRestoreMode;
	bool bExternalMovementSession = false;
	bool bSavedExternalWarning = true;
 TWeakObjectPtr<class UAnimMontage> RootMotionMontage;
 int32 RootMotionMontageInstance=INDEX_NONE;
 TWeakObjectPtr<class UAnimInstance> RootMotionAnim;
};
