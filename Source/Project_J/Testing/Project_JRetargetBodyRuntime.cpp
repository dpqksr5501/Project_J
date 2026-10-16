#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Testing/Project_JAuthoredAnimationFixture.h"
#include "UObject/Package.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/Project_JRetargetBodyProfile.h"
#include "Animation/AnimClassInterface.h"
#include "AnimNodes/AnimNode_RetargetPoseFromMesh.h"
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
			Player->GetCharacterMovement()->Velocity = Warmup < 120 ? FVector::ZeroVector : FVector(200, 0, 0);
			Player->AddActorWorldOffset(Player->GetCharacterMovement()->Velocity / 60.f);
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
#endif
