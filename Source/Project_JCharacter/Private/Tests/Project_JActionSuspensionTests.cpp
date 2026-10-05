#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Components/Project_JNPCActionComponent.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "Project_JNPCCharacter.h"
#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "TimerManager.h"
#include "Components/Project_JNPCActivationComponent.h"
#include "Combat/Project_JGameplayAbility_NPCAttack.h"
#include "Project_JAttributeSet.h"
#include "GameFramework/WorldSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJActionSuspensionTest, "ProjectJ.Maturity.NPC.ActionSuspension",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJActionSuspensionTest::RunTest(const FString&)
{
	auto* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { if (World->GetBegunPlay()) { World->EndPlay(EEndPlayReason::LevelTransition); } World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* NPC = World->SpawnActor<AProject_JNPCCharacter>();
	auto* AI = World->SpawnActor<AAIController>(); AI->Possess(NPC);
	NPC->GetAbilitySystemComponent()->InitAbilityActorInfo(NPC, NPC);
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
	NPC->GetAttributeSet()->InitMaxHealth(100); NPC->GetAttributeSet()->InitHealth(100);
	auto* Scoring = NewObject<UProject_JTargetScoringComponent>(NPC); Scoring->RegisterComponent();
	TestTrue(TEXT("Native scoring registration"), Scoring->StartBatchedNPCDecisions(1));
	auto* Action = NewObject<UProject_JNPCActionComponent>(NPC); Action->RegisterComponent();
	TestTrue(TEXT("Pursuit-only action starts"), Action->StartActions(Scoring, {}));
	FProjectJNPCActionSuspension A;
	TestTrue(TEXT("Movement owner can suspend its own action"), Action->SuspendActions(A));
	auto* Peer = NewObject<UProject_JNPCActionComponent>(NPC); Peer->RegisterComponent();
	TestFalse(TEXT("A disabled peer cannot claim a suspended owner's activation"), Peer->StartActions(Scoring, {}));
	Action->StopActions();
	TestTrue(TEXT("A later explicit Stop supersedes the recovery lease"),
		Action->ResumeSuspendedActions(A) == EProjectJNPCActionResume::Superseded);
	Action->StartActions(Scoring, {});
	FProjectJNPCActionSuspension B; Action->SuspendActions(B);
	Action->AttackRange = 0; // Temporary activation precondition; no fabricated scheduler callback.
	TestTrue(TEXT("A temporarily unavailable activation is deferred"),
		Action->ResumeSuspendedActions(B) == EProjectJNPCActionResume::Deferred);
	TestTrue(TEXT("Deferred recovery is owned by the original component"), Action->HasPendingResume());
	Action->AttackRange = 200;
	++GFrameCounter; World->GetTimerManager().Tick(0.3f);
	++GFrameCounter; World->GetTimerManager().Tick(0.3f);
	TestTrue(TEXT("The owned timer actually resumes the action"), Action->GetActionState() != EProjectJNPCActionState::Disabled);
	TestFalse(TEXT("Successful resume releases the timer contract"), Action->HasPendingResume());
	TestTrue(TEXT("A consumed suspension cannot run twice"), Action->ResumeSuspendedActions(B) == EProjectJNPCActionResume::Superseded);
	Action->StopActions(); Scoring->StopBatchedNPCDecisions();
	auto* Activation = NewObject<UProject_JNPCActivationComponent>(NPC); Activation->RegisterComponent();
	auto* Definition = NewObject<UProject_JNPCActivationDefinition>();
	Definition->AttackAbility = UProject_JGameplayAbility_NPCAttack::StaticClass();
	auto* ASC = NPC->GetAbilitySystemComponent();
	const auto ForeignGrant = ASC->GiveAbility(FGameplayAbilitySpec(Definition->AttackAbility));
	const int32 BaselineGrants = ASC->GetActivatableAbilities().Num();
	Action->AttackRange = 0;
	TestFalse(TEXT("Partial activation rolls back a failed action start"), Activation->ActivateNPC(Definition, Scoring, Action, 1));
	TestFalse(TEXT("Only the newly owned scoring registration is removed"), Scoring->IsBatchedNPCDecisionRegistered());
	TestEqual(TEXT("Failed activation rolls back only its own grant"), ASC->GetActivatableAbilities().Num(), BaselineGrants);
	Action->AttackRange = 200;
	TestTrue(TEXT("An explicit combat definition activates as one contract"), Activation->ActivateNPC(Definition, Scoring, Action, 1));
	TestEqual(TEXT("Successful activation owns one grant"), ASC->GetActivatableAbilities().Num(), BaselineGrants + 1);
	TestFalse(TEXT("Duplicate activation cannot create a second lease"), Activation->ActivateNPC(Definition, Scoring, Action, 1));
	FProjectJNPCActionSuspension C; Action->SuspendActions(C);
	Activation->DeactivateNPC();
	TestTrue(TEXT("Deactivation supersedes a Mass recovery ticket"), Action->ResumeSuspendedActions(C) == EProjectJNPCActionResume::Superseded);
	TestFalse(TEXT("Deactivation releases its scoring registration"), Scoring->IsBatchedNPCDecisionRegistered());
	TestNotNull(TEXT("Deactivation preserves a pre-existing ability grant"), ASC->FindAbilitySpecFromHandle(ForeignGrant));
	TestEqual(TEXT("Deactivation releases only its own ability grant"), ASC->GetActivatableAbilities().Num(), BaselineGrants);
	Scoring->StartBatchedNPCDecisions(2); Action->StartActions(Scoring, {});
	Activation->DeactivateNPC();
	TestTrue(TEXT("Repeated deactivation leaves another owner's action running"), Action->GetActionState() != EProjectJNPCActionState::Disabled);
	Action->StopActions(); Scoring->StopBatchedNPCDecisions();
	return true;
}
#endif
