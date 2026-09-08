#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Components/Project_JNPCActionComponent.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "System/Project_JNPCPathSubsystem.h"
#include "System/Project_JNPCDecisionSubsystem.h"
#include "Project_JNPCCharacter.h"
#include "Project_JAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AIController.h"
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
#include "Components/Project_JCombatHitValidationComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameplayEffect.h"

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
		void ActionTick() { Actions->TickComponent(0.1f, LEVELTICK_All, nullptr); }
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
	F.Paths->SimulateDispatchForTest(Expired, true); F.Paths->Tick(0);
	TestEqual(TEXT("Deadline delivered once"), Callbacks, 1);
	TestEqual(TEXT("Deadline reports expiry"), DeliveredStatus, EProjectJNPCPathStatus::Expired);
	TestEqual(TEXT("Timeout retains physical in-flight slot"), F.Paths->GetRequestCount(), 1);
	F.Paths->Complete(Cancelled, 1, ENavigationQueryResult::Fail, nullptr);
	TestEqual(TEXT("Old token does not release new request"), F.Paths->GetRequestCount(), 1);
	F.Paths->Complete(Expired, 1, ENavigationQueryResult::Fail, nullptr); F.Paths->Tick(0);
	TestEqual(TEXT("Expired job cannot deliver again"), Callbacks, 1);
	TestEqual(TEXT("Late expired completion releases slot"), F.Paths->GetRequestCount(), 0);
	const uint64 Teardown = F.Paths->Submit(F.Actions, F.NPC, FVector(1000, 0, 0), 4, [&Callbacks](const auto&) { ++Callbacks; });
	F.Paths->SimulateDispatchForTest(Teardown, false); F.Paths->OnWorldEndPlay(*F.World);
	F.Paths->Complete(Teardown, 1, ENavigationQueryResult::Fail, nullptr);
	TestEqual(TEXT("World end prevents late delivery"), Callbacks, 1);
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
	F.NPC->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	F.Actions->UpdateAction();
	TestTrue(TEXT("Real montage activates through the action consumer"), Ability->IsActive());
	TestEqual(TEXT("Movement remains walking; no flying override"), F.NPC->GetCharacterMovement()->MovementMode.GetValue(), MOVE_Walking);
	TestTrue(TEXT("Existing server hit validation starts"), Hit->GetActiveAttackDefinition() == Definition);
	F.NPC->GetAttributeSet()->InitHealth(0); F.Actions->UpdateAction();
	TestFalse(TEXT("Death stops the owned ability"), Ability->IsActive());
	TestNull(TEXT("Death closes the hit definition"), Hit->GetActiveAttackDefinition());
	TestEqual(TEXT("Animation visibility policy restored"), Mesh->VisibilityBasedAnimTickOption, Visibility);
	TestEqual(TEXT("URO restored"), Mesh->bEnableUpdateRateOptimizations != 0, bURO);
	F.NPC->GetAttributeSet()->InitHealth(100);
	F.Actions->StartActions(F.Scoring, Handle); F.Scoring->OnQueryCompleted.Broadcast(F.Target(100), 1);
	TestTrue(TEXT("Ability reusable after cancellation"), ASC->TryActivateAbility(Handle, false));
	F.NPC->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	TestFalse(TEXT("Leaving the ground cancels the ground attack"), Ability->IsActive());
	TestEqual(TEXT("Cleanup preserves the new falling mode"), F.NPC->GetCharacterMovement()->MovementMode.GetValue(), MOVE_Falling);
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
				Actions.Add(Action);
				NPC->GetAttributeSet()->InitHealth(0); // All consumers must eventually observe invalidation and self-unregister.
				Test->TestTrue(TEXT("Death is observable through the actual combat interface"), IProject_JCombatInterface::Execute_IsDead(NPC));
			}
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
#endif
