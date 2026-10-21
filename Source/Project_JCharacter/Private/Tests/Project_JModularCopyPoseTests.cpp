#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Animation/Project_JCopyPoseAnimInstance.h"
#include "Components/Project_JModularMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "ReferenceSkeleton.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
class FModularCopyPoseCommand final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;UWorld* World=nullptr;USkeletalMeshComponent* Source=nullptr;UProject_JModularMeshComponent* Part=nullptr;
 TStrongObjectPtr<USkeletalMesh> ExtraMesh;TStrongObjectPtr<USkeleton> ExtraSkeleton;
 uint64 LastFrame=MAX_uint64;int32 Frames=0;
 void Close(){if(World){GEngine->DestroyWorldContext(World);World->DestroyWorld(false);World=nullptr;}}
public:
 explicit FModularCopyPoseCommand(FAutomationTestBase* InTest):Test(InTest){}
 virtual ~FModularCopyPoseCommand(){Close();}
 virtual bool Update() override
 {
  if(LastFrame==GFrameCounter)return false;LastFrame=GFrameCounter;
  if(!World)
  {
   auto* Base=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
   auto* Sequence=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Characters/Mannequins/Animations/Locomotion/Run/M_Relaxed_Run_Loop_F.M_Relaxed_Run_Loop_F"));
   if(!Test->TestNotNull(TEXT("Manny fixture"),Base)||!Test->TestNotNull(TEXT("Canonical Manny motion"),Sequence))return true;
   if(!Test->TestEqual(TEXT("CopyPose fixture evaluates native Manny motion"),Sequence->GetSkeleton(),Base->GetSkeleton()))return true;
   ExtraMesh.Reset(DuplicateObject<USkeletalMesh>(Base,GetTransientPackage(),MakeUniqueObjectName(GetTransientPackage(),USkeletalMesh::StaticClass(),TEXT("CopyPoseCapeMesh"))));
   ExtraSkeleton.Reset(DuplicateObject<USkeleton>(Base->GetSkeleton(),GetTransientPackage(),MakeUniqueObjectName(GetTransientPackage(),USkeleton::StaticClass(),TEXT("CopyPoseCapeSkeleton"))));
   ExtraMesh->SetSkeleton(ExtraSkeleton.Get());
   {
    FReferenceSkeletonModifier Modify(ExtraMesh->GetRefSkeleton(),ExtraSkeleton.Get());
    Modify.Add(FMeshBoneInfo(TEXT("projectj_test_cape"),TEXT("projectj_test_cape"),ExtraMesh->GetRefSkeleton().FindBoneIndex(TEXT("spine_05"))),FTransform(FVector(-10,0,0)));
   }
   Test->TestTrue(TEXT("Additional cape skeleton can be authored without altering Manny"),ExtraSkeleton->MergeAllBonesToBoneTree(ExtraMesh.Get()));
   ExtraSkeleton->AddCompatibleSkeleton(Base->GetSkeleton());
   Test->TestTrue(TEXT("Extra peripheral bones do not require IK retargeting"),UProject_JModularMeshComponent::CanCopyBasePose(Base,ExtraMesh.Get()));
   World=UWorld::CreateWorld(EWorldType::Game,false);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
   auto* Actor=World->SpawnActor<AActor>();Source=NewObject<USkeletalMeshComponent>(Actor);Actor->SetRootComponent(Source);Actor->AddInstanceComponent(Source);
   Source->SetSkeletalMeshAsset(Base);Source->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;Source->RegisterComponent();Source->PlayAnimation(Sequence,true);
   Part=NewObject<UProject_JModularMeshComponent>(Actor);Actor->AddInstanceComponent(Part);Part->SetSkeletalMeshAsset(ExtraMesh.Get());Part->RegisterComponent();
   Part->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
   if(!Test->TestTrue(TEXT("Independent part attaches without a retarget node"),Part->AttachAndCopyPose(Source))){Close();return true;}
   return false;
  }
  Source->TickAnimation(1.f/60,false);Source->RefreshBoneTransforms();
  Part->TickAnimation(1.f/60,false);Part->RefreshBoneTransforms();
  if(++Frames<12)return false;
  Test->TestNull(TEXT("Independent part does not use a LeaderPose buffer"),Part->LeaderPoseComponent.Get());
  Test->TestTrue(TEXT("Native CopyPose is the only base-pose producer"),Part->GetAnimInstance() && Part->GetAnimInstance()->IsA<UProject_JCopyPoseAnimInstance>());
  for(FName Bone:{FName(TEXT("pelvis")),FName(TEXT("hand_l")),FName(TEXT("foot_r"))})
   Test->TestTrue(TEXT("Common bones follow the evaluated source pose"),Part->GetSocketTransform(Bone,RTS_Component).Equals(Source->GetSocketTransform(Bone,RTS_Component),.01f));
  Test->TestTrue(TEXT("Additional bone retains a valid local transform"),!Part->GetSocketTransform(TEXT("projectj_test_cape"),RTS_Component).ContainsNaN());
  Part->UnregisterComponent();Part->RegisterComponent();
  Test->TestTrue(TEXT("Re-registration restores independent tick"),Part->IsComponentTickEnabled());
  Part->AttachAndSetLeader(Source);
  Test->TestTrue(TEXT("Switching back uses exactly one pose owner"),Part->LeaderPoseComponent.Get()==Source);
  Test->TestFalse(TEXT("LeaderPose no longer evaluates the independent graph"),Part->IsComponentTickEnabled());
  Close();return true;
 }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJModularCopyPose,"ProjectJ.PlayerLocomotion.ModularExtraBones",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJModularCopyPose::RunTest(const FString&){ADD_LATENT_AUTOMATION_COMMAND(FModularCopyPoseCommand(this));return true;}
#endif
