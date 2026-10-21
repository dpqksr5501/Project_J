#include "Animation/Project_JCopyPoseAnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "AnimNodes/AnimNode_CopyPoseFromMesh.h"

namespace
{
struct FProjectJCopyPoseProxy final : FAnimInstanceProxy
{
 explicit FProjectJCopyPoseProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance)
 {
  Copy.bUseAttachedParent=true;
  Copy.bCopyCurves=true;
  Copy.bCopyCustomAttributes=true;
 }
 virtual FAnimNode_Base* GetCustomRootNode() override { return &Copy; }
 virtual void GetCustomNodes(TArray<FAnimNode_Base*>& Nodes) override { Nodes.Add(&Copy); }
 FAnimNode_CopyPoseFromMesh Copy;
};
}
FAnimInstanceProxy* UProject_JCopyPoseAnimInstance::CreateAnimInstanceProxy() { return new FProjectJCopyPoseProxy(this); }
void UProject_JCopyPoseAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) { delete Proxy; }
