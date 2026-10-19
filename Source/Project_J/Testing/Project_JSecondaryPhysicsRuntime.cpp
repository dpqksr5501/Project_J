#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Testing/Project_JAuthoredAnimationFixture.h"
#include "BoneControllers/AnimNode_RigidBody.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsActorHandle.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/AnimClassInterface.h"
#include "AnimNodes/AnimNode_RetargetPoseFromMesh.h"
#include "Components/Project_JAnimationUpdateCoordinatorComponent.h"
#include "Components/Project_JSkillInputExecutionComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/Package.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UObject/StrongObjectPtr.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsSimulation.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
// Compare the actual authored visual graph with a data copy. Both fresh test
// components start before their first evaluation. The production body and its
// animation lifecycle are never replaced or reset by this quality fixture.
class FSecondaryPhysicsEquivalence : public IAutomationLatentCommand
{
	FAutomationTestBase* Test;
	UWorld* World = nullptr;
	AProject_JPlayerCharacter* Player = nullptr;
	USkeletalMeshComponent* Reference = nullptr;
	USkeletalMeshComponent* Candidate = nullptr;
	FAnimNode_RetargetPoseFromMesh* RetargetA = nullptr;
	FAnimNode_RetargetPoseFromMesh* RetargetB = nullptr;
	TArray<USkeletalMeshComponent*> Components;
	TArray<TStrongObjectPtr<UPhysicsAsset>> Copies;
	int32 Frame = 0, Samples = 0;
	double Deadline = 0, MaxPosition = 0, MaxAngle = 0, MaxScale = 0, MaxBasePosition = 0;
	FString Audit;
	int32 ActualActors = 0, DynamicActors = 0;
	double DynamicMotion = 0;
	int32 MeshLODCount = 1;

	void Cleanup()
	{
		if (!World) return;
		World->BeginTearingDown(); World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false); GEngine->DestroyWorldContext(World); World = nullptr;
	}
	USkeletalMeshComponent* MakeFreshBody(USkeletalMeshComponent* Authored)
	{
		auto* Result = NewObject<USkeletalMeshComponent>(Player);
		// InitAnim can immediately evaluate TickAnimation(0)/RefreshBones.
		// Defer that evaluation until both data sets have been installed, rather
		// than replacing a PA after one graph has acquired simulation history.
		Result->bUseRefPoseOnInitAnim = true;
		Result->SetupAttachment(Player->GetMesh());
		Result->SetRelativeTransform(Authored->GetRelativeTransform());
		Result->SetSkeletalMeshAsset(Authored->GetSkeletalMeshAsset());
		Result->RegisterComponent(); Player->AddInstanceComponent(Result);
		Result->AddTickPrerequisiteComponent(Player->GetMesh());
		Result->SetAnimInstanceClass(Authored->GetAnimClass());
		Result->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Result->bEnableUpdateRateOptimizations = false;
		Result->SetComponentTickEnabled(true);
		Components.Add(Result);
		return Result;
	}
	bool Setup()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
		auto* Controller = World->SpawnActor<AAIController>();
		auto* State = World->SpawnActor<AProject_JPlayerState>();
		if (!Controller || !State) return false;
		Controller->SetPlayerState(State); State->SetOwner(Controller);
		Player = ProjectJAuthoredAnimationFixture::Spawn(World, Controller, State, FVector(0,0,1000));
		if (!Player) return false;
		Player->FindComponentByClass<UProject_JAnimationUpdateCoordinatorComponent>()->bAllowVisualBudget = false;
		TInlineComponentArray<USkeletalMeshComponent*> Visuals(Player);
		USkeletalMeshComponent* Authored = nullptr;
		for (auto* Mesh : Visuals)
		{
			Components.Add(Mesh);
			if (Cast<UProject_JRetargetAnimInstance>(Mesh->GetAnimInstance())) Authored = Mesh;
		}
		if (!Authored) return false;
		Reference = MakeFreshBody(Authored); Candidate = MakeFreshBody(Authored);
		MeshLODCount=Authored->GetSkeletalMeshAsset()->GetLODNum();
		Reference->SetForcedLOD(1);
		Test->TestFalse(TEXT("Reference has not updated before data installation"), Reference->GetAnimInstance()->GetUpdateCounter().HasEverBeenUpdated());
		Test->TestFalse(TEXT("Candidate has not updated before data installation"), Candidate->GetAnimInstance()->GetUpdateCounter().HasEverBeenUpdated());
		for (auto* Property : IAnimClassInterface::GetFromClass(Authored->GetAnimClass())->GetAnimNodeProperties())
		{
			if (Property->Struct == FAnimNode_RetargetPoseFromMesh::StaticStruct())
			{
				RetargetA = Property->ContainerPtrToValuePtr<FAnimNode_RetargetPoseFromMesh>(Reference->GetAnimInstance());
				RetargetB = Property->ContainerPtrToValuePtr<FAnimNode_RetargetPoseFromMesh>(Candidate->GetAnimInstance());
			}
			if (Property->Struct == FAnimNode_RigidBody::StaticStruct())
			{
				auto* Node = Property->ContainerPtrToValuePtr<FAnimNode_RigidBody>(Candidate->GetAnimInstance());
				if (!Node->OverridePhysicsAsset) return false;
				auto* Copy = DuplicateObject<UPhysicsAsset>(Node->OverridePhysicsAsset.Get(),GetTransientPackage());
				if (!Copy) return false;
				Copy->UpdateBodySetupIndexMap(); Copy->UpdateBoundsBodiesArray();
				Audit += FString::Printf(TEXT("PhysicsCopy Source=%s OriginalBodies=%d CopiedBodies=%d\n"), *Node->OverridePhysicsAsset->GetPathName(), Node->OverridePhysicsAsset->SkeletalBodySetups.Num(), Copy->SkeletalBodySetups.Num());
				Copies.Emplace(Copy); Node->OverridePhysicsAsset = Copy;
			}
		}
		Test->TestEqual(TEXT("All three authored physics nodes participate"), Copies.Num(), 3);
		// Reproduce InitAnim's immediate zero-delta evaluation only after both
		// complete data sets are installed. There is no mid-history PA swap.
		Reference->TickAnimation(0.f,false); Reference->RefreshBoneTransforms();
		Candidate->TickAnimation(0.f,false); Candidate->RefreshBoneTransforms();
		Deadline = FPlatformTime::Seconds()+60;
		return Copies.Num()==3 && RetargetA && RetargetB;
	}
	void AdvanceInput()
	{
		auto* Movement = Player->GetCharacterMovement();
		const bool bAir = Frame>=360 && Frame<420;
		Movement->SetMovementMode(bAir ? MOVE_Falling : MOVE_Walking);
		if (Frame>=240 && Frame<360) Player->AddActorWorldRotation(FRotator(0,.5,0));
		Movement->Velocity = Player->GetActorForwardVector()*(Frame<120 ? 0 : Frame<240 ? 200 : 600) + FVector(0,0,bAir ? 300-(Frame-360)*10 : 0);
		Player->AddActorWorldOffset(Movement->Velocity/60.f);
		if (Frame==440) Player->SetActorLocation(Player->GetActorLocation()+FVector(1200,0,0),false,nullptr,ETeleportType::TeleportPhysics);
		if (Frame==480 || Frame==540 || Frame==600) Player->SetActorScale3D(FVector(Frame==480 ? .85 : Frame==540 ? 1.15 : 1));
		if (Frame==660)
		{
			Player->GetAbilitySystemComponent()->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
			Player->FindComponentByClass<UProject_JSkillInputExecutionComponent>()->HandleInputTagPressed(FProject_JGameplayTags::Get().InputTag_Weapon_LMB);
			Test->TestTrue(TEXT("Actual authored attack montage starts"), Player->GetMesh()->GetAnimInstance()->IsAnyMontagePlaying());
		}
	}
	void Compare()
	{
		Test->TestTrue(TEXT("Both graphs consume the same canonical source"), RetargetA->SourceMeshComponent==RetargetB->SourceMeshComponent && RetargetA->SourceMeshComponent.Get()==Player->GetMesh());
		Test->TestTrue(TEXT("Paired component transforms match"), Reference->GetComponentTransform().Equals(Candidate->GetComponentTransform(),.000001));
		if (!RetargetA->GetRetargetProcessor() || !RetargetB->GetRetargetProcessor()) { Test->AddError(TEXT("No real retarget processor")); return; }
		const auto& BaseA = RetargetA->GetRetargetProcessor()->GetTargetSkeleton().OutputGlobalPose;
		const auto& BaseB = RetargetB->GetRetargetProcessor()->GetTargetSkeleton().OutputGlobalPose;
		if (Test->TestEqual(TEXT("Retarget input layouts match"),BaseA.Num(),BaseB.Num()))
			for (int32 I=0; I<BaseA.Num(); ++I) MaxBasePosition=FMath::Max(MaxBasePosition,FVector::Distance(BaseA[I].GetLocation(),BaseB[I].GetLocation()));
		const auto& A = Reference->GetComponentSpaceTransforms(); const auto& B = Candidate->GetComponentSpaceTransforms();
		if (!Test->TestEqual(TEXT("Evaluated bone layouts match"),A.Num(),B.Num())) return;
		for (int32 I=0; I<A.Num(); ++I)
		{
			const double P=FVector::Distance(A[I].GetLocation(),B[I].GetLocation());
			const double R=FMath::RadiansToDegrees(A[I].GetRotation().AngularDistance(B[I].GetRotation()));
			const double S=FVector::Distance(A[I].GetScale3D(),B[I].GetScale3D());
			if (!FMath::IsFinite(P) || !FMath::IsFinite(R) || !FMath::IsFinite(S)) { Test->AddError(TEXT("Nonfinite physics output")); continue; }
			MaxPosition=FMath::Max(MaxPosition,P); MaxAngle=FMath::Max(MaxAngle,R); MaxScale=FMath::Max(MaxScale,S);
		}
		if (Frame==0)
			for (auto* Property : IAnimClassInterface::GetFromClass(Candidate->GetAnimClass())->GetAnimNodeProperties())
				if (Property->Struct==FAnimNode_RigidBody::StaticStruct())
				{
					auto* Node=Property->ContainerPtrToValuePtr<FAnimNode_RigidBody>(Candidate->GetAnimInstance());
					Test->TestEqual(TEXT("Actual evaluated physics uses the installed copy"),Node->GetPhysicsAsset(),Node->OverridePhysicsAsset.Get());
					const auto* Default = IConsoleManager::Get().FindConsoleVariable(TEXT("p.RigidBodyNode.DeferredSimulationDefault"));
					const auto* Force = IConsoleManager::Get().FindConsoleVariable(TEXT("p.RigidBodyNode.DeferredSimulationForceDefault"));
					const bool Deferred = !Default || !Force || ((Node->SimulationTiming==ESimulationTiming::Default || Force->GetInt()) ? Default->GetInt()!=0 : Node->SimulationTiming==ESimulationTiming::Deferred);
					Test->TestFalse(TEXT("This internal-state audit requires synchronous simulation"),Deferred);
					if (!Deferred && Test->TestNotNull(TEXT("Physics simulation actually exists"),Node->GetSimulation()))
					{
						Test->TestEqual(TEXT("Installed data creates its actual actor population"),Node->GetSimulation()->NumActors(),Node->OverridePhysicsAsset->SkeletalBodySetups.Num());
						ActualActors+=Node->GetSimulation()->NumActors();
						for (int32 I=0; I<Node->GetSimulation()->NumActors(); ++I)
						{
							const auto* Actor=Node->GetSimulation()->GetActorHandle(I);
							if (Actor && Actor->IsSimulated()) { ++DynamicActors; DynamicMotion+=Actor->GetLinearVelocity().SizeSquared(); }
						}
						Audit+=FString::Printf(TEXT("ActualPhysics Asset=%s Actors=%d\n"),*Node->GetPhysicsAsset()->GetPathName(),Node->GetSimulation()->NumActors());
					}
				}
		++Samples;
	}
public:
	explicit FSecondaryPhysicsEquivalence(FAutomationTestBase* InTest):Test(InTest) {}
	~FSecondaryPhysicsEquivalence() { Cleanup(); }
	virtual bool Update() override
	{
		if (!World && !Setup()) { Test->AddError(TEXT("Secondary physics fixture setup failed")); Cleanup(); return true; }
		if (FPlatformTime::Seconds()>Deadline) { Test->AddError(TEXT("Secondary physics fixture timed out")); Cleanup(); return true; }
		const uint32 RevisionA=Reference->GetBoneTransformRevisionNumber(), RevisionB=Candidate->GetBoneTransformRevisionNumber();
		AdvanceInput();
		const int32 ExpectedLOD=Frame<240 || Frame>=720 ? 0 : Frame<480 ? FMath::Min(1,MeshLODCount-1) : MeshLODCount-1;
		Candidate->SetForcedLOD(ExpectedLOD+1);
		for (auto* Component : Components) Component->SetLastRenderTime(World->GetTimeSeconds());
		World->Tick(LEVELTICK_All,1.f/60);
		if(MeshLODCount>1)
		{
			Test->TestEqual(TEXT("Candidate actually evaluates the requested mesh LOD"),Candidate->GetPredictedLODLevel(),ExpectedLOD);
			if(Frame==300 || Frame==600)
			{
				int32 EnabledDynamics=0;
				for(auto* Property:IAnimClassInterface::GetFromClass(Candidate->GetAnimClass())->GetAnimNodeProperties()) if(Property->Struct==FAnimNode_RigidBody::StaticStruct())
					if(auto* Simulation=Property->ContainerPtrToValuePtr<FAnimNode_RigidBody>(Candidate->GetAnimInstance())->GetSimulation())
						for(int32 I=0;I<Simulation->NumActors();++I)
						{ const auto* Actor=Simulation->GetActorHandle(I); EnabledDynamics+=Actor && Actor->IsSimulated() && Actor->GetEnabled(); }
				Test->TestEqual(TEXT("Lower mesh LOD does not silently disable secondary dynamics"),EnabledDynamics,30);
				Audit+=FString::Printf(TEXT("ActualMeshLOD Frame=%d LOD=%d EnabledDynamics=%d\n"),Frame,Candidate->GetPredictedLODLevel(),EnabledDynamics);
			}
		}
		if (Frame>0 && (!Test->TestTrue(TEXT("Both paired poses are newly finalized"),Reference->GetBoneTransformRevisionNumber()!=RevisionA && Candidate->GetBoneTransformRevisionNumber()!=RevisionB))) { Cleanup(); return true; }
		Compare();
		if (++Frame<900) return false;
		Test->TestEqual(TEXT("Nine hundred paired pose samples"),Samples,900);
		Test->TestEqual(TEXT("All authored simulation actors participate"),ActualActors,91);
		Test->TestEqual(TEXT("All authored dynamic actors participate"),DynamicActors,30);
		Test->TestTrue(TEXT("Dynamic physics actually produces motion"),DynamicMotion>UE_DOUBLE_SMALL_NUMBER);
		Test->TestTrue(TEXT("Physics preserves positions within 0.05cm"),MaxPosition<.05);
		Test->TestTrue(TEXT("Physics preserves rotations within 0.05degrees"),MaxAngle<.05);
		Test->TestTrue(TEXT("Physics preserves scales"),MaxScale<.000001);
		Test->TestTrue(TEXT("The retarget base inputs are identical"),MaxBasePosition<.000001);
		Audit+=FString::Printf(TEXT("Equivalence Samples=%d PositionCm=%.9f AngleDeg=%.9f Scale=%.9f RetargetBaseCm=%.9f Actors=%d Dynamic=%d Motion=%.9f\n"),Samples,MaxPosition,MaxAngle,MaxScale,MaxBasePosition,ActualActors,DynamicActors,DynamicMotion);
		const FString Path=FPaths::ProjectSavedDir()/TEXT("Validation/SecondaryPhysics/Equivalence.txt");
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
		Test->TestTrue(TEXT("Equivalence audit saved"),FFileHelper::SaveStringToFile(Audit,*Path));
		Cleanup(); return true;
	}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSecondaryPhysicsEquivalence,"ProjectJ.Retarget.SecondaryPhysicsEquivalence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJSecondaryPhysicsEquivalence::RunTest(const FString&)
{ ADD_LATENT_AUTOMATION_COMMAND(FSecondaryPhysicsEquivalence(this)); return true; }
#endif
