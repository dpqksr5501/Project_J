#include "Animation/Project_JPlayerLocomotionLibrary.h"
#include "Animation/AnimClassInterface.h"
#include "AnimNodes/AnimNode_RetargetPoseFromMesh.h"
#include "BoneControllers/AnimNode_RigidBody.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "ClothingSystemRuntimeTypes.h"
#include "ClothingAsset.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"

namespace
{
TArray<TSharedPtr<FJsonValue>> VectorJSON(const FVector& V)
{
	return {MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y), MakeShared<FJsonValueNumber>(V.Z)};
}
}

FString UProject_JPlayerLocomotionLibrary::InspectSkinnedMeshSurface(USkeletalMeshComponent* Mesh, int32 LODIndex)
{
	check(IsInGameThread());
	if (!IsValid(Mesh) || !Mesh->GetSkeletalMeshAsset()) return TEXT("{}");
	auto* PoseOwner = Cast<USkeletalMeshComponent>(Mesh->LeaderPoseComponent.Get());
	if (!PoseOwner) PoseOwner = Mesh;
	PoseOwner->HandleExistingParallelEvaluationTask(true, true);
	Mesh->WaitForExistingParallelClothSimulation_GameThread();
	const FSkeletalMeshRenderData* RenderData = Mesh->GetSkeletalMeshRenderData();
	if (!RenderData || !RenderData->LODRenderData.IsValidIndex(LODIndex)) return TEXT("{}");
	const FSkeletalMeshLODRenderData& LOD = RenderData->LODRenderData[LODIndex];
	const FSkinWeightVertexBuffer* Weights = Mesh->GetSkinWeightBuffer(LODIndex);
	if (!Weights || Weights->GetNumVertices() != LOD.GetNumVertices() || LOD.GetNumVertices() == 0) return TEXT("{}");
	TArray<FMatrix44f> RefToLocal;
	Mesh->GetCurrentRefToLocalMatrices(RefToLocal, LODIndex);
	TArray<FVector3f> Positions;
	USkinnedMeshComponent::ComputeSkinnedPositions(Mesh, Positions, RefToLocal, LOD, *Weights);
	TArray<uint32> Indices;
	LOD.MultiSizeIndexContainer.GetIndexBuffer(Indices);
	TArray<FVector> WorldPositions;
	const auto& ClothData = Mesh->GetCurrentClothingData_AnyThread();
	if (!ClothData.IsEmpty())
	{
		// Completed simulation particles and their native topology, rather than the
		// underlying bone-skinned input that the cloth replaces.
		Indices.Reset();
		const auto& Assets = Mesh->GetSkeletalMeshAsset()->GetMeshClothingAssets();
		for (const auto& Pair : ClothData)
		{
			if (!Assets.IsValidIndex(Pair.Key)) return TEXT("{}");
			const auto* Asset = Cast<UClothingAssetCommon>(Assets[Pair.Key]);
			if (!Asset || !Asset->LodData.IsValidIndex(Pair.Value.LODIndex)) return TEXT("{}");
			const auto& Physical = Asset->LodData[Pair.Value.LODIndex].PhysicalMeshData;
			if (Physical.Vertices.Num() != Pair.Value.Positions.Num()) return TEXT("{}");
			const uint32 Offset = WorldPositions.Num();
			for (const FVector3f& Position : Pair.Value.Positions)
				WorldPositions.Add(Pair.Value.Transform.TransformPosition(FVector(Position)));
			for (uint32 Index : Physical.Indices) Indices.Add(Offset + Index);
		}
	}
	else
		for (const FVector3f& Position : Positions)
			WorldPositions.Add(Mesh->GetComponentTransform().TransformPosition(FVector(Position)));
	const auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("mesh"), Mesh->GetPathName());
	Result->SetStringField(TEXT("asset"), Mesh->GetSkeletalMeshAsset()->GetPathName());
	Result->SetStringField(TEXT("leader"), GetPathNameSafe(Mesh->LeaderPoseComponent.Get()));
	Result->SetNumberField(TEXT("lod"), LODIndex);
	Result->SetNumberField(TEXT("bone_revision"), PoseOwner->GetBoneTransformRevisionNumber());
	Result->SetStringField(TEXT("surface_source"), ClothData.IsEmpty() ? TEXT("bone_skinning") : TEXT("cloth_simulation"));
	TArray<TSharedPtr<FJsonValue>> Vertices, Triangles;
	Vertices.Reserve(WorldPositions.Num());
	bool bFinite = true;
	for (const FVector& WorldPosition : WorldPositions)
	{
		bFinite &= !WorldPosition.ContainsNaN();
		Vertices.Add(MakeShared<FJsonValueArray>(VectorJSON(WorldPosition)));
	}
	Triangles.Reserve(Indices.Num());
	for (uint32 Index : Indices) Triangles.Add(MakeShared<FJsonValueNumber>(Index));
	Result->SetBoolField(TEXT("finite"), bFinite);
	Result->SetArrayField(TEXT("positions"), Vertices);
	Result->SetArrayField(TEXT("indices"), Triangles);
	FString Text; FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Text)); return Text;
}

FString UProject_JPlayerLocomotionLibrary::InspectSecondaryPhysics(USkeletalMeshComponent* Mesh)
{
	check(IsInGameThread());
	if (!IsValid(Mesh) || !Mesh->GetSkeletalMeshAsset()) return TEXT("{}");
	Mesh->HandleExistingParallelEvaluationTask(true, true);
	Mesh->WaitForExistingParallelClothSimulation_GameThread();
	const auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("mesh"), Mesh->GetPathName());
	Result->SetStringField(TEXT("asset"), Mesh->GetSkeletalMeshAsset()->GetPathName());
	Result->SetStringField(TEXT("leader"), GetPathNameSafe(Mesh->LeaderPoseComponent.Get()));
	Result->SetBoolField(TEXT("tick"), Mesh->IsComponentTickEnabled());
	Result->SetBoolField(TEXT("anim_instance"), Mesh->GetAnimInstance() != nullptr);
	Result->SetBoolField(TEXT("active_cloth_asset"), Mesh->GetSkeletalMeshAsset()->HasActiveClothingAssets());
	Result->SetNumberField(TEXT("cloth_instances"), Mesh->GetClothingSimulationInstances().Num());
	Result->SetNumberField(TEXT("bone_revision"), Mesh->GetBoneTransformRevisionNumber());
	const auto& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
	const auto& Final = Mesh->GetComponentSpaceTransforms();
	const TArray<FTransform>* Base = nullptr;
	UAnimInstance* Anim = Mesh->GetAnimInstance();
	const auto* Interface = Anim ? IAnimClassInterface::GetFromClass(Anim->GetClass()) : nullptr;
	if (Interface)
		for (auto* Property : Interface->GetAnimNodeProperties())
			if (Property->Struct == FAnimNode_RetargetPoseFromMesh::StaticStruct())
			{
				auto* Node = Property->ContainerPtrToValuePtr<FAnimNode_RetargetPoseFromMesh>(Anim);
				if (const auto* Processor = Node->GetRetargetProcessor(); Processor && Processor->IsInitialized())
					Base = &Processor->GetTargetSkeleton().OutputGlobalPose;
			}
	TArray<TSharedPtr<FJsonValue>> Nodes, Duplicates;
	TSet<FName> DynamicOwners;
	if (Interface)
		for (auto* Property : Interface->GetAnimNodeProperties())
			if (Property->Struct == FAnimNode_RigidBody::StaticStruct())
			{
				auto* Node = Property->ContainerPtrToValuePtr<FAnimNode_RigidBody>(Anim);
				const auto Entry = MakeShared<FJsonObject>();
				const UPhysicsAsset* PA = Node->GetPhysicsAsset();
				if (!PA) PA = Node->OverridePhysicsAsset.Get();
				Entry->SetStringField(TEXT("asset"), GetPathNameSafe(PA));
				// Do not inspect mutable immediate-physics actor arrays while a deferred
				// simulation can be running. Completed mesh poses prove visible output.
				Entry->SetBoolField(TEXT("simulation_created"), Node->GetSimulation() != nullptr);
				TArray<TSharedPtr<FJsonValue>> Bones;
				if (PA) for (const USkeletalBodySetup* Setup : PA->SkeletalBodySetups)
				{
					if (!Setup || !(Setup->PhysicsType == PhysType_Simulated ||
						(Node->bUseDefaultAsSimulated && Setup->PhysicsType == PhysType_Default))) continue;
					const FName Name = Setup->BoneName;
					if (DynamicOwners.Contains(Name)) Duplicates.Add(MakeShared<FJsonValueString>(Name.ToString()));
					DynamicOwners.Add(Name);
					const auto Bone = MakeShared<FJsonObject>();
					Bone->SetStringField(TEXT("name"), Name.ToString());
					const int32 Index = Skeleton.FindBoneIndex(Name), Parent = Index != INDEX_NONE ? Skeleton.GetParentIndex(Index) : INDEX_NONE;
					if (Final.IsValidIndex(Index))
					{
						const FTransform Local = Final.IsValidIndex(Parent) ? Final[Index].GetRelativeTransform(Final[Parent]) : Final[Index];
						Bone->SetBoolField(TEXT("finite"), !Local.ContainsNaN());
						Bone->SetArrayField(TEXT("local_position"), VectorJSON(Local.GetLocation()));
						const FQuat Q = Local.GetRotation();
						Bone->SetArrayField(TEXT("local_rotation"), {MakeShared<FJsonValueNumber>(Q.X), MakeShared<FJsonValueNumber>(Q.Y), MakeShared<FJsonValueNumber>(Q.Z), MakeShared<FJsonValueNumber>(Q.W)});
						if (Base && Base->IsValidIndex(Index))
							Bone->SetNumberField(TEXT("post_retarget_rotation_degrees"), FMath::RadiansToDegrees((*Base)[Index].GetRotation().AngularDistance(Final[Index].GetRotation())));
					}
					Bones.Add(MakeShared<FJsonValueObject>(Bone));
				}
				Entry->SetArrayField(TEXT("dynamic_bones"), Bones); Nodes.Add(MakeShared<FJsonValueObject>(Entry));
			}
	Result->SetArrayField(TEXT("rigid_nodes"), Nodes);
	Result->SetArrayField(TEXT("duplicate_dynamic_bones"), Duplicates);
	TArray<TSharedPtr<FJsonValue>> Cloth;
	// Unlike the GameThread accessor, AnyThread does not change bWaitForParallelClothTask.
	for (const auto& Pair : Mesh->GetCurrentClothingData_AnyThread())
	{
		const auto Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("id"), Pair.Key); Entry->SetNumberField(TEXT("lod"), Pair.Value.LODIndex);
		Entry->SetNumberField(TEXT("particles"), Pair.Value.Positions.Num());
		TArray<TSharedPtr<FJsonValue>> Positions;
		const int32 Step = FMath::Max(1, Pair.Value.Positions.Num() / 16);
		for (int32 Index = 0; Index < Pair.Value.Positions.Num() && Positions.Num() < 16; Index += Step)
			Positions.Add(MakeShared<FJsonValueArray>(VectorJSON(FVector(Pair.Value.Positions[Index]))));
		Entry->SetArrayField(TEXT("positions"), Positions); Cloth.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Result->SetArrayField(TEXT("cloth"), Cloth);
	FString Text; FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Text)); return Text;
}
