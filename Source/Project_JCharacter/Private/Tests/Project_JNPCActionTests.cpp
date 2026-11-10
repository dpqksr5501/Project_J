#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Components/Project_JNPCActionComponent.h"
#include "Components/Project_JNPCActivationComponent.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "System/Project_JNPCPathSubsystem.h"
#include "System/Project_JNPCDecisionSubsystem.h"
#include "Project_JNPCCharacter.h"
#include "Project_JAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeManager.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BehaviorTree/Tasks/BTTask_Wait.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Project_JCombatInterface.h"
#include "Combat/Project_JGameplayAbility_NPCAttack.h"
#include "Combat/Project_JAttackDefinition.h"
#include "NativeGameplayTags.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "System/Project_JCharacterAnimationBudgetSubsystem.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Tests/Project_JMoverFrameFixture.h"
#include "Engine/SkeletalMesh.h"
#include "GameplayEffect.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Animation/Project_JAnimNotifyState_MeleeHit.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "UObject/GarbageCollection.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(NPCTestHitTag, "ProjectJ.Tests.NPC.Hit");

namespace
{
	struct FActionFixture
	{
		UWorld* World;
		AProject_JNPCCharacter* NPC;
		AAIController* AI;
		UProject_JTargetScoringComponent* Scoring;
		UProject_JNPCActionComponent* Actions;
		UProject_JNPCDecisionSubsystem* Decisions;
		UProject_JNPCPathSubsystem* Paths;
		FActionFixture()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			FActorSpawnParameters P;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			NPC = World->SpawnActor<AProject_JNPCCharacter>(AProject_JNPCCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, P);
			NPC->GetAttributeSet()->InitMaxHealth(100); NPC->GetAttributeSet()->InitHealth(100);
			NPC->GetAbilitySystemComponent()->InitAbilityActorInfo(NPC, NPC);
			AI = World->SpawnActor<AAIController>(); AI->Possess(NPC);
			Scoring = NewObject<UProject_JTargetScoringComponent>(NPC);
			NPC->AddInstanceComponent(Scoring); Scoring->RegisterComponent();
			Actions = NewObject<UProject_JNPCActionComponent>(NPC);
			NPC->AddInstanceComponent(Actions); Actions->RegisterComponent();
			Decisions = World->GetSubsystem<UProject_JNPCDecisionSubsystem>();
			Paths = World->GetSubsystem<UProject_JNPCPathSubsystem>();
			// AActor::ProcessEvent requires initialized actors even for native interface events.
			World->InitializeActorsForPlay(FURL());
			World->GetWorldSettings()->NotifyBeginPlay();
			World->GetWorldSettings()->NotifyMatchStarted();
			NPC->GetAttributeSet()->InitMaxHealth(100); NPC->GetAttributeSet()->InitHealth(100);
		}
		~FActionFixture()
		{
			if (World->GetBegunPlay()) { World->EndPlay(EEndPlayReason::LevelTransition); }
			World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
		}
		AActor* Target(double X, int32 Team = 2)
		{
			auto* Actor = World->SpawnActor<AActor>();
			auto* Root = NewObject<USceneComponent>(Actor);
			Actor->SetRootComponent(Root); Root->RegisterComponent(); Actor->SetActorLocation(FVector(X, 0, 0));
			Decisions->RegisterTarget(Actor, Team); return Actor;
		}
		void ActionTick() { Actions->UpdateAction(); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCActionLifecycleTest, "ProjectJ.NPCAction.IntentLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCActionLifecycleTest::RunTest(const FString& Parameters)
{
	FActionFixture F;
	TestFalse(TEXT("Disabled by default"), F.Actions->IsComponentTickEnabled());
	TestFalse(TEXT("Manual scoring cannot authorize actions"), F.Actions->StartActions(F.Scoring, {}));
	TestTrue(TEXT("Explicit registration"), F.Scoring->StartBatchedNPCDecisions(1));
	TestTrue(TEXT("Opt-in movement-only start"), F.Actions->StartActions(F.Scoring, {}));
	TestTrue(TEXT("Duplicate start is idempotent"), F.Actions->StartActions(F.Scoring, {}));
	auto* A = F.Target(700); auto* B = F.Target(1200);
	F.Scoring->OnQueryCompleted.Broadcast(A, 1);
	F.ActionTick();
	TestEqual(TEXT("First intent requests path"), F.Actions->GetActionState(), EProjectJNPCActionState::AwaitingPath);
	TestEqual(TEXT("One queued path"), F.Paths->GetRequestCount(), 1);
	F.Scoring->OnQueryCompleted.Broadcast(A, 1); F.ActionTick();
	TestEqual(TEXT("Same target does not duplicate path"), F.Paths->GetStats().Accepted, uint64(1));
	F.Scoring->OnQueryCompleted.Broadcast(B, 1);
	TestEqual(TEXT("Target switch cancels old queued path"), F.Paths->GetRequestCount(), 0);
	TestEqual(TEXT("New target published"), F.Actions->GetIntentTarget(), B);
	F.Scoring->InvalidateQueryContext();
	TestNull(TEXT("Equipment/skill invalidation clears action intent"), F.Actions->GetIntentTarget());
	F.Scoring->OnQueryCompleted.Broadcast(A, 1);
	F.Decisions->RegisterTarget(A, 1); F.ActionTick();
	TestNull(TEXT("Team changed after selection cannot be pursued"), F.Actions->GetIntentTarget());
	F.Decisions->RegisterTarget(A, 2); F.Scoring->OnQueryCompleted.Broadcast(A, 1);
	F.AI->UnPossess(); F.ActionTick();
	TestEqual(TEXT("Possession change stops action consumer"), F.Actions->GetActionState(), EProjectJNPCActionState::Disabled);
	TestEqual(TEXT("No remaining queued path after stop"), F.Paths->GetRequestCount(), 0);
	TestTrue(TEXT("Stopping consumer preserves scoring registration"), F.Scoring->IsBatchedNPCDecisionRegistered());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCActionContextTest, "ProjectJ.NPCAction.ContextSeparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCActionContextTest::RunTest(const FString& Parameters)
{
	FActionFixture F;
	F.Scoring->StartBatchedNPCDecisions(1);
	F.Target(700);
	int32 Invalidations = 0;
	const auto Handle = F.Scoring->OnContextInvalidated.AddLambda([&Invalidations]() { ++Invalidations; });
	F.Decisions->Tick(0);
	TestEqual(TEXT("Routine snapshot preparation preserves consumer intent"), Invalidations, 0);
	F.Scoring->InvalidateQueryContext();
	TestEqual(TEXT("Semantic context invalidation notifies consumers"), Invalidations, 1);
	F.Scoring->OnContextInvalidated.Remove(Handle);
	F.Scoring->StopBatchedNPCDecisions();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCActionRecipientTest, "ProjectJ.NPCAction.RecipientLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCActionRecipientTest::RunTest(const FString&)
{
	FActionFixture F;
	auto* ASC = CastChecked<UProject_JAbilitySystemComponent>(F.NPC->GetAbilitySystemComponent());
	TestTrue(TEXT("Native scoring registration"), F.Scoring->StartBatchedNPCDecisions(1));
	TestTrue(TEXT("Pursuit starts"), F.Actions->StartActions(F.Scoring, {}));
	F.Scoring->OnQueryCompleted.Broadcast(F.Target(700), 1); F.ActionTick();
	TestEqual(TEXT("Pursuit owns a real queued path"), F.Paths->GetRequestCount(), 1);
	TestTrue(TEXT("Metadata refresh preserves pursuit"), ASC->RefreshActorInfoForCurrentActor(F.NPC));
	TestTrue(TEXT("Metadata refresh keeps the same action lease"), F.Actions->GetOwnershipToken() != 0);
	ASC->InitAbilityActorInfo(F.NPC, F.NPC);
	TestEqual(TEXT("Same-pointer recipient replacement stops the action immediately"), F.Actions->GetActionState(), EProjectJNPCActionState::Disabled);
	TestEqual(TEXT("Retirement cancels the actual queued path before any later delivery"), F.Paths->GetRequestCount(), 0);
	TestEqual(TEXT("Retirement releases the shared action slot"), F.Decisions->GetActionCount(), 0);
	TestTrue(TEXT("A direct action cannot release another owner's scoring registration"), F.Scoring->IsBatchedNPCDecisionRegistered());
	TestTrue(TEXT("A fresh recipient may start a new action"), F.Actions->StartActions(F.Scoring, {}));
	FProjectJNPCActionSuspension Suspension;
	TestTrue(TEXT("A valid action can hand off its representation"), F.Actions->SuspendActions(Suspension));
	ASC->InitAbilityActorInfo(F.NPC, F.NPC);
	TestTrue(TEXT("The old representation ticket cannot restore an action in a new recipient epoch"),
		F.Actions->ResumeSuspendedActions(Suspension) == EProjectJNPCActionResume::Superseded);
	TestEqual(TEXT("A superseded ticket releases its dormant ownership token"), F.Actions->GetOwnershipToken(), uint64(0));
	TestFalse(TEXT("A superseded ticket schedules no retries"), F.Actions->HasPendingResume());
	TestTrue(TEXT("A fresh action can start after ticket retirement"), F.Actions->StartActions(F.Scoring, {}));
	F.Actions->StopActions();
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCFollowRecipientTest, "ProjectJ.NPCAction.FollowRecipientLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCFollowRecipientTest::RunTest(const FString&)
{
	FActionFixture F;
	auto* Owner = F.World->SpawnActor<AProject_JNPCCharacter>();
	auto* ASC = CastChecked<UProject_JAbilitySystemComponent>(Owner->GetAbilitySystemComponent());
	ASC->InitAbilityActorInfo(Owner, Owner);
	Owner->GetAttributeSet()->InitMaxHealth(100); Owner->GetAttributeSet()->InitHealth(100);
	int32 Stopped = 0;
	TestTrue(TEXT("Existing noncombat follow starts"), F.Actions->StartFollowing(Owner, ASC, 100, [&] { ++Stopped; }));
	const auto Token = F.Actions->GetOwnershipToken();
	TestTrue(TEXT("Follow owner metadata refresh succeeds"), ASC->RefreshActorInfoForCurrentActor(Owner));
	ASC->InitAbilityActorInfo(Owner, Owner);
	TestEqual(TEXT("Normal same-recipient reinitialization preserves follow ownership"), F.Actions->GetOwnershipToken(), Token);
	TestEqual(TEXT("Normal same-recipient reinitialization sends no stop notification"), Stopped, 0);
	ASC->ClearActorInfo();
	TestEqual(TEXT("Clearing the actual follow recipient immediately retires its consumer"), Stopped, 1);
	TestEqual(TEXT("Follow retirement releases its action slot"), F.Decisions->GetActionCount(), 0);
	ASC->InitAbilityActorInfo(Owner, Owner);
	TestEqual(TEXT("Restoring old pointers cannot revive the retired follow lease"), F.Actions->GetOwnershipToken(), uint64(0));
	TestEqual(TEXT("Old stop notification cannot run twice"), Stopped, 1);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCOutOfRangeTest, "ProjectJ.Crowd.OutOfRangeDuringPathCooldown",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCOutOfRangeTest::RunTest(const FString&)
{
	FActionFixture F; F.Scoring->StartBatchedNPCDecisions(1); F.Actions->StartActions(F.Scoring, {});
	auto* Target = F.Target(700); F.Scoring->OnQueryCompleted.Broadcast(Target, 1); F.ActionTick();
	Target->SetActorLocation(FVector(50, 0, 0)); F.ActionTick();
	TestEqual(TEXT("Approaching target reaches attack range"), F.Actions->GetActionState(), EProjectJNPCActionState::InRange);
	Target->SetActorLocation(FVector(700, 0, 0)); F.ActionTick();
	TestTrue(TEXT("Moving away clears arrival even before another path request is allowed"), F.Actions->GetActionState() != EProjectJNPCActionState::InRange);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCPathAdmissionTest, "ProjectJ.NPCAction.PathAdmission",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCPathAdmissionTest::RunTest(const FString& Parameters)
{
	FActionFixture F;
	int32 Callbacks = 0;
	TArray<TStrongObjectPtr<USceneComponent>> Owners;
	TArray<uint64> Tokens;
	for (int32 I = 0; I < UProject_JNPCPathSubsystem::MaxRequests; ++I)
	{
		Owners.Emplace(NewObject<USceneComponent>(F.NPC));
		Tokens.Add(F.Paths->Submit(Owners.Last().Get(), F.NPC, FVector(1000, 0, 0), 1,
			[&Callbacks](const auto&) { ++Callbacks; }));
		TestTrue(TEXT("Capacity entry accepted"), Tokens.Last() != 0);
	}
	TestEqual(TEXT("Overflow rejected"), F.Paths->Submit(F.Actions, F.NPC, FVector(1000, 0, 0), 1, [](const auto&) {}), uint64(0));
	for (const uint64 Token : Tokens) { F.Paths->Cancel(Token); F.Paths->Cancel(Token); }
	TestEqual(TEXT("Queued cancellation releases all slots"), F.Paths->GetRequestCount(), 0);
	TestEqual(TEXT("Cancellation has no callback"), Callbacks, 0);
	const auto Token = F.Paths->Submit(F.Actions, F.NPC, FVector(1000, 0, 0), 1, [&Callbacks](const auto&) { ++Callbacks; });
	TestTrue(TEXT("Capacity reusable"), Token != 0);
	TestEqual(TEXT("One outstanding request per owner"), F.Paths->Submit(F.Actions, F.NPC, FVector(1200, 0, 0), 2, [](const auto&) {}), uint64(0));
	F.AI->UnPossess(); F.Paths->Tick(0);
	TestEqual(TEXT("Possession loss cancels before dispatch"), F.Paths->GetRequestCount(), 0);
	TestEqual(TEXT("Possession cancellation has no callback"), Callbacks, 0);
	F.Paths->OnWorldEndPlay(*F.World);
	F.AI->Possess(F.NPC);
	TestEqual(TEXT("Stopped world rejects submissions"), F.Paths->Submit(F.Actions, F.NPC, FVector(1000, 0, 0), 1, [](const auto&) {}), uint64(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCPathLateCompletionTest, "ProjectJ.NPCAction.LatePathCompletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCPathLateCompletionTest::RunTest(const FString& Parameters)
{
	FActionFixture F;
	F.Paths->SetLatencyCapturePhaseForTest(17);
	int32 Callbacks = 0;
	const uint64 Cancelled = F.Paths->Submit(F.Actions, F.NPC, FVector(1000, 0, 0), 1, [&Callbacks](const auto&) { ++Callbacks; });
	F.Paths->SimulateDispatchForTest(Cancelled, false);
	F.Paths->Cancel(Cancelled);
	TestEqual(TEXT("Dispatched cancellation retains slot"), F.Paths->GetRequestCount(), 1);
	TestEqual(TEXT("Owner cannot replace an unfinished cancelled job"), F.Paths->Submit(F.Actions, F.NPC, FVector(1200, 0, 0), 2, [](const auto&) {}), uint64(0));
	F.Paths->Complete(Cancelled, 1, ENavigationQueryResult::Fail, nullptr); F.Paths->Tick(0);
	TestEqual(TEXT("Late cancellation completion has no callback"), Callbacks, 0);
	TestEqual(TEXT("Engine completion releases slot"), F.Paths->GetRequestCount(), 0);
	EProjectJNPCPathStatus DeliveredStatus = EProjectJNPCPathStatus::Success;
	const uint64 Expired = F.Paths->Submit(F.Actions, F.NPC, FVector(1000, 0, 0), 3,
		[&](const auto& C) { ++Callbacks; DeliveredStatus = C.Status; });
	F.Paths->SetLatencyCapturePhaseForTest(18); // A later stage must not relabel an older submission.
	F.Paths->SimulateDispatchForTest(Expired, true); F.Paths->Tick(0);
	TestEqual(TEXT("Deadline delivered once"), Callbacks, 1);
	TestEqual(TEXT("Deadline reports expiry"), DeliveredStatus, EProjectJNPCPathStatus::Expired);
	TestEqual(TEXT("Timeout retains physical in-flight slot"), F.Paths->GetRequestCount(), 1);
	F.Paths->Complete(Cancelled, 1, ENavigationQueryResult::Fail, nullptr);
	TestEqual(TEXT("Old token does not release new request"), F.Paths->GetRequestCount(), 1);
	F.Paths->Complete(Expired, 1, ENavigationQueryResult::Fail, nullptr); F.Paths->Tick(0);
	TestEqual(TEXT("Expired job cannot deliver again"), Callbacks, 1);
	TestEqual(TEXT("Late expired completion releases slot"), F.Paths->GetRequestCount(), 0);
	TArray<FProjectJNPCPathLatencySample> Samples;
	F.Paths->DrainLatencySamplesForTest(Samples);
	TestEqual(TEXT("Capture excludes cancelled and duplicate late results"), Samples.Num(), 1);
	if (Samples.Num() == 1)
	{
		TestEqual(TEXT("Capture keeps submission phase"), Samples[0].Phase, 17);
		TestEqual(TEXT("Capture keeps request token"), Samples[0].Token, Expired);
		TestEqual(TEXT("Capture keeps expiry outcome"), Samples[0].Status, EProjectJNPCPathStatus::Expired);
	}
	const uint64 Teardown = F.Paths->Submit(F.Actions, F.NPC, FVector(1000, 0, 0), 4, [&Callbacks](const auto&) { ++Callbacks; });
	F.Paths->SimulateDispatchForTest(Teardown, false); F.Paths->OnWorldEndPlay(*F.World);
	F.Paths->Complete(Teardown, 1, ENavigationQueryResult::Fail, nullptr);
	TestEqual(TEXT("World end prevents late delivery"), Callbacks, 1);
	TestEqual(TEXT("Late engine callback after stop is observable"), F.Paths->GetStats().IgnoredAfterStop, uint64(1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCAttackContractTest, "ProjectJ.Integrated.NPCAttackContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCAttackContractTest::RunTest(const FString& Parameters)
{
	FActionFixture F;
	auto* CDO = GetMutableDefault<UProject_JGameplayAbility_NPCAttack>();
	TestEqual(TEXT("NPC ability runs on the server"), CDO->GetNetExecutionPolicy(), EGameplayAbilityNetExecutionPolicy::ServerOnly);
	TestFalse(TEXT("Runtime configuration cannot modify an ability asset/CDO"), CDO->ConfigureHitEvent(NPCTestHitTag));
	auto* Definition = NewObject<UProject_JAttackDefinition>(F.NPC);
	auto* ASC = F.NPC->GetAbilitySystemComponent();
	const auto Handle = ASC->GiveAbility(FGameplayAbilitySpec(UProject_JGameplayAbility_NPCAttack::StaticClass(), 1, INDEX_NONE, Definition));
	auto* Spec = ASC->FindAbilitySpecFromHandle(Handle);
	auto* Ability = Spec ? Cast<UProject_JGameplayAbility_NPCAttack>(Spec->GetPrimaryInstance()) : nullptr;
	if (!TestNotNull(TEXT("Granted NPC ability has a private instance"), Ability)) { return false; }
	TestTrue(TEXT("Granted instance can configure the authored notify event"), Ability->ConfigureHitEvent(NPCTestHitTag));
	F.Scoring->StartBatchedNPCDecisions(1);
	TestTrue(TEXT("Action consumer accepts the native NPC attack spec"), F.Actions->StartActions(F.Scoring, Handle));
	auto* Target = F.Target(700);
	F.Scoring->OnQueryCompleted.Broadcast(Target, 1);
	TestFalse(TEXT("Search range does not authorize an out-of-range attack"), F.Actions->CanCommitToTarget(Target));
	Target->SetActorLocation(FVector(100, 0, 0));
	TestTrue(TEXT("Current target is attackable inside melee range"), F.Actions->CanCommitToTarget(Target));
	TestFalse(TEXT("Missing montage/hit data fails closed without an empty attack"), ASC->TryActivateAbility(Handle, false));
	TestFalse(TEXT("Rejected attack leaves no active spec"), Spec->IsActive());
	F.Actions->StopActions(); ASC->ClearAbility(Handle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCAttackMovementPolicyTest, "ProjectJ.NPCGameplay.GroundRootMotion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCAttackMovementPolicyTest::RunTest(const FString& Parameters)
{
	FActionFixture F;
	auto* MeshAsset = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"));
	auto* Montage = LoadObject<UAnimMontage>(nullptr, TEXT("/Game/Anim_Assets/Great_Sword/Animations/Sword/Montage/AM_Greatsword_LMB1.AM_Greatsword_LMB1"));
	if (!TestNotNull(TEXT("Existing NPC mesh fixture"), MeshAsset) || !TestNotNull(TEXT("Existing greatsword montage fixture"), Montage)) { return false; }
	auto* Mesh = F.NPC->GetMesh(); Mesh->SetSkeletalMesh(MeshAsset); Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass());
	if (!TestNotNull(TEXT("Native animation instance"), Mesh->GetAnimInstance())) { return false; }
	// This movement-policy fixture requires actual animation ticks in NullRHI.
	// ABA ownership/drain is covered by the dedicated runtime ownership test.
	F.World->GetSubsystem<UProject_JCharacterAnimationBudgetSubsystem>()->SetEnabledOverride(false);
	Mesh->SetComponentTickEnabled(true);
	auto* Hit = NewObject<UProject_JCombatHitValidationComponent>(F.NPC); F.NPC->AddInstanceComponent(Hit); Hit->RegisterComponent();
	auto* Definition = NewObject<UProject_JAttackDefinition>(F.NPC);
	Definition->AttackTag = NPCTestHitTag; Definition->Montage = Montage; Definition->DamageEffect = UGameplayEffect::StaticClass();
	Definition->MovementPolicy = EProject_JAttackMovementPolicy::RootMotionMontage;
	TestTrue(TEXT("Ground root-motion policy accepted"), UProject_JGameplayAbility_NPCAttack::SupportsMovementPolicy(*Definition));
	Definition->MovementPolicy = EProject_JAttackMovementPolicy::RootMotionWarped;
	TestFalse(TEXT("Warping still requires a separate contract"), UProject_JGameplayAbility_NPCAttack::SupportsMovementPolicy(*Definition));
	Definition->MovementPolicy = EProject_JAttackMovementPolicy::RootMotionMontage; Definition->bUseFlyingMovementModeForRootMotion = true;
	TestFalse(TEXT("Flying override remains rejected"), UProject_JGameplayAbility_NPCAttack::SupportsMovementPolicy(*Definition));
	Definition->bUseFlyingMovementModeForRootMotion = false;
	auto* ASC = F.NPC->GetAbilitySystemComponent();
	const auto Handle = ASC->GiveAbility(FGameplayAbilitySpec(UProject_JGameplayAbility_NPCAttack::StaticClass(), 1, INDEX_NONE, Definition));
	auto* Ability = CastChecked<UProject_JGameplayAbility_NPCAttack>(ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
	Ability->ConfigureHitEvent(NPCTestHitTag);
	F.Scoring->StartBatchedNPCDecisions(1); F.Actions->StartActions(F.Scoring, Handle);
	F.Scoring->OnQueryCompleted.Broadcast(F.Target(100), 1);
	const auto Visibility = Mesh->VisibilityBasedAnimTickOption;
	const bool bURO = Mesh->bEnableUpdateRateOptimizations;
	TestTrue(TEXT("Publish grounded attack fixture"),ProjectJ::Tests::PublishMoverFrame(F.NPC,MOVE_Walking));
	F.Actions->UpdateAction();
	TestTrue(TEXT("Real montage activates through the action consumer"), Ability->IsActive());
	TestEqual(TEXT("Movement remains walking; no flying override"), Project_J::Movement::ReadPhysicalFrame(F.NPC).Mode, MOVE_Walking);
	TestTrue(TEXT("Existing server hit validation starts"), Hit->GetActiveAttackDefinition() == Definition);
	F.NPC->GetAttributeSet()->InitHealth(0); F.Actions->UpdateAction();
	TestFalse(TEXT("Death stops the owned ability"), Ability->IsActive());
	TestNull(TEXT("Death closes the hit definition"), Hit->GetActiveAttackDefinition());
	TestFalse(TEXT("Death immediately stops the owned montage"), Mesh->GetAnimInstance()->Montage_IsPlaying(Montage));
	// A stopped instance must receive its final animation tick before the
	// montage's independent pose demand can restore the pre-attack policy.
	for (int32 Tick = 0; Tick < 20 && Mesh->GetAnimInstance()->IsAnyMontagePlaying(); ++Tick)
	{
		F.World->Tick(LEVELTICK_All, 0.05f);
	}
	TestFalse(TEXT("Death's stopped montage drains within one second"), Mesh->GetAnimInstance()->IsAnyMontagePlaying());
	TestEqual(TEXT("Animation visibility policy restored"), Mesh->VisibilityBasedAnimTickOption, Visibility);
	TestEqual(TEXT("URO restored"), Mesh->bEnableUpdateRateOptimizations != 0, bURO);
	F.NPC->GetAttributeSet()->InitHealth(100);
	// The transient fixture has no floor; reset ground state for the next case.
	TestTrue(TEXT("Publish reused grounded attack fixture"),ProjectJ::Tests::PublishMoverFrame(F.NPC,MOVE_Walking));
	F.Actions->StartActions(F.Scoring, Handle); F.Scoring->OnQueryCompleted.Broadcast(F.Target(100), 1);
	TestTrue(TEXT("Ability reusable after cancellation"), ASC->TryActivateAbility(Handle, false));
	TestTrue(TEXT("Publish completed falling frame"),ProjectJ::Tests::PublishMoverFrame(F.NPC,MOVE_Falling));
	TestFalse(TEXT("Leaving the ground cancels the ground attack"), Ability->IsActive());
	TestEqual(TEXT("Cleanup preserves the new falling mode"), Project_J::Movement::ReadPhysicalFrame(F.NPC).Mode, MOVE_Falling);
	TestNull(TEXT("Falling closes the hit definition"), Hit->GetActiveAttackDefinition());
	AddInfo(FString::Printf(TEXT("Existing montage root_motion=%d; activation, death, falling cancellation verified; no asset saved."), Montage->HasRootMotion()));
	F.Actions->StopActions(); ASC->ClearAbility(Handle);
	return true;
}

namespace
{
	class FActionBudgetCommand : public IAutomationLatentCommand
	{
	public:
		explicit FActionBudgetCommand(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds())
		{
			for (int32 I = 0; I < 96; ++I)
			{
				FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				auto* NPC = Fixture.World->SpawnActor<AProject_JNPCCharacter>(AProject_JNPCCharacter::StaticClass(), FVector(I * 200, 0, 0), FRotator::ZeroRotator, P);
				auto* AI = Fixture.World->SpawnActor<AAIController>(); AI->Possess(NPC);
				NPC->GetAbilitySystemComponent()->InitAbilityActorInfo(NPC, NPC);
				NPC->GetAttributeSet()->InitHealth(100);
				auto* Scoring = NewObject<UProject_JTargetScoringComponent>(NPC); NPC->AddInstanceComponent(Scoring); Scoring->RegisterComponent(); Scoring->StartBatchedNPCDecisions(1);
				auto* Action = NewObject<UProject_JNPCActionComponent>(NPC); NPC->AddInstanceComponent(Action); Action->RegisterComponent();
				Test->TestTrue(TEXT("Native action admitted"), Action->StartActions(Scoring, {}));
				Test->TestFalse(TEXT("No per-NPC action Tick"), Action->PrimaryComponentTick.bCanEverTick);
				Actions.Add(Action); ScoringSources.Add(Scoring);
				NPC->GetAttributeSet()->InitHealth(0); // All consumers must eventually observe invalidation and self-unregister.
				Test->TestTrue(TEXT("Death is observable through the actual combat interface"), IProject_JCombatInterface::Execute_IsDead(NPC));
			}
			Test->TestEqual(TEXT("Actual action registry contains all96 consumers"), Fixture.Decisions->GetActionCount(), 96);
			for (int32 Index = 0; Index < Actions.Num(); Index += 3) { Actions[Index]->StopActions(); }
			Test->TestEqual(TEXT("Middle/front/tail removals detach exactly32 consumers"), Fixture.Decisions->GetActionCount(), 64);
			for (int32 Index = 0; Index < Actions.Num(); Index += 3)
			{
				Actions[Index]->StopActions();
				Test->TestTrue(TEXT("Stopped consumer can re-admit without a stale derived index"), Actions[Index]->StartActions(ScoringSources[Index], {}));
			}
			for (auto* Action : Actions) { Test->TestTrue(TEXT("Duplicate native registration is idempotent"), Fixture.Decisions->RegisterAction(Action)); }
			Test->TestEqual(TEXT("Repeated detach/re-admit keeps exactly96 current consumers"), Fixture.Decisions->GetActionCount(), 96);
		}
		bool Update() override
		{
			Fixture.Decisions->Tick(0);
			const auto& Stats = Fixture.Decisions->GetStats();
			Peak = FMath::Max(Peak, Stats.LastTickActionUpdates);
			Test->TestTrue(TEXT("Action applications bounded"), Stats.LastTickActionUpdates <= Fixture.Decisions->MaxActionUpdatesPerTick);
			Test->TestTrue(TEXT("Action visits bounded"), Stats.LastTickActionVisits <= Fixture.Decisions->MaxActionVisitsPerTick);
			const bool Finished = !Actions.ContainsByPredicate([](const auto* Action) { return Action->GetActionState() != EProjectJNPCActionState::Disabled; });
			if (Finished)
			{
				Test->TestEqual(TEXT("Callback-driven self-removal drains every current registry entry"), Fixture.Decisions->GetActionCount(), 0);
				Test->AddInfo(FString::Printf(TEXT("96 action consumers serviced and removed; peak_updates=%d cap=%d elapsed=%.3fs"), Peak, Fixture.Decisions->MaxActionUpdatesPerTick, FPlatformTime::Seconds() - Started));
				return true;
			}
			if (FPlatformTime::Seconds() - Started > 5)
			{
				int32 Remaining = 0;
				for (const auto* Action : Actions)
				{
					if (Action->GetActionState() != EProjectJNPCActionState::Disabled)
					{
						++Remaining;
						if (Remaining == 1)
						{
							Test->AddInfo(FString::Printf(TEXT("First remaining state=%d health=%.1f"), int32(Action->GetActionState()),
								CastChecked<AProject_JNPCCharacter>(Action->GetOwner())->GetAttributeSet()->GetHealth()));
						}
					}
				}
				Test->AddError(FString::Printf(TEXT("Action budget fairness timed out remaining=%d registered=%d peak=%d last_visits=%d last_updates=%d"),
					Remaining, Fixture.Decisions->GetActionCount(), Peak, Stats.LastTickActionVisits, Stats.LastTickActionUpdates));
				return true;
			}
			return false;
		}
	private:
		FAutomationTestBase* Test;
		FActionFixture Fixture;
		TArray<UProject_JNPCActionComponent*> Actions;
		TArray<UProject_JTargetScoringComponent*> ScoringSources;
		double Started;
		int32 Peak = 0;
	};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCActionBudgetTest, "ProjectJ.Integrated.ActionBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCActionBudgetTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FActionBudgetCommand(this));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCPathPressureTest, "ProjectJ.GroupA.PathPressure",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCPathPressureTest::RunTest(const FString& Parameters)
{
 FActionFixture F;
 TArray<TStrongObjectPtr<USceneComponent>> Owners;
 TArray<uint64> Tokens; TArray<int32> Delivered; TArray<FVector> DeliveredGoals; TArray<uint64> DeliveredTokens;
 for (int32 I = 0; I < F.Paths->MaxInFlight + 6; ++I)
 {
  Owners.Emplace(NewObject<USceneComponent>(F.NPC));
  Tokens.Add(F.Paths->Submit(Owners.Last().Get(), F.NPC, FVector(700,0,0), 1,
   [&, I](const auto& Result) { Delivered.Add(I); DeliveredGoals.Add(Result.Goal); DeliveredTokens.Add(Result.Token); }, I == F.Paths->MaxInFlight + 5 ? EProjectJNPCPathPriority::Urgent : EProjectJNPCPathPriority::Normal));
  if (I < F.Paths->MaxInFlight) { F.Paths->SimulateDispatchForTest(Tokens.Last(), false); }
 }
 F.Paths->AgeForTest(Tokens[F.Paths->MaxInFlight], 0.5);
 const int32 QueuedIndex=F.Paths->MaxInFlight;
 const FVector LatestGoal(900,100,0);
 const uint64 AcceptedBeforeRefresh=F.Paths->GetStats().Accepted;
 TestTrue(TEXT("Queued same-intent goal coalesces without re-admission"),F.Paths->RefreshQueuedGoal(Tokens[QueuedIndex],Owners[QueuedIndex].Get(),1,LatestGoal));
 TestTrue(TEXT("Equal queued goal is a successful noop"),F.Paths->RefreshQueuedGoal(Tokens[QueuedIndex],Owners[QueuedIndex].Get(),1,LatestGoal));
 TestEqual(TEXT("Refresh/noop retain accepted request count"),F.Paths->GetStats().Accepted,AcceptedBeforeRefresh);
 TestFalse(TEXT("Foreign owner cannot update the queue"),F.Paths->RefreshQueuedGoal(Tokens[QueuedIndex],Owners[QueuedIndex+1].Get(),1,FVector(800,0,0)));
 TestFalse(TEXT("Old intent cannot update the queue"),F.Paths->RefreshQueuedGoal(Tokens[QueuedIndex],Owners[QueuedIndex].Get(),2,FVector(800,0,0)));
 TestFalse(TEXT("Invalid goal cannot update the queue"),F.Paths->RefreshQueuedGoal(Tokens[QueuedIndex],Owners[QueuedIndex].Get(),1,FVector(1.e10,0,0)));
 TestFalse(TEXT("Dispatched engine query is immutable"),F.Paths->RefreshQueuedGoal(Tokens[0],Owners[0].Get(),1,FVector(800,0,0)));
 F.Paths->Tick(0);
 TestEqual(TEXT("Physical in-flight cap blocks new dispatch"), F.Paths->GetStats().LastTickDispatches, 0);
 for (int32 I = 0; I < F.Paths->MaxInFlight; ++I) { F.Paths->Cancel(Tokens[I]); }
 F.Paths->Tick(0);
 TestEqual(TEXT("Cancellation cannot bypass the physical cap"), F.Paths->GetStats().LastTickDispatches, 0);
 for (int32 I = 0; I < F.Paths->MaxInFlight; ++I) { F.Paths->Complete(Tokens[I], 1, ENavigationQueryResult::Fail, nullptr); }
 for (int32 I = 0; I < 20 && Delivered.Num() < 6; ++I) { F.Paths->Tick(0); }
 TestEqual(TEXT("All queued owners eventually delivered"), Delivered.Num(), 6);
 if (Delivered.Num() >= 2)
 {
  TestEqual(TEXT("Aged normal request beats fresh urgent after goal coalescing"), Delivered[0], F.Paths->MaxInFlight);
  TestEqual(TEXT("Coalescing retains the original token"),DeliveredTokens[0],Tokens[QueuedIndex]);
  TestTrue(TEXT("Completion carries the latest queued goal"),DeliveredGoals[0]==LatestGoal);
  TestEqual(TEXT("Urgent request beats fresh normal"), Delivered[1], F.Paths->MaxInFlight + 5);
 }
 TestEqual(TEXT("All tombstones drained"), F.Paths->GetRequestCount(), 0);
 TestFalse(TEXT("Retired token cannot update another request"),F.Paths->RefreshQueuedGoal(Tokens[QueuedIndex],Owners[QueuedIndex].Get(),1,LatestGoal));
 int32 ExpiryCallbacks=0; EProjectJNPCPathStatus ExpiryStatus=EProjectJNPCPathStatus::Failed; FVector ExpiryGoal=FVector::ZeroVector;
 const auto Expiring=F.Paths->Submit(Owners[0].Get(),F.NPC,FVector(700,0,0),7,
  [&](const auto& Result){++ExpiryCallbacks;ExpiryStatus=Result.Status;ExpiryGoal=Result.Goal;});
 F.Paths->AgeForTest(Expiring,1.5,true);
 TestTrue(TEXT("Near-deadline queued goal still refreshes"),F.Paths->RefreshQueuedGoal(Expiring,Owners[0].Get(),7,LatestGoal));
 F.Paths->AgeForTest(Expiring,.6,true);
 TestFalse(TEXT("Refresh did not extend original deadline"),F.Paths->RefreshQueuedGoal(Expiring,Owners[0].Get(),7,FVector(800,0,0)));
 const uint64 DispatchedBeforeExpiry=F.Paths->GetStats().Dispatched;
 F.Paths->Tick(0);
 TestEqual(TEXT("Queued expiry delivers exactly once"),ExpiryCallbacks,1);
 TestEqual(TEXT("Original deadline still reports expired"),ExpiryStatus,EProjectJNPCPathStatus::Expired);
 TestTrue(TEXT("Expiry retains last accepted queued goal"),ExpiryGoal==LatestGoal);
 TestEqual(TEXT("Expired coalesced work never reaches engine"),F.Paths->GetStats().Dispatched,DispatchedBeforeExpiry);
 TestEqual(TEXT("Expiry releases the queued slot"),F.Paths->GetRequestCount(),0);
 F.Actions->RetryInterval = 0.1;
 F.Actions->BackoffPath(); const double FirstDeadline = F.Actions->NextPathTime;
 F.Actions->BackoffPath();
 TestTrue(TEXT("Repeated failure increases retry delay"), F.Actions->NextPathTime > FirstDeadline);
 F.Actions->ClearIntent();
 TestEqual(TEXT("New semantic context resets failure backoff"), F.Actions->PathFailures, uint32(0));
 AddInfo(TEXT("16 physical slots retained through cancel; 6 queued owners drained with bounded urgency and aging."));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCBoundedCleaveTest, "ProjectJ.NPCGameplay.BoundedCleave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCBoundedCleaveTest::RunTest(const FString&)
{
	FActionFixture F;
	const auto* Reference = LoadObject<UProject_JAttackDefinition>(nullptr,
		TEXT("/Game/DataAssetSets/AttackDefinition/Spellblade/DA_Attack_Spellblade_LMB1.DA_Attack_Spellblade_LMB1"));
	auto* MeshAsset = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"));
	if (!TestNotNull(TEXT("Existing damage/montage authoring"), Reference) || !Reference->Montage || !Reference->DamageEffect
		|| !TestNotNull(TEXT("Existing NPC mesh"), MeshAsset) || MeshAsset->GetRefSkeleton().GetNum() == 0) { return false; }
	const auto* Effect = Reference->DamageEffect->GetDefaultObject<UGameplayEffect>();
	float HealthDelta = 0;
	for (const auto& Modifier : Effect->Modifiers)
	{
		if (Modifier.Attribute == UProject_JAttributeSet::GetHealthAttribute() && Modifier.ModifierOp == EGameplayModOp::Additive)
		{ float Magnitude = 0; if (Modifier.ModifierMagnitude.GetStaticMagnitudeIfPossible(1.f, Magnitude)) { HealthDelta += Magnitude; } }
	}
	if (!TestTrue(TEXT("Actual authored instant effect reduces health"), Effect->DurationPolicy == EGameplayEffectDurationType::Instant && HealthDelta < 0)) { return false; }
	auto* Mesh = F.NPC->GetMesh(); Mesh->SetSkeletalMesh(MeshAsset); Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass());
	if (!TestNotNull(TEXT("Native montage animation instance"), Mesh->GetAnimInstance())) { return false; }
	F.World->GetSubsystem<UProject_JCharacterAnimationBudgetSubsystem>()->SetEnabledOverride(false);
	auto* Hit = NewObject<UProject_JCombatHitValidationComponent>(F.NPC); F.NPC->AddInstanceComponent(Hit); Hit->RegisterComponent();
	const TStrongObjectPtr<UProject_JAttackDefinition> Definition(NewObject<UProject_JAttackDefinition>(F.NPC));
	Definition->AttackTag = NPCTestHitTag; Definition->Montage = Reference->Montage; Definition->DamageEffect = Reference->DamageEffect;
	Definition->MovementPolicy = EProject_JAttackMovementPolicy::RootMotionMontage;
	Definition->HitSpec.TraceRadius = 300;
	TestEqual(TEXT("Existing definitions default to one locked NPC target"), Definition->NPCMaxMeleeTargets, 1);
	auto* ASC = F.NPC->GetAbilitySystemComponent();
	const auto Handle = ASC->GiveAbility(FGameplayAbilitySpec(UProject_JGameplayAbility_NPCAttack::StaticClass(), 1, INDEX_NONE, Definition.Get()));
	auto* Ability = Cast<UProject_JGameplayAbility_NPCAttack>(ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
	if (!TestNotNull(TEXT("Private real NPC attack instance"), Ability) || !Ability->ConfigureHitEvent(NPCTestHitTag)) { return false; }
	F.Scoring->StartBatchedNPCDecisions(1);
	TArray<AProject_JNPCCharacter*> Targets;
	const FVector Positions[] = {{100,0,0}, {100,80,0}, {100,-80,0}, {150,130,0}, {120,-130,0}, {160,30,0}, {120,130,0}, {250,0,0}};
	for (const FVector& Position : Positions)
	{
		FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		auto* Target = F.World->SpawnActor<AProject_JNPCCharacter>(AProject_JNPCCharacter::StaticClass(), Position, FRotator::ZeroRotator, P);
		if (!TestNotNull(TEXT("Real ASC/capsule victim"), Target)) { return false; }
		Target->GetAbilitySystemComponent()->InitAbilityActorInfo(Target, Target);
		Target->GetAttributeSet()->InitMaxHealth(100); Target->GetAttributeSet()->InitHealth(100);
		Target->GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Target->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Target->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
		Target->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		TestTrue(TEXT("Explicit authoritative target registration"), F.Decisions->RegisterTarget(Target, Targets.Num() == 4 ? 1 : 2));
		Targets.Add(Target);
	}
	Targets[5]->GetAttributeSet()->InitHealth(0);
	auto* Blocker = F.World->SpawnActor<AActor>();
	auto* Box = NewObject<UBoxComponent>(Blocker); Blocker->SetRootComponent(Box); Box->SetBoxExtent(FVector(5,5,130));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Box->SetCollisionObjectType(ECC_WorldStatic);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore); Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Box->RegisterComponent(); Blocker->SetActorLocation(FVector(60,65,0));
	const TStrongObjectPtr<UProject_JAnimNotifyState_MeleeHit> Notify(NewObject<UProject_JAnimNotifyState_MeleeHit>());
	auto* SocketProperty = FindFProperty<FNameProperty>(Notify->GetClass(), TEXT("SocketName"));
	auto* EventProperty = FindFProperty<FStructProperty>(Notify->GetClass(), TEXT("HitEventTag"));
	if (!TestNotNull(TEXT("Authored notify socket property"), SocketProperty) || !TestNotNull(TEXT("Authored notify event property"), EventProperty)) { return false; }
	SocketProperty->SetPropertyValue_InContainer(Notify.Get(), MeshAsset->GetRefSkeleton().GetBoneName(0));
	*EventProperty->ContainerPtrToValuePtr<FGameplayTag>(Notify.Get()) = NPCTestHitTag;
	FAnimNotifyEventReference Event;
	const auto Stop = [&]
	{
		Notify->NotifyEnd(Mesh, Definition->Montage, Event);
		F.Actions->StopActions();
	};
	const auto Start = [&](int32 Limit)
	{
		Stop(); Definition->NPCMaxMeleeTargets = Limit;
		if (!F.Actions->StartActions(F.Scoring, Handle)) { return false; }
		F.Scoring->OnQueryCompleted.Broadcast(Targets[0], 1);
		if (!ProjectJ::Tests::PublishMoverFrame(F.NPC, MOVE_Walking)) { return false; }
		F.Actions->UpdateAction();
		if (!Ability->IsActive()) { return false; }
		Notify->NotifyBegin(Mesh, Definition->Montage, 1, Event);
		return true;
	};
	const auto ResetVictims = [&]
	{
		for (auto* Target : Targets) { Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 100); }
		Targets[5]->GetAbilitySystemComponent()->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 0);
	};
	const auto DamagedEligible = [&]
	{
		int32 Count = 0;
		for (int32 I = 0; I < 4; ++I) { Count += Targets[I]->GetAttributeSet()->GetHealth() < 100; }
		return Count;
	};
	TestFalse(TEXT("Zero target budget fails closed"), Start(0));
	TestFalse(TEXT("Over-cap target budget fails closed"), Start(UProject_JAttackDefinition::MaxNPCMeleeTargets + 1));
	if (!TestTrue(TEXT("Real action consumer starts the default attack"), Start(1))) { return false; }
	TestTrue(TEXT("Secondary registered enemy is spatially eligible"), F.Actions->CanCommitSweepHit(Targets[0], Targets[1]));
	TestFalse(TEXT("Friendly registry team is excluded"), F.Actions->CanCommitSweepHit(Targets[0], Targets[4]));
	TestFalse(TEXT("Actual zero-health NPC is excluded"), F.Actions->CanCommitSweepHit(Targets[0], Targets[5]));
	TestFalse(TEXT("Physical visibility obstruction is excluded"), F.Actions->CanCommitSweepHit(Targets[0], Targets[6]));
	TestFalse(TEXT("Attack range remains distinct from acquisition range"), F.Actions->CanCommitSweepHit(Targets[0], Targets[7]));
	Notify->NotifyTick(Mesh, Definition->Montage, 1.f/60, Event);
	TestTrue(TEXT("Default real notify applies the actual authored damage only to primary"),
		FMath::IsNearlyEqual(Targets[0]->GetAttributeSet()->GetHealth(), FMath::Max(0.f, 100 + HealthDelta)) && DamagedEligible() == 1);
	Stop(); ResetVictims();
	if (!TestTrue(TEXT("Same runtime consumer accepts explicit three-target cleave"), Start(3))) { return false; }
	Notify->NotifyTick(Mesh, Definition->Montage, 1.f/60, Event);
	TestEqual(TEXT("Canonical collision notify applies exactly three eligible victims"), DamagedEligible(), 3);
	for (int32 I : {4,6,7}) { TestEqual(TEXT("Ineligible contact receives no damage"), Targets[I]->GetAttributeSet()->GetHealth(), 100.f); }
	TestEqual(TEXT("Dead contact remains unchanged"), Targets[5]->GetAttributeSet()->GetHealth(), 0.f);
	TArray<float> FirstHealth; for (auto* Target : Targets) { FirstHealth.Add(Target->GetAttributeSet()->GetHealth()); }
	Definition->NPCMaxMeleeTargets = UProject_JAttackDefinition::MaxNPCMeleeTargets;
	Notify->NotifyTick(Mesh, Definition->Montage, 1.f/60, Event);
	for (int32 I = 0; I < Targets.Num(); ++I) { TestEqual(TEXT("Dedup and captured limit survive a repeated notify and live asset edit"), Targets[I]->GetAttributeSet()->GetHealth(), FirstHealth[I]); }
	Stop(); ResetVictims();
	if (!TestTrue(TEXT("Reused ability starts a fresh bounded attack"), Start(2))) { return false; }
	bool bNested = false;
	TArray<FDelegateHandle> Callbacks;
	for (int32 I = 0; I < 4; ++I)
	{
		Callbacks.Add(Targets[I]->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
			[&, I](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle)
			{
				if (bNested) { return; } bNested = true;
				FGameplayEventData Payload; Payload.EventTag = NPCTestHitTag; Payload.Instigator = F.NPC; Payload.Target = Targets[I == 1 ? 2 : 1];
				UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(F.NPC, NPCTestHitTag, Payload);
			}));
	}
	Notify->NotifyTick(Mesh, Definition->Montage, 1.f/60, Event);
	TestTrue(TEXT("Real effect callback delivered a nested hit"), bNested);
	TestEqual(TEXT("Pre-effect reservation bounds nested hit plus remaining sweep contacts"), DamagedEligible(), 2);
	for (int32 I = 0; I < 4; ++I) { Targets[I]->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.Remove(Callbacks[I]); }
	Stop(); ResetVictims();
	if (!TestTrue(TEXT("Death/reentry case owns an actual active action attack"), Start(3))) { return false; }
	bool bKilled = false; Callbacks.Reset();
	for (int32 I = 0; I < 4; ++I)
	{
		Callbacks.Add(Targets[I]->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
			[&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle)
			{
				if (bKilled) { return; } bKilled = true;
				F.NPC->GetAttributeSet()->InitHealth(0); F.Actions->UpdateAction(); CollectGarbage(RF_NoFlags);
			}));
	}
	Notify->NotifyTick(Mesh, Definition->Montage, 1.f/60, Event);
	TestTrue(TEXT("Damage callback killed the owner"), bKilled);
	TestEqual(TEXT("Owner death retires remaining contacts in the same sweep"), DamagedEligible(), 1);
	TestFalse(TEXT("Existing action death path cancels GAS"), Ability->IsActive());
	TestNull(TEXT("Death retires hit authority"), Hit->GetActiveAttackDefinition());
	for (int32 I = 0; I < 4; ++I) { Targets[I]->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.Remove(Callbacks[I]); }
	Stop(); F.NPC->GetAttributeSet()->InitHealth(100); F.Scoring->StopBatchedNPCDecisions();
	TestTrue(TEXT("Existing noncombat follow mode starts"), F.Actions->StartFollowing(Targets[0], Targets[0]->GetAbilitySystemComponent(), 100));
	TestFalse(TEXT("Follow owner never becomes attack authority"), F.Actions->CanCommitSweepHit(Targets[0], Targets[1]));
	F.Actions->StopActions(); ASC->ClearAbility(Handle);
	AddInfo(TEXT("Actual action/GAS/montage + canonical notify collision + authored health effect; no save/asset edits or fake hit window."));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCNeutralPointLeaseTest, "ProjectJ.NPCAction.NeutralPointLease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCNeutralPointLeaseTest::RunTest(const FString&)
{
	FActionFixture Point130;
	int32 Point130FirstStops = 0, Point130SecondStops = 0, Point130RejectedStops = 0;
	bool Point130Restarted = false, Point130RetiredBeforeCallback = false, Point130CleaningUp = false;
	ON_SCOPE_EXIT { Point130CleaningUp = true; Point130.Actions->StopActions(); Point130.NPC->StopNavigation(); };
	TestFalse(TEXT("Invalid radius is rejected without a lease"), Point130.Actions->StartNavigationTo(FVector(700,0,0), 0, [&](bool) { ++Point130RejectedStops; }));
	TestFalse(TEXT("An unbounded point is rejected without a lease"), Point130.Actions->StartNavigationTo(FVector(1.e10,0,0), 60, [&](bool) { ++Point130RejectedStops; }));
	if (!TestTrue(TEXT("Neutral point admission needs no scoring or attack grant"), Point130.Actions->StartNavigationTo(FVector(700,0,0), 60,
		[&](bool Arrived)
		{
			++Point130FirstStops; TestFalse(TEXT("Explicit old-lease stop reports cancellation"), Arrived);
			Point130RetiredBeforeCallback = Point130.Actions->GetActionState() == EProjectJNPCActionState::Disabled &&
				Point130.Actions->GetOwnershipToken() == 0 && Point130.Actions->GetPendingPathTokenForTest() == 0 && Point130.Decisions->GetActionCount() == 0;
			if (!Point130CleaningUp) Point130Restarted = Point130.Actions->StartNavigationTo(FVector(900,100,0), 60,
				[&](bool SecondArrived) { ++Point130SecondStops; TestFalse(TEXT("ASC epoch replacement is cancellation"), SecondArrived); });
		}))) return false;
	const uint64 Point130OldOwner = Point130.Actions->GetOwnershipToken();
	TestTrue(TEXT("Neutral point owns exactly one shared action slot"), Point130OldOwner != 0 && Point130.Decisions->GetActionCount() == 1);
	TestEqual(TEXT("Neutral point registers no hostile decision source"), Point130.Decisions->GetAgentCount(), 0);
	TestNull(TEXT("Neutral point is not represented as an Actor target"), Point130.Actions->GetIntentTarget());
	TestNull(TEXT("Neutral point creates no scoring writer"), Point130.Actions->GetScoringSource());
	TestFalse(TEXT("Neutral point has no attack grant"), Point130.Actions->GetAttackAbilityHandle().IsValid());
	TestFalse(TEXT("Neutral point cannot commit a hit"), Point130.Actions->CanCommitToTarget(Point130.NPC));
	TestFalse(TEXT("Neutral point cannot authorize secondary sweep hits"), Point130.Actions->CanCommitSweepHit(Point130.NPC, Point130.NPC));
	TestFalse(TEXT("Neutral movement cannot enter combat-only Mass suspension"), Point130.Actions->CanSuspendMovement());
	TestFalse(TEXT("Another point command cannot replace an active lease implicitly"), Point130.Actions->StartNavigationTo(FVector(900,0,0), 60, [&](bool) { ++Point130RejectedStops; }));
	TestEqual(TEXT("Rejected replacement preserves exact ownership"), Point130.Actions->GetOwnershipToken(), Point130OldOwner);
	Point130.ActionTick();
	TestTrue(TEXT("The neutral consumer owns an actual queued path request"), Point130.Paths->GetRequestCount() == 1 && Point130.Actions->GetPendingPathTokenForTest() != 0);
	TestTrue(TEXT("Exact old ownership may explicitly stop"), Point130.Actions->StopActionsIfOwned(Point130OldOwner));
	TestTrue(TEXT("Retirement precedes a callback which can start a fresh lease"), Point130RetiredBeforeCallback && Point130Restarted);
	TestEqual(TEXT("The old stop fires once"), Point130FirstStops, 1);
	TestEqual(TEXT("Rejected admission never calls its completion"), Point130RejectedStops, 0);
	const uint64 Point130NewOwner = Point130.Actions->GetOwnershipToken();
	TestTrue(TEXT("Old stop's return preserves the reentrant new owner"), Point130NewOwner != 0 && Point130NewOwner != Point130OldOwner && Point130.Decisions->GetActionCount() == 1);
	TestFalse(TEXT("A stale owner cannot cancel the newer intent"), Point130.Actions->StopActionsIfOwned(Point130OldOwner));
	auto* Point130ASC = CastChecked<UProject_JAbilitySystemComponent>(Point130.NPC->GetAbilitySystemComponent());
	TestTrue(TEXT("Real same-epoch metadata refresh remains supported"), Point130ASC->RefreshActorInfoForCurrentActor(Point130.NPC));
	TestEqual(TEXT("Metadata refresh preserves neutral ownership"), Point130.Actions->GetOwnershipToken(), Point130NewOwner);
	Point130.ActionTick();
	Point130ASC->InitAbilityActorInfo(Point130.NPC, Point130.NPC);
	TestEqual(TEXT("Actual ASC recipient epoch replacement retires neutral navigation once"), Point130SecondStops, 1);
	TestTrue(TEXT("Epoch retirement leaves no action or path ownership"), Point130.Actions->GetOwnershipToken() == 0 && Point130.Decisions->GetActionCount() == 0 && Point130.Paths->GetRequestCount() == 0);
	Point130.Actions->StopActions();
	TestEqual(TEXT("Repeated retirement cannot notify again"), Point130SecondStops, 1);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCNeutralPromotionTest, "ProjectJ.NPCAction.NeutralCombatPromotion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCNeutralPromotionTest::RunTest(const FString&)
{
	FActionFixture Promote130;
	// CreateWorld(Game) omits AI services by default; this test actually runs a BT.
	if (!TestNotNull(TEXT("Foreign-brain fixture creates the real engine AI system"), Promote130.World->CreateAISystem()) ||
		!TestNotNull(TEXT("Foreign-brain fixture has the real world behavior-tree manager"), UBehaviorTreeManager::GetCurrent(Promote130.World))) return false;
	// A real transient native behavior tree supplies a running foreign brain;
	// no protected flags or fake IsRunning override are used.
	const TStrongObjectPtr<UBehaviorTree> Promote130Tree(NewObject<UBehaviorTree>(Promote130.AI));
	auto* Promote130Sequence = NewObject<UBTComposite_Sequence>(Promote130Tree.Get());
	Promote130Tree->RootNode = Promote130Sequence;
	Promote130Sequence->Children.AddDefaulted_GetRef().ChildTask = NewObject<UBTTask_Wait>(Promote130Sequence);
	if (!TestTrue(TEXT("The controller starts an actual native foreign brain"), Promote130.AI->RunBehaviorTree(Promote130Tree.Get()) &&
		Promote130.AI->GetBrainComponent() && Promote130.AI->GetBrainComponent()->IsRunning())) return false;
	TestFalse(TEXT("Neutral helper cannot commandeer a running foreign brain"), Promote130.NPC->StartNavigationTo(FVector(700,0,0), 60));
	TestTrue(TEXT("Rejected neutral request preserves the foreign brain"), Promote130.AI->GetBrainComponent()->IsRunning());
	Promote130.AI->GetBrainComponent()->StopLogic(TEXT("Native fixture releases its own tree"));
	if (!TestFalse(TEXT("Foreign tree is stopped before neutral admission"), Promote130.AI->GetBrainComponent()->IsRunning())) return false;
	int32 Promote130Stops = 0;
	ON_SCOPE_EXIT { Promote130.NPC->StopNavigation(); Promote130.Actions->StopActions(); };
	if (!TestTrue(TEXT("Public NPC helper reuses its existing native controller/action"), Promote130.NPC->StartNavigationTo(FVector(700,0,0), 60,
		[&](bool Arrived) { ++Promote130Stops; TestFalse(TEXT("Promotion explicitly cancels the old point"), Arrived); }))) return false;
	const uint64 Promote130OldOwner = Promote130.Actions->GetOwnershipToken();
	const auto* Promote130Controller = Promote130.NPC->GetController();
	TestTrue(TEXT("Helper selected the existing action without duplicates"), Promote130.NPC->FindComponentByClass<UProject_JNPCActionComponent>() == Promote130.Actions);
	Promote130.NPC->StopNavigation();
	TestEqual(TEXT("Neutral lease retires once before combat activation"), Promote130Stops, 1);
	auto* Promote130Activation = NewObject<UProject_JNPCActivationComponent>(Promote130.NPC);
	Promote130.NPC->AddInstanceComponent(Promote130Activation); Promote130Activation->RegisterComponent();
	const TStrongObjectPtr<UProject_JNPCActivationDefinition> Promote130Definition(NewObject<UProject_JNPCActivationDefinition>());
	Promote130Definition->AttackAbility = UProject_JGameplayAbility_NPCAttack::StaticClass();
	auto* Promote130ASC = Promote130.NPC->GetAbilitySystemComponent();
	const int32 Promote130Grants = Promote130ASC->GetActivatableAbilities().Num();
	if (!TestTrue(TEXT("The same native NPC promotes through the existing combat activation owner"),
		Promote130Activation->ActivateNPC(Promote130Definition.Get(), Promote130.Scoring, Promote130.Actions, 1))) return false;
	TestTrue(TEXT("Promotion retains the existing controller and installs a different action owner"), Promote130.NPC->GetController() == Promote130Controller && Promote130.Actions->GetOwnershipToken() != Promote130OldOwner);
	TestTrue(TEXT("Only combat activation owns its scoring and attack grant"), Promote130Activation->HasOwnedActivation() && Promote130.Decisions->GetAgentCount() == 1 &&
		Promote130ASC->GetActivatableAbilities().Num() == Promote130Grants + 1 && Promote130.Actions->GetAttackAbilityHandle().IsValid());
	Promote130.NPC->StopNavigation();
	TestFalse(TEXT("An old neutral token cannot retire the combat action"), Promote130.Actions->StopActionsIfOwned(Promote130OldOwner));
	TestTrue(TEXT("Neutral retirement never releases a promoted combat owner"), Promote130Activation->HasOwnedActivation() && Promote130.Decisions->GetActionCount() == 1);
	TestFalse(TEXT("A registered combat owner rejects neutral admission"), Promote130.NPC->StartNavigationTo(FVector(700,0,0), 60));
	Promote130Activation->DeactivateNPC();
	TestTrue(TEXT("Combat owner releases exactly its action, scoring and granted ability"), !Promote130Activation->HasOwnedActivation() &&
		Promote130.Decisions->GetAgentCount() == 0 && Promote130.Decisions->GetActionCount() == 0 && Promote130ASC->GetActivatableAbilities().Num() == Promote130Grants);
	TestEqual(TEXT("Combat retirement cannot notify the old neutral lease again"), Promote130Stops, 1);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCActorCombatExecutionTest, "ProjectJ.NPCAction.ActorCombatExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCActorCombatExecutionTest::RunTest(const FString&)
{
	FActionFixture Combat131;
	const TStrongObjectPtr<UProject_JNPCActivationDefinition> Combat131Definition(NewObject<UProject_JNPCActivationDefinition>());
	Combat131Definition->AttackAbility = UProject_JGameplayAbility_NPCAttack::StaticClass();
	int32 Combat131NeutralStops = 0;
	ON_SCOPE_EXIT
	{
		if (IsValid(Combat131.NPC)) { Combat131.NPC->StopNavigation(); Combat131.NPC->StopCombatActions(); }
		if (IsValid(Combat131.Actions)) Combat131.Actions->StopActions();
		if (IsValid(Combat131.Scoring)) Combat131.Scoring->StopBatchedNPCDecisions();
	};
	// Remove only this fixture's idle setup so the public actor helper must supply
	// real components/controller. No running foreign writer is retired here.
	Combat131.Actions->DestroyComponent(); Combat131.Actions = nullptr;
	Combat131.Scoring->DestroyComponent(); Combat131.Scoring = nullptr;
	Combat131.AI->UnPossess(); Combat131.AI->Destroy(); Combat131.AI = nullptr;
	const TStrongObjectPtr<UProject_JAbilitySystemComponent> Combat131ASC(CastChecked<UProject_JAbilitySystemComponent>(Combat131.NPC->GetAbilitySystemComponent()));
	Combat131ASC->InitAbilityActorInfo(Combat131.NPC, Combat131.NPC);
	const int32 Combat131BaselineGrants = Combat131ASC->GetActivatableAbilities().Num();
	TestTrue(TEXT("Combat control has no controller/action/scoring/activation or automatic combat"), !Combat131.NPC->GetController() &&
		!Combat131.NPC->FindComponentByClass<UProject_JNPCActionComponent>() && !Combat131.NPC->FindComponentByClass<UProject_JTargetScoringComponent>() &&
		!Combat131.NPC->FindComponentByClass<UProject_JNPCActivationComponent>() && !Combat131.NPC->HasCombatActions());
	if (!TestTrue(TEXT("Public actor combat helper prepares the real native execution consumer"), Combat131.NPC->StartCombatActions(Combat131Definition.Get(), 1))) return false;
	Combat131.AI = Cast<AAIController>(Combat131.NPC->GetController());
	Combat131.Actions = Combat131.NPC->FindComponentByClass<UProject_JNPCActionComponent>();
	Combat131.Scoring = Combat131.NPC->FindComponentByClass<UProject_JTargetScoringComponent>();
	const TStrongObjectPtr<UProject_JNPCActivationComponent> Combat131Activation(Combat131.NPC->FindComponentByClass<UProject_JNPCActivationComponent>());
	if (!TestTrue(TEXT("The helper creates registered real components and reciprocal possession"), Combat131.AI && Combat131.AI->GetPawn() == Combat131.NPC &&
		Combat131.Actions && Combat131.Actions->IsRegistered() && Combat131.Scoring && Combat131.Scoring->IsRegistered() && Combat131Activation.IsValid() && Combat131Activation->IsRegistered())) return false;
	const TWeakObjectPtr<AAIController> Combat131CreatedController(Combat131.AI);
	const uint64 Combat131First = Combat131Activation->GetOwnershipToken();
	TestTrue(TEXT("Combat helper owns one real activation/action/scoring/grant"), Combat131.NPC->HasCombatActions() && Combat131First != 0 &&
		Combat131.Decisions->GetActionCount() == 1 && Combat131.Decisions->GetAgentCount() == 1 && Combat131.Actions->GetAttackAbilityHandle().IsValid() &&
		Combat131ASC->GetActivatableAbilities().Num() == Combat131BaselineGrants + 1);
	TestFalse(TEXT("An already owned combat start is rejected without reactivation"), Combat131.NPC->StartCombatActions(Combat131Definition.Get(), 1));
	TestEqual(TEXT("Rejected duplicate start preserves its exact activation"), Combat131Activation->GetOwnershipToken(), Combat131First);
	Combat131.Actions->UpdateAction(); // Existing consumer runs; no target or hit is fabricated.
	Combat131.NPC->StopCombatActions();
	TestTrue(TEXT("Normal stop releases only the owned activation and keeps its native controller reusable"), !Combat131.NPC->HasCombatActions() &&
		Combat131Activation->GetOwnershipToken() == 0 && Combat131.Decisions->GetActionCount() == 0 && Combat131.Decisions->GetAgentCount() == 0 &&
		Combat131ASC->GetActivatableAbilities().Num() == Combat131BaselineGrants && Combat131.NPC->GetController() == Combat131CreatedController.Get());
	if (!TestTrue(TEXT("Same actor reuses the controller and prepared execution components"), Combat131.NPC->StartCombatActions(Combat131Definition.Get(), 1))) return false;
	TestTrue(TEXT("Restart did not duplicate any execution component"), Combat131.NPC->FindComponentByClass<UProject_JNPCActionComponent>() == Combat131.Actions &&
		Combat131.NPC->FindComponentByClass<UProject_JTargetScoringComponent>() == Combat131.Scoring && Combat131.NPC->FindComponentByClass<UProject_JNPCActivationComponent>() == Combat131Activation.Get());
	const uint64 Combat131Second = Combat131Activation->GetOwnershipToken();
	TestTrue(TEXT("A stale activation token cannot release a newer helper lease"), Combat131Second != 0 && Combat131Second != Combat131First && !Combat131Activation->DeactivateNPCIfOwned(Combat131First));
	Combat131ASC->InitAbilityActorInfo(Combat131.NPC, Combat131.NPC);
	TestTrue(TEXT("Actual ASC recipient replacement retires its owned combat grant/scoring/action"), !Combat131.NPC->HasCombatActions() &&
		Combat131Activation->GetOwnershipToken() == 0 && Combat131.Decisions->GetAgentCount() == 0 && Combat131.Decisions->GetActionCount() == 0 &&
		Combat131ASC->GetActivatableAbilities().Num() == Combat131BaselineGrants);
	if (!TestTrue(TEXT("A new actor command drops only its stale epoch binding and reuses the released activation"), Combat131.NPC->StartCombatActions(Combat131Definition.Get(), 1))) return false;
	Combat131.NPC->StopCombatActions();
	if (!TestTrue(TEXT("Explicit neutral navigation remains available after combat retirement"), Combat131.NPC->StartNavigationTo(FVector(700,0,0), 60,
		[&](bool Arrived) { ++Combat131NeutralStops; TestFalse(TEXT("Explicit combat promotion cancels neutral movement"), Arrived); }))) return false;
	const uint64 Combat131NeutralOwner = Combat131.Actions->GetOwnershipToken();
	TestFalse(TEXT("Combat helper cannot implicitly cancel an active neutral command"), Combat131.NPC->StartCombatActions(Combat131Definition.Get(), 1));
	TestTrue(TEXT("Rejected combat preserves the neutral owner and callback"), Combat131.Actions->GetOwnershipToken() == Combat131NeutralOwner && Combat131NeutralStops == 0);
	Combat131.NPC->StopNavigation();
	if (!TestTrue(TEXT("Explicit neutral stop permits same-actor combat promotion"), Combat131.NPC->StartCombatActions(Combat131Definition.Get(), 1))) return false;
	TestEqual(TEXT("The old neutral command stopped exactly once"), Combat131NeutralStops, 1);
	TestFalse(TEXT("Neutral helper cannot implicitly replace active combat"), Combat131.NPC->StartNavigationTo(FVector(700,0,0), 60));
	Combat131.NPC->Destroy();
	TestTrue(TEXT("Actor EndPlay releases exact helper activation/grant/scoring/action ownership"), Combat131Activation->GetOwnershipToken() == 0 &&
		Combat131.Decisions->GetAgentCount() == 0 && Combat131.Decisions->GetActionCount() == 0 && Combat131ASC->GetActivatableAbilities().Num() == Combat131BaselineGrants);
	TestTrue(TEXT("Actor EndPlay retires only its directly prepared controller"), !Combat131CreatedController.IsValid() || Combat131CreatedController->IsActorBeingDestroyed());
	TestEqual(TEXT("Actor retirement cannot notify the old neutral command again"), Combat131NeutralStops, 1);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCActorCombatOwnershipTest, "ProjectJ.NPCAction.ActorCombatOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCActorCombatOwnershipTest::RunTest(const FString&)
{
	FActionFixture Foreign131;
	const TStrongObjectPtr<UProject_JNPCActivationDefinition> Foreign131Definition(NewObject<UProject_JNPCActivationDefinition>());
	Foreign131Definition->AttackAbility = UProject_JGameplayAbility_NPCAttack::StaticClass();
	UProject_JNPCActivationComponent* Foreign131Activation = nullptr; uint64 Foreign131OwnedExternal = 0;
	ON_SCOPE_EXIT
	{
		Foreign131.NPC->StopCombatActions(); Foreign131.NPC->StopNavigation();
		if (IsValid(Foreign131Activation)) Foreign131Activation->DeactivateNPCIfOwned(Foreign131OwnedExternal);
		Foreign131.Actions->StopActions(); Foreign131.Scoring->StopBatchedNPCDecisions();
	};
	if (!TestNotNull(TEXT("Foreign combat brain uses the real engine AI system"), Foreign131.World->CreateAISystem()) ||
		!TestNotNull(TEXT("Foreign combat brain uses its actual behavior-tree manager"), UBehaviorTreeManager::GetCurrent(Foreign131.World))) return false;
	const TStrongObjectPtr<UBehaviorTree> Foreign131Tree(NewObject<UBehaviorTree>(Foreign131.AI));
	auto* Foreign131Sequence = NewObject<UBTComposite_Sequence>(Foreign131Tree.Get()); Foreign131Tree->RootNode = Foreign131Sequence;
	Foreign131Sequence->Children.AddDefaulted_GetRef().ChildTask = NewObject<UBTTask_Wait>(Foreign131Sequence);
	if (!TestTrue(TEXT("Actual foreign native brain is running before admission"), Foreign131.AI->RunBehaviorTree(Foreign131Tree.Get()) &&
		Foreign131.AI->GetBrainComponent() && Foreign131.AI->GetBrainComponent()->IsRunning())) return false;
	TestFalse(TEXT("Combat helper cannot commandeer a foreign running brain"), Foreign131.NPC->StartCombatActions(Foreign131Definition.Get(), 1));
	TestTrue(TEXT("Failed combat admission leaves the brain running and owns no action"), Foreign131.AI->GetBrainComponent()->IsRunning() &&
		!Foreign131.NPC->HasCombatActions() && Foreign131.Decisions->GetActionCount() == 0 && Foreign131.Decisions->GetAgentCount() == 0);
	Foreign131.AI->GetBrainComponent()->StopLogic(TEXT("Fixture releases its actual native brain"));
	if (!TestTrue(TEXT("Independent action consumer registers through its normal APIs"), Foreign131.Scoring->StartBatchedNPCDecisions(2) && Foreign131.Actions->StartActions(Foreign131.Scoring, {}))) return false;
	const uint64 Foreign131ActionOwner = Foreign131.Actions->GetOwnershipToken();
	TestFalse(TEXT("Combat helper cannot adopt an independent action/scoring owner"), Foreign131.NPC->StartCombatActions(Foreign131Definition.Get(), 1));
	Foreign131.NPC->StopCombatActions();
	TestTrue(TEXT("Unowned actor stop preserves independent action ownership"), Foreign131.Actions->GetOwnershipToken() == Foreign131ActionOwner && Foreign131.Scoring->IsBatchedNPCDecisionRegistered());
	Foreign131.Actions->StopActions(); Foreign131.Scoring->StopBatchedNPCDecisions();
	// Use a fresh real Action for each activation below, so both component-local
	// action counters naturally issue the same value while activation owners differ.
	Foreign131.Actions->DestroyComponent();
	Foreign131.Actions = NewObject<UProject_JNPCActionComponent>(Foreign131.NPC);
	Foreign131.NPC->AddInstanceComponent(Foreign131.Actions); Foreign131.Actions->RegisterComponent();
	if (!TestTrue(TEXT("Combat helper starts after the independent owner explicitly releases"), Foreign131.NPC->StartCombatActions(Foreign131Definition.Get(), 1))) return false;
	Foreign131Activation = Foreign131.NPC->FindComponentByClass<UProject_JNPCActivationComponent>();
	if (!TestNotNull(TEXT("Actual activation owner is observable"), Foreign131Activation)) return false;
	const uint64 Foreign131Old = Foreign131Activation->GetOwnershipToken();
	const uint64 Foreign131OldActionValue = Foreign131.Actions->GetOwnershipToken();
	if (!TestTrue(TEXT("An exact activation token may release the helper lease"), Foreign131Activation->DeactivateNPCIfOwned(Foreign131Old))) return false;
	TestFalse(TEXT("Helper no longer reports the retired activation"), Foreign131.NPC->HasCombatActions());
	Foreign131.Actions->DestroyComponent();
	Foreign131.Actions = NewObject<UProject_JNPCActionComponent>(Foreign131.NPC);
	Foreign131.NPC->AddInstanceComponent(Foreign131.Actions); Foreign131.Actions->RegisterComponent();
	if (!TestTrue(TEXT("Another native owner may activate the released same component"), Foreign131Activation->ActivateNPC(Foreign131Definition.Get(), Foreign131.Scoring, Foreign131.Actions, 2))) return false;
	Foreign131OwnedExternal = Foreign131Activation->GetOwnershipToken();
	TestEqual(TEXT("Different real Action components naturally reuse their local action counter"), Foreign131.Actions->GetOwnershipToken(), Foreign131OldActionValue);
	Foreign131.NPC->StopCombatActions();
	TestTrue(TEXT("Old helper stop cannot release a new external activation token"), Foreign131OwnedExternal != 0 && Foreign131OwnedExternal != Foreign131Old &&
		Foreign131Activation->GetOwnershipToken() == Foreign131OwnedExternal && Foreign131.Decisions->GetAgentCount() == 1 && Foreign131.Decisions->GetActionCount() == 1);
	TestFalse(TEXT("Old exact-token API cannot cancel that external owner"), Foreign131Activation->DeactivateNPCIfOwned(Foreign131Old));
	TestFalse(TEXT("Helper cannot adopt the external activation"), Foreign131.NPC->StartCombatActions(Foreign131Definition.Get(), 1));
	if (!TestTrue(TEXT("The external owner can explicitly retire its own activation"), Foreign131Activation->DeactivateNPCIfOwned(Foreign131OwnedExternal))) return false;
	if (!TestTrue(TEXT("A fresh helper can start after external retirement"), Foreign131.NPC->StartCombatActions(Foreign131Definition.Get(), 1))) return false;
	TestTrue(TEXT("The old external token cannot release the newest helper"), !Foreign131Activation->DeactivateNPCIfOwned(Foreign131OwnedExternal) && Foreign131.NPC->HasCombatActions());
	Foreign131.NPC->StopCombatActions();
	TestTrue(TEXT("Final actor stop releases only the current helper lease"), !Foreign131.NPC->HasCombatActions() && Foreign131Activation->GetOwnershipToken() == 0 &&
		Foreign131.Decisions->GetAgentCount() == 0 && Foreign131.Decisions->GetActionCount() == 0);
	return !HasAnyErrors();
}

#endif
