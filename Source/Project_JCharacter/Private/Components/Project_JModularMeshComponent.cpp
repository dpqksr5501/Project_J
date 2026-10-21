#include "Components/Project_JModularMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Project_JCopyPoseAnimInstance.h"
#include "Animation/Skeleton.h"

UProject_JModularMeshComponent::UProject_JModularMeshComponent()
{
	// Default optimization: don't tick animations if not rendered, relying on the main mesh's significance or leader pose.
	VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bEnableUpdateRateOptimizations = true;
}

void UProject_JModularMeshComponent::AttachAndSetLeader(USkeletalMeshComponent* MainMesh)
{
	if (!MainMesh) return;
	ClearPosePrerequisite();
	SetAnimInstanceClass(nullptr);

	// Attach to the main mesh
	AttachToComponent(MainMesh, FAttachmentTransformRules::KeepRelativeTransform);

	// Force this component to blindly follow the main mesh's evaluated pose.
	// This zeroes out the animation evaluation overhead for modular parts (armor, boots, etc.).
	SetLeaderPoseComponent(MainMesh, true, true);
	SetComponentTickEnabled(false);
}

bool UProject_JModularMeshComponent::CanCopyBasePose(const USkeletalMesh* Source,const USkeletalMesh* Target)
{
 if(!Source || !Target || !Source->GetSkeleton() || !Target->GetSkeleton())return false;
 const auto* A=Source->GetSkeleton();const auto* B=Target->GetSkeleton();
 const auto Has=[](const USkeleton* From,const USkeleton* To)
 {return From->GetCompatibleSkeletons().ContainsByPredicate([To](const TSoftObjectPtr<USkeleton>& Entry){return Entry.Get()==To;});};
 if(A!=B && !Has(A,B) && !Has(B,A))return false;
 const auto& S=Source->GetRefSkeleton();const auto& T=Target->GetRefSkeleton();
 if(S.GetNum()==0 || T.GetNum()==0 || S.GetBoneName(0)!=T.GetBoneName(0))return false;
 int32 Common=0;
 for(int32 Index=1;Index<T.GetNum();++Index)
 {
  const int32 Other=S.FindBoneIndex(T.GetBoneName(Index));if(Other==INDEX_NONE)continue;
  const int32 Parent=T.GetParentIndex(Index),OtherParent=S.GetParentIndex(Other);
  if(Parent==INDEX_NONE || OtherParent==INDEX_NONE || T.GetBoneName(Parent)!=S.GetBoneName(OtherParent))return false;
  ++Common;
 }
 return Common>0;
}

bool UProject_JModularMeshComponent::AttachAndCopyPose(USkeletalMeshComponent* MainMesh)
{
 if(!MainMesh || MainMesh==this || !CanCopyBasePose(MainMesh->GetSkeletalMeshAsset(),GetSkeletalMeshAsset()))return false;
 ClearPosePrerequisite();
 SetLeaderPoseComponent(nullptr);
 if(!AttachToComponent(MainMesh,FAttachmentTransformRules::KeepRelativeTransform))return false;
 AddTickPrerequisiteComponent(MainMesh);PosePrerequisite=MainMesh;
 SetAnimInstanceClass(UProject_JCopyPoseAnimInstance::StaticClass());
 SetComponentTickEnabled(true);
 return true;
}

void UProject_JModularMeshComponent::ClearPosePrerequisite()
{
 if(auto* Source=PosePrerequisite.Get())RemoveTickPrerequisiteComponent(Source);
 PosePrerequisite.Reset();
}
void UProject_JModularMeshComponent::OnUnregister()
{
 ClearPosePrerequisite();
 Super::OnUnregister();
}
void UProject_JModularMeshComponent::OnRegister()
{
 Super::OnRegister();
 if(GetAnimClass()==UProject_JCopyPoseAnimInstance::StaticClass())
  if(auto* Source=Cast<USkeletalMeshComponent>(GetAttachParent()))
   if(CanCopyBasePose(Source->GetSkeletalMeshAsset(),GetSkeletalMeshAsset()))
   {AddTickPrerequisiteComponent(Source);PosePrerequisite=Source;SetComponentTickEnabled(true);}
}
