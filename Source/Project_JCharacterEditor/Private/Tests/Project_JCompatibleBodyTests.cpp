#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JPlayerLocomotionLibrary.h"
#include "Animation/Skeleton.h"
#include "Animation/BlendProfile.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Project_JGreatswordCharacter.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "ClothingAsset.h"
#include "UObject/UnrealType.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/Project_JCharacterAnimProfile.h"
#include "Animation/Project_JRetargetBodyProfile.h"
#include "Project_JPlayerCharacter.h"
#include "AnimGraphNode_CopyBone.h"
#include "EdGraph/EdGraph.h"

namespace
{
USkeletalMesh* MakeBody(bool Extra, bool WrongParent=false)
{
 auto* Skeleton=NewObject<USkeleton>();
 {
  FReferenceSkeletonModifier Modifier(Skeleton);
  Modifier.Add(FMeshBoneInfo(TEXT("root"),TEXT("root"),INDEX_NONE),FTransform::Identity);
  Modifier.Add(FMeshBoneInfo(TEXT("pelvis"),TEXT("pelvis"),0),FTransform(FVector(0,0,90)));
  Modifier.Add(FMeshBoneInfo(TEXT("foot_l"),TEXT("foot_l"),WrongParent?0:1),FTransform(FVector(0,0,-80)));
  if (Extra) Modifier.Add(FMeshBoneInfo(TEXT("cape"),TEXT("cape"),1),FTransform(FVector(0,-10,0)));
 }
 auto* Mesh=NewObject<USkeletalMesh>(); Mesh->SetSkeleton(Skeleton);
 Mesh->GetRefSkeleton()=Skeleton->GetReferenceSkeleton();
 return Mesh;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCompatibleBodyContract,"ProjectJ.PlayerLocomotion.CompatibleBodyContract",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJCompatibleBodyContract::RunTest(const FString&)
{
 auto* Source=MakeBody(false); auto* Target=MakeBody(true);
 auto* From=Source->GetSkeleton(); auto* To=Target->GetSkeleton();
 auto* Profile=From->CreateNewBlendProfile(TEXT("Player_FastFeet_FastRoot"));
 Profile->Mode=EBlendProfileMode::TimeFactor; Profile->SetBoneBlendScale(TEXT("foot_l"),.25f,false,true);
 From->SetSlotGroupName(TEXT("UpperBody"),TEXT("Combat"));
 TestTrue(TEXT("Compatible body accepts the existing locomotion contract"),UProject_JPlayerLocomotionLibrary::PrepareCompatibleBody(Source,Target));
 auto* Copy=To->GetBlendProfile(TEXT("Player_FastFeet_FastRoot"));
 if (!TestNotNull(TEXT("Exact authored name is preserved without another Player_ prefix"),Copy)) return false;
 TestEqual(TEXT("Per-bone blend factor follows the bone name"),Copy->GetBoneBlendScale(TEXT("foot_l")),.25f);
 TestEqual(TEXT("Added cape branch retains neutral blend factor"),Copy->GetBoneBlendScale(TEXT("cape")),1.f);
 TestEqual(TEXT("Montage slot group is preserved"),To->GetSlotGroupName(TEXT("UpperBody")),FName(TEXT("Combat")));
 TestTrue(TEXT("Repeating authoring does not duplicate profiles"),UProject_JPlayerLocomotionLibrary::PrepareCompatibleBody(Source,Target));
 TestEqual(TEXT("One profile after repeated preparation"),To->BlendProfiles.Num(),1);
 Copy->SetBoneBlendScale(TEXT("foot_l"),.75f,false,true);
 From->SetSlotGroupName(TEXT("NewSlot"),TEXT("NewGroup"));
 TestFalse(TEXT("Conflicting body tuning is rejected"),UProject_JPlayerLocomotionLibrary::PrepareCompatibleBody(Source,Target));
 TestEqual(TEXT("Rejected preparation preserves authored factor"),Copy->GetBoneBlendScale(TEXT("foot_l")),.75f);
 TestFalse(TEXT("Preflight rejection cannot partially add slots"),To->ContainsSlotName(TEXT("NewSlot")));
 TestEqual(TEXT("Source profile remains authoritative"),Profile->GetBoneBlendScale(TEXT("foot_l")),.25f);
 auto* Wrong=MakeBody(true,true);
 TestFalse(TEXT("Shared names cannot hide an incompatible parent chain"),UProject_JPlayerLocomotionLibrary::PrepareCompatibleBody(Source,Wrong));
 TestTrue(TEXT("Rejected hierarchy receives no blend profiles"),Wrong->GetSkeleton()->BlendProfiles.IsEmpty());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCompatibleBodyPoseSourcePolicy,"ProjectJ.PlayerLocomotion.CompatibleBodyPoseSourcePolicy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJCompatibleBodyPoseSourcePolicy::RunTest(const FString&)
{
 auto* World=UWorld::CreateWorld(EWorldType::Game,false);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 auto* Pawn=World->SpawnActor<AProject_JGreatswordCharacter>();
 if (TestNotNull(TEXT("Client source fixture"),Pawn))
 {
  auto* Source=Pawn->GetMesh();
  Source->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
  Source->SetVisibility(false); Source->SetHiddenInGame(true);
  auto* PostProcess=NewObject<UProject_JRetargetAnimInstance>(Source);
  PostProcess->NativeInitializeAnimation();
  TestTrue(TEXT("Hidden gameplay source keeps its authored evaluation policy"),Source->VisibilityBasedAnimTickOption==EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones);
  auto* Follower=NewObject<USkeletalMeshComponent>(Pawn); Follower->SetupAttachment(Source); Follower->RegisterComponent();
  Follower->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
  auto* FollowerInstance=NewObject<UProject_JRetargetAnimInstance>(Follower);
  FollowerInstance->NativeInitializeAnimation();
  TestTrue(TEXT("Standalone visual keeps its rendered-only default"),Follower->VisibilityBasedAnimTickOption==EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered);
 }
 World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAvatarPoseRoutes,"ProjectJ.PlayerLocomotion.AvatarPoseRoutes",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJAvatarPoseRoutes::RunTest(const FString&)
{
 const auto* Class=LoadClass<AProject_JPlayerCharacter>(nullptr,TEXT("/Game/Character_BPs/GreatSword/BP_GreatSword.BP_Greatsword_C"));
 const auto* Avatar=Class?Class->GetDefaultObject<AProject_JPlayerCharacter>():nullptr;
 const auto* Master=LoadObject<UAnimBlueprint>(nullptr,TEXT("/Game/Animation_Logic/ABPs/ABP_Player_Master.ABP_Player_Master"));
 const auto* Fallback=LoadObject<UAnimBlueprint>(nullptr,TEXT("/Game/GreatSword/Animation/ABP_GreatSword_Retarget.ABP_GreatSword_Retarget"));
 if (!TestNotNull(TEXT("Direct body fixture"),Avatar) || !Master || !Fallback) return false;
 auto* Source=Avatar->GetMesh();const auto* Mesh=Source->GetSkeletalMeshAsset();
 if (!Mesh || !Mesh->GetPostProcessAnimBlueprint()) return false;
 const UClass* BodyClass=Mesh->GetPostProcessAnimBlueprint().Get();
 TestEqual(TEXT("Compatible body executes the common master on its actual mesh"),Source->GetAnimClass(),Master->GeneratedClass.Get());
 const auto* Body=Cast<UProject_JRetargetAnimInstance>(BodyClass->GetDefaultObject());
 if (!TestNotNull(TEXT("Direct post process reuses the shared contact controller"),Body)) return false;
 TestNull(TEXT("Direct body cannot select a retarget family"),Body->RetargetBodyProfile.Get());
 TestTrue(TEXT("Direct character profile cannot install a follower override"),Avatar->GetCharacterAnimProfile() && !Avatar->GetCharacterAnimProfile()->RetargetBodyProfile);
 auto Count=[](const UClass* InClass,FName Name)
 { int32 N=0;for(TFieldIterator<FStructProperty> P(InClass);P;++P)N+=P->Struct->GetFName()==Name;return N; };
 TestEqual(TEXT("Common master contains no realtime retarget node"),Count(Master->GeneratedClass,TEXT("AnimNode_RetargetPoseFromMesh")),0);
 TestEqual(TEXT("Direct body contains no realtime retarget node"),Count(BodyClass,TEXT("AnimNode_RetargetPoseFromMesh")),0);
 TestEqual(TEXT("Direct body consumes one upstream pose"),Count(BodyClass,TEXT("AnimNode_LinkedInputPose")),1);
 TestEqual(TEXT("Direct body has two contact solvers"),Count(BodyClass,TEXT("Project_JAnimNode_GuidedHandIK")),2);
 TestEqual(TEXT("Direct body publishes one coherent set of current-pose wrist goals"),Count(BodyClass,TEXT("Project_JAnimNode_HandContactGoals")),1);
 TestEqual(TEXT("Direct body uses one ordinary contact rig without a retarget processor"),Count(BodyClass,TEXT("AnimNode_IKRig")),1);
 TestEqual(TEXT("Direct body has exactly two secondary physics solvers"),Count(BodyClass,TEXT("AnimNode_RigidBody")),2);
 int32 Markers=0;TArray<UEdGraph*> Graphs;Master->GetAllGraphs(Graphs);
 for(auto* Graph:Graphs) for(UEdGraphNode* Node:Graph->Nodes)
  if(Node->IsA<UAnimGraphNode_CopyBone>() && Node->NodeComment==TEXT("ProjectJ.SourcePoseMarkers")) ++Markers;
 TestEqual(TEXT("Common master has no body-specific final marker writers"),Markers,0);
 auto* BodyBlueprint=LoadObject<UAnimBlueprint>(nullptr,TEXT("/Game/GreatSword/Animation/ABP_GreatSword_Body.ABP_GreatSword_Body"));
 if(!BodyBlueprint)return false;
 Graphs.Reset();BodyBlueprint->GetAllGraphs(Graphs);Markers=0;
 for(auto* Graph:Graphs)for(UEdGraphNode* Node:Graph->Nodes)
  if(Node->IsA<UAnimGraphNode_CopyBone>() && Node->NodeComment==TEXT("ProjectJ.BodyPoseMarkers"))++Markers;
 TestEqual(TEXT("Direct body owns its five input-pose markers"),Markers,5);
 TestEqual(TEXT("Body marker nodes are compiled once"),Count(BodyClass,TEXT("AnimNode_CopyBone")),5);
 TestEqual(TEXT("Fallback retains its single retarget evaluator"),Count(Fallback->GeneratedClass,TEXT("AnimNode_RetargetPoseFromMesh")),1);
 const auto* FallbackBody=Cast<UProject_JRetargetAnimInstance>(Fallback->GeneratedClass->GetDefaultObject());
 TestTrue(TEXT("Fallback retains an explicit target-body family"),FallbackBody && FallbackBody->RetargetBodyProfile && FallbackBody->RetargetBodyProfile->Family);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCompatibleBodyServerSource,"ProjectJ.PlayerLocomotion.CompatibleBodyServerSource",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJCompatibleBodyServerSource::RunTest(const FString&)
{
 auto* World=UWorld::CreateWorld(EWorldType::PIE,false);
 GEngine->CreateNewWorldContext(EWorldType::PIE).SetCurrentWorld(World);
 World->SetPlayInEditorInitialNetMode(NM_DedicatedServer);
 auto* Pawn=World->SpawnActor<AProject_JGreatswordCharacter>();
 if (TestNotNull(TEXT("Dedicated source fixture"),Pawn))
 {
  TestEqual(TEXT("Fixture uses the server initialization path"),Pawn->GetNetMode(),NM_DedicatedServer);
  auto* Source=Pawn->GetMesh(); Source->SetComponentTickEnabled(true);
  auto* PostProcess=NewObject<UProject_JRetargetAnimInstance>(Source);
  PostProcess->NativeInitializeAnimation();
  TestTrue(TEXT("Secondary post-process cannot disable the gameplay pose source"),Source->IsComponentTickEnabled());
  TestFalse(TEXT("Dedicated source keeps cosmetic body physics disabled"),PostProcess->bEnableSecondaryMotion);
  auto* Follower=NewObject<USkeletalMeshComponent>(Pawn); Follower->SetupAttachment(Source); Follower->RegisterComponent();
  Follower->SetComponentTickEnabled(true);
  auto* FollowerInstance=NewObject<UProject_JRetargetAnimInstance>(Follower);
  FollowerInstance->NativeInitializeAnimation();
  TestFalse(TEXT("Dedicated visual follower still avoids cosmetic evaluation"),Follower->IsComponentTickEnabled());
 }
 World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCompatiblePhysics,"ProjectJ.PlayerLocomotion.CompatiblePhysicsFrames",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJCompatiblePhysics::RunTest(const FString&)
{
 auto Make=[](FName Foot)
 {
  auto* Skeleton=NewObject<USkeleton>();
  {FReferenceSkeletonModifier M(Skeleton);M.Add(FMeshBoneInfo(TEXT("root"),TEXT("root"),INDEX_NONE),FTransform::Identity);M.Add(FMeshBoneInfo(Foot,Foot.ToString(),0),FTransform(FVector(0,0,10)));}
  auto* Mesh=NewObject<USkeletalMesh>();Mesh->SetSkeleton(Skeleton);Mesh->GetRefSkeleton()=Skeleton->GetReferenceSkeleton();return Mesh;
 };
 auto* Old=Make(TEXT("old_foot"));auto* New=Make(TEXT("foot_l"));auto* PA=NewObject<UPhysicsAsset>();
 auto* Root=NewObject<USkeletalBodySetup>(PA);Root->BoneName=TEXT("root");
 FKSphereElem Sphere(4.f);Sphere.Center=FVector(1,0,0);Root->AggGeom.SphereElems.Add(Sphere);Root->PhysicsType=PhysType_Kinematic;
 auto* Foot=NewObject<USkeletalBodySetup>(PA);Foot->BoneName=TEXT("old_foot");Foot->PhysicsType=PhysType_Simulated;
 FKSphylElem Capsule(3.f,20.f);Capsule.Center=FVector(0,2,0);Foot->AggGeom.SphylElems.Add(Capsule);
 PA->SkeletalBodySetups={Root,Foot};PA->DisableCollision(0,1);
 auto* Joint=NewObject<UPhysicsConstraintTemplate>(PA);auto& C=Joint->DefaultInstance;
 C.JointName=TEXT("old_foot");C.ConstraintBone1=TEXT("old_foot");C.ConstraintBone2=TEXT("root");
 C.SetRefFrame(EConstraintFrame::Frame1,FTransform(FVector(1,0,0)));C.SetRefFrame(EConstraintFrame::Frame2,FTransform(FVector(1,0,10)));
 C.SetAngularSwing1Limit(ACM_Limited,37.f);PA->ConstraintSetup.Add(Joint);
 const TMap<FName,FName> Map{{TEXT("old_foot"),TEXT("foot_l")}};
 TestTrue(TEXT("Physics copy accepts verified mesh basis and bone mapping"),UProject_JPlayerLocomotionLibrary::RebaseCompatiblePhysics(PA,Old,New,Map,FTransform(FRotator(0,90,0))));
 TestEqual(TEXT("Simulation body name follows the new skeleton"),Foot->BoneName,FName(TEXT("foot_l")));
 TestTrue(TEXT("Sphere preserves its location in the new mesh coordinates"),Root->AggGeom.SphereElems[0].Center.Equals(FVector(0,1,0),.001));
 TestTrue(TEXT("Capsule preserves its location in the new mesh coordinates"),Foot->AggGeom.SphylElems[0].Center.Equals(FVector(-2,0,0),.001));
 TestEqual(TEXT("Capsule radius remains authored"),Foot->AggGeom.SphylElems[0].Radius,3.f);
 TestEqual(TEXT("Capsule length remains authored"),Foot->AggGeom.SphylElems[0].Length,20.f);
 TestEqual(TEXT("Simulation mode remains authored"),Foot->PhysicsType.GetValue(),PhysType_Simulated);
 TestFalse(TEXT("Existing collision exclusions survive unchanged body ordering"),PA->IsCollisionEnabled(0,1));
 TestEqual(TEXT("Body lookup is rebuilt after renaming"),PA->FindBodyIndex(TEXT("foot_l")),1);
 TestTrue(TEXT("Joint child frame receives the same coordinate change"),C.GetRefFrame(EConstraintFrame::Frame1).GetTranslation().Equals(FVector(0,1,0),.001));
 TestEqual(TEXT("Joint angular limit is preserved"),C.GetAngularSwing1Limit(),37.f);
 TestFalse(TEXT("Already converted bodies cannot be transformed twice against the old skeleton"),UProject_JPlayerLocomotionLibrary::RebaseCompatiblePhysics(PA,Old,New,Map,FTransform(FRotator(0,90,0))));
 TestTrue(TEXT("Rejected repeat keeps the converted center"),Root->AggGeom.SphereElems[0].Center.Equals(FVector(0,1,0),.001));
 auto* LocalPA=DuplicateObject<UPhysicsAsset>(PA,GetTransientPackage(),
  MakeUniqueObjectName(GetTransientPackage(),UPhysicsAsset::StaticClass(),TEXT("PreservedLocalPhysics")));
 const TMap<FName,FName> Reverse{{TEXT("foot_l"),TEXT("old_foot")}};
 const FVector Center=LocalPA->SkeletalBodySetups[1]->AggGeom.SphylElems[0].Center;
 const FTransform Frame=LocalPA->ConstraintSetup[0]->DefaultInstance.GetRefFrame(EConstraintFrame::Frame1);
 TestFalse(TEXT("Local-frame preservation cannot silently ignore a nonidentity mesh basis"),
  UProject_JPlayerLocomotionLibrary::RebaseCompatiblePhysics(LocalPA,New,Old,Reverse,FTransform(FRotator(0,90,0)),true));
 TestTrue(TEXT("Reposed preserved axes can retain authored body-local frames"),
  UProject_JPlayerLocomotionLibrary::RebaseCompatiblePhysics(LocalPA,New,Old,Reverse,FTransform::Identity,true));
 TestTrue(TEXT("Rebinding preserves the body-local shape"),LocalPA->SkeletalBodySetups[1]->AggGeom.SphylElems[0].Center.Equals(Center,.001));
 TestTrue(TEXT("Rebinding preserves the joint-local frame"),LocalPA->ConstraintSetup[0]->DefaultInstance.GetRefFrame(EConstraintFrame::Frame1).Equals(Frame,.001));
 TestEqual(TEXT("Rebinding still rebuilds the mapped body lookup"),LocalPA->FindBodyIndex(TEXT("old_foot")),1);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCompatibleClothConfiguration,"ProjectJ.PlayerLocomotion.CompatibleClothConfiguration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJCompatibleClothConfiguration::RunTest(const FString&)
{
 auto* ConfigClass=LoadClass<UClothConfigBase>(nullptr,TEXT("/Script/ChaosCloth.ChaosClothConfig"));
 if(!TestNotNull(TEXT("Project Chaos cloth config"),ConfigClass))return false;
 auto* Density=FindFProperty<FFloatProperty>(ConfigClass,TEXT("Density"));
 if(!TestNotNull(TEXT("Authored density field"),Density))return false;
 auto* Mass=FindFProperty<FEnumProperty>(ConfigClass,TEXT("MassMode"));
 if(!TestNotNull(TEXT("Cloth mass mode field"),Mass))return false;
 auto* Old=NewObject<USkeletalMesh>();auto* New=NewObject<USkeletalMesh>();
 UClothConfigBase* Shared=nullptr;
 UPhysicsAsset* Collision=NewObject<UPhysicsAsset>();
 for(int32 I=0;I<3;++I)
 {
  auto* A=NewObject<UClothingAssetCommon>(Old);auto* B=NewObject<UClothingAssetCommon>(New);
  if(!Shared)
  {
   Shared=NewObject<UClothConfigBase>(A,ConfigClass);Density->SetPropertyValue_InContainer(Shared,.7f);
   Mass->GetUnderlyingProperty()->SetIntPropertyValue(Mass->ContainerPtrToValuePtr<void>(Shared),uint64(1));
  }
  A->ClothConfigs.Add(ConfigClass->GetFName(),Shared);
  B->ClothConfigs.Add(ConfigClass->GetFName(),NewObject<UClothConfigBase>(B,ConfigClass));
  B->PhysicsAsset=Collision;
  Old->AddClothingAsset(A);New->AddClothingAsset(B);
 }
 TestTrue(TEXT("Matching section configs can be copied"),UProject_JPlayerLocomotionLibrary::CopySurfaceClothingConfiguration(Old,New));
 const auto* First=CastChecked<UClothingAssetCommon>(New->GetMeshClothingAssets()[0]);
 auto* Copy=First->ClothConfigs.FindChecked(ConfigClass->GetFName()).Get();
 TestNotEqual(TEXT("New config does not retain a source object reference"),Copy,Shared);
 for(UClothingAssetBase* Asset:New->GetMeshClothingAssets())
 {
  const auto* Cloth=CastChecked<UClothingAssetCommon>(Asset);
  TestEqual(TEXT("Shared settings remain shared across sections"),Cloth->ClothConfigs.FindChecked(ConfigClass->GetFName()).Get(),Copy);
  TestEqual(TEXT("Target keeps its converted collision asset"),Cloth->PhysicsAsset.Get(),Collision);
  TestEqual(TEXT("Authored density survives copying"),Density->GetPropertyValue_InContainer(Copy),.7f);
  TestEqual(TEXT("Unwrapped enum values survive native copying"),Mass->GetUnderlyingProperty()->GetSignedIntPropertyValue(Mass->ContainerPtrToValuePtr<void>(Copy)),int64(1));
 }
 Density->SetPropertyValue_InContainer(Copy,.9f);
 TestEqual(TEXT("Source tuning remains independent"),Density->GetPropertyValue_InContainer(Shared),.7f);
 TestFalse(TEXT("Configuration copy cannot overwrite its source"),UProject_JPlayerLocomotionLibrary::CopySurfaceClothingConfiguration(Old,Old));
 return true;
}
#endif
