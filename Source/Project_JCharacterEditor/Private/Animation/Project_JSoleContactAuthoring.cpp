#include "Animation/Project_JPlayerLocomotionLibrary.h"
#include "Animation/Project_JFootContactData.h"
#include "Animation/Project_JAnimGraphNode_FootClearance.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimClassInterface.h"
#include "Components/SkeletalMeshComponent.h"
#include "UObject/UnrealType.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "AnimGraphNode_RetargetPoseFromMesh.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "EdGraph/EdGraph.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Editor.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

FString UProject_JPlayerLocomotionLibrary::InspectFootClearance(USkeletalMeshComponent* Mesh)
{
	check(IsInGameThread());
	if(!IsValid(Mesh))return TEXT("{}");
	Mesh->HandleExistingParallelEvaluationTask(true,true);
	const auto* Instance=Mesh->GetPostProcessInstance();
	if(!Instance)Instance=Mesh->GetAnimInstance();
	const auto* Interface=Instance?IAnimClassInterface::GetFromClass(Instance->GetClass()):nullptr;
	if(!Interface)return TEXT("{}");
	TArray<TSharedPtr<FJsonValue>> Legs;
	for(const auto* Property:Interface->GetAnimNodeProperties())
	{
		if(Property->Struct!=FProject_JAnimNode_FootClearance::StaticStruct())continue;
		const auto* Node=Property->ContainerPtrToValuePtr<FProject_JAnimNode_FootClearance>(Instance);
		for(const auto& Leg:Node->GetContactDebugData())
		{
			const auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("foot"),Leg.Foot.ToString());
			Row->SetBoolField(TEXT("prepared"),Leg.bPrepared);Row->SetNumberField(TEXT("required_lift_cm"),Leg.RequiredLift);Row->SetNumberField(TEXT("applied_lift_cm"),Leg.AppliedLift);
			Legs.Add(MakeShared<FJsonValueObject>(Row));
		}
	}
	const auto Result=MakeShared<FJsonObject>();Result->SetArrayField(TEXT("legs"),Legs);
	FString Text;FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Text));return Text;
}

FString UProject_JPlayerLocomotionLibrary::AuthorFootSupport(USkeletalMesh* Mesh,FName LeftFoot,FName RightFoot,float SoleBand,float Clearance,float MaxLift)
{
	if(!Mesh || LeftFoot.IsNone() || RightFoot.IsNone() || LeftFoot==RightFoot || (GEditor && GEditor->PlayWorld) || !FMath::IsFinite(SoleBand) || SoleBand<=0 || !FMath::IsFinite(Clearance) || Clearance<0 || !FMath::IsFinite(MaxLift) || MaxLift<=0)return TEXT("{}");
	const auto* Model=Mesh->GetImportedModel();if(!Model || Model->LODModels.IsEmpty())return TEXT("{}");
	const auto& Ref=Mesh->GetRefSkeleton();TArray<FTransform> Bind=Ref.GetRefBonePose();
	for(int32 B=1;B<Bind.Num();++B)Bind[B]*=Bind[Ref.GetParentIndex(B)];
	TArray<FProject_JFootSupportSole> Soles;
	TArray<TSharedPtr<FJsonValue>> Reports;
	for(FName Foot:{LeftFoot,RightFoot})
	{
		const int32 FootIndex=Ref.FindBoneIndex(Foot);if(FootIndex==INDEX_NONE)return TEXT("{}");
		const int32 Calf=Ref.GetParentIndex(FootIndex),Thigh=Calf!=INDEX_NONE?Ref.GetParentIndex(Calf):INDEX_NONE;if(Thigh==INDEX_NONE)return TEXT("{}");
		if(Bind[FootIndex].GetLocation().Equals(Bind[Calf].GetLocation(),.001) || Bind[Calf].GetLocation().Equals(Bind[Thigh].GetLocation(),.001))return TEXT("{}");
		auto Descendant=[&](int32 Index){for(;Index!=INDEX_NONE;Index=Ref.GetParentIndex(Index))if(Index==FootIndex)return true;return false;};
		struct FSample {FVector Position;FProject_JFootSupportPoint Point;};TArray<FSample> Candidates;
		for(const auto& Section:Model->LODModels[0].Sections)for(const auto& Vertex:Section.SoftVertices)
		{
			FSample Sample;Sample.Position=FVector(Vertex.Position);float Sum=0;bool Valid=true;
			for(int32 I=0;I<UE_ARRAY_COUNT(Vertex.InfluenceBones);++I)
			{
				const float Weight=Vertex.InfluenceWeights[I];if(Weight<=0)continue;
				const int32 LocalIndex=Vertex.InfluenceBones[I];
				if(!Section.BoneMap.IsValidIndex(LocalIndex)){Valid=false;break;}
				const int32 Bone=Section.BoneMap[LocalIndex];if(!Ref.IsValidIndex(Bone) || !Descendant(Bone)){Valid=false;break;}
				auto& Influence=Sample.Point.Influences.AddDefaulted_GetRef();Influence.Bone=Ref.GetBoneName(Bone);
				Influence.Position=Bind[Bone].InverseTransformPosition(Sample.Position);Influence.Weight=Weight;Sum+=Weight;
			}
			if(!Valid || Sum<=0)continue;
			for(auto& Influence:Sample.Point.Influences)Influence.Weight/=Sum;
			Sample.Point.Influences.Sort([](const auto& A,const auto& B){return A.Bone.LexicalLess(B.Bone);});
			Candidates.Add(MoveTemp(Sample));
		}
		if(Candidates.IsEmpty())return TEXT("{}");
		double MinZ=TNumericLimits<double>::Max();for(const auto& C:Candidates)MinZ=FMath::Min(MinZ,C.Position.Z);
		FProject_JFootSupportSole Sole;Sole.Foot=Foot;Sole.Calf=Ref.GetBoneName(Calf);Sole.Thigh=Ref.GetBoneName(Thigh);Sole.Clearance=Clearance;Sole.MaxLift=MaxLift;
		TSet<FString> Seen;
		for(const auto& C:Candidates)
		{
			if(C.Position.Z>MinZ+SoleBand)continue;
			FString Key=FString::Printf(TEXT("%.5f,%.5f,%.5f"),C.Position.X,C.Position.Y,C.Position.Z);
			for(const auto& I:C.Point.Influences)Key+=FString::Printf(TEXT("/%s:%.7f"),*I.Bone.ToString(),I.Weight);
			if(!Seen.Contains(Key)){Seen.Add(Key);Sole.Points.Add(C.Point);}
		}
		if(Sole.Points.IsEmpty() || Sole.Points.Num()>UProject_JFootContactData::MaxPointsPerLeg)return TEXT("{}");
		const auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("foot"),Foot.ToString());Row->SetNumberField(TEXT("points"),Sole.Points.Num());Row->SetNumberField(TEXT("reference_min_z_cm"),MinZ);Reports.Add(MakeShared<FJsonValueObject>(Row));Soles.Add(MoveTemp(Sole));
	}
	Mesh->Modify();auto* Data=Cast<UProject_JFootContactData>(Mesh->GetAssetUserDataOfClass(UProject_JFootContactData::StaticClass()));
	if(!Data){Data=NewObject<UProject_JFootContactData>(Mesh,NAME_None,RF_Transactional);Mesh->AddAssetUserData(Data);}
	Data->Modify();Data->Soles=MoveTemp(Soles);Mesh->MarkPackageDirty();
	const auto Result=MakeShared<FJsonObject>();Result->SetBoolField(TEXT("completed"),true);Result->SetArrayField(TEXT("soles"),Reports);
	FString Text;FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Text));return Text;
}

bool UProject_JPlayerLocomotionLibrary::InstallFootClearance(UAnimBlueprint* Blueprint)
{
	if(!Blueprint || (GEditor && GEditor->PlayWorld) || !Blueprint->ParentClass || !Blueprint->ParentClass->IsChildOf(UProject_JRetargetAnimInstance::StaticClass()))return false;
	TArray<UEdGraph*> Graphs;Blueprint->GetAllGraphs(Graphs);UEdGraphNode* Producer=nullptr;int32 Existing=0;
	for(auto* Graph:Graphs)for(UEdGraphNode* Node:Graph->Nodes)
	{
		if(Node->IsA<UProject_JAnimGraphNode_FootClearance>())++Existing;
		if(Node->IsA<UAnimGraphNode_LinkedInputPose>() || Node->IsA<UAnimGraphNode_RetargetPoseFromMesh>()){if(Producer)return false;Producer=Node;}
	}
	if(Existing)return Existing==1;if(!Producer)return false;
	auto* Head=Producer->FindPin(TEXT("Pose"),EGPD_Output);if(!Head || Head->LinkedTo.Num()!=1)return false;
	auto* Graph=Producer->GetGraph();auto* CS=Cast<UAnimGraphNode_LocalToComponentSpace>(Head->LinkedTo[0]->GetOwningNode());
	Blueprint->Modify();Graph->Modify();TArray<UEdGraphNode*> Added;
	auto Add=[&](UEdGraphNode* Node){Graph->AddNode(Node,false,false);Node->CreateNewGuid();Node->PostPlacedNewNode();Node->AllocateDefaultPins();Added.Add(Node);};
	UAnimGraphNode_ComponentToLocalSpace* Local=nullptr;
	if(!CS){CS=NewObject<UAnimGraphNode_LocalToComponentSpace>(Graph,NAME_None,RF_Transactional);Add(CS);Local=NewObject<UAnimGraphNode_ComponentToLocalSpace>(Graph,NAME_None,RF_Transactional);Add(Local);}
	auto* Solve=NewObject<UProject_JAnimGraphNode_FootClearance>(Graph,NAME_None,RF_Transactional);Solve->Node.Alpha=1.f;Add(Solve);
	auto* Input=Solve->FindPin(TEXT("ComponentPose"));auto* Output=Solve->FindPin(TEXT("Pose"),EGPD_Output);
	auto* Before=Local?Head:CS->FindPin(TEXT("ComponentPose"),EGPD_Output);
	if(!Input || !Output || !Before || Before->LinkedTo.Num()!=1){for(auto* N:Added)FBlueprintEditorUtils::RemoveNode(Blueprint,N,true);return false;}
	auto* After=Before->LinkedTo[0];Before->BreakLinkTo(After);
	if(Local){Before->MakeLinkTo(CS->FindPin(TEXT("LocalPose")));CS->FindPin(TEXT("ComponentPose"),EGPD_Output)->MakeLinkTo(Input);Output->MakeLinkTo(Local->FindPin(TEXT("ComponentPose")));Local->FindPin(TEXT("Pose"),EGPD_Output)->MakeLinkTo(After);}
	else{Before->MakeLinkTo(Input);Output->MakeLinkTo(After);}
	Solve->NodePosX=CS->NodePosX+220;Solve->NodePosY=CS->NodePosY;
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);FKismetEditorUtilities::CompileBlueprint(Blueprint);Blueprint->MarkPackageDirty();return Blueprint->Status!=BS_Error;
}
