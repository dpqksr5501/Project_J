#include "Equipment/Project_JWeaponPresentationActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

AProject_JWeaponPresentationActor::AProject_JWeaponPresentationActor()
{
	// Presentation is reconstructed by the equipped character on each client.
	// Do not replicate a second actor or a per-frame transform for cosmetic motion.
	bReplicates = false;
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = false;

	WeaponRoot = CreateDefaultSubobject<USceneComponent>(TEXT("WeaponRoot"));
	SetRootComponent(WeaponRoot);

	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetupAttachment(WeaponRoot);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMesh->SetGenerateOverlapEvents(false);
	WeaponMesh->SetCanEverAffectNavigation(false);
	WeaponMesh->SetIsReplicated(false);
	// Both local and remote weapons use the mesh's screen-size LOD chain.
	// Possession must not pin a small, zoomed-out weapon to the most expensive LOD.
	// Explicit Blueprint/component forced LOD overrides remain engine-owned.
}

void AProject_JWeaponPresentationActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	for (USkeletalMeshComponent* Part : SkeletalVisuals) if (IsValid(Part)) Part->DestroyComponent();
	SkeletalVisuals.Reset();
	for (const auto& Definition : SkeletalParts)
	{
		auto* Part=NewObject<USkeletalMeshComponent>(this,NAME_None,RF_Transient);
		Part->SetupAttachment(WeaponRoot);
		Part->SetRelativeTransform(Definition.RelativeTransform);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetGenerateOverlapEvents(false);
		Part->SetCanEverAffectNavigation(false);
		Part->SetIsReplicated(false);
		if (!Definition.PartId.IsNone()) Part->ComponentTags.Add(Definition.PartId);
		Part->bEnableUpdateRateOptimizations=true;
		Part->bPauseAnims=true;
		Part->SetSkeletalMesh(bPresentationDrawn ? Definition.DrawnMesh : Definition.SheathedMesh);
		Part->RegisterComponent();
		const auto* Mesh=Part->GetSkeletalMeshAsset();
		Part->SetComponentTickEnabled(Mesh && Mesh->GetLODNum()>1);
		SkeletalVisuals.Add(Part);
	}
}

void AProject_JWeaponPresentationActor::SetPresentationDrawnState(bool bDrawn)
{
	bPresentationDrawn=bDrawn;
	for (int32 I=0; I<SkeletalVisuals.Num() && I<SkeletalParts.Num(); ++I)
	{
		auto* Part=SkeletalVisuals[I].Get();
		if (!IsValid(Part)) continue;
		auto* Mesh=bDrawn ? SkeletalParts[I].DrawnMesh.Get() : SkeletalParts[I].SheathedMesh.Get();
		if (Part->GetSkeletalMeshAsset()!=Mesh) Part->SetSkeletalMesh(Mesh);
		// UpdateLODStatus lives in the engine component tick. Keep it available
		// for authored LOD chains while the fixed reference pose skips animation.
		Part->SetComponentTickEnabled(Mesh && Mesh->GetLODNum()>1);
	}
}

TArray<UMeshComponent*> AProject_JWeaponPresentationActor::GetVisualMeshes() const
{
	TArray<UMeshComponent*> Result;
	if (WeaponMesh && WeaponMesh->GetStaticMesh()) Result.Add(WeaponMesh);
	for (USkeletalMeshComponent* Part : SkeletalVisuals)
		if (IsValid(Part) && Part->GetSkeletalMeshAsset()) Result.Add(Part);
	return Result;
}

#if WITH_EDITOR
EDataValidationResult AProject_JWeaponPresentationActor::IsDataValid(FDataValidationContext& Context) const
{
	bool bInvalid=Super::IsDataValid(Context)==EDataValidationResult::Invalid;
	TSet<FName> Ids;
	for (const auto& Part:SkeletalParts)
	{
		if (Part.PartId.IsNone() || Ids.Contains(Part.PartId) || (!Part.DrawnMesh && !Part.SheathedMesh))
		{
			Context.AddError(FText::FromString(TEXT("Weapon skeletal parts require unique non-empty PartIds and at least one visual variant.")));
			bInvalid=true;
		}
		Ids.Add(Part.PartId);
		for (const USkeletalMesh* Mesh:{Part.DrawnMesh.Get(),Part.SheathedMesh.Get()})
		{
			if (Mesh && !Mesh->GetSkeleton())
			{
				Context.AddError(FText::FromString(TEXT("Weapon skin requires a saved Skeleton asset; an in-memory import result is insufficient.")));
				bInvalid=true;
			}
			if (Mesh && Mesh->GetPostProcessAnimBlueprint())
			{
				Context.AddError(FText::FromString(TEXT("Reference-pose weapon parts cannot silently suppress an authored post-process ABP. Use an explicitly animated presentation for secondary motion.")));
				bInvalid=true;
			}
		}
	}
	return bInvalid ? EDataValidationResult::Invalid : EDataValidationResult::Valid;
}
#endif
