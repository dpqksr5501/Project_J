#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Project_JRetargetBodyProfile.generated.h"

class USkeleton;
class UIKRetargeter;
class UProject_JHandGripProfile;

/** Shared skeleton bridge. No gameplay, montage, equipment or physics ownership. */
UCLASS(BlueprintType)
class PROJECT_JCHARACTER_API UProject_JRetargetFamilyProfile : public UPrimaryDataAsset
{

	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retarget")
	TObjectPtr<USkeleton> SourceSkeleton = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retarget")
	TObjectPtr<USkeleton> TargetSkeleton = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Retarget")
	TObjectPtr<UIKRetargeter> Retargeter = nullptr;
	bool Matches(const USkeleton* Source, const USkeleton* Target) const;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};

/** Per-body calibration, independently reusable by classes/weapons on this skeleton family. */
UCLASS(BlueprintType)
class PROJECT_JCHARACTER_API UProject_JRetargetBodyProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Body")
	TObjectPtr<UProject_JRetargetFamilyProfile> Family = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Body")
	TObjectPtr<UProject_JHandGripProfile> HandGrip = nullptr;
	/** Named sets authored in the family's Retargeter. False preserves node settings. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Body")
	bool bOverrideRetargetSets = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Body", meta=(EditCondition="bOverrideRetargetSets"))
	TArray<FName> RetargetOverrideSets;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
