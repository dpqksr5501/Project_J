#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Project_JPlayerCharacter.h"
#include "Animation/Project_JPlayerAnimInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimBlueprint.h"
#include "Components/Project_JModularMeshComponent.h"
#include "Components/Project_JAnimationUpdateCoordinatorComponent.h"
#include "Components/Project_JPlayerInputBindingComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_CallFunction.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectHash.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPlayerMasterHierarchy,
	"ProjectJ.PlayerLocomotion.PlayerMasterHierarchy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPlayerMasterHierarchy::RunTest(const FString&)
{
	auto* Master = LoadObject<UBlueprint>(nullptr, TEXT("/Game/Character_BPs/BP_Player_Master.BP_Player_Master"));
	auto* Child = LoadObject<UBlueprint>(nullptr, TEXT("/Game/Character_BPs/GreatSword/BP_GreatSword.BP_Greatsword"));
	auto* Anim = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Animation_Logic/ABPs/ABP_Player_Master.ABP_Player_Master"));
	if (!TestNotNull(TEXT("Shared player authoring base"), Master) ||
		!TestNotNull(TEXT("Existing production child identity"), Child) ||
		!TestNotNull(TEXT("Shared animation master"), Anim)) return false;
	TestEqual(TEXT("Master retains the player native contract"), Master->ParentClass.Get(), AProject_JPlayerCharacter::StaticClass());
	TestEqual(TEXT("Greatsword inherits the shared BP"), Child->ParentClass.Get(), Master->GeneratedClass.Get());
	for (auto* BP : {Master, Child})
	{
		TestTrue(TEXT("Saved player BP compiled cleanly"), BP->Status == BS_UpToDate);
		TestEqual(TEXT("No parallel BP state variables"), BP->NewVariables.Num(), 0);
		TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
		for (const auto* Graph : Graphs)
			for (const UEdGraphNode* Node : Graph->Nodes)
				TestFalse(TEXT("Modular binding has one native lifecycle owner"), Node->IsA<UK2Node_CallFunction>());
		const auto* Defaults = CastChecked<AProject_JPlayerCharacter>(BP->GeneratedClass->GetDefaultObject());
		ForEachObjectWithOuter(Defaults, [this](UObject* Object)
		{
			if (Object->HasAnyFlags(RF_ArchetypeObject))
				TestTrue(TEXT("Inherited inline archetypes can be referenced by child packages"), Object->HasAnyFlags(RF_Public));
		}, EGetObjectsFlags::IncludeNestedObjects);
		TestEqual(TEXT("One common locomotion graph"), Defaults->GetMesh()->GetAnimClass(), Anim->GeneratedClass.Get());
		TestNotNull(TEXT("Existing animation profile provides movement policy"), Defaults->GetLocomotionProfile());
		TestNull(TEXT("No avatar-local attribute object inherited from test BP"), Defaults->GetAttributeSet());
		const auto* Abilities = FindFProperty<FArrayProperty>(BP->GeneratedClass, TEXT("DefaultAbilities"));
		if (TestNotNull(TEXT("Ability authoring contract"), Abilities))
			TestEqual(TEXT("PlayerState/class owns ability grants"), FScriptArrayHelper(Abilities, Abilities->ContainerPtrToValuePtr<void>(Defaults)).Num(), 0);
	}
	TestTrue(TEXT("Master owns typed native animation calculations"), Anim->ParentClass->IsChildOf(UProject_JPlayerAnimInstance::StaticClass()));
	TestEqual(TEXT("Master animation has no duplicate BP-owned state"), Anim->NewVariables.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPlayerMasterAssembly,
	"ProjectJ.PlayerLocomotion.PlayerMasterAssembly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPlayerMasterAssembly::RunTest(const FString&)
{
	auto* Class = LoadClass<AProject_JPlayerCharacter>(nullptr,
		TEXT("/Game/Character_BPs/GreatSword/BP_GreatSword.BP_Greatsword_C"));
	if (!TestNotNull(TEXT("Production child class"), Class)) return false;
	auto* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	auto* Avatar = World->SpawnActor<AProject_JPlayerCharacter>(Class);
	if (TestNotNull(TEXT("Inherited avatar constructs"), Avatar))
	{
		TArray<UProject_JModularMeshComponent*> Parts; Avatar->GetComponents(Parts);
		TestEqual(TEXT("All eight authored clothing parts retained"), Parts.Num(), 8);
		for (const auto* Part : Parts)
		{
			TestTrue(TEXT("Clothing uses native authored binding"), Part->bFollowAttachParentPose);
			TestNotNull(TEXT("Clothing retains its mesh"), Part->GetSkeletalMeshAsset());
			TestEqual(TEXT("Clothing follows visual body, not hidden Manny source"),
				static_cast<USceneComponent*>(Part->LeaderPoseComponent.Get()), Part->GetAttachParent());
			TestTrue(TEXT("Visual body and simulation source stay separate"), Part->LeaderPoseComponent.Get() != Avatar->GetMesh());
			TestEqual(TEXT("Only independent cloth retains its component tick"), Part->IsComponentTickEnabled(),
				Part->GetSkeletalMeshAsset()->HasActiveClothingAssets());
			TestNull(TEXT("Shared-pose clothing has no duplicate animation instance"), Part->GetAnimInstance());
		}
		TArray<UProject_JPlayerInputBindingComponent*> Inputs; Avatar->GetComponents(Inputs);
		TArray<UProject_JAnimationUpdateCoordinatorComponent*> Budgets; Avatar->GetComponents(Budgets);
		TestEqual(TEXT("Inheritance does not duplicate input binding"), Inputs.Num(), 1);
		TestEqual(TEXT("Existing animation budget coordinator remains the sole owner"), Budgets.Num(), 1);
	}
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}
#endif
