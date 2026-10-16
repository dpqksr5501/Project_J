#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "Misc/Crc.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "AssetCompilingManager.h"

namespace
{
uint32 PoseGeometryCRC(const FSkeletalMeshLODRenderData& LOD)
{
    uint32 Hash=0;
    auto Add=[&](const auto& Value){Hash=FCrc::MemCrc32(&Value,sizeof(Value),Hash);};
    const auto Count=LOD.GetNumVertices(); Add(Count);
    const auto UVs=LOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords(); Add(UVs);
    const auto Influences=LOD.SkinWeightVertexBuffer.GetMaxBoneInfluences(); Add(Influences);
    for(uint32 V=0;V<Count;++V)
    {
        Add(LOD.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(V));
        Add(LOD.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentX(V));
        Add(LOD.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(V));
        for(uint32 UV=0;UV<UVs;++UV) Add(LOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(V,UV));
        if(LOD.StaticVertexBuffers.ColorVertexBuffer.GetNumVertices()>V) Add(LOD.StaticVertexBuffers.ColorVertexBuffer.VertexColor(V));
        for(uint32 I=0;I<Influences;++I)
        { Add(LOD.SkinWeightVertexBuffer.GetBoneIndex(V,I)); Add(LOD.SkinWeightVertexBuffer.GetBoneWeight(V,I)); }
    }
    const auto* Indices=LOD.MultiSizeIndexContainer.GetIndexBuffer();
    Add(Indices->Num());
    for(int32 I=0;I<Indices->Num();++I) Add(Indices->Get(I));
    Add(LOD.RenderSections.Num());
    for(const auto& Section:LOD.RenderSections)
    {
        Add(Section.MaterialIndex); Add(Section.BaseIndex); Add(Section.NumTriangles); Add(Section.bDisabled);
        Add(Section.BoneMap.Num()); for(const auto Bone:Section.BoneMap) Add(Bone);
    }
    Add(LOD.ActiveBoneIndices.Num()); for(const auto Bone:LOD.ActiveBoneIndices) Add(Bone);
    Add(LOD.RequiredBones.Num()); for(const auto Bone:LOD.RequiredBones) Add(Bone);
    return Hash;
}
}

// Read-only comparison against a preserved .uasset in an isolated mount. No
// authoring/save entry point, and no mount is created without explicit inputs.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMeshLODPreservation,"ProjectJ.Retarget.MeshLODPreservation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMeshLODPreservation::RunTest(const FString&)
{
    FString AssetPath, BaselineDirectory;
    if(!FParse::Value(FCommandLine::Get(),TEXT("ProjectJMeshLODAsset="),AssetPath) ||
        !FParse::Value(FCommandLine::Get(),TEXT("ProjectJMeshLODBaselineDir="),BaselineDirectory))
    { AddInfo(TEXT("No explicit baseline supplied; use production physics/LOD regression for the installed asset.")); return true; }
    BaselineDirectory.ReplaceInline(TEXT("\\"),TEXT("/"));
    if(!BaselineDirectory.EndsWith(TEXT("/"))) BaselineDirectory+=TEXT("/");
    const FString Mount=TEXT("/ProjectJValidationBaseline/");
    FPackageName::RegisterMountPoint(Mount,BaselineDirectory);
    ON_SCOPE_EXIT { FPackageName::UnRegisterMountPoint(Mount,BaselineDirectory); };
    auto* Mesh=LoadObject<USkeletalMesh>(nullptr,*AssetPath);
    if(!TestNotNull(TEXT("Explicit current mesh"),Mesh)) return false;
    const FString Name=Mesh->GetName();
    auto* Baseline=LoadObject<USkeletalMesh>(nullptr,*(Mount+Name+TEXT(".")+Name));
    if(!TestNotNull(TEXT("Preserved baseline mesh"),Baseline)) return false;
    TArray<UObject*> Pending{Mesh,Baseline}; FAssetCompilingManager::Get().FinishCompilationForObjects(Pending);
    const auto* CurrentRender=Mesh->GetResourceForRendering(); const auto* OriginalRender=Baseline->GetResourceForRendering();
    if(!TestNotNull(TEXT("Current render data"),CurrentRender) || !TestNotNull(TEXT("Baseline render data"),OriginalRender) ||
        !TestTrue(TEXT("Both have LOD0"),!CurrentRender->LODRenderData.IsEmpty() && !OriginalRender->LODRenderData.IsEmpty())) return false;
    const auto& Base=OriginalRender->LODRenderData[0];
    const uint32 CRC=PoseGeometryCRC(Base);
    TestEqual(TEXT("LOD0 geometry, weights, section bone maps and bone arrays are preserved"),PoseGeometryCRC(CurrentRender->LODRenderData[0]),CRC);
    TestTrue(TEXT("Material assignments unchanged"),Mesh->GetMaterials()==Baseline->GetMaterials());
    TestEqual(TEXT("Skeleton asset unchanged"),Mesh->GetSkeleton(),Baseline->GetSkeleton());
    TestEqual(TEXT("Physics asset unchanged"),Mesh->GetPhysicsAsset(),Baseline->GetPhysicsAsset());
    const auto& Ref=Mesh->GetRefSkeleton(); const auto& OriginalRef=Baseline->GetRefSkeleton();
    TestEqual(TEXT("Reference skeleton size unchanged"),Ref.GetRawBoneNum(),OriginalRef.GetRawBoneNum());
    if(Ref.GetRawBoneNum()==OriginalRef.GetRawBoneNum()) for(int32 I=0;I<Ref.GetRawBoneNum();++I)
    {
        TestEqual(TEXT("Reference bone name unchanged"),Ref.GetBoneName(I),OriginalRef.GetBoneName(I));
        TestEqual(TEXT("Reference parent unchanged"),Ref.GetParentIndex(I),OriginalRef.GetParentIndex(I));
        TestTrue(TEXT("Reference bone pose unchanged"),Ref.GetRefBonePose()[I].Equals(OriginalRef.GetRefBonePose()[I],0));
    }
    for(int32 I=0;I<CurrentRender->LODRenderData.Num();++I)
        TestTrue(TEXT("Each LOD keeps the exact required-bone array"),CurrentRender->LODRenderData[I].RequiredBones==Base.RequiredBones);
    AddInfo(FString::Printf(TEXT("Read-only preservation: LODs=%d FullLOD0CRC=%u RequiredBones=%d"),CurrentRender->LODRenderData.Num(),CRC,Base.RequiredBones.Num()));
    return true;
}
#endif
