#include "Components/Project_JModularMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Project_JCopyPoseAnimInstance.h"
#include "Animation/Skeleton.h"
#include "Animation/Project_JPresentationMeshResolver.h"

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
	if (!MainMesh || MainMesh == this) return;
	if (!AttachToComponent(MainMesh, FAttachmentTransformRules::KeepRelativeTransform)) return;
	bFollowAttachParentPose = true;
	RefreshAttachParentPose();
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
 const bool PreviousAuto = bFollowAttachParentPose;
 bFollowAttachParentPose = false;
 if(!AttachToComponent(MainMesh,FAttachmentTransformRules::KeepRelativeTransform))
 {bFollowAttachParentPose=PreviousAuto;return false;}
 ClearPosePrerequisite();
 SetLeaderPoseComponent(nullptr);
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
 // SetLeaderPoseComponent recreates physics while registered. The engine has
 // already destroyed physics before OnUnregister, so clear the leader only
 // after Super marks us unregistered and releases the clothing resources.
 Super::OnUnregister();
	if (bFollowAttachParentPose) SetLeaderPoseComponent(nullptr);
}
void UProject_JModularMeshComponent::OnRegister()
{
 Super::OnRegister();
	if (bFollowAttachParentPose)
	{
		RefreshAttachParentPose();
		return;
	}
 if(GetAnimClass()==UProject_JCopyPoseAnimInstance::StaticClass())
  if(auto* Source=Cast<USkeletalMeshComponent>(GetAttachParent()))
   if(CanCopyBasePose(Source->GetSkeletalMeshAsset(),GetSkeletalMeshAsset()))
   {AddTickPrerequisiteComponent(Source);PosePrerequisite=Source;SetComponentTickEnabled(true);}
}

void UProject_JModularMeshComponent::OnAttachmentChanged()
{
	Super::OnAttachmentChanged();
	if (bFollowAttachParentPose && IsRegistered()) RefreshAttachParentPose();
}

void UProject_JModularMeshComponent::RefreshAttachParentPose()
{
	// SCS establishes the attach parent before registration. Reattachment and
	// re-registration must replace the old source without a Tick or owner-name lookup.
	ClearPosePrerequisite();
	if (auto* Source = Cast<USkeletalMeshComponent>(GetAttachParent()))
	{
		if (GetAnimClass()) SetAnimInstanceClass(nullptr);
		// Rebinding an unchanged source must not recreate cloth or reset its history.
		SetLeaderPoseComponent(Source, LeaderPoseComponent.Get() != Source || bFollowerShouldTickPose, false);
	}
	else
	{
		SetLeaderPoseComponent(nullptr);
	}
	RefreshSharedPoseTick();
}

void UProject_JModularMeshComponent::Activate(bool bReset)
{
	Super::Activate(bReset);
	if (bFollowAttachParentPose) RefreshSharedPoseTick();
}

void UProject_JModularMeshComponent::RefreshSharedPoseTick()
{
	// Bone sharing does not replace Chaos Cloth's vertex simulation. Cloth needs
	// the component tick, but bFollowerShouldTickPose=false prevents another AnimGraph.
	const bool bNeedsCloth = LeaderPoseComponent.IsValid() && !IsNetMode(NM_DedicatedServer) &&
		Project_J::Animation::UsesIndependentClothSimulation(*this);
	SetComponentTickEnabled(bNeedsCloth);
}

void UProject_JModularMeshComponent::SetSkeletalMesh(USkeletalMesh* NewMesh, bool bReinitPose)
{
	Super::SetSkeletalMesh(NewMesh, bReinitPose);
	if (bFollowAttachParentPose && IsRegistered()) RefreshAttachParentPose();
}
