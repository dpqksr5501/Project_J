#pragma once

#include "Animation/AnimInstance.h"
#include "Project_JWingerAnimInstance.generated.h"

/** Visual wing gestures layered onto the owning mesh's locomotion pose. */
UCLASS()
class PROJECT_JCHARACTER_API UProject_JWingerAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    // Keep the first five indices stable for existing authored graphs.
    enum class EWingMotion : int32 { Idle, Walk, Run, Takeoff, Land, Air, Count };

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Wings")
    int32 WingMotionIndex = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Wings")
    float WingSprintBlendPercent = 0.f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Cloth")
    FRotator ClothRootRotation = FRotator::ZeroRotator;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Cloth")
    FRotator ClothHemRotation = FRotator::ZeroRotator;

    UPROPERTY(EditDefaultsOnly, Category="Wings", meta=(ClampMin="0"))
    float LandingDuration = 0.4f;

    UPROPERTY(EditDefaultsOnly, Category="Wings", meta=(ClampMin="0"))
    float TakeoffDuration = 0.f;

    virtual void NativeInitializeAnimation() override;
    virtual void NativeUninitializeAnimation() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

private:
    void ResetWingMotion();
    void UpdateClothMotion(const USkeletalMeshComponent& Mesh, const FVector& Forward, const FVector& Velocity, float DeltaSeconds);

    FVector ClothDrag = FVector::ZeroVector;
    FVector ClothFollow = FVector::ZeroVector;
    bool bWasAirborne = false;
    bool bHasMotionSample = false;
    uint64 AirEpisode = 0;
    float PhaseRemaining = 0.f;
    EWingMotion GroundMotion = EWingMotion::Idle;
};
