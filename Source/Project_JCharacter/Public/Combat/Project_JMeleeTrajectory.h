#pragma once
#include "CoreMinimal.h"
#include "Project_JMeleeTrajectory.generated.h"

class USkeletalMesh;
class UAnimMontage;
struct FProject_JAuthoritativeTraceSegment;

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JMeleeTrajectoryKey
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Melee Path") float MontageTime=0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Melee Path") FVector MeshPosition=FVector::ZeroVector;
};

/** Authored gameplay geometry, independent of the current blended/IK pose.
 * World pieces join these keys after mapping through two observed physical-root
 * anchors: translation lerp and shortest quaternion slerp in montage time.
 * This contract does not reconstruct unsampled physics or render animation. */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JMeleeTrajectory
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Melee Path") bool bEnabled=false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Melee Path") int32 SchemaVersion=1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Melee Path") TSoftObjectPtr<USkeletalMesh> SourceMesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Melee Path") TSoftObjectPtr<UAnimMontage> SourceMontage;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Melee Path") int32 NotifyIndex=INDEX_NONE;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Melee Path") FName SocketName;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Melee Path") bool bUseBladeTipOffset=false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Melee Path") FVector BladeTipOffset=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Melee Path") float WindowStart=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Melee Path") float WindowEnd=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Melee Path") FString SourceStamp;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Melee Path") TArray<FProject_JMeleeTrajectoryKey> Keys;
    static constexpr int32 MaxKeys=64;
    FString GetValidationFailure() const;
    bool BuildWorldSegments(float PreviousMontageTime,float CurrentMontageTime,float From,float To,
        const FTransform& PreviousRoot,const FTransform& CurrentRoot,const FTransform& MeshToRoot,
        TArray<FProject_JAuthoritativeTraceSegment>& OutSegments) const;
};
