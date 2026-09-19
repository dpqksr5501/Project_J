#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/Project_JInternalTestReceiver.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "System/Project_JCharacterAnimationBudgetSubsystem.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Components/Project_JAnimationUpdateCoordinatorComponent.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Components/Project_JCombatIntroComponent.h"
#include "UI/Project_JCharacterUIBindingComponent.h"
#include "UI/Project_JCharacterViewModel.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Project_JGameplayTags.h"
#include "Project_JNPCCharacter.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JGreatswordCharacter.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include <limits>

namespace
{
struct FComponentOwnershipWorld
{
	UWorld* World;
	explicit FComponentOwnershipWorld(EWorldType::Type Type = EWorldType::Game)
	{
		World = UWorld::CreateWorld(Type, false);
		GEngine->CreateNewWorldContext(Type).SetCurrentWorld(World);
	}
	void BeginPlay()
	{
		World->InitializeActorsForPlay(FURL());
		World->GetWorldSettings()->NotifyBeginPlay();
	}
	~FComponentOwnershipWorld()
	{
		if (World->GetBegunPlay()) { World->EndPlay(EEndPlayReason::LevelTransition); }
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
};
constexpr auto ComponentFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAnimationDemandCompositionTest, "ProjectJ.Components.AnimationDemandComposition", ComponentFlags)
bool FProjectJAnimationDemandCompositionTest::RunTest(const FString&)
{
	FComponentOwnershipWorld Scope;
	auto* Owner = Scope.World->SpawnActor<AProject_JNPCCharacter>();
	Owner->SwapRoles();
	auto* Mesh = CastChecked<UProject_JBudgetedSkeletalMeshComponent>(Owner->GetMesh());
	auto* MeshAsset = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"));
	if (!TestNotNull(TEXT("Budget fixture mesh"), MeshAsset)) { return false; }
	Mesh->SetSkeletalMesh(MeshAsset);
	Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass());
	Scope.BeginPlay();
	auto* Budget = Scope.World->GetSubsystem<UProject_JCharacterAnimationBudgetSubsystem>();
	if (!TestNotNull(TEXT("World budget service"), Budget)) { return false; }
	Budget->SetEnabledOverride(true);
	TestTrue(TEXT("Fixture begins under actual ABA ownership"), Mesh->IsManagedByBudget());
	auto* Urgent = NewObject<UProject_JAnimationUpdateCoordinatorComponent>(Owner); Urgent->RegisterComponent();
	auto* Hit = NewObject<UProject_JCombatHitValidationComponent>(Owner); Hit->RegisterComponent();
	auto* SecondHit = NewObject<UProject_JCombatHitValidationComponent>(Owner); SecondHit->RegisterComponent();
	auto* Attack = NewObject<UProject_JAttackDefinition>(Owner);
	const auto Tag = FProject_JGameplayTags::Get().InputTag_Weapon_LMB;
	const auto Reset = [&]
	{
		Mesh->bEnableUpdateRateOptimizations = true;
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
		Mesh->bSuppressNotifyEventDispatch = true;
	};
	Reset();
	// Restore the authored baseline through ABA before exercising request overlap.
	Budget->SetEnabledOverride(false);
	Reset();
	Budget->SetEnabledOverride(true);
	Urgent->RequestUrgentRemoteAnimationUpdate(0.5f);
	TestFalse(TEXT("Urgent request immediately exits ABA"), Mesh->IsManagedByBudget());
	TestFalse(TEXT("Urgent presentation prevents ABA ownership too"), Mesh->CanUseBudget());
	Hit->BeginAttackNode(Tag, Attack);
	SecondHit->BeginAttackNode(Tag, Attack);
	Urgent->DestroyComponent();
	TestFalse(TEXT("Urgent teardown cannot release attack URO demand"), Mesh->bEnableUpdateRateOptimizations);
	Hit->EndAttack();
	TestTrue(TEXT("Another attack still protects pose"), Mesh->IsCombatCritical());
	SecondHit->EndAttack();
	TestTrue(TEXT("Last caller restores original URO"), Mesh->bEnableUpdateRateOptimizations);
	TestTrue(TEXT("Last gameplay caller restores notify policy"), Mesh->bSuppressNotifyEventDispatch);
	TestTrue(TEXT("Last gameplay caller restores visibility"), Mesh->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered);
	Budget->Refresh();
	TestTrue(TEXT("Last release permits ABA reentry"), Mesh->IsManagedByBudget());
	Budget->SetEnabledOverride(false);

	Reset();
	Hit->BeginAttackNode(Tag, Attack);
	Urgent = NewObject<UProject_JAnimationUpdateCoordinatorComponent>(Owner); Urgent->RegisterComponent();
	Urgent->RequestUrgentRemoteAnimationUpdate(0.5f);
	Hit->EndAttack();
	TestFalse(TEXT("Attack end cannot release urgent URO demand"), Mesh->bEnableUpdateRateOptimizations);
	TestFalse(TEXT("Urgent window alone is not a gameplay pose"), Mesh->IsCombatCritical());
	TestTrue(TEXT("Pose-only settings restore while urgency remains"), Mesh->bSuppressNotifyEventDispatch);
	Urgent->DestroyComponent();
	TestTrue(TEXT("Reverse release order also restores the baseline"), Mesh->bEnableUpdateRateOptimizations);
	Mesh->bEnableUpdateRateOptimizations = false;
	Hit->BeginAttackNode(Tag, Attack); Hit->DestroyComponent();
	TestFalse(TEXT("Originally disabled URO remains disabled"), Mesh->bEnableUpdateRateOptimizations);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJUIDemandTest, "ProjectJ.Components.UIDemand", ComponentFlags)
bool FProjectJUIDemandTest::RunTest(const FString&)
{
	FComponentOwnershipWorld Scope;
	auto* Owner = Scope.World->SpawnActor<ACharacter>();
	auto* Binding = NewObject<UProject_JCharacterUIBindingComponent>(Owner); Binding->RegisterComponent();
	auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Owner); ASC->RegisterComponent();
	auto* Attributes = NewObject<UProject_JAttributeSet>(Owner); ASC->AddAttributeSetSubobject(Attributes);
	Attributes->InitHealth(75); Attributes->InitMaxHealth(100);
	Binding->InitializeFromAttributes(ASC, Attributes, 3);
	TestNull(TEXT("An unobserved remote has no ViewModel"), Binding->PeekCharacterViewModel());
	auto* PanelA = NewObject<UProject_JInternalTestReceiver>(Owner);
	auto* PanelB = NewObject<UProject_JInternalTestReceiver>(Owner);
	auto* View = Binding->AcquireCharacterViewModel(PanelA);
	if (!TestNotNull(TEXT("A panel requests presentation"), View)) { return false; }
	TestEqual(TEXT("First bind captures current attributes"), View->GetHealth(), 75.0f);
	TestEqual(TEXT("Consumers share a ViewModel"), Binding->AcquireCharacterViewModel(PanelB), View);
	Binding->ReleaseCharacterViewModel(PanelA);
	ASC->SetNumericAttributeBase(Attributes->GetHealthAttribute(), 42);
	TestEqual(TEXT("Remaining panel receives real GAS changes"), View->GetHealth(), 42.0f);
	Binding->InitializeFromAttributes(ASC, Attributes, 8);
	TestEqual(TEXT("Same source preserves ViewModel identity"), Binding->PeekCharacterViewModel(), View);
	TestEqual(TEXT("Level refresh updates existing binding"), View->GetLevel(), 8);
	Binding->InitializeFromAttributes(nullptr, nullptr, 8);
	TestEqual(TEXT("Source loss clears stale health"), View->GetHealth(), 0.0f);
	Binding->InitializeFromAttributes(ASC, Attributes, 8);
	Binding->ReleaseCharacterViewModel(PanelB);
	TestNull(TEXT("Last remote panel releases presentation"), Binding->PeekCharacterViewModel());
	Binding->UpdateCharacterLevel(9);
	TestNull(TEXT("Progression alone does not allocate presentation"), Binding->PeekCharacterViewModel());
	View = Binding->GetCharacterViewModel();
	TestEqual(TEXT("Legacy getter remains compatible"), View->GetLevel(), 9);
	Binding->ReleaseLegacyViewModelRequest();
	Scope.BeginPlay();
	auto* Controller = Scope.World->SpawnActor<APlayerController>();
	Controller->SetAsLocalPlayerController(); Controller->Possess(Owner);
	TestNotNull(TEXT("Local HUD activates automatically"), Binding->PeekCharacterViewModel());
	Binding->GetCharacterViewModel(); // A normal HUD getter must not pin the old avatar.
	Controller->UnPossess();
	TestNull(TEXT("Ownership loss releases automatic HUD demand"), Binding->PeekCharacterViewModel());
	Binding->AcquireCharacterViewModel(PanelA); Binding->DestroyComponent();
	TestNull(TEXT("Teardown releases presentation"), Binding->PeekCharacterViewModel());
	TestNull(TEXT("Late consumer cannot revive a destroyed binding"), Binding->AcquireCharacterViewModel(PanelA));
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJUIDedicatedServerTest, "ProjectJ.Components.UIDedicatedServer", ComponentFlags)
bool FProjectJUIDedicatedServerTest::RunTest(const FString&)
{
	FComponentOwnershipWorld Scope(EWorldType::PIE);
	Scope.World->SetPlayInEditorInitialNetMode(NM_DedicatedServer);
	auto* Owner = Scope.World->SpawnActor<ACharacter>();
	auto* Binding = NewObject<UProject_JCharacterUIBindingComponent>(Owner); Binding->RegisterComponent();
	TestEqual(TEXT("Fixture uses dedicated server mode"), Owner->GetNetMode(), NM_DedicatedServer);
	Binding->InitializeFromAttributes(nullptr, nullptr, 1);
	TestNull(TEXT("Legacy query does not allocate on a dedicated server"), Binding->GetCharacterViewModel());
	TestNull(TEXT("Explicit demand does not allocate on a dedicated server"), Binding->AcquireCharacterViewModel(Owner));
	return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCombatTransitionOwnershipTest, "ProjectJ.Components.CombatTransitionOwnership", ComponentFlags)
bool FProjectJCombatTransitionOwnershipTest::RunTest(const FString&)
{
	FComponentOwnershipWorld Scope;
	auto* Owner = Scope.World->SpawnActor<ACharacter>();
	auto* Transition = NewObject<UProject_JCombatIntroComponent>(Owner); Transition->RegisterComponent();
	auto* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"));
	auto* Montage = LoadObject<UAnimMontage>(nullptr, TEXT("/Game/Anim_Assets/Great_Sword/Animations/Sword/Montage/AM_Greatsword_LMB1.AM_Greatsword_LMB1"));
	if (!TestNotNull(TEXT("Existing mesh fixture"), Mesh) || !TestNotNull(TEXT("Existing montage fixture"), Montage)) { return false; }
	Owner->GetMesh()->SetSkeletalMesh(Mesh); Owner->GetMesh()->SetAnimInstanceClass(UAnimInstance::StaticClass());
	TestTrue(TEXT("Real montage starts a draw transition"), Transition->PlayIntro(*Owner, Montage, 1));
	const uint64 OldRevision = Transition->TransitionRevision;
	TestFalse(TEXT("Draw and sheathe cannot overlap"), Transition->PlayOutro(*Owner, Montage, 1));
	Transition->CancelIntro(*Owner, Montage);
	TestFalse(TEXT("Cancel clears pending combat before callbacks"), Transition->IsPendingCombatMode());
	TestTrue(TEXT("Sheathe can start after cancellation"), Transition->PlayOutro(*Owner, Montage, 1));
	Transition->HandleMontageEnded(Montage, false, OldRevision);
	TestTrue(TEXT("Stale callback for the same asset cannot end the newer sheathe"), Transition->IsPlayingOutro());
	Transition->HandleMontageEnded(Montage, false, Transition->TransitionRevision);
	TestFalse(TEXT("Current completion ends the sheathe"), Transition->IsPlayingOutro());
	TestTrue(TEXT("Draw can restart"), Transition->PlayIntro(*Owner, Montage, 1));
	Transition->HandleMontageEnded(Montage, false, Transition->TransitionRevision);
	TestFalse(TEXT("Unconsumed pending activation cannot leak beyond completion"), Transition->IsPendingCombatMode());
	TestTrue(TEXT("Another draw starts before component teardown"), Transition->PlayIntro(*Owner, Montage, 1));
	Transition->DestroyComponent();
	TestFalse(TEXT("Teardown clears state"), Transition->IsPlayingIntro() || Transition->IsPendingCombatMode());
	TestFalse(TEXT("Teardown prevents new playback"), Transition->PlayIntro(*Owner, Montage, 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJLocomotionCadenceTest, "ProjectJ.Components.LocomotionCadence", ComponentFlags)
bool FProjectJLocomotionCadenceTest::RunTest(const FString&)
{
	FComponentOwnershipWorld Scope;
	auto* Owner = Scope.World->SpawnActor<AProject_JGreatswordCharacter>();
	if (!TestNotNull(TEXT("Concrete player fixture"), Owner)) { return false; }
	Owner->SwapRoles();
	auto* State = Owner->FindComponentByClass<UProject_JLocomotionAnimStateComponent>();
	State->RefreshCachedReferences();
	State->HiddenRemoteUpdateInterval = 0.1f;
	State->bUseInputDerivedRequestsForRemotePlayers = false;
	// Run after the initial render timestamp tolerance, without ticking the actor.
	Scope.World->Tick(LEVELTICK_All, 1.0f);
	float Delta = 0.04f;
	TestTrue(TEXT("Hidden sample waits for its cadence"), State->ShouldSkipUpdateForCurrentContext(Delta));
	Delta = 0.04f;
	TestTrue(TEXT("Second frame also waits"), State->ShouldSkipUpdateForCurrentContext(Delta));
	Delta = 0.04f;
	TestFalse(TEXT("Third frame samples"), State->ShouldSkipUpdateForCurrentContext(Delta));
	TestTrue(TEXT("Sample receives all elapsed time"), FMath::IsNearlyEqual(Delta, 0.12f));
	Delta = 0.04f; State->ShouldSkipUpdateForCurrentContext(Delta);
	State->HiddenRemoteUpdateInterval = 0;
	Delta = 0.02f; State->ShouldSkipUpdateForCurrentContext(Delta);
	TestTrue(TEXT("Disabling throttling flushes the skipped time"), FMath::IsNearlyEqual(Delta, 0.06f));
	State->HiddenRemoteUpdateInterval = 1;
	State->bIsJumping = true; State->JumpStartElapsedTime = 0;
	State->UpdateState(0.02f);
	TestTrue(TEXT("Semantic jump clock advances even on a skipped sample"), FMath::IsNearlyEqual(State->JumpStartElapsedTime, 0.02f));
	State->UpdateState(std::numeric_limits<float>::quiet_NaN());
	TestTrue(TEXT("Invalid deltas cannot poison clocks"), FMath::IsFinite(State->JumpStartElapsedTime));
	return true;
}
#endif
