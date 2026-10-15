#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Testing/Project_JAuthoredAnimationFixture.h"
#include "UObject/Package.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/Project_JRetargetBodyProfile.h"
#include "Animation/AnimClassInterface.h"
#include "AnimNodes/AnimNode_RetargetPoseFromMesh.h"
#include "BoneControllers/AnimNode_RigidBody.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
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
		for (auto* Property : Interface->GetAnimNodeProperties())
		{
			if (Property->Struct == FAnimNode_RetargetPoseFromMesh::StaticStruct()) { Node = Property->ContainerPtrToValuePtr<FAnimNode_RetargetPoseFromMesh>(Anim); ++Retargets; }
			if (Property->Struct == FAnimNode_RigidBody::StaticStruct())
			{
				++PhysicsNodes;
				FString Value; Property->ExportText_InContainer(0, Value, Anim, nullptr, Anim, PPF_None);
				Audit += TEXT("Physics=") + Value + TEXT("\n");
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
	explicit FRetargetBodyRuntime(FAutomationTestBase* InTest) : Test(InTest) {}
	~FRetargetBodyRuntime() { Cleanup(); }
	virtual bool Update() override
	{
		if (!World && !Setup()) { Test->AddError(TEXT("Production retarget fixture setup failed")); Cleanup(); return true; }
		if (FPlatformTime::Seconds() > Deadline) { Test->AddError(TEXT("Retarget runtime fixture timeout")); Cleanup(); return true; }
		// One World tick per engine frame: synchronous ticks share traversal counters.
		const auto Previous = Anim->GetUpdateCounter().Get();
		for (auto* Component : Components) Component->SetLastRenderTime(World->GetTimeSeconds());
		World->Tick(LEVELTICK_All, 1.f/60);
		if (Anim->GetUpdateCounter().Get() == Previous)
		{
			if (++WaitFrames > 30) { Test->AddError(TEXT("No actual visual graph updates within bounded budget wait")); Cleanup(); return true; }
			return false;
		}
		WaitFrames = 0;
		switch (Stage)
		{
		case 0:
			if (++Warmup < 12) return false;
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
