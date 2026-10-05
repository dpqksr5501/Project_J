#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/Project_JMountRefinementFixtures.h"

#include "EnhancedInputComponent.h"
#include "InputAction.h"
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
#include "GameFramework/PlayerController.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Tests/Project_JMountLifecycleFixture.h"
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
	Mount->SetTestInteractAction(Action);
	Mount->SetupPlayerInputComponent(SecondInput);
	Mount->SetupPlayerInputComponent(SecondInput);
	TestEqual(TEXT("Flying mount inherits exactly one common Started dismount binding beside its Move binding"), SecondInput->GetActionEventBindings().Num(), 2);
	Mount->NotifyTestUnPossessed();
	TestEqual(TEXT("Flying/base cleanup jointly retires only their owned bindings"), SecondInput->GetActionEventBindings().Num(), 0);
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJLandingRecoveryTest, "ProjectJ.Maturity.Mount.LandingRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJLandingRecoveryTest::RunTest(const FString&)
{
	auto* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Mount = World->SpawnActor<AProject_JMountRefinementFixture>();
	Mount->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	Mount->SetTestFlightState(EProject_JMountFlightState::Flying);
	TestTrue(TEXT("Landing is accepted from flight"), Mount->EndFlight());
	TestTrue(TEXT("Landing protects input until recovery"), Mount->IsFlightInputLocked());
	Mount->StepTestLanding(1); Mount->StepTestLanding(1);
	TestTrue(TEXT("No spatial progress returns to flying"), Mount->GetFlightState() == EProject_JMountFlightState::Flying);
	TestTrue(TEXT("Blocked landing reports its cause"), Mount->GetLastLandingFailure() == EProject_JMountLandingFailure::Blocked);
	TestFalse(TEXT("Recovery returns flight input"), Mount->IsFlightInputLocked());
	TestTrue(TEXT("Recovery retains flying movement"), Mount->GetCharacterMovement()->IsFlying());
	Mount->SetTestLandingLimits(10, 1);
	Mount->EndFlight(); Mount->SetActorLocation(FVector(0, 0, -100)); Mount->StepTestLanding(1.1f);
	TestTrue(TEXT("Progress does not bypass the total landing deadline"), Mount->GetLastLandingFailure() == EProject_JMountLandingFailure::TimedOut);
	Mount->EndFlight();
	TestTrue(TEXT("An unfinished attempt can be explicitly cancelled"), Mount->CancelLanding());
	TestFalse(TEXT("Duplicate cancellation is harmless"), Mount->CancelLanding());
	Mount->SetTestRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("Clients cannot cancel authority landing"), Mount->CancelLanding());
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJDismountFloorHeightTest, "ProjectJ.PivotMountFollowup.Mount.UnequalCapsuleFloorExit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJDismountFloorHeightTest::RunTest(const FString&)
{
	auto* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* FloorActor = World->SpawnActor<AActor>(Params);
	auto* Floor = NewObject<UBoxComponent>(FloorActor); FloorActor->SetRootComponent(Floor);
	Floor->SetBoxExtent(FVector(3000, 3000, 10)); Floor->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Floor->SetCollisionObjectType(ECC_WorldStatic); Floor->SetCollisionResponseToAllChannels(ECR_Block); Floor->RegisterComponent();
	Floor->SetWorldLocation(FVector(0, 0, -10));
	for (int32 Kind = 0; Kind < 2; ++Kind)
	{
		AProject_JMountCharacter* Mount = Kind == 0
			? static_cast<AProject_JMountCharacter*>(World->SpawnActor<AProject_JMountLifecycleFixture>(Params))
			: static_cast<AProject_JMountCharacter*>(World->SpawnActor<AProject_JMountRefinementFixture>(Params));
		auto* Rider = World->SpawnActor<ACharacter>(Params);
		auto* Controller = World->SpawnActor<APlayerController>(Params);
		Controller->SetAsLocalPlayerController(); Controller->Possess(Rider);
		Mount->GetCapsuleComponent()->SetCapsuleSize(30, 40);
		Mount->SetActorLocation(FVector(Kind * 1000, 0, 40));
		Mount->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Rider->GetCapsuleComponent()->SetCapsuleSize(34, 96);
		Rider->SetActorLocation(FVector(Kind * 1000, 0, 96));
		auto* Action = NewObject<UInputAction>();
		if (Kind == 0) { CastChecked<AProject_JMountLifecycleFixture>(Mount)->SetTestInteractAction(Action); }
		else { CastChecked<AProject_JMountRefinementFixture>(Mount)->SetTestInteractAction(Action); }
		TestTrue(TEXT("Ground/flight mount session starts with unequal capsule heights"), Mount->TryMountRider(Rider));
		auto* Input = NewObject<UEnhancedInputComponent>(Mount); Mount->SetupPlayerInputComponent(Input);
		if (!TestEqual(TEXT("Ground/flight mount binds the dismount action"), Input->GetActionEventBindings().Num(), 1)) { return false; }
		auto Dispatch = Input->GetActionEventBindings()[0]->Clone();
		FInputActionInstance Instance(Action); Dispatch->Execute(Instance);
		TestTrue(TEXT("F action returns possession on a flat floor despite shorter mount capsule"), Controller->GetPawn() == Rider);
		TestTrue(TEXT("Rider capsule rests above the floor"), Rider->GetActorLocation().Z >= 96.0f);
		TestFalse(TEXT("Exit clears rider ownership"), Mount->GetRider() != nullptr);
		Controller->Destroy(); Rider->Destroy(); Mount->Destroy();
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMountRetainedInputRecoveryTest, "ProjectJ.MountRemount.RetainedInputRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMountRetainedInputRecoveryTest::RunTest(const FString&)
{
	auto* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	struct FMoveInstance : FInputActionInstance
	{
		explicit FMoveInstance(const UInputAction* Action) : FInputActionInstance(Action)
		{ TriggerEvent = ETriggerEvent::Triggered; Value = FInputActionValue(FVector2D(0, 1)); }
	};
	for (int32 Kind = 0; Kind < 2; ++Kind)
	{
		auto* Ground = Kind == 0 ? World->SpawnActor<AProject_JMountLifecycleFixture>(Params) : nullptr;
		auto* Flight = Kind == 1 ? World->SpawnActor<AProject_JMountRefinementFixture>(Params) : nullptr;
		AProject_JMountCharacter* Mount = Ground ? static_cast<AProject_JMountCharacter*>(Ground) : Flight;
		auto* Rider = World->SpawnActor<ACharacter>(Params);
		auto* Controller = World->SpawnActor<APlayerController>(Params); Controller->SetAsLocalPlayerController(); Controller->Possess(Rider);
		auto* Interact = NewObject<UInputAction>();
		auto* Move = NewObject<UInputAction>(); Move->ValueType = EInputActionValueType::Axis2D;
		if (Ground) { Ground->SetTestInteractAction(Interact); }
		else { Flight->SetTestInteractAction(Interact); Flight->SetTestFlightActions(Move, NewObject<UInputAction>(), NewObject<UInputAction>(), NewObject<UInputAction>()); }
		Mount->SetActorLocation(FVector(Kind * 1000, 0, 100)); Rider->SetActorLocation(Mount->GetActorLocation());
		TestTrue(TEXT("Session starts"), Mount->TryMountRider(Rider));
		auto* Input = NewObject<UEnhancedInputComponent>(Mount);
		Input->BindAction(Interact, ETriggerEvent::Completed, Mount, &AActor::ForceNetUpdate);
		if (Ground) { Ground->SetTestInputComponent(Input); }
		else { Flight->SetTestInputComponent(Input); }
		Mount->SetupPlayerInputComponent(Input);
		const int32 ExpectedBindings = Ground ? 2 : 7;
		for (int32 Cycle = 0; Cycle < 3; ++Cycle)
		{
			// A replicated Controller loss does not execute authority UnPossessed
			// and therefore retains the client's InputComponent.
			Mount->SetController(nullptr);
			if (Ground) { Ground->NotifyTestControllerChanged(); }
			else { Flight->NotifyTestControllerChanged(); }
			TestEqual(TEXT("Controller loss removes owned bindings only"), Input->GetActionEventBindings().Num(), 1);
			TestTrue(TEXT("Client keeps the same input component"), Mount->InputComponent == Input);
			Mount->SetController(Controller); Controller->SetPawn(Mount);
			if (Cycle == 0)
			{
				if (Ground) { Ground->NotifyTestControllerChanged(); }
				else { Flight->NotifyTestControllerChanged(); }
			}
			if (Ground) { Ground->NotifyTestClientRestart(); Ground->NotifyTestClientRestart(); }
			else { Flight->NotifyTestClientRestart(); Flight->NotifyTestClientRestart(); }
			TestEqual(TEXT("Reused component restores all bindings without duplicates"), Input->GetActionEventBindings().Num(), ExpectedBindings);
			TestEqual(TEXT("Engine skips Setup when it reuses the input component"), Ground ? Ground->InputSetupCount : Flight->InputSetupCount, 1);
			if (Flight)
			{
				Mount->ConsumeMovementInputVector();
				for (const auto& Binding : Input->GetActionEventBindings())
				{
					if (Binding->GetAction() == Move && Binding->GetTriggerEvent() == ETriggerEvent::Triggered)
					{ auto Dispatch = Binding->Clone(); FMoveInstance Instance(Move); Dispatch->Execute(Instance); }
				}
				TestTrue(TEXT("Recovered WASD dispatch produces movement input"), Mount->GetPendingMovementInputVector().SizeSquared2D() > 0);
			}
		}
		TUniquePtr<FEnhancedInputActionEventBinding> Exit;
		for (const auto& Binding : Input->GetActionEventBindings())
		{
			if (Binding->GetAction() == Interact && Binding->GetTriggerEvent() == ETriggerEvent::Started) { Exit = Binding->Clone(); break; }
		}
		if (!TestTrue(TEXT("Recovered F binding exists"), Exit.IsValid())) { return false; }
		FInputActionInstance ExitInstance(Interact); Exit->Execute(ExitInstance);
		TestTrue(TEXT("Recovered F dispatch restores rider possession"), Controller->GetPawn() == Rider);
		TestTrue(TEXT("Authority unpossess destroys its old input component"), Mount->InputComponent == nullptr);
		TestTrue(TEXT("Rider can actually remount after dismount"), Mount->TryMountRider(Rider));
		if (Ground) { Ground->NotifyTestClientRestart(); }
		else { Flight->NotifyTestClientRestart(); }
		auto* NewInput = Cast<UEnhancedInputComponent>(Mount->InputComponent);
		if (!TestNotNull(TEXT("Restart creates a fresh component when old one was destroyed"), NewInput)) { return false; }
		TestEqual(TEXT("Fresh component has one set of native bindings"), NewInput->GetActionEventBindings().Num(), ExpectedBindings - 1);
		TestTrue(TEXT("Second session can also dismount"), Mount->DismountRider(false));
		Controller->Destroy(); Rider->Destroy(); Mount->Destroy();
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMountDismountMotionTest, "ProjectJ.MountDismountMotion.SuccessAndRejection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMountDismountMotionTest::RunTest(const FString&)
{
	auto* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 Kind = 0; Kind < 2; ++Kind)
	{
		auto* Ground = Kind == 0 ? World->SpawnActor<AProject_JMountLifecycleFixture>(Params) : nullptr;
		auto* Flight = Kind == 1 ? World->SpawnActor<AProject_JMountRefinementFixture>(Params) : nullptr;
		AProject_JMountCharacter* Mount = Ground ? static_cast<AProject_JMountCharacter*>(Ground) : Flight;
		auto* Rider = World->SpawnActor<ACharacter>(Params);
		auto* Controller = World->SpawnActor<APlayerController>(Params); Controller->SetAsLocalPlayerController(); Controller->Possess(Rider);
		Mount->SetActorLocation(FVector(Kind * 1000, 0, 100)); Rider->SetActorLocation(Mount->GetActorLocation());
		auto* Movement = Mount->GetCharacterMovement();
		Movement->bRunPhysicsWithNoController = true;
		Movement->SetMovementMode(Flight ? MOVE_Flying : MOVE_Walking);
		if (Flight) { Flight->SetTestFlightState(EProject_JMountFlightState::Flying); }
		TestTrue(TEXT("Moving mount session starts"), Mount->TryMountRider(Rider));
		// Possession can restart default walking; enter flight after that hook.
		Movement->SetMovementMode(Flight ? MOVE_Flying : MOVE_Walking);
		const FVector MovingVelocity(500, 100, Flight ? 50 : 0);
		Movement->Velocity = MovingVelocity;
		Mount->AddMovementInput(FVector::ForwardVector);
		const FVector Pending = Mount->GetPendingMovementInputVector();
		if (Flight)
		{
			TestFalse(TEXT("Air dismount remains rejected when disabled"), Mount->DismountRider(false));
			Flight->SetTestAllowAirDismount(true);
		}
		else
		{
			auto* Obstacle = World->SpawnActor<AActor>(Params);
			auto* Box = NewObject<UBoxComponent>(Obstacle); Obstacle->SetRootComponent(Box);
			Box->SetBoxExtent(FVector(2000)); Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent(); Box->SetWorldLocation(Mount->GetActorLocation());
			TestFalse(TEXT("Blocked dismount remains rejected"), Mount->DismountRider(false));
			Obstacle->Destroy();
		}
		TestTrue(TEXT("Rejected exit preserves velocity"), Movement->Velocity.Equals(MovingVelocity));
		TestTrue(TEXT("Rejected exit preserves pending movement input"), Mount->GetPendingMovementInputVector().Equals(Pending));
		TestTrue(TEXT("Rejected exit preserves possession"), Controller->GetPawn() == Mount);
		TestTrue(TEXT("Valid exit succeeds"), Mount->DismountRider(false));
		TestTrue(TEXT("Successful exit clears mount velocity"), Movement->Velocity.IsNearlyZero());
		TestTrue(TEXT("Successful exit clears acceleration"), Movement->GetCurrentAcceleration().IsNearlyZero());
		TestTrue(TEXT("Successful exit clears queued input"), Mount->GetPendingMovementInputVector().IsNearlyZero());
		TestEqual(TEXT("Exit preserves movement mode"), Movement->MovementMode.GetValue(), Flight ? MOVE_Flying : MOVE_Walking);
		TestTrue(TEXT("Exit returns rider possession"), Controller->GetPawn() == Rider);
		if (Flight) { TestEqual(TEXT("Exit preserves authored flight state"), Flight->GetFlightState(), EProject_JMountFlightState::Flying); }
		TestTrue(TEXT("Mount can be reused"), Mount->TryMountRider(Rider));
		Mount->AddMovementInput(FVector::RightVector);
		TestTrue(TEXT("New rider input works after remount"), !Mount->GetPendingMovementInputVector().IsNearlyZero());
		Movement->Velocity = MovingVelocity;
		TestTrue(TEXT("Forced lifecycle exit also succeeds"), Mount->DismountRider(true));
		TestTrue(TEXT("Forced exit retires movement"), Movement->Velocity.IsNearlyZero() && Mount->GetPendingMovementInputVector().IsNearlyZero());
		Controller->Destroy(); Rider->Destroy(); Mount->Destroy();
	}
	return true;
}
#endif
