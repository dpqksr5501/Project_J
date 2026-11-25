#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Combat/Project_JServerSideRewindComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/Project_JCommandBudgetComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Game/Project_JPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Mount/Project_JMountCharacter.h"
#include "Network/Project_JNetObjectPrioritizer_Combat.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Project_JGameplayTags.h"
#include "Project_JNPCCharacter.h"
#include "Project_JPlayerCharacter.h"
#include "Tests/Project_JMountLifecycleFixture.h"
#include "World/Project_JWorldEnvironment.h"
#include "World/Project_JWorldState.h"

namespace ProjectJPhase0HardeningTests
{
struct FTestWorldScope
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FTestWorldScope()
	{
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->GetWorldSettings()->NotifyBeginPlay();
		World->GetWorldSettings()->NotifyMatchStarted();
	}
	~FTestWorldScope()
	{
		World->EndPlay(EEndPlayReason::LevelTransition);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
	template<typename T>
	T* Spawn(const FVector& Location = FVector::ZeroVector)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<T>(Location, FRotator::ZeroRotator, Params);
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPhase0DeadStateTagTest, "ProjectJ.Hardening.Phase0.DeadStateTag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPhase0DeadStateTagTest::RunTest(const FString&)
{
	ProjectJPhase0HardeningTests::FTestWorldScope Scope;
	AProject_JNPCCharacter* NPC = Scope.Spawn<AProject_JNPCCharacter>();
	if (!TestNotNull(TEXT("NPC spawned"), NPC)) return false;
	UProject_JAbilitySystemComponent* ASC = Cast<UProject_JAbilitySystemComponent>(NPC->GetAbilitySystemComponent());
	UProject_JAttributeSet* Attrs = NPC->GetAttributeSet();
	if (!TestNotNull(TEXT("NPC ASC valid"), ASC) || !TestNotNull(TEXT("NPC AttributeSet valid"), Attrs)) return false;

	const FGameplayTag DeadTag = FProject_JGameplayTags::Get().State_Dead;
	Attrs->InitMaxHealth(100.f);
	Attrs->SetHealth(100.f);
	TestFalse(TEXT("Alive character does not have State_Dead"), ASC->HasMatchingGameplayTag(DeadTag));

	Attrs->SetHealth(0.f);
	TestTrue(TEXT("Zero-health character automatically receives State_Dead"), ASC->HasMatchingGameplayTag(DeadTag));
	TestTrue(TEXT("IsDead matches zero health"), IProject_JCombatInterface::Execute_IsDead(NPC));

	Attrs->SetHealth(50.f);
	TestFalse(TEXT("Revived character clears State_Dead"), ASC->HasMatchingGameplayTag(DeadTag));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPhase0CommandBudgetMountedTest, "ProjectJ.Hardening.Phase0.CommandBudgetMounted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPhase0CommandBudgetMountedTest::RunTest(const FString&)
{
	ProjectJPhase0HardeningTests::FTestWorldScope Scope;
	APlayerController* PC = Scope.Spawn<APlayerController>();
	AProject_JPlayerState* PS = Scope.Spawn<AProject_JPlayerState>();
	AProject_JNPCCharacter* Rider = Scope.Spawn<AProject_JNPCCharacter>();
	AProject_JMountLifecycleFixture* Mount = Scope.Spawn<AProject_JMountLifecycleFixture>();
	if (!TestNotNull(TEXT("PC"), PC) || !TestNotNull(TEXT("PS"), PS) || !TestNotNull(TEXT("Rider"), Rider) || !TestNotNull(TEXT("Mount"), Mount))
		return false;

	PC->PlayerState = PS;
	PS->SetOwner(PC);
	PC->Possess(Mount);
	Rider->AttachToActor(Mount, FAttachmentTransformRules::KeepWorldTransform);

	auto* Budget = PS->FindComponentByClass<UProject_JCommandBudgetComponent>();
	if (!TestNotNull(TEXT("CommandBudget present on PlayerState"), Budget)) return false;
	Budget->CommandsPerSecond = 2;

	TestTrue(TEXT("Mounted rider first command admitted through mount PlayerState budget"), UProject_JCommandBudgetComponent::AdmitClientCommand(Rider));
	TestTrue(TEXT("Mounted rider second command admitted"), UProject_JCommandBudgetComponent::AdmitClientCommand(Rider));
	TestFalse(TEXT("Mounted rider cannot bypass exhausted budget while unpossessed"), UProject_JCommandBudgetComponent::AdmitClientCommand(Rider));
	TestFalse(TEXT("Direct PlayerController call also respects exhausted budget"), UProject_JCommandBudgetComponent::AdmitClientCommand(PC));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPhase0CombatPrioritizerDeadTest, "ProjectJ.Hardening.Phase0.CombatPrioritizerDead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPhase0CombatPrioritizerDeadTest::RunTest(const FString&)
{
	ProjectJPhase0HardeningTests::FTestWorldScope Scope;
	AProject_JNPCCharacter* NPC = Scope.Spawn<AProject_JNPCCharacter>();
	if (!TestNotNull(TEXT("NPC spawned"), NPC)) return false;
	UProject_JAttributeSet* Attrs = NPC->GetAttributeSet();
	if (!TestNotNull(TEXT("AttributeSet"), Attrs)) return false;

	Attrs->InitMaxHealth(100.f);
	Attrs->SetHealth(100.f);
	Attrs->SetHealth(0.f);

	UProject_JNetObjectPrioritizer_Combat* Prioritizer = NewObject<UProject_JNetObjectPrioritizer_Combat>();
	const float ExpectedMultiplier = Prioritizer->CalculatePriorityMultiplier(NPC);
	TestTrue(TEXT("Dead character priority multiplier is below 1.0"), ExpectedMultiplier > 0.0f && ExpectedMultiplier < 1.0f);
	FProject_JReplicationPolicyDecision Decision;
	Decision.PriorityMultiplier = 1.0f;
	Decision = Prioritizer->ApplyCombatPriority(NPC, Decision);
	TestTrue(TEXT("Dead character priority is attenuated below 1.0"),
		FMath::IsNearlyEqual(Decision.PriorityMultiplier, ExpectedMultiplier, 1e-4f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPhase0SSRLowLatencyGapTest, "ProjectJ.Hardening.Phase0.SSRLowLatencyGap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPhase0SSRLowLatencyGapTest::RunTest(const FString&)
{
	ProjectJPhase0HardeningTests::FTestWorldScope Scope;
	ACharacter* Target = Scope.Spawn<ACharacter>(FVector(0.0, 0.0, 100.0));
	if (!TestNotNull(TEXT("Target spawned"), Target)) return false;
	auto* SSR = NewObject<UProject_JServerSideRewindComponent>(Target);
	Target->AddInstanceComponent(SSR);
	SSR->RegisterComponent();
	if (!TestNotNull(TEXT("SSR component"), SSR)) return false;

	// Step once to record an initial sample at WorldTime = T0.
	Scope.World->Tick(LEVELTICK_All, 0.05f);
	double Oldest = 0.0, Newest = 0.0;
	if (!TestTrue(TEXT("SSR recorded initial sample"), SSR->TryGetHistoryTimeBounds(Oldest, Newest))) return false;

	// Advance 15ms (< 33.33ms 30Hz interval) and move target 100cm on X without triggering the next 30Hz record.
	Target->SetActorLocation(FVector(100.0, 0.0, 100.0));
	Scope.World->Tick(LEVELTICK_All, 0.015f);
	const double Now = Scope.World->GetTimeSeconds();
	double Oldest2 = 0.0, Newest2 = 0.0;
	SSR->TryGetHistoryTimeBounds(Oldest2, Newest2);

	const double QueryTime = (Newest2 + Now) * 0.5;
	const FVector ExpectedCenter = FMath::Lerp(FVector(0.0, 0.0, 100.0), Target->GetCapsuleComponent()->GetComponentLocation(), 0.5f);
	FVector RewoundLocation = FVector::ZeroVector;
	const bool bHit = SSR->ServerVerifyHitWithRewoundLocation(
		QueryTime, ExpectedCenter - FVector(0.0, 50.0, 0.0), ExpectedCenter + FVector(0.0, 50.0, 0.0), 15.0f, RewoundLocation);
	TestTrue(TEXT("Low-latency intra-tick hit between newest 30Hz sample and Now is accepted"), bHit);

	FVector UnusedLocation;
	const bool bFutureHit = SSR->ServerVerifyHitWithRewoundLocation(
		Now + 0.1, ExpectedCenter - FVector(0.0, 50.0, 0.0), ExpectedCenter + FVector(0.0, 50.0, 0.0), 15.0f, UnusedLocation);
	TestFalse(TEXT("Future timestamp beyond tolerance is still rejected"), bFutureHit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPhase0WorldEnvironmentSunSnapTest, "ProjectJ.Hardening.Phase0.WorldEnvironmentSunSnap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPhase0WorldEnvironmentSunSnapTest::RunTest(const FString&)
{
	ProjectJPhase0HardeningTests::FTestWorldScope Scope;
	auto* WorldState = Scope.Spawn<AProject_JWorldState>();
	auto* Sun = Scope.Spawn<ADirectionalLight>();
	auto* Env = Scope.Spawn<AProject_JWorldEnvironment>();
	if (!TestNotNull(TEXT("WorldState"), WorldState) || !TestNotNull(TEXT("Sun"), Sun) || !TestNotNull(TEXT("Env"), Env))
		return false;

	Sun->SetMobility(EComponentMobility::Movable);
	Env->State = WorldState;
	Env->Sun = Sun;
	Env->SunRotationStepDegrees = 0.5f;

	WorldState->ClockSettings.DayLengthSeconds = 2400.f;
	WorldState->ClockSettings.InitialHour = 10.f;
	WorldState->ClockStart = WorldState->GetSynchronizedSeconds();
	Env->Tick(0.1f);
	const FRotator InitialRot = Sun->GetActorRotation();

	// Micro time step (0.1s -> 0.015 deg) should not mutate sun rotation and invalidate VSM static cache.
	WorldState->ClockStart -= 0.1;
	Env->Tick(0.1f);
	TestEqual(TEXT("Sub-threshold tick preserves exact sun rotation for VSM cache"), Sun->GetActorRotation(), InitialRot);

	// Large time advance (120s -> 18 deg > 0.5 deg) updates sun rotation.
	WorldState->ClockStart -= 120.0;
	Env->Tick(0.1f);
	TestNotEqual(TEXT("Super-threshold advance rotates sun"), Sun->GetActorRotation(), InitialRot);
	return true;
}
#endif
