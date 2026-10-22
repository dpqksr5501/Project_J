#include "Animation/Project_JPlayerLocomotionLibrary.h"
#include "Animation/AnimClassInterface.h"
#include "AnimNodes/AnimNode_RetargetPoseFromMesh.h"
#include "BoneControllers/AnimNode_RigidBody.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "ClothingSystemRuntimeTypes.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace
{
TArray<TSharedPtr<FJsonValue>> VectorJSON(const FVector& V)
{
	return {MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y), MakeShared<FJsonValueNumber>(V.Z)};
}
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
