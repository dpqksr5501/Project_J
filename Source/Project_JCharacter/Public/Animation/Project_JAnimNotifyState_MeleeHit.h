#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "GameplayTagContainer.h"
#include "Project_JAnimNotifyState_MeleeHit.generated.h"

UCLASS(Blueprintable, meta = (DisplayName = "Melee Hit Trace"))
class PROJECT_JCHARACTER_API UProject_JAnimNotifyState_MeleeHit : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UProject_JAnimNotifyState_MeleeHit();

	virtual void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference) override;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Trace")
	FName SocketName = FName("WeaponSocket_R");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Trace")
	float TraceRadius = 40.0f;

	/** Event tag to send to the instigator when a hit is registered. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Trace")
	FGameplayTag HitEventTag;
};
