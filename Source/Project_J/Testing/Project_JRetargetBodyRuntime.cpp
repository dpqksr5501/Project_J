#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Tests/Project_JMoverFrameFixture.h"
#include "Testing/Project_JAuthoredAnimationFixture.h"
#include "UObject/Package.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "AnimPose.h"
#include "Rig/IKRigDefinition.h"
#include "Rig/IKRigProcessor.h"
#include "Rig/IKRigDataTypes.h"
#include "Rig/Solvers/IKRigLimbSolver.h"
#include "AnimationRuntime.h"
#include "Animation/Project_JRetargetBodyProfile.h"
#include "Animation/AnimClassInterface.h"
#include "AnimNodes/AnimNode_RetargetPoseFromMesh.h"
#include "Retargeter/IKRetargetProcessor.h"
#include "BoneControllers/AnimNode_RigidBody.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsSimulation.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsActorHandle.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/DataValidation.h"

namespace
{
class FRetargetBodyRuntime : public IAutomationLatentCommand
{
	FAutomationTestBase* Test;
	UWorld* World = nullptr;
	AProject_JPlayerCharacter* Player = nullptr;
	USkeletalMeshComponent* Mesh = nullptr;
	UProject_JRetargetAnimInstance* Anim = nullptr;
	FAnimNode_RetargetPoseFromMesh* Node = nullptr;
	TArray<USkeletalMeshComponent*> Components;
	TStrongObjectPtr<UProject_JRetargetBodyProfile> Body, Alternate;
	TStrongObjectPtr<UIKRetargeter> AuthoredAsset;
	bool bAuthoredCustomSets = false;
	TArray<FName> AuthoredSets;
	int32 Stage = 0, Warmup = 0, WaitFrames = 0;
	FString Audit;
	double Deadline = 0;
	// Opt-in read-only diagnostic. Normal lifetime regression keeps its 12 ticks.
	bool bCostAudit = false;
	void AuditVisualLayout()
	{
		TSet<const USkeletalMesh*> Seen;
		for (const auto* Component : Components)
		{
			const auto* Asset = Component->GetSkeletalMeshAsset();
			if (!Asset) continue;
			Audit += FString::Printf(TEXT("VisualComponent Name=%s Asset=%s LOD=%d ForcedLOD=%d Leader=%s ClothInstances=%d ActiveClothing=%d\n"),
				*Component->GetName(), *Asset->GetPathName(), Component->GetPredictedLODLevel(), Component->GetForcedLOD(),
				*GetNameSafe(Component->LeaderPoseComponent.Get()), Component->GetClothingSimulationInstances().Num(), Asset->HasActiveClothingAssets());
			if (Seen.Contains(Asset)) continue;
			Seen.Add(Asset);
			const auto* Render = Asset->GetResourceForRendering();
			if (!Render) { Audit += TEXT("VisualLOD RenderDataUnavailable\n"); continue; }
			for (int32 Index = 0; Index < Render->LODRenderData.Num(); ++Index)
			{
				const auto& LOD = Render->LODRenderData[Index];
				int32 Triangles = 0, Sections = 0, MaxInfluences = 0;
				for (const auto& Section : LOD.RenderSections) if (!Section.bDisabled)
				{ Triangles += Section.NumTriangles; ++Sections; MaxInfluences = FMath::Max(MaxInfluences, Section.MaxBoneInfluences); }
				Audit += FString::Printf(TEXT("VisualLOD Asset=%s LOD=%d Vertices=%u Triangles=%d Sections=%d ActiveBones=%d RequiredBones=%d MaxInfluences=%d\n"),
					*Asset->GetPathName(), Index, LOD.GetNumVertices(), Triangles, Sections, LOD.ActiveBoneIndices.Num(), LOD.RequiredBones.Num(), MaxInfluences);
			}
		}
	}
	void AuditPhysicsParticipation(const FAnimNode_RigidBody& PhysicsNode, const UPhysicsAsset& Asset)
	{
		// Nominal pair potential and hierarchy, never a proof that a body is unused.
		// Cooked geometry, active LOD, shared PA consumers and simulation history
		// need separate validation before authoring a reduced physics asset.
		auto IsSimulated = [&PhysicsNode](const USkeletalBodySetup* Setup)
		{ return Setup && (Setup->PhysicsType == PhysType_Simulated || (PhysicsNode.bUseDefaultAsSimulated && Setup->PhysicsType == PhysType_Default)); };
		TSet<FName> SimulatedAncestors;
		const auto& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
		for (const auto& Setup : Asset.SkeletalBodySetups) if (IsSimulated(Setup))
			for (int32 Parent = Skeleton.FindBoneIndex(Setup->BoneName); Parent != INDEX_NONE; Parent = Skeleton.GetParentIndex(Parent))
				SimulatedAncestors.Add(Skeleton.GetBoneName(Parent));
		int32 Simulated = 0, Kinematic = 0, PotentialColliders = 0, JointAnchors = 0, NoDirectRole = 0, ReferencedAncestors = 0;
		for (int32 Index = 0; Index < Asset.SkeletalBodySetups.Num(); ++Index)
		{
			const auto* Setup = Asset.SkeletalBodySetups[Index].Get();
			if (!Setup) continue;
			if (IsSimulated(Setup)) { ++Simulated; continue; }
			++Kinematic;
			bool bPotentialContact = false, bJointAnchor = false;
			for (int32 Other = 0; Other < Asset.SkeletalBodySetups.Num(); ++Other)
			{
				const auto* Dynamic = Asset.SkeletalBodySetups[Other].Get();
				if (!IsSimulated(Dynamic)) continue;
				// Conservative pair potential, not proof that narrow-phase contact
				// occurs. Constraint-specific suppression can reduce it further.
				bPotentialContact |= Setup->CollisionReponse != EBodyCollisionResponse::BodyCollision_Disabled && Dynamic->CollisionReponse != EBodyCollisionResponse::BodyCollision_Disabled &&
					Setup->AggGeom.GetElementCount() > 0 && Dynamic->AggGeom.GetElementCount() > 0 && Asset.IsCollisionEnabled(Index, Other);
				for (const auto& Joint : Asset.ConstraintSetup) if (Joint)
				{
					const auto& CI = Joint->DefaultInstance;
					bJointAnchor |= (CI.ConstraintBone1 == Setup->BoneName && CI.ConstraintBone2 == Dynamic->BoneName) ||
						(CI.ConstraintBone2 == Setup->BoneName && CI.ConstraintBone1 == Dynamic->BoneName);
				}
			}
			PotentialColliders += bPotentialContact; JointAnchors += bJointAnchor;
			if (!bPotentialContact && !bJointAnchor)
			{
				++NoDirectRole;
				const bool bAncestor = SimulatedAncestors.Contains(Setup->BoneName);
				ReferencedAncestors += bAncestor;
				Audit += FString::Printf(TEXT("PhysicsNoDirectRole Asset=%s Bone=%s AncestorOfSimulated=%d\n"), *Asset.GetPathName(), *Setup->BoneName.ToString(), bAncestor);
			}
		}
		const auto& Solver = Asset.SolverSettings;
		Audit += FString::Printf(TEXT("PhysicsParticipation Asset=%s Simulated=%d Kinematic=%d PotentialColliders=%d JointAnchors=%d NoDirectRole=%d ReferencedAncestors=%d PositionIterations=%d VelocityIterations=%d ProjectionIterations=%d Linear=%d Manifolds=%d FixedTimeStep=%.6f\n"),
			*Asset.GetPathName(), Simulated, Kinematic, PotentialColliders, JointAnchors, NoDirectRole, ReferencedAncestors,
			Solver.PositionIterations, Solver.VelocityIterations, Solver.ProjectionIterations, Solver.bUseLinearJointSolver, Solver.bUseManifolds, Solver.FixedTimeStep);
	}
	void CaptureRetargetCost(const TCHAR* Phase)
	{
		// Engine-owned 30-evaluation EMA, sampled after world tick has joined the
		// animation task. It is neither frame wall time nor a fresh independent mean.
		Audit += FString::Printf(TEXT("RetargetCost Phase=%s NodeEMAms=%.6f\n"), Phase, Node->GetAverageExecutionTime() * 1000);
		if (const auto* Processor = Node->GetRetargetProcessor())
			for (const auto& OpStruct : Processor->GetRetargetOps())
			{
				const auto& Op = OpStruct.Get<FIKRetargetOpBase>();
				Audit += FString::Printf(TEXT("RetargetOp Phase=%s Name=%s Type=%s Initialized=%d Enabled=%d EMAms=%.6f\n"),
					Phase, *Op.GetName().ToString(), *OpStruct.GetScriptStruct()->GetName(), Op.IsInitialized(), Op.IsEnabled(), Op.Timer.GetRunningAverage() * 1000);
			}
		if (const auto* Interface = IAnimClassInterface::GetFromClass(Anim->GetClass()))
			for (auto* Property : Interface->GetAnimNodeProperties()) if (Property->Struct == FAnimNode_RigidBody::StaticStruct())
			{
				auto* PhysicsNode = Property->ContainerPtrToValuePtr<FAnimNode_RigidBody>(Anim);
				const auto* DeferredDefault = IConsoleManager::Get().FindConsoleVariable(TEXT("p.RigidBodyNode.DeferredSimulationDefault"));
				const auto* ForceDefault = IConsoleManager::Get().FindConsoleVariable(TEXT("p.RigidBodyNode.DeferredSimulationForceDefault"));
				const bool bDeferred = !DeferredDefault || !ForceDefault ||
					((PhysicsNode->SimulationTiming == ESimulationTiming::Default || ForceDefault->GetInt()) ? DeferredDefault->GetInt() != 0 : PhysicsNode->SimulationTiming == ESimulationTiming::Deferred);
				if (bDeferred) { Audit += TEXT("PhysicsActualSkipped DeferredSimulation\n"); continue; }
				if (auto* Simulation = PhysicsNode->GetSimulation())
				{
					int32 Dynamic = 0, Kinematic = 0;
					for (int32 Index = 0; Index < Simulation->NumActors(); ++Index)
					{
						const auto* Actor = Simulation->GetActorHandle(Index);
						Dynamic += Actor && Actor->IsSimulated(); Kinematic += Actor && Actor->GetIsKinematic();
					}
					Audit += FString::Printf(TEXT("PhysicsActual Phase=%s Asset=%s Actors=%d Dynamic=%d Kinematic=%d\n"),
						Phase, *GetPathNameSafe(PhysicsNode->GetPhysicsAsset()), Simulation->NumActors(), Dynamic, Kinematic);
				}
			}
	}

	void Cleanup()
	{
		if (!World) return;
		World->BeginTearingDown(); World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false); GEngine->DestroyWorldContext(World); World = nullptr;
	}
	bool Setup()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
		auto* Controller = World->SpawnActor<AAIController>(); auto* State = World->SpawnActor<AProject_JPlayerState>();
		if (!Controller || !State) return false;
		Controller->SetPlayerState(State); State->SetOwner(Controller);
		Player = ProjectJAuthoredAnimationFixture::Spawn(World, Controller, State, FVector(0,0,1000));
		if (!Test->TestNotNull(TEXT("Production authored player"), Player)) return false;
		TInlineComponentArray<USkeletalMeshComponent*> Visuals(Player);
		int32 Bodies = 0;
		for (auto* Component : Visuals)
		{
			Components.Add(Component);
			if (Cast<UProject_JRetargetAnimInstance>(Component->GetAnimInstance())) { Mesh = Component; ++Bodies; }
		}
		Test->TestEqual(TEXT("One visual body performs retargeting"), Bodies, 1);
		Anim = Mesh ? Cast<UProject_JRetargetAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
		if (!Test->TestNotNull(TEXT("Production visual instance"), Anim)) return false;
		Body.Reset(const_cast<UProject_JRetargetBodyProfile*>(Anim->GetRetargetBodyProfile()));
		if (!Test->TestNotNull(TEXT("Saved body binding"), Body.Get()) || !Test->TestNotNull(TEXT("Saved skeleton family"), Body->Family.Get()) ||
			!Test->TestNotNull(TEXT("Saved hand calibration"), Body->HandGrip.Get())) return false;
		FDataValidationContext FamilyContext, BodyContext, GripContext;
		Test->TestTrue(TEXT("Family data validation"), Body->Family->IsDataValid(FamilyContext) == EDataValidationResult::Valid);
		Test->TestTrue(TEXT("Body data validation"), Body->IsDataValid(BodyContext) == EDataValidationResult::Valid);
		Test->TestTrue(TEXT("Hand calibration data validation"), Body->HandGrip->IsDataValid(GripContext) != EDataValidationResult::Invalid);
		Test->TestNull(TEXT("Migrated visual CDO has no duplicate legacy calibration"), Anim->HandGripProfile.Get());
		Test->TestEqual(TEXT("Body resolves the original contact profile"), Anim->GetResolvedHandGripProfile(), static_cast<const UProject_JHandGripProfile*>(Body->HandGrip.Get()));
		Test->TestTrue(TEXT("Family matches the real mesh pair"), Body->Family->Matches(Player->GetMesh()->GetSkeletalMeshAsset()->GetSkeleton(), Mesh->GetSkeletalMeshAsset()->GetSkeleton()));
		const auto* Interface = IAnimClassInterface::GetFromClass(Anim->GetClass());
		if (!Interface) return false;
		int32 Retargets = 0, PhysicsNodes = 0;
		Audit = FString::Printf(TEXT("Body=%s\nFamily=%s\nSource=%s\nTarget=%s\n"), *Body->GetPathName(), *Body->Family->GetPathName(),
			*Player->GetMesh()->GetSkeletalMeshAsset()->GetPathName(), *Mesh->GetSkeletalMeshAsset()->GetPathName());
		if (bCostAudit) AuditVisualLayout();
		for (auto* Property : Interface->GetAnimNodeProperties())
		{
			if (Property->Struct == FAnimNode_RetargetPoseFromMesh::StaticStruct()) { Node = Property->ContainerPtrToValuePtr<FAnimNode_RetargetPoseFromMesh>(Anim); ++Retargets; }
			if (Property->Struct == FAnimNode_RigidBody::StaticStruct())
			{
				++PhysicsNodes;
				FString Value; Property->ExportText_InContainer(0, Value, Anim, nullptr, Anim, PPF_None);
				Audit += TEXT("Physics=") + Value + TEXT("\n");
				const auto* PhysicsNode = Property->ContainerPtrToValuePtr<FAnimNode_RigidBody>(Anim);
				const auto* PhysicsAsset = PhysicsNode->OverridePhysicsAsset ? PhysicsNode->OverridePhysicsAsset.Get()
					: (PhysicsNode->bDefaultToSkeletalMeshPhysicsAsset ? Mesh->GetPhysicsAsset() : nullptr);
				if (PhysicsAsset)
				{
					if (bCostAudit) AuditPhysicsParticipation(*PhysicsNode, *PhysicsAsset);
					Audit += FString::Printf(TEXT("PhysicsAsset=%s Bodies=%d Constraints=%d\n"),
						*PhysicsAsset->GetPathName(), PhysicsAsset->SkeletalBodySetups.Num(), PhysicsAsset->ConstraintSetup.Num());
					for (const auto& Setup : PhysicsAsset->SkeletalBodySetups) if (Setup)
						Audit += FString::Printf(TEXT("PhysicsBone=%s Type=%d Asset=%s\n"),
							*Setup->BoneName.ToString(), int32(Setup->PhysicsType), *PhysicsAsset->GetPathName());
				}
			}
		}
		Test->TestEqual(TEXT("Composed source is retargeted once"), Retargets, 1);
		Test->TestEqual(TEXT("Three existing secondary physics nodes remain"), PhysicsNodes, 3);
		if (Retargets != 1) return false;
		AuthoredAsset.Reset(Node->IKRetargeterAsset.Get()); bAuthoredCustomSets = Node->bUseCustomOverrideSets; AuthoredSets = Node->OverrideSetsToApply;
		Test->TestEqual(TEXT("Body bridge reuses the authored retargeter"), AuthoredAsset.Get(), Body->Family->Retargeter.Get());
		Audit += FString::Printf(TEXT("Retargets=%d PhysicsNodes=%d\n"), Retargets, PhysicsNodes);
		Deadline = FPlatformTime::Seconds() + 60;
		return true;
	}
public:
	explicit FRetargetBodyRuntime(FAutomationTestBase* InTest) : Test(InTest), bCostAudit(FParse::Param(FCommandLine::Get(), TEXT("ProjectJRetargetCostAudit"))) {}
	~FRetargetBodyRuntime() { Cleanup(); }
	virtual bool Update() override
	{
		if (!World && !Setup()) { Test->AddError(TEXT("Production retarget fixture setup failed")); Cleanup(); return true; }
		if (FPlatformTime::Seconds() > Deadline) { Test->AddError(TEXT("Retarget runtime fixture timeout")); Cleanup(); return true; }
		// One World tick per engine frame: synchronous ticks share traversal counters.
		const auto Previous = Anim->GetUpdateCounter().Get();
		const auto PreviousBones = Mesh->GetBoneTransformRevisionNumber();
		if (bCostAudit && Stage == 0)
		{
			const FVector Velocity=Warmup < 120 ? FVector::ZeroVector : FVector(200,0,0);
   Test->TestTrue(TEXT("Synthetic retarget frame published"),ProjectJ::Tests::PublishMoverFrame(Player,MOVE_None,Velocity));
   Player->AddActorWorldOffset(Velocity/60.f);
		}
		for (auto* Component : Components) Component->SetLastRenderTime(World->GetTimeSeconds());
		World->Tick(LEVELTICK_All, 1.f/60);
		if (Anim->GetUpdateCounter().Get() == Previous)
		{
			if (++WaitFrames > 30) { Test->AddError(TEXT("No actual visual graph updates within bounded budget wait")); Cleanup(); return true; }
			return false;
		}
		WaitFrames = 0;
		// An update can be followed by interpolation instead of a graph evaluation.
		// Count the diagnostic window only when a new body pose was finalized;
		// preserve the production budget instead of forcing full-rate evaluation.
		if (bCostAudit && Stage == 0 && Mesh->GetBoneTransformRevisionNumber() == PreviousBones) return false;
		switch (Stage)
		{
		case 0:
			++Warmup;
			if (bCostAudit && (Warmup == 120 || Warmup == 240)) CaptureRetargetCost(Warmup == 120 ? TEXT("Idle") : TEXT("Moving"));
			if (Warmup < (bCostAudit ? 240 : 12)) return false;
			Test->TestTrue(TEXT("Target body actually evaluated bones"), !Mesh->GetComponentSpaceTransforms().IsEmpty());
   {
   const auto* Processor=Node->GetRetargetProcessor();
   Test->TestTrue(TEXT("The actual body retarget processor initialized"),Processor && Processor->IsInitialized());
   if(Processor) Test->TestEqual(TEXT("The actual body processor consumes the authored source-family retargeter"),Processor->GetRetargetAsset(),static_cast<const UIKRetargeter*>(Body->Family->Retargeter.Get()));
   // With no authored hand contact, all post-retarget controls must preserve the
   // arm pose. Comparing two copies of the same graph alone cannot catch an
   // accidental shared correction (the earlier physics equivalence fixture).
   Test->TestTrue(TEXT("Passive arm fixture has no hand IK contact"),Anim->RightGripAlpha<=UE_SMALL_NUMBER && Anim->LeftGripAlpha<=UE_SMALL_NUMBER);
   if(Processor && Processor->IsInitialized())
   {
    const auto& Before=Processor->GetTargetSkeleton().OutputGlobalPose;
    const auto& After=Mesh->GetComponentSpaceTransforms();
    const auto Calibration=Anim->GetHandGripCalibration();
    double MaxArmPosition=0.,MaxArmAngle=0.;int32 CheckedArms=0;
    for(const auto& Arm:{Calibration.PrimaryArm,Calibration.SecondaryArm})
     for(const FName Bone:{Arm.Shoulder,Arm.Elbow,Arm.Hand})
     {
      const int32 Index=Mesh->GetBoneIndex(Bone);
      if(!Test->TestTrue(TEXT("Passive arm roles exist in the evaluated target pose"),Before.IsValidIndex(Index) && After.IsValidIndex(Index)))continue;
      const double Position=FVector::Distance(Before[Index].GetLocation(),After[Index].GetLocation());
      const double Angle=FMath::RadiansToDegrees(Before[Index].GetRotation().GetNormalized().AngularDistance(After[Index].GetRotation().GetNormalized()));
      Test->TestTrue(TEXT("Passive arm output is finite"),FMath::IsFinite(Position) && FMath::IsFinite(Angle));
      MaxArmPosition=FMath::Max(MaxArmPosition,Position);MaxArmAngle=FMath::Max(MaxArmAngle,Angle);++CheckedArms;
     }
    Test->TestEqual(TEXT("Both complete passive arm chains checked"),CheckedArms,6);
    Test->TestTrue(TEXT("No post-retarget arm displacement without contact"),MaxArmPosition<.02);
    Test->TestTrue(TEXT("No post-retarget arm rotation without contact"),MaxArmAngle<.05);
    Audit+=FString::Printf(TEXT("PassiveArmParity Bones=%d MaxPositionCm=%.8f MaxRotationDegrees=%.8f\n"),CheckedArms,MaxArmPosition,MaxArmAngle);
   }

   }
			Test->TestTrue(TEXT("Both body contact anchors are valid"), Anim->GetBodyHandContact(true).IsValid() && Anim->GetBodyHandContact(false).IsValid());
			Test->TestTrue(TEXT("Native retarget node captures the real parent source"), Node->SourceMeshComponent.Get() == Player->GetMesh());
			Alternate.Reset(DuplicateObject<UProject_JRetargetBodyProfile>(Body.Get(), GetTransientPackage()));
			Alternate->Family = DuplicateObject<UProject_JRetargetFamilyProfile>(Body->Family, GetTransientPackage());
			Alternate->Family->Retargeter = DuplicateObject<UIKRetargeter>(AuthoredAsset.Get(), GetTransientPackage());
			Anim->RetargetBodyProfile = Alternate.Get(); break;
		case 1:
			Test->TestEqual(TEXT("Profile changes reach the native node before source pre-update"), Node->IKRetargeterAsset.Get(), Alternate->Family->Retargeter.Get());
			Anim->RetargetBodyProfile = nullptr; break;
		case 2:
			Test->TestEqual(TEXT("Removing a profile restores the authored retargeter"), Node->IKRetargeterAsset.Get(), AuthoredAsset.Get());
			Test->TestTrue(TEXT("Authored override sets are restored"), Node->bUseCustomOverrideSets == bAuthoredCustomSets && Node->OverrideSetsToApply == AuthoredSets);
			Anim->RetargetBodyProfile = Alternate.Get(); break;
		case 3:
			Test->TestEqual(TEXT("Bridge recovers on a second activation"), Node->IKRetargeterAsset.Get(), Alternate->Family->Retargeter.Get());
			Anim->InitializeAnimation(); Anim->RetargetBodyProfile = nullptr; break;
		case 4:
			Test->TestEqual(TEXT("Reinitialize cannot promote a prior override to an authored default"), Node->IKRetargeterAsset.Get(), AuthoredAsset.Get());
			Alternate->Family->SourceSkeleton = Mesh->GetSkeletalMeshAsset()->GetSkeleton();
			Anim->RetargetBodyProfile = Alternate.Get(); break;
		case 5:
			Test->TestEqual(TEXT("An incompatible source cannot hijack the native graph"), Node->IKRetargeterAsset.Get(), AuthoredAsset.Get());
			Anim->RetargetBodyProfile = Body.Get(); break;
		default:
			Test->TestEqual(TEXT("Original body is restored"), Anim->GetResolvedHandGripProfile(), static_cast<const UProject_JHandGripProfile*>(Body->HandGrip.Get()));
			Audit += FString::Printf(TEXT("PrimaryHand=%s SecondaryHand=%s\n"), *Anim->GetBodyHandContact(true).Hand.ToString(), *Anim->GetBodyHandContact(false).Hand.ToString());
			const FString Output = FPaths::ProjectSavedDir() / TEXT("Validation/RuntimeRetarget_20261013/ProductionRuntime.txt");
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output), true);
			Test->TestTrue(TEXT("Runtime audit saved"), FFileHelper::SaveStringToFile(Audit, *Output));
			Cleanup(); return true;
		}
		++Stage; return false;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJRetargetBodyRuntime, "ProjectJ.Retarget.ProductionRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJRetargetBodyRuntime::RunTest(const FString&)
{ ADD_LATENT_AUTOMATION_COMMAND(FRetargetBodyRuntime(this)); return true; }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMannyBip01LimbGeometry, "ProjectJ.Retarget.MannyBip01LimbGeometry",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMannyBip01LimbGeometry::RunTest(const FString&)
{
 auto* Family=LoadObject<UProject_JRetargetFamilyProfile>(nullptr,TEXT("/Game/DataAssetSets/Presentation/Retarget/DA_RetargetFamily_Mannequin_Bip01.DA_RetargetFamily_Mannequin_Bip01"));
 if(!TestNotNull(TEXT("Production family"),Family) || !TestNotNull(TEXT("Production retargeter"),Family->Retargeter.Get()))return false;
 const auto* Retargeter=Family->Retargeter.Get();const auto* Source=Retargeter->GetPreviewMesh(ERetargetSourceOrTarget::Source);const auto* Target=Retargeter->GetPreviewMesh(ERetargetSourceOrTarget::Target);
 if(!TestNotNull(TEXT("Actual source mesh"),Source)||!TestNotNull(TEXT("Actual target mesh"),Target))return false;
 const auto* Rig=Retargeter->GetIKRig(ERetargetSourceOrTarget::Target);if(!TestNotNull(TEXT("Actual target chains"),Rig))return false;
 for(const TCHAR* Side:{TEXT("Left"),TEXT("Right")})for(int32 Digit=1;Digit<=4;++Digit)
 {
  const TCHAR* Names[]={TEXT("Thumb"),TEXT("Index"),TEXT("Middle"),TEXT("Ring"),TEXT("Pinky")};const FString Name=FString(Side)+Names[Digit];const auto* Chain=Rig->GetRetargetChainByName(FName(*Name));
  if(TestNotNull(TEXT("Anatomical finger chain exists"),Chain))TestEqual(TEXT("The proximal phalanx is part of its finger"),Chain->StartBone.BoneName,FName(*FString::Printf(TEXT("Bip01-%s-Finger%d"),FString(Side)==TEXT("Left")?TEXT("L"):TEXT("R"),Digit)));
  TestNull(TEXT("A proximal phalanx cannot also be a fake metacarpal"),Rig->GetRetargetChainByName(FName(*(Name+TEXT("Metacarpal")))));
 }
 FIKRetargetProcessor Processor;FRetargetInitParameters Init;Init.SourceSkeletalMesh=Source;Init.TargetSkeletalMesh=Target;Init.RetargeterAsset=Retargeter;Processor.Initialize(Init);
 if(!TestTrue(TEXT("Actual processor initializes"),Processor.IsInitialized()))return false;
 // A processor can report initialized while an incomplete limb solver silently
 // does nothing. Check the actual Bip01 hierarchy and exercise each effector.
 const auto& TargetRef=Target->GetRefSkeleton();TArray<FTransform> Reference;
 FAnimationRuntime::FillUpComponentSpaceTransforms(TargetRef,TargetRef.GetRefBonePose(),Reference);
 int32 SolvedLimbs=0;double MaxGoalError=0.;
 for(const auto& Entry:Rig->GetSolverStructs())
 {
  const auto* Limb=Entry.GetPtr<FIKRigLimbSolver>();if(!Limb || !Limb->IsEnabled())continue;
  const auto& Settings=Limb->Settings;
  const auto* Chain=Rig->GetRetargetChains().FindByPredicate([&](const FBoneChain& C){return C.IKGoalName==Settings.GoalName;});
  if(!TestNotNull(TEXT("Each limb goal has an anatomical retarget chain"),Chain))continue;
  TestEqual(TEXT("Solver starts at the chain's actual shoulder or hip"),Settings.StartBone,Chain->StartBone.BoneName);
  TestEqual(TEXT("Solver ends at the chain's actual wrist or ankle"),Settings.EndBone,Chain->EndBone.BoneName);
  const int32 Start=TargetRef.FindBoneIndex(Settings.StartBone),End=TargetRef.FindBoneIndex(Settings.EndBone);
  if(!TestTrue(TEXT("Limb endpoints exist on the actual target mesh"),Reference.IsValidIndex(Start)&&Reference.IsValidIndex(End)))continue;
  const int32 Middle=TargetRef.GetParentIndex(End);
  if(!TestTrue(TEXT("Bip01 shoulder-elbow-wrist and hip-knee-ankle are direct three-joint chains"),Middle!=INDEX_NONE && TargetRef.GetParentIndex(Middle)==Start))continue;
  const auto* Goal=Rig->GetGoalArray().FindByPredicate([&](const UIKRigEffectorGoal* G){return G && G->GoalName==Settings.GoalName;});
  if(!TestTrue(TEXT("Effector goal drives the chain endpoint"),Goal && (*Goal)->BoneName==Settings.EndBone))continue;
  FIKRigProcessor Solve;Solve.Initialize(Rig,Target,FIKRigGoalContainer());
  if(!TestTrue(TEXT("Target IK processor initializes"),Solve.IsInitialized()))continue;
  Solve.SetInputPoseToRefPose();FIKRigGoal Request(Settings.GoalName,Settings.EndBone);
  Request.PositionSpace=EIKRigGoalSpace::Component;Request.RotationSpace=EIKRigGoalSpace::Component;
  Request.Position=Reference[End].GetLocation()+(Reference[Start].GetLocation()-Reference[End].GetLocation()).GetSafeNormal();
  Request.Rotation=Reference[End].Rotator();Solve.SetIKGoal(Request);Solve.Solve();TArray<FTransform> Solved;Solve.GetOutputPoseGlobal(Solved);
  if(!TestTrue(TEXT("IK returns the real endpoint"),Solved.IsValidIndex(End)))continue;
  const double Error=FVector::Distance(Solved[End].GetLocation(),Request.Position);MaxGoalError=FMath::Max(MaxGoalError,Error);
  TestTrue(TEXT("The solver actually moves the effector to its reachable goal"),Error<.05);
  TestTrue(TEXT("Limb solve preserves its shoulder/hip location"),FVector::Distance(Solved[Start].GetLocation(),Reference[Start].GetLocation())<.001);
  ++SolvedLimbs;
 }
 TestEqual(TEXT("Both arms and both legs were functionally solved"),SolvedLimbs,4);
 double MaxElbow=0.,MaxFinger=0.,MaxKnee=0.,MaxArmDirection=0.;int32 Elbows=0,Fingers=0,Knees=0;
 const auto Flex=[](const FVector& A,const FVector& B,const FVector& C)
 {return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct((B-A).GetSafeNormal(),(C-B).GetSafeNormal()),-1.,1.)));};
 const TArray<FString> Clips={TEXT("Locomotion/Idle/M_Relaxed_Stand_Idle_Loop"),TEXT("Locomotion/Idle/M_Neutral_Stand_Idle_Loop"),TEXT("Locomotion/Walk/M_Relaxed_Walk_Loop_F"),TEXT("Locomotion/Run/M_Relaxed_Run_Loop_F"),TEXT("Locomotion/Sprint/M_Relaxed_Sprint_Loop_F"),TEXT("ProjectJump/Jump/M_Neutral_Jump_F_Start_Stand_Lfoot")};
 FAnimPoseEvaluationOptions Options;Options.OptionalSkeletalMesh=const_cast<USkeletalMesh*>(Source);Options.EvaluationType=EAnimDataEvalType::Compressed;Options.bIncorporateRootMotionIntoPose=false;
 for(const FString& Clip:Clips)
 {
  auto* Sequence=LoadObject<UAnimSequence>(nullptr,*(TEXT("/Game/Characters/Mannequins/Animations/")+Clip));if(!TestNotNull(TEXT("Authored pose sample"),Sequence))continue;
  for(const double Phase:{0.,.25,.5,.75})
  {
   FAnimPose Pose;UAnimPoseExtensions::GetAnimPoseAtTime(Sequence,Sequence->GetPlayLength()*Phase,Options,Pose);if(!TestTrue(TEXT("Source pose is valid"),Pose.IsValid()))continue;
   TArray<FTransform> Input;const auto& Ref=Source->GetRefSkeleton();for(int32 I=0;I<Ref.GetNum();++I)Input.Add(UAnimPoseExtensions::GetBonePose(Pose,Ref.GetBoneName(I),EAnimPoseSpaces::World));
   FRetargetRunParameters Run;Run.SourceGlobalPose=&Input;Run.OverrideSetsToApply=Retargeter->GetOverrideSetsToApply();const auto& Output=Processor.RunRetargeter(Run);
   const auto Src=[&](const FString& Bone){return UAnimPoseExtensions::GetBonePose(Pose,FName(*Bone),EAnimPoseSpaces::World).GetLocation();};
   const auto Dst=[&](const FString& Bone){const int32 I=Target->GetRefSkeleton().FindBoneIndex(FName(*Bone));if(!Output.IsValidIndex(I)){AddError(TEXT("Missing target anatomy: ")+Bone);return FVector::ZeroVector;}return Output[I].GetLocation();};
   for(const TCHAR* Side:{TEXT("l"),TEXT("r")})
   {
    const FString Label=FString(Side).ToUpper();const FString Prefix=TEXT("Bip01-")+Label+TEXT("-");
    const double S=Flex(Src(TEXT("upperarm_")+FString(Side)),Src(TEXT("lowerarm_")+FString(Side)),Src(TEXT("hand_")+FString(Side)));
    const double T=Flex(Dst(Prefix+TEXT("UpperArm")),Dst(Prefix+TEXT("Forearm")),Dst(Prefix+TEXT("Hand")));
    TestTrue(TEXT("Elbow geometry is finite"),FMath::IsFinite(S)&&FMath::IsFinite(T));MaxElbow=FMath::Max(MaxElbow,FMath::Abs(S-T));++Elbows;
    const FVector SourceArm=(Src(TEXT("hand_")+FString(Side))-Src(TEXT("upperarm_")+FString(Side))).GetSafeNormal();
    const FVector TargetArm=(Dst(Prefix+TEXT("Hand"))-Dst(Prefix+TEXT("UpperArm"))).GetSafeNormal();
    MaxArmDirection=FMath::Max(MaxArmDirection,FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(SourceArm,TargetArm),-1.,1.))));
    const double SourceKnee=Flex(Src(TEXT("thigh_")+FString(Side)),Src(TEXT("calf_")+FString(Side)),Src(TEXT("foot_")+FString(Side)));
    const double TargetKnee=Flex(Dst(Prefix+TEXT("Thigh")),Dst(Prefix+TEXT("Calf")),Dst(Prefix+TEXT("Foot")));
    TestTrue(TEXT("Knee geometry is finite"),FMath::IsFinite(SourceKnee)&&FMath::IsFinite(TargetKnee));MaxKnee=FMath::Max(MaxKnee,FMath::Abs(SourceKnee-TargetKnee));++Knees;
    const TCHAR* Digits[]={TEXT("thumb"),TEXT("index"),TEXT("middle"),TEXT("ring"),TEXT("pinky")};
    for(int32 Digit=0;Digit<5;++Digit)
    {
     const FString Stem=Digits[Digit];const FString End=TEXT("_")+FString(Side);const FString TargetStem=Prefix+TEXT("Finger")+FString::FromInt(Digit);
     const double A=Flex(Src(Stem+TEXT("_01")+End),Src(Stem+TEXT("_02")+End),Src(Stem+TEXT("_03")+End));
     const double B=Flex(Dst(TargetStem),Dst(TargetStem+TEXT("1")),Dst(TargetStem+TEXT("2")));
     TestTrue(TEXT("Phalanx geometry is finite"),FMath::IsFinite(A)&&FMath::IsFinite(B));MaxFinger=FMath::Max(MaxFinger,FMath::Abs(A-B));++Fingers;
    }
   }
  }
 }
 TestEqual(TEXT("Both arms covered across Idle, movement and jump"),Elbows,48);TestEqual(TEXT("All ten fingers covered across the same poses"),Fingers,240);
 TestTrue(TEXT("Body retargeting preserves authored elbow bend"),MaxElbow<4.);TestTrue(TEXT("Body retargeting preserves authored phalanx bend"),MaxFinger<4.);
 TestEqual(TEXT("Both knees covered across the same poses"),Knees,48);
 TestTrue(TEXT("Body retargeting preserves authored knee bend"),MaxKnee<2.);
 TestTrue(TEXT("Body retargeting preserves shoulder-to-wrist direction"),MaxArmDirection<1.);
 AddInfo(FString::Printf(TEXT("Limb geometry: elbows=%d maxDelta=%.6f deg; fingers=%d maxDelta=%.6f deg; knees=%d maxDelta=%.6f deg; armDirection=%.6f deg; solvedGoals=%d maxGoalError=%.6f cm"),Elbows,MaxElbow,Fingers,MaxFinger,Knees,MaxKnee,MaxArmDirection,SolvedLimbs,MaxGoalError));return true;
}

#endif
