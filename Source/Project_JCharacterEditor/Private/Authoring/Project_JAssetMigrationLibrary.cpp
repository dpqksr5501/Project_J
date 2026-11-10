#include "Authoring/Project_JAssetMigrationLibrary.h"
#include "Animation/Project_JHandContact.h"
#include "Animation/Project_JHandGripProfile.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Serialization/JsonSerializer.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Animation/Skeleton.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Algo/AllOf.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Misc/PackageName.h"
#include "UObject/PerPlatformProperties.h"
#include "UObject/UnrealType.h"

bool UProject_JAssetMigrationLibrary::ResolveSkeletalWeaponGripPreview(USkeletalMeshComponent* Body,
    USkeletalMesh* Weapon, FName GripSocket, FTransform& OutWorldMount)
{
    using namespace Project_J::Animation;
    const auto* Controller = FindBodyContactController(Body);
    if (!Controller || !Weapon || !IsPositiveUniformContactScale(Body->GetComponentScale())) { return false; }

    const auto Calibration = Controller->GetHandGripCalibration();
    const auto Palm = ResolveHandContact(*Body, Calibration.PrimaryPalmSocketName, Calibration.PrimaryArm.Hand, false);
    FTransform Grip, RootInHand;
    if (!Palm.IsValid() || !ResolveFixedSkeletalGripInRoot(*Weapon, GripSocket, Grip)
        || !MakePrimaryGripAttachment(Grip, Palm.PalmInHand, Calibration.PrimaryHandOffset, RootInHand)) { return false; }

    const FTransform HandWorld = Body->GetBoneTransform(Palm.Hand, RTS_World);
    if (!IsPositiveUniformContactScale(HandWorld.GetScale3D())) { return false; }
    OutWorldMount = RootInHand * HandWorld;
    return !OutWorldMount.ContainsNaN();
}

bool UProject_JAssetMigrationLibrary::MatchAnimationPlaybackToAuthoredRate(UAnimSequence* Sequence)
{
    if (!IsValid(Sequence) || (GEditor && GEditor->PlayWorld) || !Sequence->GetDataModel()) { return false; }
    const FFrameRate AuthoredRate = Sequence->GetDataModel()->GetFrameRate();
    auto* Property = FindFProperty<FStructProperty>(UAnimSequence::StaticClass(), TEXT("PlatformTargetFrameRate"));
    if (!AuthoredRate.IsValid() || AuthoredRate.Numerator <= 0 || !Property
        || Property->Struct != FPerPlatformFrameRate::StaticStruct()) { return false; }
    auto* Playback = Property->ContainerPtrToValuePtr<FPerPlatformFrameRate>(Sequence);
    for (const auto& Override : Playback->PerPlatform)
    {
        if (Override.Value != AuthoredRate) { return false; }
    }
    if (Playback->Default == AuthoredRate) { return true; }

    // The data controller's frame rate controls authored keys only. This editor
    // property has its own resample/compression notification.
    Sequence->Modify();
    Sequence->PreEditChange(Property);
    Playback->Default = AuthoredRate;
    FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
    Sequence->PostEditChangeProperty(Event);
    return Sequence->GetSamplingFrameRate() == AuthoredRate;
}

bool UProject_JAssetMigrationLibrary::RebuildBlendSpaceInterpolation(UBlendSpace* BlendSpace)
{
    if (!IsValid(BlendSpace) || BlendSpace->GetBlendSamples().IsEmpty()) { return false; }
    const auto IsSampleIndex = [BlendSpace](int32 Index)
    {
        return BlendSpace->GetBlendSamples().IsValidIndex(Index);
    };
    const auto HasValidIndices = [&]()
    {
        const FBlendSpaceData& Data = BlendSpace->GetBlendSpaceData();
        return Algo::AllOf(BlendSpace->GetGridSamples(), [&](const FEditorElement& Element)
            {
                return Algo::AllOf(Element.Indices, [&](int32 Index) { return Index == INDEX_NONE || IsSampleIndex(Index); });
            }) &&
            Algo::AllOf(Data.Triangles, [&](const FBlendSpaceTriangle& Triangle) { return Algo::AllOf(Triangle.SampleIndices, IsSampleIndex); }) &&
            Algo::AllOf(Data.Segments, [&](const FBlendSpaceSegment& Segment) { return Algo::AllOf(Segment.SampleIndices, IsSampleIndex); });
    };
    const bool bHadValidIndices = HasValidIndices();
    BlendSpace->Modify();
    BlendSpace->ResampleData();
    if (!HasValidIndices()) { return false; }
    if (!bHadValidIndices)
    {
        UE_LOG(LogTemp, Display, TEXT("Rebuilt stale interpolation indices for %s."), *BlendSpace->GetPathName());
    }
    BlendSpace->MarkPackageDirty();
    return true;
}

USkeleton* UProject_JAssetMigrationLibrary::CreateSharedSkeleton(const TArray<USkeletalMesh*>& Meshes,const FString& PackageName)
{
    if (Meshes.IsEmpty() || !Meshes[0] || !PackageName.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(PackageName)) return nullptr;
    const auto& Ref=Meshes[0]->GetRefSkeleton();
    if (!Ref.GetNum()) return nullptr;
    auto* Existing=LoadObject<USkeleton>(nullptr,*FString::Printf(TEXT("%s.%s"),*PackageName,*FPackageName::GetLongPackageAssetName(PackageName)),nullptr,LOAD_NoWarn);
    for (const auto* Mesh:Meshes)
    {
        if (!Mesh || (Mesh->GetSkeleton() && Mesh->GetSkeleton()!=Existing)) return nullptr;
        const auto& Other=Mesh->GetRefSkeleton();
        if (Other.GetNum()!=Ref.GetNum()) return nullptr;
        for (int32 I=0;I<Ref.GetNum();++I)
            if (Other.GetBoneName(I)!=Ref.GetBoneName(I) || Other.GetParentIndex(I)!=Ref.GetParentIndex(I) || !Other.GetRefBonePose()[I].Equals(Ref.GetRefBonePose()[I],.001f)) return nullptr;
    }
    USkeleton* Skeleton=Existing;
    if (!Skeleton)
    {
        auto* Package=CreatePackage(*PackageName);
        const FName Name(*FPackageName::GetLongPackageAssetName(PackageName));
        if (FindObject<UObject>(Package,*Name.ToString())) return nullptr;
        Skeleton=NewObject<USkeleton>(Package,Name,RF_Public|RF_Standalone|RF_Transactional);
        if (!Skeleton->MergeAllBonesToBoneTree(Meshes[0])) return nullptr;
        FAssetRegistryModule::AssetCreated(Skeleton);
    }
    if (Skeleton->GetReferenceSkeleton().GetNum()!=Ref.GetNum()) return nullptr;
    Skeleton->SetPreviewMesh(Meshes[0]);Skeleton->MarkPackageDirty();
    for (auto* Mesh:Meshes) { Mesh->Modify();Mesh->SetSkeleton(Skeleton);Mesh->MarkPackageDirty(); }
    return Skeleton;
}

FString UProject_JAssetMigrationLibrary::InspectReferenceSkeleton(USkeletalMesh* Mesh)
{
    if (!Mesh) return TEXT("{}");
    const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (int32 I=0; I<Ref.GetNum(); ++I)
    {
        auto Row=MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("name"),Ref.GetBoneName(I).ToString());
        Row->SetNumberField(TEXT("parent"),Ref.GetParentIndex(I));
        Row->SetStringField(TEXT("local"),Ref.GetRefBonePose()[I].ToString());
        FTransform Component=Ref.GetRefBonePose()[I];
        for (int32 Parent=Ref.GetParentIndex(I); Parent!=INDEX_NONE; Parent=Ref.GetParentIndex(Parent))
            Component=Component*Ref.GetRefBonePose()[Parent];
        Row->SetStringField(TEXT("component"),Component.ToString());
        Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    auto Root=MakeShared<FJsonObject>(); Root->SetArrayField(TEXT("bones"),Rows);
    Root->SetStringField(TEXT("skeleton"),Mesh->GetSkeleton() ? Mesh->GetSkeleton()->GetPathName() : TEXT(""));
    Root->SetNumberField(TEXT("skeleton_bones"),Mesh->GetSkeleton() ? Mesh->GetSkeleton()->GetReferenceSkeleton().GetNum() : 0);
    const auto VectorJson=[](const FVector& V)
    {
        return TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)};
    };
    if (Ref.GetNum())
    {
        const auto& Pose=Ref.GetRefBonePose()[0];const FQuat Q=Pose.GetRotation();
        Root->SetArrayField(TEXT("root_translation"),VectorJson(Pose.GetTranslation()));
        Root->SetArrayField(TEXT("root_scale"),VectorJson(Pose.GetScale3D()));
        Root->SetArrayField(TEXT("root_rotation_xyzw"),{MakeShared<FJsonValueNumber>(Q.X),MakeShared<FJsonValueNumber>(Q.Y),MakeShared<FJsonValueNumber>(Q.Z),MakeShared<FJsonValueNumber>(Q.W)});
    }
    if (const auto* Model=Mesh->GetImportedModel();Model && Model->LODModels.Num())
    {
        TMap<FName,int32> Weighted;int32 Vertices=0,Triangles=0,Invalid=0,Unweighted=0;
        FVector3f Tip=FVector3f::ZeroVector;float TipDistance=-1;
        const FVector3f Origin=Ref.GetNum() ? FVector3f(Ref.GetRefBonePose()[0].GetTranslation()) : FVector3f::ZeroVector;
        for (const auto& Section:Model->LODModels[0].Sections)
        {
            Triangles+=Section.NumTriangles;
            for (const auto& Vertex:Section.SoftVertices)
            {
                ++Vertices;uint32 Sum=0;
                const float Distance=(Vertex.Position-Origin).SizeSquared();
                if (Distance>TipDistance) { TipDistance=Distance;Tip=Vertex.Position; }
                for (int32 I=0;I<MAX_TOTAL_INFLUENCES;++I)
                {
                    if (!Vertex.InfluenceWeights[I]) continue;
                    Sum+=Vertex.InfluenceWeights[I];
                    const int32 Local=Vertex.InfluenceBones[I];
                    if (!Section.BoneMap.IsValidIndex(Local) || Section.BoneMap[Local]>=Ref.GetNum()) { ++Invalid;continue; }
                    ++Weighted.FindOrAdd(Ref.GetBoneName(Section.BoneMap[Local]));
                }
                Unweighted+=Sum==0;
            }
        }
        auto Weights=MakeShared<FJsonObject>();
        for (const auto& Pair:Weighted) Weights->SetNumberField(Pair.Key.ToString(),Pair.Value);
        Root->SetObjectField(TEXT("weighted_vertex_counts"),Weights);
        Root->SetNumberField(TEXT("lod0_vertices"),Vertices);Root->SetNumberField(TEXT("lod0_triangles"),Triangles);
        Root->SetNumberField(TEXT("invalid_influences"),Invalid);Root->SetNumberField(TEXT("unweighted_vertices"),Unweighted);
        Root->SetArrayField(TEXT("farthest_vertex"),VectorJson(FVector(Tip)));
    }
    FString Json; FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json));
    return Json;
}

bool UProject_JAssetMigrationLibrary::AddMeshSocket(USkeletalMesh* Mesh,FName Name,FName Bone,FTransform Relative)
{
    if (!Mesh || Name.IsNone() || Mesh->GetRefSkeleton().FindBoneIndex(Bone)==INDEX_NONE || !Relative.IsValid()) return false;
    if (const auto* Existing=Mesh->FindSocket(Name))
        return Existing->BoneName==Bone && Existing->GetSocketLocalTransform().Equals(Relative,.001f);
    Mesh->Modify();
    auto* Socket=NewObject<USkeletalMeshSocket>(Mesh,NAME_None,RF_Transactional);
    Socket->SocketName=Name; Socket->BoneName=Bone;
    Socket->RelativeLocation=Relative.GetLocation();
    Socket->RelativeRotation=Relative.Rotator();
    Socket->RelativeScale=Relative.GetScale3D();
    Mesh->GetMeshOnlySocketList().Add(Socket);
    Mesh->MarkPackageDirty();
    return true;
}
