#pragma once
#include "GameFramework/Pawn.h"
#include "MoverSimulationTypes.h"
#include "Movement/Project_JMoverPolicy.h"
#include "Project_JMoverPawn.generated.h"
class UCharacterMoverComponent;
class UNavMoverComponent;
class UCapsuleComponent;
class USkeletalMeshComponent;
class UAnimMontage;
/** Physical avatar foundation. Mover is the sole transform and prediction owner. */
UCLASS(Blueprintable)
class PROJECT_JCHARACTER_API AProject_JMoverPawn : public APawn, public IMoverInputProducerInterface, public IProject_JMoverPolicySource
{
 GENERATED_BODY()
public:
 static const FName MeshComponentName;
 AProject_JMoverPawn(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
 virtual void PostInitializeComponents() override;
 virtual void BeginPlay() override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 virtual FVector GetVelocity() const override;
 virtual FVector GetNavAgentLocation() const override;
 virtual void UpdateNavigationRelevance() override;
 virtual void ProduceInput_Implementation(int32 SimTimeMs, FMoverInputCmdContext& InputCmd) override;
 virtual FProject_JMoverPolicy ResolveMoverPolicy(const FProject_JMoverInputs& Requested,const FCharacterDefaultInputs* Input=nullptr) const override;
 virtual float GetMoverMaxSpeed(bool bAcceptedSprint) const override;
 void FaceGameplayDirection(FRotator Facing);
 UFUNCTION(BlueprintPure, Category="Movement") UCharacterMoverComponent* GetMover() const { return Mover; }
 UFUNCTION(BlueprintPure, Category="Animation") USkeletalMeshComponent* GetMesh() const { return Mesh; }
 UFUNCTION(BlueprintPure, Category="Animation") USkeletalMeshComponent* GetSourceMesh() const { return Mesh; }
 UFUNCTION(BlueprintPure, Category="Movement") UCapsuleComponent* GetCapsuleComponent() const { return Capsule; }
 UFUNCTION(BlueprintCallable, Category="Movement") virtual void Jump();
 UFUNCTION(BlueprintCallable, Category="Movement") virtual void StopJumping();
 UFUNCTION(BlueprintCallable, Category="Movement") bool CanJump() const { return CanJumpInternal_Implementation(); }
 virtual bool CanJumpInternal_Implementation() const;
 UFUNCTION(BlueprintNativeEvent, Category="Movement") void OnJumped();
 virtual void OnJumped_Implementation();
 UFUNCTION(BlueprintCallable, Category="Animation") float PlayAnimMontage(UAnimMontage* Montage, float Rate=1.f, FName StartSection=NAME_None);
 UFUNCTION(BlueprintCallable, Category="Animation") void StopAnimMontage(UAnimMontage* Montage=nullptr);
 UFUNCTION(BlueprintPure, Category="Animation") UAnimMontage* GetCurrentMontage() const;
 UFUNCTION(BlueprintCallable, Category="Movement") virtual void LaunchCharacter(FVector Velocity, bool bXYOverride, bool bZOverride);
 UFUNCTION(BlueprintCallable, Category="Movement") void SetStrafe(bool Value) { bStrafe=Value; }
 UFUNCTION(BlueprintCallable, Category="Movement") void SetSprint(bool Value) { bSprint=Value; }
 UFUNCTION(BlueprintCallable, Category="Movement") void SetWalk(bool Value) { bWalk=Value; }
 UFUNCTION(BlueprintCallable, Category="Movement") void ToggleWalk() { bWalk=!bWalk; }
 UFUNCTION(BlueprintCallable, Category="Movement") void SetMoveIntent(FVector2D Value) { MoveIntent=Value.GetClampedToMaxSize(1.); }
 UFUNCTION(BlueprintCallable, Category="Movement") void SetJumpPressed(bool Value);
 bool WantsStrafe() const { return bStrafe; }
 bool WantsSprint() const { return bSprint; }
 bool IsJumpEpisode() const { return bJumpEpisode; }
 UFUNCTION(BlueprintPure, Category="Movement",meta=(DevelopmentOnly)) FString InspectPhysicalLifecycle() const;
 FVector GetLastLandingVelocity() const { return LastLandingVelocity; }
 FVector GetBaseTranslationOffset() const;
 FQuat GetBaseRotationOffset() const;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Movement") TObjectPtr<UCapsuleComponent> Capsule;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Movement") TObjectPtr<UCharacterMoverComponent> Mover;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Movement") TObjectPtr<UNavMoverComponent> NavMover;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation") TObjectPtr<USkeletalMeshComponent> Mesh;
protected:
 virtual void Landed(const FHitResult& Hit);
 UFUNCTION(BlueprintImplementableEvent, meta=(DisplayName="On Landed")) void K2_OnLanded(const FHitResult& Hit);
 void PopulateMoverInput(FMoverInputCmdContext& InputCmd, FVector WorldIntent, bool bUseStrafe, bool bUseSprint, float Speed, bool bAllowJump=true, bool bUseVelocityInput=true,float AnalogAmount=-1.f);
private:
 UFUNCTION() void FinalizePhysicalLifecycle(const FMoverSyncState& State,const FMoverAuxStateContext& Aux);
 UFUNCTION() void UpdateBasedOrientation(const FTransform& Delta,const FMoverTimeStep& Step);
 FVector2D MoveIntent=FVector2D::ZeroVector;
 FVector LastOrientation=FVector::ForwardVector;
 FVector LastLandingVelocity=FVector::ZeroVector;
 FVector NavIntent=FVector::ZeroVector;
 FVector NavVelocity=FVector::ZeroVector;
 UPROPERTY() TObjectPtr<class UChooserTable> RotationOffsetChooser;
 uint64 LastControlFrame=MAX_uint64;
 float LastControlYaw=0.f;
 float ControlYawRate=0.f;
 float SmoothedAnalogInputAmount=1.f;
 float LastRotationOffset=0.f;
 uint8 LastMovementDirection=0;
 bool bWalk=false;
 bool bStrafe=false;
 bool bSprint=false;
 bool bJumpPressed=false;
 bool bJumpJustPressed=false;
 bool bJumpEpisode=false;
 uint32 ConsumedJumpSequence=0;
 uint32 ConsumedJumpLanding=0;
 uint32 ConsumedFallLanding=0;
 uint32 JumpRequestId=0;
};
