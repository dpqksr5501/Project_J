#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/Project_JMountRefinementFixtures.h"

#include "EnhancedInputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Mount/Project_JMountAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMountTickLifecycleTest, "ProjectJ.Internal.Mount.FlightTickLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMountTickLifecycleTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->EndPlay(EEndPlayReason::LevelTransition);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	World->GetWorldSettings()->NotifyMatchStarted();
	AProject_JMountRefinementFixture* Mount = World->SpawnActor<AProject_JMountRefinementFixture>();
	if (!TestNotNull(TEXT("Native mount fixture"), Mount)) return false;
	TestFalse(TEXT("Grounded mount does not schedule a flight actor tick"), Mount->IsActorTickEnabled());
	TestTrue(TEXT("Movement component remains enabled independently"), Mount->GetCharacterMovement()->IsComponentTickEnabled());
	Mount->SetTestFlightState(EProject_JMountFlightState::TakingOff);
	TestTrue(TEXT("Authority takeoff activates state updates"), Mount->IsActorTickEnabled());
	Mount->SetTestFlightState(EProject_JMountFlightState::Flying);
	TestTrue(TEXT("Authority flight retains landing detection"), Mount->IsActorTickEnabled());
	Mount->SetTestFlightState(EProject_JMountFlightState::Grounded);
	TestFalse(TEXT("Landing completion releases native tick"), Mount->IsActorTickEnabled());
	Mount->SetTestBlueprintTick(true);
	TestTrue(TEXT("Blueprint Event Tick policy survives grounding"), Mount->IsActorTickEnabled());
	Mount->SetTestBlueprintTick(false);
	Mount->SetTestKeepTick(true);
	TestTrue(TEXT("Native subclass explicit tick requirement is preserved"), Mount->IsActorTickEnabled());
	Mount->SetTestKeepTick(false);
	Mount->SetTestRole(ROLE_SimulatedProxy);
	Mount->ReceiveTestFlightState(EProject_JMountFlightState::Flying);
	TestFalse(TEXT("Remote presentation does not schedule authority state updates"), Mount->IsActorTickEnabled());
	Mount->ReceiveTestFlightState(EProject_JMountFlightState::Grounded);
	Mount->SetTestPendingRequest(World->GetTimeSeconds());
	TestTrue(TEXT("Pending local request wakes timeout processing"), Mount->IsActorTickEnabled());
	Mount->Tick(0.016f);
	TestFalse(TEXT("Rejected request timeout releases input lock"), Mount->IsFlightInputLocked());
	TestFalse(TEXT("Rejected request timeout releases actor tick"), Mount->IsActorTickEnabled());
	Mount->SetTestPendingRequest(World->GetTimeSeconds() + 100.0f);
	Mount->ReceiveTestFlightState(EProject_JMountFlightState::Flying);
	TestFalse(TEXT("Server acknowledgment clears pending input lock"), Mount->IsFlightInputLocked());
	TestFalse(TEXT("Server acknowledgment releases client timeout tick"), Mount->IsActorTickEnabled());
	Mount->ReceiveTestFlightState(EProject_JMountFlightState::Grounded);
	Mount->SetTestPendingRequest(World->GetTimeSeconds() + 100.0f);
	Mount->NotifyTestControllerChanged();
	TestFalse(TEXT("Losing local controller clears obsolete request"), Mount->IsFlightInputLocked());
	TestFalse(TEXT("Losing local controller releases timeout tick"), Mount->IsActorTickEnabled());
	Mount->SetTestRole(ROLE_Authority);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMountInputOwnershipTest, "ProjectJ.Internal.Mount.InputBindingOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMountInputOwnershipTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AProject_JMountRefinementFixture* Mount = World->SpawnActor<AProject_JMountRefinementFixture>();
	if (!TestNotNull(TEXT("Native mount fixture"), Mount)) return false;
	UInputAction* Action = NewObject<UInputAction>(Mount);
	Mount->SetTestMoveAction(Action);
	UEnhancedInputComponent* FirstInput = NewObject<UEnhancedInputComponent>(Mount);
	FirstInput->BindAction(Action, ETriggerEvent::Completed, Mount, &AProject_JMountRefinementFixture::UnrelatedInput);
	Mount->SetupPlayerInputComponent(FirstInput);
	Mount->SetupPlayerInputComponent(FirstInput);
	TestEqual(TEXT("Repeated setup keeps exactly one native binding and the unrelated binding"), FirstInput->GetActionEventBindings().Num(), 2);
	UEnhancedInputComponent* SecondInput = NewObject<UEnhancedInputComponent>(Mount);
	Mount->SetupPlayerInputComponent(SecondInput);
	TestEqual(TEXT("Replacing input component removes only owned bindings from old component"), FirstInput->GetActionEventBindings().Num(), 1);
	TestEqual(TEXT("Replacement component receives one binding"), SecondInput->GetActionEventBindings().Num(), 1);
	Mount->NotifyTestUnPossessed();
	TestEqual(TEXT("Unpossessing clears native input bindings"), SecondInput->GetActionEventBindings().Num(), 0);
	Mount->SetupPlayerInputComponent(SecondInput);
	TestEqual(TEXT("Repossessing can rebuild bindings"), SecondInput->GetActionEventBindings().Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMountAnimationOwnerLossTest, "ProjectJ.Internal.Mount.AnimationOwnerLoss",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMountAnimationOwnerLossTest::RunTest(const FString&)
{
	USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>();
	UProject_JMountAnimRefinementFixture* Anim = NewObject<UProject_JMountAnimRefinementFixture>(Mesh);
	Anim->Speed = 100.0f;
	Anim->VerticalSpeed = -50.0f;
	Anim->bIsFalling = Anim->bIsFlying = Anim->bIsGliding = Anim->bIsTakingOff = Anim->bIsAutoAscending = Anim->bIsLanding = true;
	Anim->NativeUpdateAnimation(0.016f);
	TestEqual(TEXT("Owner loss clears speed"), Anim->Speed, 0.0f);
	TestEqual(TEXT("Owner loss clears vertical speed"), Anim->VerticalSpeed, 0.0f);
	TestFalse(TEXT("Owner loss clears movement and flight presentation"),
		Anim->bIsFalling || Anim->bIsFlying || Anim->bIsGliding || Anim->bIsTakingOff || Anim->bIsAutoAscending || Anim->bIsLanding);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMountHealthAuthorityTest, "ProjectJ.Internal.Mount.HealthSingleSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMountHealthAuthorityTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->EndPlay(EEndPlayReason::LevelTransition);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	World->GetWorldSettings()->NotifyMatchStarted();
	AProject_JMountRefinementFixture* Mount = World->SpawnActorDeferred<AProject_JMountRefinementFixture>(
		AProject_JMountRefinementFixture::StaticClass(), FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("Native mount fixture"), Mount)) return false;
	Mount->SetTestInitialHealth(250.0f, 100.0f);
	Mount->FinishSpawning(FTransform::Identity);
	UAbilitySystemComponent* ASC = Mount->GetAbilitySystemComponent();
	UProject_JMountAttributeSet* Attributes = const_cast<UProject_JMountAttributeSet*>(ASC->GetSet<UProject_JMountAttributeSet>());
	if (!TestNotNull(TEXT("Mount attributes registered with ASC"), Attributes)) return false;
	TestEqual(TEXT("Initial health clamps to authored maximum"), Mount->GetHealth(), 100.0f);
	TestTrue(TEXT("Direct damage accepted"), Mount->ApplyMountDamage(30.0f));
	TestEqual(TEXT("Direct damage writes GAS health"), Attributes->GetHealth(), 70.0f);
	TestEqual(TEXT("Compatibility getter follows GAS damage"), Mount->GetHealth(), 70.0f);
	TestFalse(TEXT("Negative damage rejected"), Mount->ApplyMountDamage(-1.0f));
	TestFalse(TEXT("Non-finite damage rejected"), Mount->ApplyMountDamage(std::numeric_limits<float>::quiet_NaN()));
	auto ApplyHealthEffect = [&](float Magnitude)
	{
		UGameplayEffect* Effect = NewObject<UGameplayEffect>(Mount);
		Effect->DurationPolicy = EGameplayEffectDurationType::Instant;
		FGameplayModifierInfo& Modifier = Effect->Modifiers.AddDefaulted_GetRef();
		Modifier.Attribute = UProject_JMountAttributeSet::GetHealthAttribute();
		Modifier.ModifierOp = EGameplayModOp::Additive;
		Modifier.ModifierMagnitude = FScalableFloat(Magnitude);
		ASC->ApplyGameplayEffectToSelf(Effect, 1.0f, ASC->MakeEffectContext());
	};
	ApplyHealthEffect(-20.0f);
	TestEqual(TEXT("Gameplay effect updates the same compatibility health"), Mount->GetHealth(), 50.0f);
	ApplyHealthEffect(500.0f);
	TestEqual(TEXT("Healing effect cannot exceed maximum"), Mount->GetHealth(), 100.0f);
	Attributes->SetMaxHealth(40.0f);
	TestEqual(TEXT("Maximum reduction clamps existing health"), Mount->GetHealth(), 40.0f);
	TestEqual(TEXT("Maximum mirror follows GAS"), Mount->GetMaxHealth(), 40.0f);
	ApplyHealthEffect(-1000.0f);
	TestEqual(TEXT("Lethal effect clamps health to zero"), Mount->GetHealth(), 0.0f);
	TestEqual(TEXT("Lethal effect enters death handling once"), Mount->HealthDepletionCount, 1);
	TestTrue(TEXT("Reentrant damage during death handling is rejected"), Mount->bRejectedReentrantDamage);
	ApplyHealthEffect(-10.0f);
	TestFalse(TEXT("Repeated direct damage on dead mount is rejected"), Mount->ApplyMountDamage(5.0f));
	TestEqual(TEXT("Repeated depletion does not re-enter death handling"), Mount->HealthDepletionCount, 1);
	Attributes->SetHealth(10.0f);
	Mount->ApplyMountDamage(20.0f);
	TestEqual(TEXT("A revived mount can have a new death transition"), Mount->HealthDepletionCount, 2);
	Attributes->SetMaxHealth(-1.0f);
	TestEqual(TEXT("Invalid maximum clamps to one"), Mount->GetMaxHealth(), 1.0f);
	Attributes->SetMaxHealth(100.0f);
	Mount->SetTestRole(ROLE_SimulatedProxy);
	const FGameplayAttributeData PreviousHealth = Attributes->Health;
	Attributes->InitHealth(25.0f);
	Attributes->OnRep_Health(PreviousHealth);
	TestEqual(TEXT("Attribute replication updates the compatibility mirror"), Mount->GetHealth(), 25.0f);
	TestFalse(TEXT("Client cannot apply authoritative damage"), Mount->ApplyMountDamage(5.0f));
	TestEqual(TEXT("Replication does not execute authority death handling"), Mount->HealthDepletionCount, 2);
	Mount->SetTestRole(ROLE_Authority);
	Mount->Destroy();
	TestFalse(TEXT("EndPlay releases the owned health delegate"), ASC->GetGameplayAttributeValueChangeDelegate(UProject_JMountAttributeSet::GetHealthAttribute()).IsBoundToObject(Mount));
	TestFalse(TEXT("EndPlay releases the owned maximum delegate"), ASC->GetGameplayAttributeValueChangeDelegate(UProject_JMountAttributeSet::GetMaxHealthAttribute()).IsBoundToObject(Mount));
	return true;
}
#endif
