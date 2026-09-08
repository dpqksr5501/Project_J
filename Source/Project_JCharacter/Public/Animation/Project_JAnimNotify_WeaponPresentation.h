#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Project_JAnimNotify_WeaponPresentation.generated.h"

/**
 * Shared, weapon-agnostic notify that moves the equipped weapon visual between
 * the sockets authored by its WeaponPresentationProfile.
 *
 * This never equips, removes, grants, or revokes an item. Equipment remains
 * authoritative in EquipmentManager; this is presentation timing only.
 */
UCLASS(meta = (DisplayName = "Set Weapon Presentation Socket"))
class PROJECT_JCHARACTER_API UProject_JAnimNotify_WeaponPresentation : public UAnimNotify
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Presentation")
	EProject_JWeaponPresentationSocket Socket = EProject_JWeaponPresentationSocket::Drawn;

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
};
