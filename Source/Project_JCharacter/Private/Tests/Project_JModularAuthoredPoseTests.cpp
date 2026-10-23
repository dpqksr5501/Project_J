#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Components/Project_JModularMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Animation/Project_JPresentationMeshResolver.h"
#include "UObject/StrongObjectPtr.h"
#include "Tests/Project_JClothPolicyFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJModularAuthoredPose,
	"ProjectJ.PlayerLocomotion.ModularAuthoredPoseLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJModularAuthoredPose::RunTest(const FString&)
{
	auto* Mesh = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	if (!TestNotNull(TEXT("Shared humanoid fixture"), Mesh)) return false;
	auto* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	auto* Owner = World->SpawnActor<AActor>();
	auto MakeSource = [&]()
	{
		auto* Source = NewObject<USkeletalMeshComponent>(Owner);
		Owner->AddInstanceComponent(Source);
		Source->SetSkeletalMeshAsset(Mesh);
		Source->RegisterComponent();
		return Source;
	};
	auto* First = MakeSource();
	auto* Second = MakeSource();
	Owner->SetRootComponent(First);
	auto* Part = NewObject<UProject_JModularMeshComponent>(Owner);
	Owner->AddInstanceComponent(Part);
	Part->bFollowAttachParentPose = true;
	Part->SetSkeletalMeshAsset(Mesh);
	Part->SetupAttachment(First);
	Part->RegisterComponent();
	TestTrue(TEXT("Authored registration binds the skeletal parent"), Part->LeaderPoseComponent.Get() == First);
	TestFalse(TEXT("Shared parts do not evaluate another graph"), Part->IsComponentTickEnabled());
	Part->Deactivate();
	Part->Activate(true);
	TestFalse(TEXT("Actor auto-activation cannot re-enable shared-pose ticking"), Part->IsComponentTickEnabled());
	Part->AttachToComponent(Second, FAttachmentTransformRules::KeepRelativeTransform);
	TestTrue(TEXT("Changing parent replaces the pose owner"), Part->LeaderPoseComponent.Get() == Second);
	Part->UnregisterComponent();
	TestNull(TEXT("Unregistration releases the authored source"), Part->LeaderPoseComponent.Get());
	TestFalse(TEXT("Releasing the leader after unregister must not recreate orphaned physics"), Part->IsPhysicsStateCreated());
	Part->RegisterComponent();
	TestTrue(TEXT("Re-registration restores the current source"), Part->LeaderPoseComponent.Get() == Second);
	Part->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
	TestNull(TEXT("Detachment cannot retain a stale source"), Part->LeaderPoseComponent.Get());
	Part->DestroyComponent();
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJModularClothTick,
	"ProjectJ.PlayerLocomotion.ModularClothTickLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJModularClothTick::RunTest(const FString&)
{
	auto* Body = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/GreatSword/GreatSword_Woman.GreatSword_Woman"));
	if (!TestNotNull(TEXT("Body fixture"), Body)) return false;
	// Policy fixture only: exercise admission before streamed simulation resources
	// exist, without depending on retired production cloth or Saved backups.
	TStrongObjectPtr<USkeletalMesh> ClothFixture(MakeProjectJClothPolicyFixture(Body));
	auto* Cloak = ClothFixture.Get();
	if (!TestNotNull(TEXT("Private cloth admission metadata fixture"), Cloak)) return false;
	TestFalse(TEXT("Production body data remains unchanged"), Body->HasActiveClothingAssets());
	auto* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	auto* Owner = World->SpawnActor<AActor>();
	auto* Source = NewObject<USkeletalMeshComponent>(Owner);
	Owner->SetRootComponent(Source); Owner->AddInstanceComponent(Source);
	Source->SetSkeletalMeshAsset(Body); Source->RegisterComponent();
	auto* Part = NewObject<UProject_JModularMeshComponent>(Owner);
	Owner->AddInstanceComponent(Part); Part->SetSkeletalMeshAsset(Cloak);
	Part->AttachAndSetLeader(Source); Part->RegisterComponent();
	TestTrue(TEXT("Active section metadata is admitted before streamed simulation resources exist"), Project_J::Animation::UsesIndependentClothSimulation(*Part));
	TestTrue(TEXT("Shared bones retain independent cloth ticking"), Part->IsComponentTickEnabled());
	TestNull(TEXT("No second graph for bone-shared cloth"), Part->GetAnimInstance());
	Part->AttachAndSetLeader(Source);
	TestTrue(TEXT("Idempotent binding retains source and cloth admission"),
		Part->LeaderPoseComponent.Get() == Source && Part->IsComponentTickEnabled());
	Part->Deactivate(); Part->Activate(true);
	TestTrue(TEXT("Activation preserves required cloth ticking"), Part->IsComponentTickEnabled());
	Part->UnregisterComponent();
	TestFalse(TEXT("Cloth unregister cannot recreate rigid physics after the engine destroyed it"), Part->IsPhysicsStateCreated());
	Part->RegisterComponent();
	TestTrue(TEXT("Re-registration restores cloth policy"), Part->IsComponentTickEnabled());
	Part->SetSkeletalMeshAsset(Body);
	TestFalse(TEXT("Changing to a non-cloth mesh removes the extra tick"), Part->IsComponentTickEnabled());
	Part->SetSkeletalMeshAsset(Cloak);
	TestTrue(TEXT("Changing back to cloth restores its simulation tick"), Part->IsComponentTickEnabled());
	Part->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
	TestFalse(TEXT("Detached automatic part cannot keep simulating a stale source"), Part->IsComponentTickEnabled());
	Part->DestroyComponent(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
	return true;
}
#endif
