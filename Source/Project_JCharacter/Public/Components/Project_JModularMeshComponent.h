#pragma once

#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "Project_JModularMeshComponent.generated.h"

/** Modular visual part with explicit shared-pose or independent extra-bone evaluation. */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JModularMeshComponent : public USkeletalMeshComponent
{
	GENERATED_BODY()

public:
	UProject_JModularMeshComponent();
	virtual void Activate(bool bReset = false) override;
	virtual void SetSkeletalMesh(USkeletalMesh* NewMesh, bool bReinitPose = true) override;

	/** Authored parts may follow their skeletal attach parent without a BP construction chain.
	 * Leave disabled for socket attachments. AttachAndSetLeader enables the same lifecycle for runtime equipment.
	 * Parts requiring their own extra-bone graph must use AttachAndCopyPose instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="ModularMesh")
	bool bFollowAttachParentPose = false;
	/** The character coordinator may tune this cosmetic cloth instance. Disable when another system owns its interactor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="ModularMesh|Cloth")
	bool bAllowClothQualityBudget = true;
	/** Distant cloth keeps body collisions, using discrete detection. Local/full-quality cloth retains authored CCD. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="ModularMesh|Cloth")
	bool bAllowDiscreteRemoteClothCollision = true;

	/**
	 * Attaches this modular part to the main character mesh and forces it to use the main mesh's leader pose.
	 * This eliminates redundant animation evaluations for this component.
	 */
	UFUNCTION(BlueprintCallable, Category = "ModularMesh")
	void AttachAndSetLeader(USkeletalMeshComponent* MainMesh);

	/** Independent pose buffer for extra bones. The mesh's post-process ABP may simulate them. */
	UFUNCTION(BlueprintCallable, Category="ModularMesh")
	bool AttachAndCopyPose(USkeletalMeshComponent* MainMesh);
	/** Common bones must keep the same parent chain; extra peripheral bones are allowed. */
	static bool CanCopyBasePose(const USkeletalMesh* Source, const USkeletalMesh* Target);
protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void OnAttachmentChanged() override;
private:
	void RefreshAttachParentPose();
	void RefreshSharedPoseTick();
	void ClearPosePrerequisite();
	TWeakObjectPtr<USkeletalMeshComponent> PosePrerequisite;

};
