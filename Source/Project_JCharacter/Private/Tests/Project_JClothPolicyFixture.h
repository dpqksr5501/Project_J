#pragma once

#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"

/** Metadata-only admission fixture, not a cloth-solver or visual-quality test.
 * Editor HasActiveClothingAssets derives from render sections, not the cooked
 * cached bit. Use a private duplicated resource; never edit production data.
 */
inline USkeletalMesh* MakeProjectJClothPolicyFixture(USkeletalMesh* Base)
{
 auto* Mesh = DuplicateObject<USkeletalMesh>(Base, GetTransientPackage());
 auto* Render = Mesh->GetResourceForRendering();
 if (!Render || Render == Base->GetResourceForRendering() || Render->LODRenderData.IsEmpty() ||
     Render->LODRenderData[0].RenderSections.IsEmpty()) return nullptr;
 Render->LODRenderData[0].RenderSections[0].ClothingData.AssetGuid = FGuid::NewGuid();
 return Mesh;
}
