#include "Authoring/Project_JMeshLODAuthoring.h"
#include "Authoring/Project_JMeshLODSettings.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "StaticMeshResources.h"
#include "AssetCompilingManager.h"
#include "LODUtilities.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Misc/Crc.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "EditorFramework/AssetImportData.h"

namespace ProjectJ::MeshLODAuthoring
{
const TCHAR* DefaultPolicyPath(){return TEXT("/Game/DataAssetSets/Editor/LOD/DA_MeshLOD_Default.DA_MeshLOD_Default");}
UProject_JMeshLODSettings* LoadPolicy(const FString& Path)
{return Cast<UProject_JMeshLODSettings>(StaticLoadObject(UProject_JMeshLODSettings::StaticClass(),nullptr,Path.IsEmpty() ? DefaultPolicyPath() : *Path,nullptr,LOAD_NoWarn|LOAD_Quiet));}
static void Finish(UObject* Asset)
{ TArray<UObject*> Pending{Asset}; FAssetCompilingManager::Get().FinishCompilationForObjects(Pending); }
static bool HasNonzeroMinLOD(UObject* Asset)
{
    // A single-LOD asset may currently clamp an authored platform minimum to 0.
    // Adding levels would activate that minimum and change local quality.
    for(const TCHAR* Name:{TEXT("MinLod"),TEXT("MinQualityLevelLod")})
        if(const auto* Struct=FindFProperty<FStructProperty>(Asset->GetClass(),Name))
        {
            const void* Data=Struct->ContainerPtrToValuePtr<void>(Asset);
            for(TFieldIterator<FProperty> It(Struct->Struct);It;++It)
            {
                if(const auto* Number=CastField<FNumericProperty>(*It);Number && Number->IsInteger() && Number->GetSignedIntPropertyValue(Number->ContainerPtrToValuePtr<void>(Data))!=0)return true;
                if(const auto* Map=CastField<FMapProperty>(*It))
                    if(const auto* Number=CastField<FNumericProperty>(Map->ValueProp);Number && Number->IsInteger())
                    {
                        FScriptMapHelper Values(Map,Map->ContainerPtrToValuePtr<void>(Data));
                        for(int32 I=0;I<Values.GetMaxIndex();++I)if(Values.IsValidIndex(I) && Number->GetSignedIntPropertyValue(Values.GetValuePtr(I))!=0)return true;
                    }
            }
        }
    return false;
}
bool ValidatePolicy(const UProject_JMeshLODSettings& Policy,FString& Error)
{
    if(!Policy.SkeletalLODs){Error=TEXT("Native skeletal LOD settings are missing.");return false;}
    auto Valid=[](float Fraction,float Screen,float PriorFraction,float PriorScreen)
    {return FMath::IsFinite(Fraction) && Fraction>0 && Fraction<PriorFraction && FMath::IsFinite(Screen) && Screen>0 && Screen<PriorScreen;};
    if(Policy.SkeletalLODs->GetNumberOfSettings()<2 || Policy.SkeletalLODs->GetNumberOfSettings()>8 || Policy.StaticMeshLODs.ReductionSettings.Num()<2 || Policy.StaticMeshLODs.ReductionSettings.Num()>8)
    {Error=TEXT("Both native recipes require 2..8 LODs including LOD0.");return false;}
    float Fraction=1,Screen=1;
    for(int32 I=1;I<Policy.SkeletalLODs->GetNumberOfSettings();++I)
    {
        const auto& S=Policy.SkeletalLODs->GetSettingsForLODLevel(I);const auto R=S.GetReductionSettings();
        for(TFieldIterator<FProperty> It(FSkeletalMeshOptimizationSettings::StaticStruct());It;++It)
            if(const auto* N=CastField<FNumericProperty>(*It);N && N->IsFloatingPoint() && !FMath::IsFinite(N->GetFloatingPointPropertyValue(N->ContainerPtrToValuePtr<void>(&R))))
            {Error=TEXT("Nonfinite native reduction field.");return false;}
        if(!Valid(R.NumOfTrianglesPercentage,S.ScreenSize.Default,Fraction,Screen) || !S.ScreenSize.PerPlatform.IsEmpty() ||
            S.BoneFilterActionOption!=EBoneFilterActionOption::Remove || !S.BoneList.IsEmpty() || S.BakePose || !S.BonesToPrioritize.IsEmpty() ||
            !S.SectionsToPrioritize.IsEmpty() || !S.bAllowMeshDeformer || !FMath::IsFinite(S.LODHysteresis) || S.LODHysteresis<0 ||
            !FMath::IsFinite(S.WeightOfPrioritization) || S.WeightOfPrioritization<0 || R.WeldingThreshold<0 || R.NormalsThreshold<0 || R.VolumeImportance<0 ||
            R.TerminationCriterion!=SMTC_NumOfTriangles || R.BaseLOD!=0 || R.bRecalcNormals)
        {Error=TEXT("Unsafe skeletal recipe: require decreasing finite ratios/screens, no bone removal/bake/skeleton-specific priorities/platform override or normal rebuild.");return false;}
        Fraction=R.NumOfTrianglesPercentage;Screen=S.ScreenSize.Default;
    }
    Fraction=Screen=1;
    for(int32 I=1;I<Policy.StaticMeshLODs.ReductionSettings.Num();++I)
    {
        const auto& S=Policy.StaticMeshLODs.ReductionSettings[I];
        if(!Valid(S.PercentTriangles,S.ScreenSize,Fraction,Screen)){Error=TEXT("Static recipe ratios and screens must decrease and be finite.");return false;}
        Fraction=S.PercentTriangles;Screen=S.ScreenSize;
    }
    for(const FString& Root:Policy.ValidationRoots) if(!Root.StartsWith(TEXT("/Game/")) || !Root.EndsWith(TEXT("/")) || Root.Contains(TEXT("..")))
    {Error=TEXT("Validation roots must be /Game/ folders ending with /.");return false;}
    return true;
}
FInspection Inspect(UObject* Asset,const UProject_JMeshLODSettings& Policy,ERole Role)
{
    FInspection R;
    if(!IsValid(Asset)){R.Reason=TEXT("Missing asset.");return R;}
    Finish(Asset);
    auto* Skel=Cast<USkeletalMesh>(Asset);auto* Static=Cast<UStaticMesh>(Asset);
    R.LODs=Skel ? Skel->GetLODNum() : Static ? Static->GetNumLODs() : 0;
    if(R.LODs>1){R.Status=EStatus::Existing;R.Reason=TEXT("Existing LOD chain preserved; this is not visual quality approval.");return R;}
    if(!Skel && !Static){R.Status=EStatus::Review;R.Reason=TEXT("Use the asset's native LOD workflow (including Groom/bindings); no geometry conversion.");return R;}
    if(R.LODs!=1){R.Reason=TEXT("No valid LOD0.");return R;}
    if(HasNonzeroMinLOD(Asset)){R.Status=EStatus::Review;R.Reason=TEXT("Nonzero platform/quality MinLOD could become active after generation; preserve it and review local quality.");return R;}
    if(Role==ERole::Hair || Policy.ManualReviewAssets.Contains(FSoftObjectPath(Asset)))
    {R.Status=EStatus::Review;R.Reason=TEXT("Declare ordinary surface geometry explicitly; hair/fur/cards require manual review.");return R;}
    if(Skel && (Skel->HasActiveClothingAssets() || !Skel->GetMeshClothingAssets().IsEmpty() || !Skel->GetMorphTargets().IsEmpty() ||
        !Skel->GetSkinWeightProfiles().IsEmpty() || Skel->GetDefaultMeshDeformer() || Skel->GetTargetMeshDeformers() || Skel->GetNaniteSettings().bEnabled ||
        Skel->GetLODSettings()))
    {R.Status=EStatus::Review;R.Reason=TEXT("Cloth/morph/alternate weights/deformer/Nanite or an existing native LOD recipe requires its own review; data preserved.");return R;}
    if(Static && (Static->GetNaniteSettings().bEnabled || Static->GetLODGroup()!=NAME_None))
    {R.Status=EStatus::Review;R.Reason=TEXT("Existing Nanite/LODGroup policy preserved; use its native workflow.");return R;}
    if(Role!=ERole::Surface){R.Status=EStatus::Review;R.Reason=TEXT("Single LOD: explicitly review this as ordinary surface geometry before generating; no name-based hair guessing.");return R;}
    FString Error;if(!ValidatePolicy(Policy,Error)){R.Reason=Error;return R;}
    R.Status=EStatus::Missing;R.Reason=TEXT("Missing lower LODs; eligible for isolated preview/generation with this recipe.");return R;
}
static uint32 RenderCRC(const FStaticMeshVertexBuffers& Buffers,uint32 Count)
{
    uint32 Hash=0;auto Add=[&](const auto& V){Hash=FCrc::MemCrc32(&V,sizeof(V),Hash);};
    Add(Count);const auto UVs=Buffers.StaticMeshVertexBuffer.GetNumTexCoords();Add(UVs);
    for(uint32 V=0;V<Count;++V)
    {
        Add(Buffers.PositionVertexBuffer.VertexPosition(V));
        Add(Buffers.StaticMeshVertexBuffer.VertexTangentX(V));Add(Buffers.StaticMeshVertexBuffer.VertexTangentZ(V));
        for(uint32 U=0;U<UVs;++U) Add(Buffers.StaticMeshVertexBuffer.GetVertexUV(V,U));
        if(Buffers.ColorVertexBuffer.GetNumVertices()>V) Add(Buffers.ColorVertexBuffer.VertexColor(V));
    }
    return Hash;
}
FString ProtectedFingerprint(UObject* Asset)
{
    Finish(Asset);FString Text;
    for(const TCHAR* Name:{TEXT("Skeleton"),TEXT("PhysicsAsset"),TEXT("ShadowPhysicsAsset"),TEXT("Sockets"),
        TEXT("MinLod"),TEXT("MinQualityLevelLod"),TEXT("LODSettings"),TEXT("bSupportLODStreaming"),TEXT("bOverrideLODStreamingSettings"),
        TEXT("MaxNumStreamedLODs"),TEXT("MaxNumOptionalLODs"),TEXT("BodySetup"),TEXT("LODForCollision"),TEXT("bEnablePerPolyCollision")})
        if(const auto* P=FindFProperty<FProperty>(Asset->GetClass(),Name)){Text+=Name;P->ExportText_InContainer(0,Text,Asset,Asset,Asset,PPF_None);}
    if(const auto* P=FindFProperty<FObjectPropertyBase>(Asset->GetClass(),TEXT("AssetImportData")))
        if(const auto* Import=Cast<UAssetImportData>(P->GetObjectPropertyValue_InContainer(Asset)))Text+=Import->GetSourceData().ToJson();
    if(auto* Mesh=Cast<USkeletalMesh>(Asset))
    {
        const auto* Render=Mesh->GetResourceForRendering();if(!Render || Render->LODRenderData.IsEmpty())return TEXT("INVALID");
        const auto& L=Render->LODRenderData[0];uint32 H=RenderCRC(L.StaticVertexBuffers,L.GetNumVertices());auto Add=[&](const auto& V){H=FCrc::MemCrc32(&V,sizeof(V),H);};
        for(auto Material:Mesh->GetMaterials())
        {
            // Native reduction recomputes automatic streaming density across LODs.
            // Authored material/slot/explicit density overrides must stay exact.
            if(!Material.UVChannelData.bOverrideDensities)Material.UVChannelData=FMeshUVChannelInfo();
            FSkeletalMaterial::StaticStruct()->ExportText(Text,&Material,nullptr,nullptr,PPF_None,nullptr);
        }
        const auto* Indices=L.MultiSizeIndexContainer.GetIndexBuffer();for(int32 I=0;I<Indices->Num();++I)Add(Indices->Get(I));
        for(uint32 V=0;V<L.GetNumVertices();++V)for(uint32 I=0;I<L.SkinWeightVertexBuffer.GetMaxBoneInfluences();++I)
        {Add(L.SkinWeightVertexBuffer.GetBoneIndex(V,I));Add(L.SkinWeightVertexBuffer.GetBoneWeight(V,I));}
        for(const auto& S:L.RenderSections){Add(S.MaterialIndex);Add(S.BaseIndex);Add(S.NumTriangles);Add(S.bDisabled);for(auto B:S.BoneMap)Add(B);}
        for(auto B:L.RequiredBones)Add(B);for(auto B:L.ActiveBoneIndices)Add(B);
        Text+=FString::Printf(TEXT("|geometry=%u"),H);
        const auto& Ref=Mesh->GetRefSkeleton();for(int32 I=0;I<Ref.GetNum();++I)
        {
            Text+=Ref.GetBoneName(I).ToString()+FString::FromInt(Ref.GetParentIndex(I));
            const auto& T=Ref.GetRefBonePose()[I];const auto P=T.GetTranslation(),S=T.GetScale3D();const auto Q=T.GetRotation();
            Add(P.X);Add(P.Y);Add(P.Z);Add(S.X);Add(S.Y);Add(S.Z);Add(Q.X);Add(Q.Y);Add(Q.Z);Add(Q.W);
        }
        Text+=FString::Printf(TEXT("|refpose=%u"),H);
        const auto& Info=*Mesh->GetLODInfo(0);
        FPerPlatformFloat::StaticStruct()->ExportText(Text,&Info.ScreenSize,nullptr,nullptr,PPF_None,nullptr);
        FSkeletalMeshOptimizationSettings::StaticStruct()->ExportText(Text,&Info.ReductionSettings,nullptr,nullptr,PPF_None,nullptr);
    }
    else if(auto* StaticMesh=Cast<UStaticMesh>(Asset))
    {
        const auto* Render=StaticMesh->GetRenderData();if(!Render || Render->LODResources.IsEmpty())return TEXT("INVALID");
        const auto& L=Render->LODResources[0];uint32 H=RenderCRC(L.VertexBuffers,L.GetNumVertices());
        for(auto Material:StaticMesh->GetStaticMaterials())
        {if(!Material.UVChannelData.bOverrideDensities)Material.UVChannelData=FMeshUVChannelInfo();FStaticMaterial::StaticStruct()->ExportText(Text,&Material,nullptr,nullptr,PPF_None,nullptr);}
        const auto Indices=L.IndexBuffer.GetArrayView();for(int32 I=0;I<Indices.Num();++I){const auto V=Indices[I];H=FCrc::MemCrc32(&V,sizeof(V),H);}
        Text+=FString::Printf(TEXT("|geometry=%u"),H);
        const auto& S=StaticMesh->GetSourceModel(0);FMeshBuildSettings::StaticStruct()->ExportText(Text,&S.BuildSettings,nullptr,nullptr,PPF_None,nullptr);
        FMeshReductionSettings::StaticStruct()->ExportText(Text,&S.ReductionSettings,nullptr,nullptr,PPF_None,nullptr);
        for(int32 I=0;I<L.Sections.Num();++I){const auto Section=StaticMesh->GetSectionInfoMap().Get(0,I);FMeshSectionInfo::StaticStruct()->ExportText(Text,&Section,nullptr,nullptr,PPF_None,nullptr);}
        if(auto* Body=StaticMesh->GetBodySetup())FKAggregateGeom::StaticStruct()->ExportText(Text,&Body->AggGeom,nullptr,nullptr,PPF_None,nullptr);
    }
    return Text;
}
bool ValidateGenerated(UObject* Asset,const UProject_JMeshLODSettings& Policy,FString& Error)
{
    Finish(Asset);
    if(auto* Mesh=Cast<USkeletalMesh>(Asset))
    {
        const auto* Render=Mesh->GetResourceForRendering();
        if(!Render || Render->LODRenderData.Num()!=Policy.SkeletalLODs->GetNumberOfSettings()){Error=TEXT("Generated skeletal LOD count mismatch.");return false;}
        uint32 Prior=MAX_uint32;
        for(const auto& L:Render->LODRenderData)
        {
            uint32 Tris=0;for(const auto& S:L.RenderSections)Tris+=S.NumTriangles;
            if(!Tris || Tris>=Prior || L.RequiredBones!=Render->LODRenderData[0].RequiredBones)
            {Error=TEXT("No actual triangle reduction or required-bone array changed; manual review.");return false;}Prior=Tris;
        }
    }
    else if(auto* StaticMesh=Cast<UStaticMesh>(Asset))
    {
        const auto* Render=StaticMesh->GetRenderData();if(!Render || Render->LODResources.Num()!=Policy.StaticMeshLODs.ReductionSettings.Num()){Error=TEXT("Generated static LOD count mismatch.");return false;}
        uint32 Prior=MAX_uint32;for(const auto& L:Render->LODResources)
        {const uint32 N=L.IndexBuffer.GetNumIndices()/3;if(!N || N>=Prior){Error=TEXT("No actual static triangle reduction; manual review.");return false;}Prior=N;}
    }
    else return false;
    return true;
}
bool Generate(UObject* Asset,const UProject_JMeshLODSettings& Policy,ERole Role,FString& Error)
{
    if(!Asset || (Asset->GetOutermost()!=GetTransientPackage() && !IsRunningCommandlet()))
    {Error=TEXT("Live editor source mutation is disabled; use preview or the isolated commandlet.");return false;}
    const auto State=Inspect(Asset,Policy,Role);if(State.Status!=EStatus::Missing){Error=State.Reason;return false;}
    const FString Before=ProtectedFingerprint(Asset);if(Before==TEXT("INVALID")){Error=TEXT("LOD0 render data missing.");return false;}
    if(auto* Mesh=Cast<USkeletalMesh>(Asset))
    {
        int32 Influences=1;for(const auto& S:Mesh->GetResourceForRendering()->LODRenderData[0].RenderSections)Influences=FMath::Max(Influences,S.MaxBoneInfluences);
        const auto* Platform=GetTargetPlatformManagerRef().GetRunningTargetPlatform();
        if(!FLODUtilities::RegenerateLOD(Mesh,Platform,Policy.SkeletalLODs->GetNumberOfSettings(),false,false)){Error=TEXT("Native reduction unavailable.");return false;}Finish(Mesh);
        for(int32 I=1;I<Policy.SkeletalLODs->GetNumberOfSettings();++I)
        {
            Policy.SkeletalLODs->SetLODSettingsToMesh(Mesh,I);auto& Info=*Mesh->GetLODInfo(I);
            Info.ReductionSettings.MaxBonesPerVertex=Influences;Info.ReductionSettings.bEnforceBoneBoundaries=true;
            FSkeletalMeshUpdateContext Context;Context.SkeletalMesh=Mesh;
            FLODUtilities::SimplifySkeletalMeshLOD(Context,I,Platform);Mesh->PostEditChange();Finish(Mesh);
        }
    }
    else if(auto* StaticMesh=Cast<UStaticMesh>(Asset))
    {
        // Use the native asset builder in both preview and commandlets. The
        // live-editor subsystem also manages GUI editors and is absent in a commandlet.
        const auto BaseBuild=StaticMesh->GetSourceModel(0).BuildSettings;
        const auto BaseReduction=StaticMesh->GetSourceModel(0).ReductionSettings;
        StaticMesh->SetNumSourceModels(Policy.StaticMeshLODs.ReductionSettings.Num());
        for(int32 I=1;I<Policy.StaticMeshLODs.ReductionSettings.Num();++I)
        {
            auto& Source=StaticMesh->GetSourceModel(I);Source.BuildSettings=BaseBuild;Source.ReductionSettings=BaseReduction;
            Source.ReductionSettings.BaseLODModel=0;Source.ReductionSettings.PercentTriangles=Policy.StaticMeshLODs.ReductionSettings[I].PercentTriangles;
            Source.ScreenSize.Default=Policy.StaticMeshLODs.ReductionSettings[I].ScreenSize;
        }
        StaticMesh->SetAutoComputeLODScreenSize(Policy.StaticMeshLODs.bAutoComputeLODScreenSize);
        StaticMesh->PostEditChange();Finish(StaticMesh);
    }
    const FString After=ProtectedFingerprint(Asset);
    if(After!=Before)
    {
        int32 At=0;while(At<FMath::Min(Before.Len(),After.Len()) && Before[At]==After[At])++At;
        Error=FString::Printf(TEXT("%s protected data changed at %d: before[%s] after[%s]; no source file saved."),*Asset->GetName(),At,
            *Before.Mid(FMath::Max(0,At-40),160),*After.Mid(FMath::Max(0,At-40),160));return false;
    }
    return ValidateGenerated(Asset,Policy,Error);
}
TStrongObjectPtr<UObject> Preview(UObject* Asset,const UProject_JMeshLODSettings& Policy,ERole Role,FString& Error)
{
    if(Inspect(Asset,Policy,Role).Status!=EStatus::Missing){Error=Inspect(Asset,Policy,Role).Reason;return {};}
    const FString Before=ProtectedFingerprint(Asset);
    TStrongObjectPtr<UObject> Copy(DuplicateObject(Asset,GetTransientPackage()));
    if(!Generate(Copy.Get(),Policy,Role,Error) || Before!=ProtectedFingerprint(Asset))return {};
    return Copy;
}
}
