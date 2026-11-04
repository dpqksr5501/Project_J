#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/DataValidation.h"
#include "Misc/ScopeExit.h"
#include "World/Project_JWorldStreamingComponent.h"
#include "World/Project_JWorldArrivalSubsystem.h"
#include "HAL/PlatformTime.h"
#include "World/Project_JWorldState.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include <limits>
#include "World/Project_JWorldPortal.h"
#include "World/Project_JPreparedArrivalEffect.h"
#include "Game/Project_JPlayerState.h"
#include "Project_JSpellbladeCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Project_JGameplayTags.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "MoverSimulationTypes.h"
#include "MoverTypes.h"
#include "MoverDataModelTypes.h"
#include "Components/Project_JCommandBudgetComponent.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJWorldPreparationShapeTest, "ProjectJ.Foundation.World.PreparationCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJWorldPreparationShapeTest::RunTest(const FString&)
{
	TArray<FStreamingSourceShape> Shapes;
	TestTrue(TEXT("Stopped source has valid geometry"), UProject_JWorldStreamingComponent::BuildPreparationShapes(FVector::ZeroVector, 3, 60000, 60000, Shapes));
	TestEqual(TEXT("Stationary sphere is not duplicated"), Shapes.Num(), 1);
	TestTrue(TEXT("Longer prediction has bounded continuous coverage"), UProject_JWorldStreamingComponent::BuildPreparationShapes(FVector(50000, 0, -50000), 3, 30000, 200000, Shapes));
	TestTrue(TEXT("Shape count respects the shared content budget"), Shapes.Num() <= UProject_JWorldDefinition::MaxPreparationShapes);
	const FVector End = UProject_JWorldStreamingComponent::PredictOffset(FVector(50000, 0, -50000), 3, 200000);
	for (int32 I = 0; I <= 200; ++I)
	{
		const FVector Point = End * (double(I) / 200);
		TestTrue(TEXT("No gap along the actual predicted 3D segment"), Shapes.ContainsByPredicate([&](const auto& S) { return FVector::DistSquared(Point, S.Location) <= FMath::Square(S.Radius); }));
	}
	TestFalse(TEXT("Uncoverable tuning is rejected rather than exceeding the source budget"), UProject_JWorldStreamingComponent::BuildPreparationShapes(FVector(100000,0,0), 3, 1000, 200000, Shapes));
	TestTrue(TEXT("Rejected geometry cannot retain an earlier shape list"), Shapes.IsEmpty());
	TestFalse(TEXT("Nonfinite radius never reaches World Partition"), UProject_JWorldStreamingComponent::BuildPreparationShapes(FVector::ZeroVector, 3, std::numeric_limits<float>::quiet_NaN(), 0, Shapes));
	TestFalse(TEXT("Extreme finite velocity cannot create NaN prediction"), UProject_JWorldStreamingComponent::PredictOffset(FVector(1.e308, 1.e308, 0), 3, 60000).ContainsNaN());
	auto* Definition = NewObject<UProject_JWorldDefinition>(); Definition->RegionId = TEXT("Test.World");
	FDataValidationContext Valid;
	TestTrue(TEXT("Existing default tuning validates"), Definition->IsDataValid(Valid) == EDataValidationResult::Valid);
	TestEqual(TEXT("Default admission policy preserves existing logical record capacity"), Definition->MaxSimultaneousPreparedArrivals, UProject_JWorldArrivalSubsystem::MaxPending);
	for (const int32 Limit : {0, UProject_JWorldArrivalSubsystem::MaxPending+1})
	{
		Definition->MaxSimultaneousPreparedArrivals = Limit; FDataValidationContext InvalidAdmission;
		TestFalse(TEXT("Out-of-range admission policy is invalid at runtime"), Definition->IsPreparedArrivalAdmissionValid());
		TestTrue(TEXT("Content validation rejects an out-of-range admission policy"), Definition->IsDataValid(InvalidAdmission) == EDataValidationResult::Invalid);
	}
	Definition->MaxSimultaneousPreparedArrivals = 1;
	TestTrue(TEXT("The minimum authored admission policy remains valid"), Definition->IsPreparedArrivalAdmissionValid());
	Definition->MaxSimultaneousPreparedArrivals = UProject_JWorldArrivalSubsystem::MaxPending;
	Definition->MaxPredictionDistance = 200000; Definition->PreparationRadius = 1000;
	FDataValidationContext Invalid;
	TestTrue(TEXT("Content validation rejects a corridor outside the shape budget"), Definition->IsDataValid(Invalid) == EDataValidationResult::Invalid);
	if (auto* Authored = LoadObject<UProject_JWorldDefinition>(nullptr, TEXT("/Game/World/MainWorld/Data/DA_MainWorld.DA_MainWorld")))
	{
		FDataValidationContext AuthoredContext;
		TestTrue(TEXT("Actual MainWorld definition meets the same content contract"), Authored->IsDataValid(AuthoredContext) == EDataValidationResult::Valid);
	}
	else AddError(TEXT("Authored MainWorld definition unavailable"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJWorldPreparationLifetimeTest, "ProjectJ.Foundation.World.LatePreparationDefinition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJWorldPreparationLifetimeTest::RunTest(const FString&)
{
	auto* GI = NewObject<UGameInstance>(GEngine); GI->InitializeStandalone();
	auto* World = GI->GetWorld();
	ON_SCOPE_EXIT { GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* PC = World->SpawnActor<APlayerController>();
	// Preparation consumes the controller's locality and pawn, not input or
	// viewport subsystems. Socket tests exercise actual remote local players.
	PC->SetAsLocalPlayerController();
	auto* Pawn = World->SpawnActor<ACharacter>(); PC->Possess(Pawn);
	auto* Source = NewObject<UProject_JWorldStreamingComponent>(PC); PC->AddInstanceComponent(Source); Source->RegisterComponent();
	Source->BeginPlay(); Source->EnableStreamingSource();
	FWorldPartitionStreamingSource Result;
	TestFalse(TEXT("No definition means no additional source"), Source->GetStreamingSource(Result));
	auto* State = World->SpawnActor<AProject_JWorldState>();
	Source->NextDefinitionSearch = 0;
	TestFalse(TEXT("Present owner with an unready definition remains closed"), Source->GetStreamingSource(Result));
	auto* Definition = NewObject<UProject_JWorldDefinition>(State); Definition->RegionId = TEXT("Test.World");
	State->Definition = Definition;
	if (!TestTrue(TEXT("Definition arriving after component BeginPlay becomes usable"), Source->GetStreamingSource(Result))) return false;
	TestEqual(TEXT("Late definition supplies authored radius"), Result.Shapes[0].Radius, Definition->PreparationRadius);
	TestEqual(TEXT("Actual stationary pawn uses one sphere"), Result.Shapes.Num(), 1);
	auto* Initialized = NewObject<UProject_JWorldStreamingComponent>(PC); PC->AddInstanceComponent(Initialized); Initialized->RegisterComponent(); Initialized->BeginPlay();
	Initialized->PreparationRadius = 90000;
	if (!TestTrue(TEXT("Normally initialized source remains usable"), Initialized->GetStreamingSource(Result))) return false;
	TestEqual(TEXT("Existing post-BeginPlay component tuning remains effective"), Result.Shapes[0].Radius, 90000.f);

	FWorldPartitionStreamingSource FirstSource, SecondSource, RepeatedSource;
	if (!TestTrue(TEXT("Both same-controller preparation components remain active"),
		Source->GetStreamingSource(FirstSource) && Initialized->GetStreamingSource(SecondSource))) return false;
	TestTrue(TEXT("Distinct additional components never share a velocity/history identity"), FirstSource.Name != SecondSource.Name);
	TestTrue(TEXT("Additional source exposes the exact opaque registration identity"), FirstSource.Name == Source->GetStreamingSourceIdentity());
	TestTrue(TEXT("Same-world providers reuse one namespace entry with distinct internal numbers"),
		FirstSource.Name.GetComparisonIndex() == SecondSource.Name.GetComparisonIndex() && FirstSource.Name.GetNumber() > 0 && SecondSource.Name.GetNumber() > 0);
	TestTrue(TEXT("Repeated provider query remains active"), Source->GetStreamingSource(RepeatedSource));
	TestEqual(TEXT("Repeated provider queries retain one identity"), RepeatedSource.Name, FirstSource.Name);
	TArray<FWorldPartitionStreamingSource> ControllerSources;
	if (!TestTrue(TEXT("Default controller streaming remains enabled and usable"),
		PC->GetStreamingSources(ControllerSources) && !ControllerSources.IsEmpty())) return false;
	for (const auto& ControllerSource : ControllerSources)
	{
		TestTrue(TEXT("Additional providers do not alias the default controller source"),
			ControllerSource.Name != FirstSource.Name && ControllerSource.Name != SecondSource.Name);
	}
	// Exercise actual provider queries and registration churn, not a duplicate ID algorithm.
	TSet<FName> IssuedIdentities{FirstSource.Name, SecondSource.Name};
	for (int32 Index = 0; Index < 32; ++Index)
	{
		auto* Churn = NewObject<UProject_JWorldStreamingComponent>(PC); PC->AddInstanceComponent(Churn); Churn->RegisterComponent(); Churn->BeginPlay();
		FWorldPartitionStreamingSource Advertised;
		if (!TestTrue(TEXT("Registered churn provider retains normal source behavior"), Churn->GetStreamingSource(Advertised))) return false;
		TestFalse(TEXT("A retired provider identity is never reissued"), IssuedIdentities.Contains(Advertised.Name));
		TestTrue(TEXT("Churn reuses the existing namespace entry"), Advertised.Name.GetComparisonIndex() == FirstSource.Name.GetComparisonIndex());
		IssuedIdentities.Add(Advertised.Name); Churn->DestroyComponent();
	}
	Source->UnregisterComponent();
	TestTrue(TEXT("Unregistration closes the public identity before late queries"), Source->GetStreamingSourceIdentity().IsNone());
	TestFalse(TEXT("Unregistered component supplies no source"), Source->GetStreamingSource(Result));
	Source->RegisterComponent();
	if (!TestTrue(TEXT("Re-registration restores a real provider"), Source->GetStreamingSource(Result))) return false;
	TestFalse(TEXT("Re-registration does not inherit old velocity/history identity"), IssuedIdentities.Contains(Result.Name));
	TestTrue(TEXT("Re-registration uses the same world namespace"), Result.Name.GetComparisonIndex() == FirstSource.Name.GetComparisonIndex());
	Source->TargetState = EStreamingSourceTargetState::Loaded; Source->Priority = EStreamingSourcePriority::High;
	Source->TargetBehavior = EStreamingSourceTargetBehavior::Exclude; Source->DebugColor = FColor(12, 34, 56);
	Source->TargetGrids.Add(TEXT("PreparationIdentityTestGrid"));
	FWorldPartitionStreamingSource Tuned, BaseSettings;
	TestTrue(TEXT("Registration identity preserves a provider with nondefault target settings"), Source->GetStreamingSource(Tuned));
	TestTrue(TEXT("The actual engine parent still supplies source policy"), Source->UWorldPartitionStreamingSourceComponent::GetStreamingSource(BaseSettings));
	bool bSameTargetGrids = Tuned.TargetGrids.Num() == BaseSettings.TargetGrids.Num();
	for (const FName Grid : Tuned.TargetGrids) bSameTargetGrids &= BaseSettings.TargetGrids.Contains(Grid);
	TestTrue(TEXT("Identity replacement preserves the parent target settings"), Tuned.TargetState == BaseSettings.TargetState && Tuned.TargetBehavior == BaseSettings.TargetBehavior &&
		Tuned.Priority == BaseSettings.Priority && Tuned.DebugColor == BaseSettings.DebugColor && bSameTargetGrids);
	Initialized->DestroyComponent();
	Source->DisableStreamingSource();
	TestFalse(TEXT("Explicit disable still closes preparation"), Source->GetStreamingSource(Result));
	Source->EnableStreamingSource(); State->Destroy();
	TestFalse(TEXT("Destroyed definition owner cannot keep a ghost source"), Source->GetStreamingSource(Result));
	auto* Replacement = World->SpawnActor<AProject_JWorldState>();
	auto* ReplacementDefinition = NewObject<UProject_JWorldDefinition>(Replacement); ReplacementDefinition->PreparationRadius = 80000;
	Replacement->Definition = ReplacementDefinition; Source->NextDefinitionSearch = 0;
	if (!TestTrue(TEXT("Replacement owner can re-establish preparation"), Source->GetStreamingSource(Result))) return false;
	TestEqual(TEXT("Replacement supplies its own tuning"), Result.Shapes[0].Radius, 80000.f);
	auto* Execution = World->GetSubsystem<UProject_JWorldArrivalSubsystem>();
	if (!TestNotNull(TEXT("Game world owns the existing provider identity issuer"), Execution)) return false;
	Execution->LastStreamingSourceNumber = MAX_int32 - 1;
	auto* Last = NewObject<UProject_JWorldStreamingComponent>(PC); PC->AddInstanceComponent(Last); Last->RegisterComponent(); Last->BeginPlay();
	FWorldPartitionStreamingSource LastSource;
	if (!TestTrue(TEXT("The final legal internal number is issued once"), Last->GetStreamingSource(LastSource))) return false;
	TestEqual(TEXT("The final identity retains the legal FName number"), LastSource.Name.GetNumber(), MAX_int32);
	auto* Exhausted = NewObject<UProject_JWorldStreamingComponent>(PC); PC->AddInstanceComponent(Exhausted); Exhausted->RegisterComponent(); Exhausted->BeginPlay();
	TestTrue(TEXT("Exhaustion publishes no aliasing identity"), Exhausted->GetStreamingSourceIdentity().IsNone());
	TestFalse(TEXT("Exhausted registration supplies no additional load source"), Exhausted->GetStreamingSource(Result));
	Exhausted->ArrivalRequest = 9000; Exhausted->ArrivalPhase = EProject_JWorldArrivalPhase::Preparing;
	TestFalse(TEXT("Exhausted registration cannot admit an arrival that can never advertise"), Exhausted->RegisterArrivalExecution());
	Exhausted->FinishArrival(EProject_JWorldArrivalPhase::Failed, false);
	TestTrue(TEXT("Existing registration remains usable after issuer exhaustion"), Source->GetStreamingSource(Result));
	Last->UnregisterComponent(); Last->RegisterComponent();
	TestTrue(TEXT("Exhausted re-registration never reuses the previous identity"), Last->GetStreamingSourceIdentity().IsNone());
	Last->DestroyComponent(); Exhausted->DestroyComponent();
	Source->DestroyComponent();
	TestFalse(TEXT("Retired component does not supply late sources"), Source->GetStreamingSource(Result));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJWorldClockSnapshotTest, "ProjectJ.Foundation.World.AuthoritativeClockValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJWorldClockSnapshotTest::RunTest(const FString&)
{
	FProject_JWorldClockSettings Settings; Settings.DayLengthSeconds = 600; Settings.InitialHour = 19;
	TestEqual(TEXT("Clock starts from server-selected values"), AProject_JWorldState::EvaluateHour(Settings, 100, 100), 19.f);
	TestTrue(TEXT("Clock wraps without loading an authoring asset"), FMath::IsNearlyEqual(AProject_JWorldState::EvaluateHour(Settings, 100, 400), 7.f));
	TestEqual(TEXT("Before start cannot reverse time"), AProject_JWorldState::EvaluateHour(Settings, 100, 90), 19.f);
	TestTrue(TEXT("Large finite elapsed remains finite"), FMath::IsFinite(AProject_JWorldState::EvaluateHour(Settings, 0, 1.e308)));
	Settings.DayLengthSeconds = std::numeric_limits<float>::quiet_NaN();
	TestEqual(TEXT("Corrupt settings use the established finite default"), AProject_JWorldState::EvaluateHour(Settings, 0, 0), 10.f);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJWorldPreparedArrivalTest, "ProjectJ.Foundation.World.PreparedArrivalBoundaries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJWorldPreparedArrivalTest::RunTest(const FString&)
{
    TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine)); Instance->InitializeStandalone();
    UWorld* TestWorld = Instance->GetWorld();
    ON_SCOPE_EXIT { if (TestWorld->HasBegunPlay()) TestWorld->EndPlay(EEndPlayReason::Quit); Instance->Shutdown(); TestWorld->DestroyWorld(false); GEngine->DestroyWorldContext(TestWorld); };
    TestWorld->InitializeActorsForPlay(FURL());
    TestWorld->GetWorldSettings()->NotifyBeginPlay();
    TestWorld->GetWorldSettings()->NotifyMatchStarted();
    FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Controller = TestWorld->SpawnActor<APlayerController>(Spawn);
    auto* PlayerState = TestWorld->SpawnActor<AProject_JPlayerState>(Spawn);
    auto* Avatar = TestWorld->SpawnActor<AProject_JSpellbladeCharacter>(FVector(0,0,100), FRotator::ZeroRotator, Spawn);
    auto* WorldState = TestWorld->SpawnActor<AProject_JWorldState>(Spawn);
    auto* Portal = TestWorld->SpawnActor<AProject_JWorldPortal>(FVector(150,0,100), FRotator::ZeroRotator, Spawn);
    if (!TestNotNull(TEXT("Controller"), Controller) || !TestNotNull(TEXT("PlayerState"), PlayerState)
        || !TestNotNull(TEXT("Concrete Avatar"), Avatar) || !TestNotNull(TEXT("World state"), WorldState) || !TestNotNull(TEXT("Portal"), Portal)) return false;
    Controller->SetAsLocalPlayerController(); PlayerState->SetOwner(Controller); Controller->SetPlayerState(PlayerState);
    Avatar->SetPlayerState(PlayerState); Controller->Possess(Avatar);
    auto* ASC = PlayerState->GetProjectJAbilitySystemComponent();
    if (!TestNotNull(TEXT("Current session ASC"), ASC)) return false;
    ASC->InitAbilityActorInfo(PlayerState, Avatar); ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 100);
    auto* Definition = NewObject<UProject_JWorldDefinition>(WorldState); WorldState->Definition = Definition;
    FProject_JWorldPlace Place; Place.PlaceId = TEXT("S01"); Place.Location = FVector(2000,0,20); Definition->Places.Add(Place);
    Portal->DestinationPlaceId = Place.PlaceId;
    auto* Preparation = NewObject<UProject_JWorldStreamingComponent>(Controller);
    Controller->AddInstanceComponent(Preparation); Preparation->RegisterComponent();
    if (!Preparation->HasBegunPlay()) Preparation->BeginPlay();
    TestFalse(TEXT("No current initialized WP is never a completed arrival"), Preparation->RequestPreparedArrival(Portal, Avatar));
    TestEqual(TEXT("Rejected admission leaves no queued request"), Preparation->ArrivalRequest, uint64(0));

    // Value/physical boundary fixture, deliberately not a substitute for a real WP/Mover queue run.
    const auto StageQueued = [&](uint64 Serial, FVector Target)
    {
        Preparation->FinishArrival(EProject_JWorldArrivalPhase::Idle, false);
        Preparation->ArrivalRequest = Serial; Preparation->ArrivalPhase = EProject_JWorldArrivalPhase::Preparing;
        Preparation->bArrivalPreparationStarted = false; Preparation->bClientArrivalReady = false;
        Preparation->ArrivalPawn = Avatar; Preparation->ArrivalPlayerState = PlayerState; Preparation->ArrivalDefinition = Definition;
        Preparation->ArrivalAbilitySystem = ASC; Preparation->ArrivalActorInfoRevision = ASC->GetActorInfoRevision();
        Preparation->ArrivalAccountId = PlayerState->GetAccountId().Value; Preparation->ArrivalCharacterId = PlayerState->GetCharacterId().Value;
        Preparation->ArrivalHint = Preparation->ArrivalPlacement = Target; Preparation->ArrivalRadius = 60000;
        Preparation->ArrivalDeadline = FPlatformTime::Seconds()+30;
        Preparation->bArrivalOutcomeReceived = Preparation->bArrivalOutcomeSucceeded = false;
        // A real exact Entry/permit backs the physical fixture. This is not WP
        // readiness or eligibility: the changing-ASC negative case below remains invalid.
        TestTrue(TEXT("Physical boundary owns an exact registered preparation permit"), Preparation->RegisterArrivalExecution() && Preparation->TryStartArrivalPreparation());
        Preparation->ArrivalPhase = EProject_JWorldArrivalPhase::Queued;
    };
    auto* FloorActor = TestWorld->SpawnActor<AActor>(Spawn);
    if (!TestNotNull(TEXT("Collision floor owner"), FloorActor)) return false;
    auto* Floor = NewObject<UBoxComponent>(FloorActor); FloorActor->SetRootComponent(Floor); FloorActor->AddInstanceComponent(Floor);
    Floor->SetBoxExtent(FVector(500,500,10)); Floor->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Floor->SetCollisionObjectType(ECC_WorldStatic); Floor->SetCollisionResponseToAllChannels(ECR_Block); Floor->RegisterComponent();
    FloorActor->SetActorLocation(FVector(2000,0,0));
    FVector LegalPlacement;
    if (!TestTrue(TEXT("Normal static ground produces a capsule-clear placement"), Preparation->FindArrivalPlacement(Avatar, Place.Location, LegalPlacement))) return false;
    Floor->SetCollisionObjectType(ECC_WorldDynamic);
    if (!TestTrue(TEXT("Normal dynamic support remains legal"), Preparation->FindArrivalPlacement(Avatar, Place.Location, LegalPlacement))) return false;
    const FVector Destination = LegalPlacement; StageQueued(1, Destination);
    TestFalse(TEXT("Registered finite source with no WP/cells cannot be ready"), Preparation->IsArrivalRegionReady());
    FProject_JPreparedArrivalEffect Effect; Effect.Request = 1; Effect.Recipient = Preparation; Effect.ExpectedPawn = Avatar; Effect.TargetLocation = Destination;
    FMoverTimeStep Step; FMoverTickStartData Start; FMoverSyncState Output;
    FApplyMovementEffectParams Params{}; Params.UpdatedComponent = Avatar->GetCapsuleComponent(); Params.UpdatedPrimitive = Avatar->GetCapsuleComponent();
    Params.MoverComp = Avatar->GetMover(); Params.StartState = &Start; Params.TimeStep = &Step;
    FTeleportSucceededEventData Unrelated(Step, Avatar->GetActorLocation(), FQuat::Identity, Destination, FQuat::Identity);
    const FMoverSimEventGameThreadContext Context{Avatar->GetMover()};
    Unrelated.OnEventProcessed(Context);
    TestFalse(TEXT("An unrelated identical teleport payload cannot confirm the request"), Preparation->bArrivalOutcomeReceived);
    if (!TestTrue(TEXT("Request's real native teleport effect applies"), Effect.ApplyMovementEffect(Params, Output))) return false;
    TestTrue(TEXT("Native effect moved the expected pawn"), Avatar->GetActorLocation().Equals(Destination, .1));
    if (!TestEqual(TEXT("Native effect owns one event"), Params.OutputEvents.Num(), 1)) return false;
    TestFalse(TEXT("Applying an effect alone does not claim delivered completion"), Preparation->bArrivalOutcomeReceived);
    Params.OutputEvents[0]->OnEventProcessed(Context);
    TestTrue(TEXT("Only the effect-owned event records its outcome"), Preparation->bArrivalOutcomeReceived && Preparation->bArrivalOutcomeSucceeded);
    TestTrue(TEXT("Outcome remains queued until the actual arrival collision gate"), Preparation->ArrivalPhase == EProject_JWorldArrivalPhase::Queued);
    Floor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TestFalse(TEXT("A position in empty air is not a legal arrival"), Preparation->IsArrivalPlacementCurrent(Avatar, Destination));
    Floor->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    StageQueued(2, Destination);
    Params.OutputEvents[0]->OnEventProcessed(Context);
    TestFalse(TEXT("Old event receipt cannot complete a new generation"), Preparation->bArrivalOutcomeReceived);
    const FVector BeforeStale = Avatar->GetActorLocation();
    TestFalse(TEXT("An obsolete queued generation cannot move the current pawn"), Effect.ApplyMovementEffect(Params, Output));
    TestTrue(TEXT("Rejected obsolete effect preserves location"), Avatar->GetActorLocation().Equals(BeforeStale));
    Controller->UnPossess(); Effect.Request = 2;
    TestFalse(TEXT("Avatar ownership replacement closes the queued effect"), Effect.ApplyMovementEffect(Params, Output));
    Controller->Possess(Avatar); ASC->InitAbilityActorInfo(PlayerState, Avatar);

    // Current life gates applying a prepared Portal, not identity or an already-applied receipt.
    // Four requested boundaries only: Health0/DeadTag before apply and before delivery.
    const auto SameLifeBinding = [&](uint64 Epoch)
    {
        return IsValid(Controller) && !Controller->IsActorBeingDestroyed() && IsValid(Avatar) && !Avatar->IsActorBeingDestroyed() &&
            IsValid(PlayerState) && !PlayerState->IsActorBeingDestroyed() && IsValid(ASC) && !ASC->IsBeingDestroyed() && !ASC->IsActorInfoChanging() &&
            Controller->GetPawn() == Avatar && Avatar->GetController() == Controller && Controller->PlayerState == PlayerState &&
            PlayerState->GetOwner() == Controller && PlayerState->GetPawn() == Avatar && PlayerState->GetProjectJAbilitySystemComponent() == ASC &&
            ASC->GetOwnerActor() == PlayerState && ASC->GetAvatarActor() == Avatar && ASC->GetActorInfoRevision() == Epoch;
    };
    const FGameplayTag DeadTag = FProject_JGameplayTags::Get().State_Dead;
    for (int32 LifeCase = 0; LifeCase < 4; ++LifeCase)
    {
        const bool bPostApply = LifeCase >= 2, bDeadTag = (LifeCase & 1) != 0;
        const uint64 Request = uint64(5 + LifeCase);
        const FVector LifeTarget = Destination + FVector(100 * (LifeCase + 1),0,0);
        StageQueued(Request, LifeTarget);
        const uint64 LifeEpoch = ASC->GetActorInfoRevision(), LifeToken = Preparation->ArrivalExecutionToken;
        auto* LifeExecution = Preparation->ArrivalExecutionOwner.Get();
        const auto* LifeAttributes = PlayerState->GetProjectJAttributeSet();
        if (!TestTrue(TEXT("Life boundary starts with a current healthy exact binding, legal geometry and a real permit"),
            LifeExecution && SameLifeBinding(LifeEpoch) && Preparation->IsArrivalCurrent(false) && Preparation->IsPreparedArrivalStarted() &&
            LifeAttributes && FMath::IsFinite(LifeAttributes->GetHealth()) && LifeAttributes->GetHealth() > 0 && DeadTag.IsValid() && !ASC->HasMatchingGameplayTag(DeadTag) &&
            Preparation->IsArrivalPlacementValid(Avatar, LifeTarget, Avatar->GetCapsuleComponent()->GetComponentQuat()))) return false;
        const float PreviousHealth = LifeAttributes->GetHealth();
        ON_SCOPE_EXIT
        {
            if (IsValid(ASC))
            {
                if (bDeadTag) ASC->RemoveLooseGameplayTag(DeadTag);
                else ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), PreviousHealth);
            }
        };
        const FName LifeSourceId = Preparation->GetStreamingSourceIdentity();
        const auto BeforeLifeStats = LifeExecution->GetStats();
        FWorldPartitionStreamingSource LifeSource;
        TestTrue(TEXT("Queued identity advertises the approved destination before life mutation"),
            Preparation->GetStreamingSource(LifeSource) && LifeSource.Name == LifeSourceId && LifeSource.Location.Equals(LifeTarget,.1));
        FProject_JPreparedArrivalEffect LifeEffect; LifeEffect.Request = Request; LifeEffect.Recipient = Preparation;
        LifeEffect.ExpectedPawn = Avatar; LifeEffect.TargetLocation = LifeTarget;
        FApplyMovementEffectParams LifeParams = Params; LifeParams.OutputEvents.Reset();
        FMoverSyncState LifeOutput;
        if (bPostApply)
        {
            if (!TestTrue(TEXT("Healthy native move actually applies before the later life change"), LifeEffect.ApplyMovementEffect(LifeParams, LifeOutput)) ||
                !TestEqual(TEXT("Actual pre-change move owns one native event"), LifeParams.OutputEvents.Num(), 1) ||
                !TestTrue(TEXT("Applied native result is SUCCESS before delivery"), LifeParams.OutputEvents[0]->CastTo<FTeleportSucceededEventData>() != nullptr && Avatar->GetActorLocation().Equals(LifeTarget,.01))) return false;
            TestFalse(TEXT("Even an applied move is unconfirmed before its real event is processed"), Preparation->bArrivalOutcomeReceived);
        }
        if (bDeadTag) ASC->AddLooseGameplayTag(DeadTag);
        else ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 0.f);
        if (!TestTrue(TEXT("Public health/dead mutation preserves PC/Pawn/PS/ASC epoch and destination geometry"),
            SameLifeBinding(LifeEpoch) && Preparation->IsArrivalPlacementValid(Avatar, LifeTarget, Avatar->GetCapsuleComponent()->GetComponentQuat()))) return false;
        TestTrue(TEXT("The actual requested life revocation is present"), bDeadTag ? ASC->HasMatchingGameplayTag(DeadTag) : PlayerState->GetProjectJAttributeSet()->GetHealth() == 0.f);
        TestTrue(TEXT("Life change does not erase queued identity or receipt eligibility"), Preparation->IsArrivalCurrent(false));
        TestTrue(TEXT("Approved queued source remains until the canonical outcome retires it"),
            Preparation->GetStreamingSource(LifeSource) && LifeSource.Name == LifeSourceId && LifeSource.Location.Equals(LifeTarget,.1));
        TestTrue(TEXT("Read-only source query retains the exact request/Entry/permit"),
            Preparation->ArrivalRequest == Request && Preparation->ArrivalPhase == EProject_JWorldArrivalPhase::Queued && Preparation->ArrivalExecutionToken == LifeToken &&
            Preparation->IsPreparedArrivalStarted() && LifeExecution->GetStats().Pending == BeforeLifeStats.Pending && LifeExecution->GetStats().ActivePreparationPermits == BeforeLifeStats.ActivePreparationPermits);
        if (!bPostApply)
        {
            const FVector BeforeLifeEffect = Avatar->GetActorLocation();
            TestFalse(TEXT("Current dead-but-bound life cannot apply the queued native movement"), LifeEffect.ApplyMovementEffect(LifeParams, LifeOutput));
            TestTrue(TEXT("Life eligibility rejection preserves the pawn transform and native output"),
                Avatar->GetActorLocation().Equals(BeforeLifeEffect) && !LifeOutput.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>());
            if (!TestEqual(TEXT("Life eligibility rejection creates one normal native outcome"), LifeParams.OutputEvents.Num(), 1)) return false;
            TestTrue(TEXT("The real attempted native outcome is FAILED"), LifeParams.OutputEvents[0]->CastTo<FTeleportFailedEventData>() != nullptr);
            TestFalse(TEXT("Failure is not a receipt until its actual event is processed"), Preparation->bArrivalOutcomeReceived);
        }
        LifeParams.OutputEvents[0]->OnEventProcessed(Context);
        TestTrue(TEXT("Actual event delivery preserves SUCCESS or FAILED regardless of current life"),
            Preparation->bArrivalOutcomeReceived && Preparation->bArrivalOutcomeSucceeded == bPostApply);
        TestTrue(TEXT("A delivered outcome remains owned by the existing queued terminal writer"),
            Preparation->ArrivalPhase == EProject_JWorldArrivalPhase::Queued && Preparation->ArrivalExecutionToken == LifeToken && Preparation->IsPreparedArrivalStarted());
        Preparation->AdvancePreparedArrival(FPlatformTime::Seconds());
        TestTrue(TEXT("Canonical writer retains real success, retires failed eligibility, and returns exact resources"),
            Preparation->ArrivalPhase == (bPostApply ? EProject_JWorldArrivalPhase::Arrived : EProject_JWorldArrivalPhase::Failed) &&
            !Preparation->ArrivalRequest && !Preparation->ArrivalExecutionToken && !Preparation->IsPreparedArrivalStarted() &&
            LifeExecution->GetStats().Pending == BeforeLifeStats.Pending - 1 && LifeExecution->GetStats().ActivePreparationPermits == BeforeLifeStats.ActivePreparationPermits - 1);
        TestTrue(TEXT("Outcome retirement does not replace the same object/ASC incarnation"), SameLifeBinding(LifeEpoch));
        AddInfo(FString::Printf(TEXT("Portal life boundary post_apply=%d case=%s epoch=%llu token=%llu received=%d phase=%d native_events=%d"),
            bPostApply, bDeadTag ? TEXT("StateDead") : TEXT("HealthZero"), LifeEpoch, LifeToken, Preparation->bArrivalOutcomeReceived, int32(Preparation->ArrivalPhase), LifeParams.OutputEvents.Num()));
    }

    // Same pointers are not the same recipient incarnation. Exercise the real
    // project Init/Clear APIs and the real native teleport consumer, without
    // substituting this value fixture for a WP readiness or socket test.
    for (bool bClearFirst : {false, true})
    {
        const uint64 Request = bClearFirst ? 11 : 10;
        const FVector DemandHint = Avatar->GetActorLocation()+FVector(40000,0,0);
        StageQueued(Request, DemandHint);
        if (!TestTrue(TEXT("Current recipient epoch admits the queued boundary"), Preparation->IsArrivalCurrent(false))) return false;
        const FName RegistrationId = Preparation->GetStreamingSourceIdentity();
        FWorldPartitionStreamingSource Advertised;
        TestTrue(TEXT("Current Queued request retains its destination advertisement and registration identity"),
            Preparation->GetStreamingSource(Advertised) && Advertised.Location.Equals(DemandHint,.1) && Advertised.Name == RegistrationId);
        Preparation->ArrivalPhase = EProject_JWorldArrivalPhase::Preparing;
        TestTrue(TEXT("Started local Preparing request retains its destination immediately"),
            Preparation->GetStreamingSource(Advertised) && Advertised.Location.Equals(DemandHint,.1) && Advertised.Name == RegistrationId);
        Preparation->ArrivalDeadline = FPlatformTime::Seconds()-1;
        TestTrue(TEXT("Expired uncommitted authority demand immediately resumes local prediction"),
            Preparation->GetStreamingSource(Advertised) && Advertised.Location.Equals(Avatar->GetActorLocation(),.1)
                && Advertised.Name == RegistrationId && !Advertised.Location.Equals(DemandHint,.1));
        TestTrue(TEXT("Read-only expiry fence leaves logical retirement and permission to the central writer"),
            Preparation->ArrivalRequest == Request && Preparation->ArrivalPhase == EProject_JWorldArrivalPhase::Preparing
                && Preparation->IsPreparedArrivalStarted());
        Preparation->ArrivalPhase = EProject_JWorldArrivalPhase::Queued;
        TestTrue(TEXT("An already queued authority request retains destination demand after its deadline"),
            Preparation->GetStreamingSource(Advertised) && Advertised.Location.Equals(DemandHint,.1) && Advertised.Name == RegistrationId);
        Preparation->ArrivalDeadline = FPlatformTime::Seconds()+30;
        const uint64 RetiredEpoch = ASC->GetActorInfoRevision();
        if (bClearFirst) ASC->ClearActorInfo();
        ASC->InitAbilityActorInfo(PlayerState, Avatar);
        TestTrue(TEXT("Explicit same-pair Init/Clear+Init retains the tuple and advances epoch"),
            ASC->GetOwnerActor() == PlayerState && ASC->GetAvatarActor() == Avatar && ASC->GetActorInfoRevision() != RetiredEpoch);
        TestFalse(TEXT("An earlier arrival cannot continue into the same-pair new epoch"), Preparation->IsArrivalCurrent(false));
        TestTrue(TEXT("Same-pair epoch retirement immediately restores the local predictor instead of old demand"),
            Preparation->GetStreamingSource(Advertised) && Advertised.Location.Equals(Avatar->GetActorLocation(),.1) && Advertised.Name == RegistrationId);
        TestEqual(TEXT("A read-only source query does not terminate the logical request"), Preparation->ArrivalRequest, Request);
        TestTrue(TEXT("A source query leaves the central terminal writer in charge"), Preparation->ArrivalPhase == EProject_JWorldArrivalPhase::Queued);
        FProject_JPreparedArrivalEffect EpochEffect; EpochEffect.Request = Request; EpochEffect.Recipient = Preparation;
        EpochEffect.ExpectedPawn = Avatar; EpochEffect.TargetLocation = Destination+FVector(100,0,0);
        FApplyMovementEffectParams RejectedParams = Params; RejectedParams.OutputEvents.Reset();
        FMoverSyncState RejectedOutput;
        const FVector BeforeEpochEffect = Avatar->GetActorLocation();
        TestFalse(TEXT("A retired recipient epoch cannot commit a queued teleport"), EpochEffect.ApplyMovementEffect(RejectedParams, RejectedOutput));
        TestTrue(TEXT("Epoch rejection preserves the current pawn transform"), Avatar->GetActorLocation().Equals(BeforeEpochEffect));
        TestEqual(TEXT("Epoch rejection emits no native teleport outcome"), RejectedParams.OutputEvents.Num(), 0);
        Preparation->AdvancePreparedArrival(FPlatformTime::Seconds());
        TestTrue(TEXT("Existing queued cancellation closes as unconfirmed, never arrived"), Preparation->ArrivalPhase == EProject_JWorldArrivalPhase::ExpiredUnconfirmed);
        TestTrue(TEXT("Terminal cleanup releases the recipient binding"), !Preparation->ArrivalAbilitySystem.IsValid() && Preparation->ArrivalActorInfoRevision == 0);
        TestTrue(TEXT("Normal predictor keeps the same opaque identity after actual central cleanup"),
            Preparation->GetStreamingSource(Advertised) && Advertised.Location.Equals(Avatar->GetActorLocation(),.1) && Advertised.Name == RegistrationId);

        // An outcome already produced for the old incarnation must not certify
        // the reinitialized incarnation. The physical move is not rolled back.
        StageQueued(Request+10, Destination);
        EpochEffect.Request = Request+10; EpochEffect.TargetLocation = Destination;
        FApplyMovementEffectParams LateParams = Params; LateParams.OutputEvents.Reset();
        FMoverSyncState LateOutput;
        if (!TestTrue(TEXT("Fresh epoch uses the real native teleport before outcome delivery"), EpochEffect.ApplyMovementEffect(LateParams, LateOutput))
            || !TestEqual(TEXT("Fresh epoch owns one native outcome"), LateParams.OutputEvents.Num(), 1)) return false;
        if (bClearFirst) ASC->ClearActorInfo();
        ASC->InitAbilityActorInfo(PlayerState, Avatar);
        LateParams.OutputEvents[0]->OnEventProcessed(Context);
        TestFalse(TEXT("Old epoch outcome cannot certify the current recipient"), Preparation->bArrivalOutcomeReceived);
        Preparation->FinishArrival(EProject_JWorldArrivalPhase::ExpiredUnconfirmed, false);
    }
    {
        // Branch/lifetime fixture only: public role setup represents a local
        // client's already-authorized hint. It does not prove RPC delivery,
        // replication, WP residency or real client arrival; socket evidence is separate.
        const FVector ClientPreparationHint = Avatar->GetActorLocation()+FVector(45000,0,0);
        StageQueued(25, ClientPreparationHint);
        const auto ClientPreparationOriginalRole = Controller->GetLocalRole();
        ON_SCOPE_EXIT { Controller->SetRole(ClientPreparationOriginalRole); };
        Controller->SetRole(ROLE_AutonomousProxy);
        if (!TestFalse(TEXT("Client branch fixture uses a nonauthority controller"), Controller->HasAuthority())
            || !TestTrue(TEXT("Client branch fixture remains local with its real pawn binding"),
                Controller->IsLocalController() && Preparation->IsArrivalCurrent(false))) return false;
        Preparation->ArrivalPhase = EProject_JWorldArrivalPhase::Preparing;
        const FName ClientPreparationIdentity = Preparation->GetStreamingSourceIdentity();
        const uint64 ClientPreparationExecutionToken = Preparation->ArrivalExecutionToken;
        FWorldPartitionStreamingSource ClientPreparationSource;
        TestTrue(TEXT("Unexpired client Preparing advertises its authorized destination"),
            Preparation->GetStreamingSource(ClientPreparationSource) && ClientPreparationSource.Location.Equals(ClientPreparationHint,.1)
                && ClientPreparationSource.Name == ClientPreparationIdentity);
        Preparation->ArrivalDeadline = FPlatformTime::Seconds()-1;
        TestTrue(TEXT("Expired client Preparing immediately resumes local prediction before its next central poll"),
            Preparation->GetStreamingSource(ClientPreparationSource) && ClientPreparationSource.Location.Equals(Avatar->GetActorLocation(),.1)
                && !ClientPreparationSource.Location.Equals(ClientPreparationHint,.1) && ClientPreparationSource.Name == ClientPreparationIdentity);
        TestTrue(TEXT("Client source expiry query preserves the central writer's exact logical request and permission"),
            Preparation->ArrivalRequest == 25 && Preparation->ArrivalPhase == EProject_JWorldArrivalPhase::Preparing
                && Preparation->ArrivalExecutionToken == ClientPreparationExecutionToken && Preparation->IsPreparedArrivalStarted());
        Preparation->ArrivalPhase = EProject_JWorldArrivalPhase::AwaitingLocalArrival;
        TestTrue(TEXT("Client AwaitingLocalArrival retains its existing late destination demand policy"),
            Preparation->GetStreamingSource(ClientPreparationSource) && ClientPreparationSource.Location.Equals(ClientPreparationHint,.1)
                && ClientPreparationSource.Name == ClientPreparationIdentity);
        Preparation->UnregisterComponent();
        TestFalse(TEXT("Actual provider unregistration closes all late client source queries"), Preparation->GetStreamingSource(ClientPreparationSource));
        TestTrue(TEXT("Unregistration retires the exact logical request and opaque provider identity"),
            Preparation->ArrivalRequest == 0 && Preparation->ArrivalExecutionToken == 0
                && Preparation->GetStreamingSourceIdentity().IsNone());
    }
    Preparation->RegisterComponent();
    if (!TestTrue(TEXT("Client branch fixture restores authority and a normal provider registration"),
        Controller->HasAuthority() && !Preparation->GetStreamingSourceIdentity().IsNone())) return false;
    StageQueued(30, Destination);
    const uint64 MetadataEpoch = ASC->GetActorInfoRevision();
    TestTrue(TEXT("Current-pair metadata refresh is supported"), ASC->RefreshActorInfoForCurrentActor(Avatar));
    TestEqual(TEXT("Metadata refresh preserves the recipient epoch"), ASC->GetActorInfoRevision(), MetadataEpoch);
    TestTrue(TEXT("Normal metadata ensure preserves the prepared arrival"), Preparation->IsArrivalCurrent(false));

    bool bSawChanging = false, bChangingCurrent = true, bChangingSourceRestored = false;
    const FDelegateHandle Changing = ASC->OnActorInfoChanging.AddLambda([&](AActor*, AActor*)
    {
        bSawChanging = ASC->IsActorInfoChanging();
        // Even a freshly captured numeric epoch cannot admit work while the
        // old pair is still installed during the actual transition callback.
        StageQueued(31, Avatar->GetActorLocation()+FVector(40000,0,0));
        bChangingCurrent = Preparation->IsArrivalCurrent(false);
        FWorldPartitionStreamingSource ChangingSource;
        bChangingSourceRestored = Preparation->GetStreamingSource(ChangingSource) && ChangingSource.Location.Equals(Avatar->GetActorLocation(),.1)
            && ChangingSource.Name == Preparation->GetStreamingSourceIdentity();
    });
    ASC->InitAbilityActorInfo(PlayerState, Avatar);
    ASC->OnActorInfoChanging.Remove(Changing);
    TestTrue(TEXT("Real changing notification runs before same-pair replacement"), bSawChanging);
    TestFalse(TEXT("Changing recipient is rejected even at the current numeric epoch"), bChangingCurrent);
    TestTrue(TEXT("Actual changing callback cannot retain old target demand"), bChangingSourceRestored);
    Preparation->FinishArrival(EProject_JWorldArrivalPhase::Failed, false);

    // Identity and owner changes can retire a request without changing the Pawn
    // pointer. They must stop target demand before a budgeted Advance visits it.
    const FName ResourceSourceId = Preparation->GetStreamingSourceIdentity();
    FWorldPartitionStreamingSource ResourceSource;
    StageQueued(60, Avatar->GetActorLocation()+FVector(40000,0,0));
    FProject_JAccountId NewAccount; NewAccount.Value = FGuid::NewGuid();
    FProject_JCharacterId NewCharacter; NewCharacter.Value = FGuid::NewGuid();
    PlayerState->SetIdentity(NewAccount, NewCharacter);
    TestFalse(TEXT("Character/account replacement retires the existing request binding"), Preparation->IsArrivalCurrent(false));
    TestTrue(TEXT("Character replacement immediately restores local source geometry"),
        Preparation->GetStreamingSource(ResourceSource) && ResourceSource.Location.Equals(Avatar->GetActorLocation(),.1) && ResourceSource.Name == ResourceSourceId);
    TestEqual(TEXT("Identity-fenced advertisement leaves request retirement to Advance"), Preparation->ArrivalRequest, uint64(60));
    Preparation->AdvancePreparedArrival(FPlatformTime::Seconds());
    TestTrue(TEXT("Central cleanup terminates the identity-retired queue normally"), Preparation->ArrivalPhase == EProject_JWorldArrivalPhase::ExpiredUnconfirmed && Preparation->ArrivalRequest == 0);
    auto* OtherController = TestWorld->SpawnActor<APlayerController>(Spawn);
    if (!TestNotNull(TEXT("Replacement authority owner"), OtherController)) return false;
    StageQueued(61, Avatar->GetActorLocation()+FVector(40000,0,0));
    PlayerState->SetOwner(OtherController);
    TestTrue(TEXT("Owner replacement immediately withdraws old target demand but preserves local prediction"),
        Preparation->GetStreamingSource(ResourceSource) && ResourceSource.Location.Equals(Avatar->GetActorLocation(),.1) && ResourceSource.Name == ResourceSourceId);
    Preparation->AdvancePreparedArrival(FPlatformTime::Seconds());
    TestTrue(TEXT("Owner replacement is still retired by the existing central writer"), Preparation->ArrivalPhase == EProject_JWorldArrivalPhase::ExpiredUnconfirmed && Preparation->ArrivalRequest == 0);
    PlayerState->SetOwner(Controller);

    // This native world has no socket driver. A normal unmarked PC without a
    // ULocalPlayer is genuinely nonlocal; actual dedicated-server mode is covered
    // separately by the existing MainWorld two-owner socket fixture.
    auto* RemoteState = TestWorld->SpawnActor<AProject_JPlayerState>(Spawn);
    auto* RemoteAvatar = TestWorld->SpawnActor<AProject_JSpellbladeCharacter>(FVector(1000,0,100), FRotator::ZeroRotator, Spawn);
    if (!TestNotNull(TEXT("Nonlocal authority state"), RemoteState) || !TestNotNull(TEXT("Nonlocal authority pawn"), RemoteAvatar)) return false;
    RemoteState->SetOwner(OtherController); OtherController->SetPlayerState(RemoteState);
    RemoteAvatar->SetPlayerState(RemoteState); OtherController->Possess(RemoteAvatar);
    auto* RemoteASC = RemoteState->GetProjectJAbilitySystemComponent();
    RemoteASC->InitAbilityActorInfo(RemoteState, RemoteAvatar);
    TestTrue(TEXT("Nonlocal source fixture uses the normal controller locality contract"), OtherController->HasAuthority() && !OtherController->IsLocalController());
    auto* RemotePreparation = NewObject<UProject_JWorldStreamingComponent>(OtherController);
    OtherController->AddInstanceComponent(RemotePreparation); RemotePreparation->RegisterComponent();
    if (!RemotePreparation->HasBegunPlay()) RemotePreparation->BeginPlay();
    RemotePreparation->ArrivalRequest = 62; RemotePreparation->ArrivalPhase = EProject_JWorldArrivalPhase::Preparing;
    RemotePreparation->bArrivalPreparationStarted = false;
    RemotePreparation->ArrivalPawn = RemoteAvatar; RemotePreparation->ArrivalPlayerState = RemoteState;
    RemotePreparation->ArrivalAbilitySystem = RemoteASC; RemotePreparation->ArrivalActorInfoRevision = RemoteASC->GetActorInfoRevision();
    RemotePreparation->ArrivalAccountId = RemoteState->GetAccountId().Value; RemotePreparation->ArrivalCharacterId = RemoteState->GetCharacterId().Value;
    RemotePreparation->ArrivalDefinition = Definition; RemotePreparation->ArrivalHint = FVector(50000,0,100); RemotePreparation->ArrivalRadius = 60000;
    RemotePreparation->ArrivalDeadline = FPlatformTime::Seconds()+30;
    if (!TestTrue(TEXT("Nonlocal physical fixture acquires its real Entry permit"), RemotePreparation->RegisterArrivalExecution() && RemotePreparation->TryStartArrivalPreparation())) return false;
    RemotePreparation->ArrivalPhase = EProject_JWorldArrivalPhase::Queued;
    TestTrue(TEXT("Current nonlocal authority request may advertise its target"), RemotePreparation->GetStreamingSource(ResourceSource) && ResourceSource.Location.Equals(RemotePreparation->ArrivalHint,.1));
    RemotePreparation->ArrivalPhase = EProject_JWorldArrivalPhase::Preparing;
    TestTrue(TEXT("Started nonlocal preparation advertises before its deadline"), RemotePreparation->GetStreamingSource(ResourceSource));
    RemotePreparation->ArrivalDeadline = FPlatformTime::Seconds()-1;
    TestFalse(TEXT("Expired uncommitted nonlocal authority request has no destination or predictor demand"), RemotePreparation->GetStreamingSource(ResourceSource));
    TestTrue(TEXT("Nonlocal expiry query leaves the request pending for central retirement"),
        RemotePreparation->ArrivalRequest == 62 && RemotePreparation->ArrivalPhase == EProject_JWorldArrivalPhase::Preparing
            && RemotePreparation->IsPreparedArrivalStarted());
    RemotePreparation->ArrivalPhase = EProject_JWorldArrivalPhase::Queued;
    TestTrue(TEXT("Nonlocal queued demand survives the same elapsed deadline"),
        RemotePreparation->GetStreamingSource(ResourceSource) && ResourceSource.Location.Equals(RemotePreparation->ArrivalHint,.1));
    RemoteASC->InitAbilityActorInfo(RemoteState, RemoteAvatar);
    TestFalse(TEXT("Retired nonlocal authority request cannot fall through to a remote predictor"), RemotePreparation->GetStreamingSource(ResourceSource));
    TestEqual(TEXT("Nonlocal advertisement fence does not mutate request state"), RemotePreparation->ArrivalRequest, uint64(62));
    RemotePreparation->FinishArrival(EProject_JWorldArrivalPhase::ExpiredUnconfirmed, false);
    TestFalse(TEXT("Normal nonlocal idle policy remains closed"), RemotePreparation->GetStreamingSource(ResourceSource));
    // Valid remote start producer, using the existing world writer rather than
    // a duplicate admission algorithm. This standalone native fixture has no
    // remote socket delivery; actual MainWorld covers ClientPrepare reception.
    auto* StartExecution = TestWorld->GetSubsystem<UProject_JWorldArrivalSubsystem>();
    if (!TestNotNull(TEXT("Remote starts reuse the existing world writer"), StartExecution)) return false;
    const FVector PreviousPortalPosition = Portal->GetActorLocation();
    Portal->SetActorLocation(RemoteAvatar->GetActorLocation()+FVector(100,0,0));
    RemoteASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(),100);
    RemoteAvatar->GetMover()->AddGameplayTag(Mover_IsOnGround);
    const double BurstDeadline = FPlatformTime::Seconds()+30;
    const auto StageRemoteWaiting = [&](UProject_JWorldStreamingComponent* Item, uint64 Request)
    {
        Item->ArrivalRequest = Request; Item->ArrivalPhase = EProject_JWorldArrivalPhase::Preparing;
        Item->ArrivalPawn = RemoteAvatar; Item->ArrivalPlayerState = RemoteState; Item->ArrivalAbilitySystem = RemoteASC;
        Item->ArrivalActorInfoRevision = RemoteASC->GetActorInfoRevision(); Item->ArrivalDefinition = Definition;
        Item->ArrivalAccountId = RemoteState->GetAccountId().Value; Item->ArrivalCharacterId = RemoteState->GetCharacterId().Value;
        Item->ArrivalPortal = Portal; Item->ArrivalPlaceId = Place.PlaceId; Item->ArrivalHint = Place.Location;
        Item->ArrivalRadius = Definition->PreparationRadius; Item->ArrivalDeadline = BurstDeadline;
        Item->bArrivalPreparationStarted = Item->bClientArrivalReady = Item->bArrivalOutcomeReceived = Item->bArrivalOutcomeSucceeded = false;
        return Item->RegisterArrivalExecution();
    };
    if (!TestTrue(TEXT("An unstarted current remote request registers normally"), StageRemoteWaiting(RemotePreparation,63))) return false;
    TestFalse(TEXT("Waiting remote request has no start permission"), RemotePreparation->IsPreparedArrivalStarted());
    TestFalse(TEXT("Read-only source query cannot start a waiting remote destination"), RemotePreparation->GetStreamingSource(ResourceSource));
    TestFalse(TEXT("Provider query leaves start permission untouched"), RemotePreparation->IsPreparedArrivalStarted());
    RemoteASC->InitAbilityActorInfo(RemoteState,RemoteAvatar);
    RemotePreparation->AdvancePreparedArrival(FPlatformTime::Seconds());
    TestTrue(TEXT("Same-pair epoch retirement before first start fails without permission"),
        RemotePreparation->ArrivalPhase == EProject_JWorldArrivalPhase::Failed && !RemotePreparation->IsPreparedArrivalStarted());
    if (!TestTrue(TEXT("Replacement generation may wait independently"), StageRemoteWaiting(RemotePreparation,64))) return false;
    RemotePreparation->FinishArrival(EProject_JWorldArrivalPhase::Failed,false);
    TestFalse(TEXT("Cancel before first Advance cannot retain start permission"), RemotePreparation->IsPreparedArrivalStarted());
    if (!TestTrue(TEXT("A pending provider can retire before its first Advance"), StageRemoteWaiting(RemotePreparation,65))) return false;
    const TWeakObjectPtr<UProject_JWorldStreamingComponent> RetiredBeforeStart(RemotePreparation);
    RemotePreparation->DestroyComponent(); CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("Unstarted arrival does not retain its destroyed provider through GC"), RetiredBeforeStart.IsValid());
    TestEqual(TEXT("Cancellation and GC release their exact waiting entries"), StartExecution->GetStats().Pending,0);
    TArray<TStrongObjectPtr<UProject_JWorldStreamingComponent>> StartBurst;
    for (int32 Index=0; Index<UProject_JWorldArrivalSubsystem::MaxFrameAdvances+2; ++Index)
    {
        auto* Item = NewObject<UProject_JWorldStreamingComponent>(OtherController);
        OtherController->AddInstanceComponent(Item); Item->RegisterComponent(); if (!Item->HasBegunPlay()) Item->BeginPlay();
        StartBurst.Emplace(Item);
        if (!TestTrue(TEXT("Valid burst request registers"), StageRemoteWaiting(Item,uint64(100+Index)))
            || !TestTrue(TEXT("Start fixture satisfies actual current origin and recipient eligibility"), Item->IsArrivalCurrent(true))) return false;
        TestFalse(TEXT("Registration alone cannot advertise a remote burst destination"), Item->GetStreamingSource(ResourceSource));
    }
    StartExecution->Tick(.1f);
    const auto StartPass = StartExecution->GetStats();
    int32 StartedCount = 0;
    UProject_JWorldStreamingComponent* Restarted = nullptr;
    for (const auto& Item : StartBurst)
    {
        const bool bStarted = Item->IsPreparedArrivalStarted(); StartedCount += bStarted ? 1 : 0;
        const bool bProvided = Item->GetStreamingSource(ResourceSource);
        TestEqual(TEXT("Only actually started remote requests advertise their destination"), bProvided,bStarted);
        if (bProvided) TestTrue(TEXT("Started target retains its opaque registration identity and geometry"),
            ResourceSource.Name == Item->GetStreamingSourceIdentity() && ResourceSource.Location.Equals(Place.Location,.1));
        TestEqual(TEXT("First start never extends the original authority deadline"),Item->ArrivalDeadline,BurstDeadline);
        TestTrue(TEXT("Start-only Advance does not fabricate a commit or readiness"),Item->ArrivalPhase == EProject_JWorldArrivalPhase::Preparing && !Item->bClientArrivalReady);
        if (bStarted && !Restarted) Restarted = Item.Get();
    }
    TestTrue(TEXT("Existing charged frame budget starts a finite part of the real consumer burst"),
        StartedCount>0 && StartedCount<=UProject_JWorldArrivalSubsystem::MaxFrameAdvances && StartedCount<StartBurst.Num());
    TestEqual(TEXT("Every new remote start used an existing charged Advance"),uint64(StartedCount),StartPass.Advances);
    StartExecution->Tick(.1f);
    TestEqual(TEXT("Repeated same-frame Tick cannot replenish start credits"),StartExecution->GetStats().Advances,StartPass.Advances);
    if (!TestNotNull(TEXT("The real start writer made progress"),Restarted)) return false;
    Restarted->FinishArrival(EProject_JWorldArrivalPhase::Failed,false);
    if (!TestTrue(TEXT("Same provider can register a distinct later request"),StageRemoteWaiting(Restarted,7000))) return false;
    StartExecution->Tick(.1f);
    TestFalse(TEXT("Cancel/re-register cannot restart within already charged frame/frontier"),Restarted->IsPreparedArrivalStarted());
    for (const auto& Item : StartBurst) { Item->FinishArrival(EProject_JWorldArrivalPhase::Failed,false); Item->DestroyComponent(); }
    StartBurst.Reset();
    TestEqual(TEXT("Burst cleanup retires only its existing logical entries"),StartExecution->GetStats().Pending,0);
    TestEqual(TEXT("Default burst cleanup returns every actual authority permit"),StartExecution->GetStats().ActivePreparationPermits,0);

    // Focused authority advertisement admission. The existing burst above owns
    // charged-frame scheduling coverage; these direct calls isolate quota/source
    // ownership, not WP I/O, client completion or a product capacity guarantee.
    const auto MakePreparation = [&](APlayerController* Owner)
    {
        auto* Item = NewObject<UProject_JWorldStreamingComponent>(Owner);
        Owner->AddInstanceComponent(Item); Item->RegisterComponent(); if (!Item->HasBegunPlay()) Item->BeginPlay();
        return Item;
    };
    auto* LocalFirst = MakePreparation(Controller); auto* LocalWaiting = MakePreparation(Controller);
    auto* RemoteWaiting = MakePreparation(OtherController);
    const auto StageLocalWaiting = [&](UProject_JWorldStreamingComponent* Item, uint64 Request)
    {
        Item->ArrivalRequest = Request; Item->ArrivalPhase = EProject_JWorldArrivalPhase::Preparing;
        Item->ArrivalPawn = Avatar; Item->ArrivalPlayerState = PlayerState; Item->ArrivalAbilitySystem = ASC;
        Item->ArrivalActorInfoRevision = ASC->GetActorInfoRevision(); Item->ArrivalDefinition = Definition;
        Item->ArrivalAccountId = PlayerState->GetAccountId().Value; Item->ArrivalCharacterId = PlayerState->GetCharacterId().Value;
        Item->ArrivalHint = Place.Location; Item->ArrivalRadius = Definition->PreparationRadius; Item->ArrivalDeadline = BurstDeadline;
        Item->bArrivalPreparationStarted = Item->bClientArrivalReady = Item->bArrivalOutcomeReceived = Item->bArrivalOutcomeSucceeded = false;
        return Item->RegisterArrivalExecution();
    };
    Definition->MaxSimultaneousPreparedArrivals = 1;
    if (!TestTrue(TEXT("Quota fixture registers existing local and remote entries"), StageLocalWaiting(LocalFirst,7100)
        && StageLocalWaiting(LocalWaiting,7101) && StageRemoteWaiting(RemoteWaiting,7102))) return false;
    const uint64 AdmissionsBeforeQuota = StartExecution->GetStats().PreparationAdmissions;
    TestTrue(TEXT("Local immediate start takes the shared authority permit"),LocalFirst->TryStartArrivalPreparation());
    TestFalse(TEXT("A second local immediate start cannot bypass authority capacity"),LocalWaiting->TryStartArrivalPreparation());
    RemoteWaiting->AdvancePreparedArrival(FPlatformTime::Seconds());
    TestFalse(TEXT("A deferred remote start obeys the same authority capacity"),RemoteWaiting->IsPreparedArrivalStarted());
    TestEqual(TEXT("One authority destination owns one logical permit"),StartExecution->GetStats().ActivePreparationPermits,1);
    TestTrue(TEXT("Waiting local request keeps only normal pawn prediction"),LocalWaiting->GetStreamingSource(ResourceSource)
        && ResourceSource.Location.Equals(Avatar->GetActorLocation(),.1) && !ResourceSource.Location.Equals(Place.Location,.1));
    TestFalse(TEXT("Waiting nonlocal authority request contributes no destination source"),RemoteWaiting->GetStreamingSource(ResourceSource));
    TestFalse(TEXT("Source queries cannot grant a preparation permit"),LocalWaiting->IsPreparedArrivalStarted());
    Definition->MaxSimultaneousPreparedArrivals = 0;
    TestFalse(TEXT("Invalid runtime policy cannot grant a waiting destination"),LocalWaiting->TryStartArrivalPreparation());
    TestTrue(TEXT("Invalid new-admission policy does not revoke an existing permit"),LocalFirst->IsPreparedArrivalStarted());
    Definition->MaxSimultaneousPreparedArrivals = 2;
    TestTrue(TEXT("A policy increase can admit the same waiting generation"),LocalWaiting->TryStartArrivalPreparation());
    TestEqual(TEXT("Distinct admitted entries own two permits"),StartExecution->GetStats().ActivePreparationPermits,2);
    Definition->MaxSimultaneousPreparedArrivals = 1;
    TestTrue(TEXT("Policy shrink keeps both earlier exact grants"),LocalFirst->IsPreparedArrivalStarted() && LocalWaiting->IsPreparedArrivalStarted());
    TestTrue(TEXT("Policy shrink preserves the existing destination advertisement"),LocalWaiting->GetStreamingSource(ResourceSource) && ResourceSource.Location.Equals(Place.Location,.1));
    LocalFirst->ArrivalPhase = EProject_JWorldArrivalPhase::Queued; LocalFirst->ArrivalDeadline = FPlatformTime::Seconds()-1;
    TestTrue(TEXT("Unknown queued outcome retains destination demand and its permit until the writer retires it"),LocalFirst->GetStreamingSource(ResourceSource)
        && ResourceSource.Location.Equals(Place.Location,.1) && LocalFirst->IsPreparedArrivalStarted());
    LocalWaiting->FinishArrival(EProject_JWorldArrivalPhase::Failed,false);
    RemoteWaiting->AdvancePreparedArrival(FPlatformTime::Seconds());
    TestTrue(TEXT("Returning one permit after shrink does not admit above the new limit"),StartExecution->GetStats().ActivePreparationPermits == 1 && !RemoteWaiting->IsPreparedArrivalStarted());
    LocalFirst->AdvancePreparedArrival(FPlatformTime::Seconds());
    TestTrue(TEXT("Actual unknown-outcome expiry releases its exact permit without claiming arrival"),LocalFirst->ArrivalPhase == EProject_JWorldArrivalPhase::ExpiredUnconfirmed
        && StartExecution->GetStats().ActivePreparationPermits == 0);
    RemoteWaiting->AdvancePreparedArrival(FPlatformTime::Seconds());
    TestTrue(TEXT("Existing remote writer admits a waiting generation after exact release"),RemoteWaiting->IsPreparedArrivalStarted()
        && RemoteWaiting->GetStreamingSource(ResourceSource) && ResourceSource.Location.Equals(Place.Location,.1));
    TestEqual(TEXT("Waiting admission preserves the original deadline"),RemoteWaiting->ArrivalDeadline,BurstDeadline);
    TestEqual(TEXT("Only actual new grants increment admission accounting"),StartExecution->GetStats().PreparationAdmissions-AdmissionsBeforeQuota,uint64(3));
    if (!TestTrue(TEXT("A later waiting generation can reuse its retired provider"),StageLocalWaiting(LocalWaiting,7103))) return false;
    TestFalse(TEXT("The later waiting generation still cannot bypass a full budget"),LocalWaiting->TryStartArrivalPreparation());
    LocalWaiting->DestroyComponent();
    TestEqual(TEXT("Retiring a waiting entry cannot return somebody else's permit"),StartExecution->GetStats().ActivePreparationPermits,1);
    const TWeakObjectPtr<UProject_JWorldStreamingComponent> RetiredAdmitted(RemoteWaiting);
    RemoteWaiting->DestroyComponent(); CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("An admitted entry does not retain its destroyed provider through GC"),RetiredAdmitted.IsValid());
    LocalFirst->DestroyComponent();
    TestEqual(TEXT("Normal cancellation and GC return every authority permit"),StartExecution->GetStats().ActivePreparationPermits,0);
    TestEqual(TEXT("Quota fixture leaves no duplicate logical entries"),StartExecution->GetStats().Pending,0);
    TestTrue(TEXT("Capacity deferrals are observable without a second queue"),StartExecution->GetStats().PreparationDeferrals > 0);
    Definition->MaxSimultaneousPreparedArrivals = UProject_JWorldArrivalSubsystem::MaxPending;
    RemoteAvatar->GetMover()->RemoveGameplayTag(Mover_IsOnGround); Portal->SetActorLocation(PreviousPortalPosition);
    OtherController->UnPossess(); RemoteAvatar->Destroy(); RemoteState->Destroy(); OtherController->Destroy();

    StageQueued(2, Destination); // Existing ACK checks need a fresh current incarnation.

    // Protocol/admission boundary: native readiness is already latched; this is not a WP or floor simulation.
    Preparation->ArrivalPhase = EProject_JWorldArrivalPhase::Preparing; Preparation->ArrivalPortal = Portal;
    Preparation->ArrivalPlaceId = Place.PlaceId; Preparation->ArrivalHint = Place.Location;
    Portal->SetActorLocation(Avatar->GetActorLocation()+FVector(100,0,0));
    Avatar->GetMover()->AddGameplayTag(Mover_IsOnGround);
    ON_SCOPE_EXIT { Avatar->GetMover()->RemoveGameplayTag(Mover_IsOnGround); };
    if (!TestTrue(TEXT("Current same-session stationary admission for protocol fixture"), Preparation->IsArrivalCurrent(true))) return false;
    auto* SessionBudget = PlayerState->FindComponentByClass<UProject_JCommandBudgetComponent>();
    auto* ReadyRPC = Preparation->FindFunction(TEXT("ServerReadyForArrival"));
    if (!TestNotNull(TEXT("Existing session command quota"), SessionBudget) || !TestNotNull(TEXT("Real owner ACK RPC"), ReadyRPC)) return false;
    TestTrue(TEXT("ACK is a reliable authority command"), ReadyRPC->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer | FUNC_NetReliable));
    const TStrongObjectPtr<UProject_JWorldStreamingComponent> ClientProtocol(NewObject<UProject_JWorldStreamingComponent>(Controller));
    const double ClientNow = FPlatformTime::Seconds();
    ClientProtocol->ArrivalRequest = Preparation->ArrivalRequest;
    ClientProtocol->ArrivalPhase = EProject_JWorldArrivalPhase::Preparing; ClientProtocol->bArrivalPreparationStarted = true;
    ClientProtocol->bClientArrivalReady = true; ClientProtocol->ArrivalDeadline = ClientNow+30;
    struct FReadyParams { uint64 Request; } ReadyParams{Preparation->ArrivalRequest};
    SessionBudget->CommandsPerSecond = 1;
    TestTrue(TEXT("Another command exhausts the actual session quota"), SessionBudget->TryConsume());
    Preparation->bArrivalPreparationStarted = false; // Direct protocol fixture stages an unsent request.
    const int64 BeforeEarlyACK = SessionBudget->GetRejectedCount();
    Preparation->ProcessEvent(ReadyRPC,&ReadyParams);
    TestFalse(TEXT("An unstarted request cannot accept an early owner ACK"),Preparation->bClientArrivalReady);
    TestEqual(TEXT("Early ACK is rejected before spending the actual shared quota"),SessionBudget->GetRejectedCount(),BeforeEarlyACK);
    Preparation->bArrivalPreparationStarted = true; // Subsequent quota assertions represent legitimately started work.
    if (TestTrue(TEXT("Ready client sends its first ACK"), ClientProtocol->TakeArrivalReadySend(ClientNow)))
        Preparation->ProcessEvent(ReadyRPC, &ReadyParams);
    TestFalse(TEXT("Reliable ACK rejected by actual shared quota is not accepted"), Preparation->bClientArrivalReady);
    TestEqual(TEXT("Quota rejection is visible in normal accounting"), SessionBudget->GetRejectedCount(), int64(1));
    // Deep origin validity does not justify spending work on an already
    // rate-limited ACK. Existing public accounting exposes the admission order.
    const FName OriginalDestination = Portal->DestinationPlaceId;
    Portal->DestinationPlaceId = NAME_None;
    TestTrue(TEXT("Origin change preserves the cheap current session binding"), Preparation->IsArrivalCurrent(false));
    TestFalse(TEXT("Origin change fails deep origin validation"), Preparation->IsArrivalCurrent(true));
    const int64 BeforeOriginRejection = SessionBudget->GetRejectedCount();
    Preparation->ProcessEvent(ReadyRPC, &ReadyParams);
    TestEqual(TEXT("Current-session quota rejects before invalid origin work"), SessionBudget->GetRejectedCount(), BeforeOriginRejection+1);
    Portal->DestinationPlaceId = OriginalDestination;
    // A stale owner binding must still be rejected before charging the old PS.
    Controller->PlayerState = nullptr;
    const int64 BeforeStaleBinding = SessionBudget->GetRejectedCount();
    Preparation->ProcessEvent(ReadyRPC, &ReadyParams);
    TestEqual(TEXT("Stale binding cannot charge the old session quota"), SessionBudget->GetRejectedCount(), BeforeStaleBinding);
    Controller->PlayerState = PlayerState;
    const int64 RejectedBeforeRetry = SessionBudget->GetRejectedCount();
    TestFalse(TEXT("A local ready latch does not flood immediate repeated ACKs"), ClientProtocol->TakeArrivalReadySend(ClientNow+.49));
    SessionBudget->CommandsPerSecond = 80; // Recover actual admission without private clock changes or a real-time wait.
    if (TestTrue(TEXT("Unconfirmed same-generation ACK is eligible again at bounded retry"), ClientProtocol->TakeArrivalReadySend(ClientNow+.5)))
        Preparation->ProcessEvent(ReadyRPC, &ReadyParams);
    TestTrue(TEXT("Retry recovers the same pending authority request"), Preparation->bClientArrivalReady);
    SessionBudget->CommandsPerSecond = 1;
    if (TestTrue(TEXT("Client may resend while terminal reply is in flight"), ClientProtocol->TakeArrivalReadySend(ClientNow+1)))
        Preparation->ProcessEvent(ReadyRPC, &ReadyParams);
    TestEqual(TEXT("Accepted duplicate ACK is idempotent before quota consumption"), SessionBudget->GetRejectedCount(), RejectedBeforeRetry);
    ClientProtocol->FinishArrival(EProject_JWorldArrivalPhase::Arrived, false);
    TestFalse(TEXT("Completed request sends no further ACK"), ClientProtocol->TakeArrivalReadySend(ClientNow+2));
    Preparation->ArrivalPhase = EProject_JWorldArrivalPhase::Queued;

    // Confirmed native evidence survives crossing the deadline before the next10Hz observer tick.
    StageQueued(3, LegalPlacement);
    FProject_JPreparedArrivalEffect ConfirmedEffect;
    ConfirmedEffect.Request = 3; ConfirmedEffect.Recipient = Preparation; ConfirmedEffect.ExpectedPawn = Avatar; ConfirmedEffect.TargetLocation = LegalPlacement;
    FApplyMovementEffectParams ConfirmedParams = Params; ConfirmedParams.OutputEvents.Reset();
    FMoverSyncState ConfirmedOutput;
    if (!TestTrue(TEXT("Known-outcome boundary uses a real native legal teleport"), ConfirmedEffect.ApplyMovementEffect(ConfirmedParams, ConfirmedOutput))
        || !TestEqual(TEXT("Known-outcome native receipt exists"), ConfirmedParams.OutputEvents.Num(), 1)) return false;
    ConfirmedParams.OutputEvents[0]->OnEventProcessed(Context);
    TestTrue(TEXT("Native success is recorded before expiry"), Preparation->bArrivalOutcomeReceived && Preparation->bArrivalOutcomeSucceeded);
    Preparation->ArrivalDeadline = FPlatformTime::Seconds()-1;
    FWorldPartitionStreamingSource ConfirmedDemand;
    TestTrue(TEXT("Known exact native outcome retains destination demand until the observer confirms placement"),
        Preparation->GetStreamingSource(ConfirmedDemand) && ConfirmedDemand.Location.Equals(Preparation->ArrivalHint,.1));
    Preparation->AdvancePreparedArrival(FPlatformTime::Seconds());
    TestTrue(TEXT("Known exact native success plus real placement wins over observation-after-deadline"), Preparation->ArrivalPhase == EProject_JWorldArrivalPhase::Arrived);
    StageQueued(4, LegalPlacement); Preparation->ArrivalDeadline = FPlatformTime::Seconds()-1;
    Preparation->AdvancePreparedArrival(FPlatformTime::Seconds());
    TestTrue(TEXT("Only an expired queue without an outcome stays unconfirmed"), Preparation->ArrivalPhase == EProject_JWorldArrivalPhase::ExpiredUnconfirmed);
    ConfirmedParams.OutputEvents[0]->OnEventProcessed(Context);
    TestTrue(TEXT("An old receipt cannot revise a terminated generation"), Preparation->ArrivalPhase == EProject_JWorldArrivalPhase::ExpiredUnconfirmed);
    Floor->SetCollisionObjectType(ECC_Pawn);
    TestFalse(TEXT("A pawn body cannot substitute for arrival ground"), Preparation->FindArrivalPlacement(Avatar, Place.Location, LegalPlacement));
    // The queued placement was valid, but support/collision can change before
    // the native effect is consumed. Use the real collision scene and effect;
    // this deliberately does not impersonate WP readiness or an actual queue tick.
    Floor->SetCollisionObjectType(ECC_WorldDynamic);
    auto* ObstacleActor = TestWorld->SpawnActor<AActor>(Spawn);
    if (!TestNotNull(TEXT("Commit obstacle owner"), ObstacleActor)) return false;
    auto* Obstacle = NewObject<UBoxComponent>(ObstacleActor);
    ObstacleActor->SetRootComponent(Obstacle); ObstacleActor->AddInstanceComponent(Obstacle);
    Obstacle->SetBoxExtent(FVector(300,300,300)); Obstacle->SetCollisionObjectType(ECC_WorldStatic);
    Obstacle->SetCollisionResponseToAllChannels(ECR_Block); Obstacle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Obstacle->RegisterComponent(); ObstacleActor->SetActorLocation(LegalPlacement);
    const FVector Origin(0,0,100);
    const TCHAR* Changes[] = {TEXT("disabled support"), TEXT("pawn support"), TEXT("new blocking geometry"), TEXT("target differs from prepared placement"), TEXT("destroyed support")};
    auto* ArrivalExecution = TestWorld->GetSubsystem<UProject_JWorldArrivalSubsystem>();
    if (!TestNotNull(TEXT("Actual arrival execution owner"), ArrivalExecution)) return false;
    TSharedPtr<FMoverSimulationEventData> RetiredCommitEvent;
    for (int32 Case = 0; Case < UE_ARRAY_COUNT(Changes); ++Case)
    {
        Preparation->FinishArrival(EProject_JWorldArrivalPhase::Idle, false);
        const auto BeforeReservation = ArrivalExecution->GetStats();
        Floor->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Floor->SetCollisionObjectType(ECC_WorldDynamic);
        Obstacle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        if (!TestTrue(TEXT("Commit fixture resets with public native teleport"), Avatar->TeleportTo(Origin, FRotator::ZeroRotator, false, true))) return false;
        FVector PreparedPlacement;
        if (!TestTrue(TEXT("Destination is legal before queueing the effect"), Preparation->FindArrivalPlacement(Avatar, Place.Location, PreparedPlacement))) return false;
        TestTrue(TEXT("Each case starts at the same recorded legal target"), PreparedPlacement.Equals(LegalPlacement,.01));
        const uint64 Generation = uint64(100+Case);
        StageQueued(Generation, PreparedPlacement);
        TestEqual(TEXT("Queued commit owns one logical permit"), ArrivalExecution->GetStats().ActivePreparationPermits, BeforeReservation.ActivePreparationPermits+1);
        TestEqual(TEXT("Queued commit owns one execution entry"), ArrivalExecution->GetStats().Pending, BeforeReservation.Pending+1);
        if (Case == 0) Floor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        else if (Case == 1) Floor->SetCollisionObjectType(ECC_Pawn);
        else if (Case == 2) Obstacle->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        else if (Case == 4) FloorActor->Destroy();
        FProject_JPreparedArrivalEffect CommitEffect;
        CommitEffect.Request = Generation; CommitEffect.Recipient = Preparation; CommitEffect.ExpectedPawn = Avatar;
        CommitEffect.TargetLocation = PreparedPlacement + (Case == 3 ? FVector(150,0,0) : FVector::ZeroVector);
        FApplyMovementEffectParams CommitParams = Params; CommitParams.OutputEvents.Reset();
        auto ExistingEvent = MakeShared<FTeleportSucceededEventData>(Step, Origin, FQuat::Identity, Origin, FQuat::Identity);
        CommitParams.OutputEvents.Add(ExistingEvent);
        FMoverSyncState CommitOutput;
        auto& Sentinel = CommitOutput.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>();
        const FVector SentinelLocation(11,22,33), SentinelVelocity(4,5,6);
        Sentinel.SetTransforms_WorldSpace(SentinelLocation, FRotator::ZeroRotator, SentinelVelocity, FVector::ZeroVector);
        const bool bApplied = CommitEffect.ApplyMovementEffect(CommitParams, CommitOutput);
        TestFalse(FString::Printf(TEXT("%s cannot commit a queued teleport"), Changes[Case]), bApplied);
        TestTrue(FString::Printf(TEXT("%s preserves the pawn transform"), Changes[Case]), Avatar->GetActorLocation().Equals(Origin,.01));
        const auto* Preserved = CommitOutput.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
        TestTrue(TEXT("Rejected commit preserves the existing simulation output"), Preserved && Preserved->GetLocation_WorldSpace().Equals(SentinelLocation)
            && Preserved->GetVelocity_WorldSpace().Equals(SentinelVelocity));
        TestEqual(TEXT("Rejected commit appends exactly one event"), CommitParams.OutputEvents.Num(),2);
        TestTrue(TEXT("Existing simulation events remain intact"), CommitParams.OutputEvents.Num() >= 1 && CommitParams.OutputEvents[0] == ExistingEvent);
        TestFalse(TEXT("Apply does not directly certify a delivered outcome"), Preparation->bArrivalOutcomeReceived);
        if (CommitParams.OutputEvents.Num() == 2 && CommitParams.OutputEvents[1])
        {
            auto Receipt = CommitParams.OutputEvents[1];
            const auto* Failure = Receipt->CastTo<FTeleportFailedEventData>();
            TestTrue(TEXT("Destination rejection uses the native failure event"), Failure && Failure->FromLocation.Equals(Origin,.01)
                && Failure->ToLocation.Equals(CommitEffect.TargetLocation,.01));
            Receipt->OnEventProcessed(Context);
            TestTrue(TEXT("Only processed exact failure records the failed outcome"), Preparation->bArrivalOutcomeReceived && !Preparation->bArrivalOutcomeSucceeded);
            Preparation->AdvancePreparedArrival(FPlatformTime::Seconds());
            TestTrue(TEXT("Known rejection finishes Failed without waiting for timeout"), Preparation->ArrivalPhase == EProject_JWorldArrivalPhase::Failed);
            TestTrue(TEXT("Failed commit withdraws its exact source permission and request"), !Preparation->IsPreparedArrivalStarted() && !Preparation->ArrivalRequest && !Preparation->ArrivalExecutionToken);
            TestEqual(TEXT("Failed commit returns its logical permit"), ArrivalExecution->GetStats().ActivePreparationPermits, BeforeReservation.ActivePreparationPermits);
            TestEqual(TEXT("Failed commit retires its execution entry"), ArrivalExecution->GetStats().Pending, BeforeReservation.Pending);
            RetiredCommitEvent = Receipt;
        }
        AddInfo(FString::Printf(TEXT("Arrival122 case=%s applied=%d moved=%d events=%d phase=%d permit=%d"), Changes[Case],bApplied,
            !Avatar->GetActorLocation().Equals(Origin,.01),CommitParams.OutputEvents.Num(),int32(Preparation->ArrivalPhase),ArrivalExecution->GetStats().ActivePreparationPermits));
    }
    // A retained old failure event cannot confirm or fail the replacement request.
    StageQueued(200, LegalPlacement);
    if (RetiredCommitEvent) RetiredCommitEvent->OnEventProcessed(Context);
    TestTrue(TEXT("Retired commit event cannot mutate the replacement generation"), Preparation->ArrivalRequest == 200
        && Preparation->ArrivalPhase == EProject_JWorldArrivalPhase::Queued && !Preparation->bArrivalOutcomeReceived);
    Preparation->FinishArrival(EProject_JWorldArrivalPhase::Failed, false);
    const TWeakObjectPtr<UProject_JWorldStreamingComponent> Retired(Preparation);
    Preparation->DestroyComponent(); CollectGarbage(RF_NoFlags);
    Params.OutputEvents[0]->OnEventProcessed(Context); // Native weak delivery must be harmless after teardown/GC.
    TestFalse(TEXT("Effect and receipt do not retain the retired provider"), Retired.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJWorldArrivalExecutionBudgetTest, "ProjectJ.Foundation.World.PreparedArrivalExecutionBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJWorldArrivalExecutionBudgetTest::RunTest(const FString&)
{
    TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine)); Instance->InitializeStandalone();
    UWorld* TestWorld = Instance->GetWorld();
    ON_SCOPE_EXIT { if (TestWorld->HasBegunPlay()) TestWorld->EndPlay(EEndPlayReason::Quit); Instance->Shutdown(); TestWorld->DestroyWorld(false); GEngine->DestroyWorldContext(TestWorld); };
    TestWorld->InitializeActorsForPlay(FURL());
    TestWorld->GetWorldSettings()->NotifyBeginPlay(); TestWorld->GetWorldSettings()->NotifyMatchStarted();
    auto* Controller = TestWorld->SpawnActor<APlayerController>();
    if (!TestNotNull(TEXT("Logical record fixture controller"), Controller)) return false;
    Controller->SetAsLocalPlayerController();
    // This bounded bookkeeping fixture is separate from the native movement
    // fences above and the actual MainWorld two-owner WP/Mover probe.
    auto* Execution = TestWorld->GetSubsystem<UProject_JWorldArrivalSubsystem>();
    if (!TestNotNull(TEXT("Game world owns the production arrival writer"), Execution)) return false;
    TArray<TStrongObjectPtr<UProject_JWorldStreamingComponent>> Scheduled;
    for (int32 Index = 0; Index <= UProject_JWorldArrivalSubsystem::MaxPending; ++Index)
    {
        auto* Item = NewObject<UProject_JWorldStreamingComponent>(Controller);
        Controller->AddInstanceComponent(Item); Item->RegisterComponent();
        if (!Item->HasBegunPlay()) Item->BeginPlay();
        Scheduled.Emplace(Item);
        Item->ArrivalRequest = uint64(1000+Index); Item->ArrivalPhase = EProject_JWorldArrivalPhase::Queued;
        Item->bArrivalPreparationStarted = true;
        Item->ArrivalDeadline = FPlatformTime::Seconds()+30;
        const bool Registered = Item->RegisterArrivalExecution();
        TestEqual(TEXT("Logical backpressure accepts only the fixed record capacity"), Registered, Index < UProject_JWorldArrivalSubsystem::MaxPending);
        if (!Registered) Item->FinishArrival(EProject_JWorldArrivalPhase::Failed, false);
        TestFalse(TEXT("A registered arrival has no independent component Tick writer"), Item->PrimaryComponentTick.bCanEverTick);
    }
    TestEqual(TEXT("Pending records reach their logical bound"), Execution->GetStats().Pending, UProject_JWorldArrivalSubsystem::MaxPending);
    const uint64 RejectedAtCapacity = Execution->GetStats().Rejected;
    TestTrue(TEXT("Logical backpressure is observable"), RejectedAtCapacity > 0);
    // Missing recipients deliberately terminate through the real private
    // Advance contract. No WP/collision readiness or I/O success is fabricated.
    Execution->Tick(.1f);
    const auto FirstPass = Execution->GetStats();
    TestTrue(TEXT("Native frame visit work is bounded"), FirstPass.FrameVisits > 0 && FirstPass.FrameVisits <= UProject_JWorldArrivalSubsystem::MaxFrameVisits);
    TestTrue(TEXT("Native frame Advance work is bounded"), FirstPass.FrameAdvances > 0 && FirstPass.FrameAdvances <= UProject_JWorldArrivalSubsystem::MaxFrameAdvances);
    TestEqual(TEXT("Frame stats use the real engine counter"), FirstPass.Frame, GFrameCounter);
    Execution->Tick(.1f);
    TestEqual(TEXT("Sequential calls in one real frame cannot replenish Advance credits"), Execution->GetStats().Advances, FirstPass.Advances);
    TestEqual(TEXT("The same frame retains its admission frontier"), Execution->AdmissionFrontier, uint64(UProject_JWorldArrivalSubsystem::MaxPending));
    auto* Reused = Scheduled[UProject_JWorldArrivalSubsystem::MaxPending-1].Get();
    const uint64 OldToken = Reused->ArrivalExecutionToken, OldRequest = Reused->ArrivalRequest;
    TestTrue(TEXT("Late ring member is still pending after the bounded pass"), OldToken != 0);
    Reused->FinishArrival(EProject_JWorldArrivalPhase::Failed, false);
    Reused->ArrivalRequest = 5000; Reused->ArrivalPhase = EProject_JWorldArrivalPhase::Preparing;
    Reused->bArrivalPreparationStarted = false;
    Reused->ArrivalDeadline = FPlatformTime::Seconds()+30;
    if (!TestTrue(TEXT("A finished logical record can be reused by a new generation"), Reused->RegisterArrivalExecution())) return false;
    const uint64 NewToken = Reused->ArrivalExecutionToken;
    TestTrue(TEXT("A reused exact registration can own one logical authority permit"), Execution->TryAdmitPreparation(NewToken, Reused, 5000, 1));
    TestTrue(TEXT("Repeated admission of the same exact Entry is idempotent"), Execution->TryAdmitPreparation(NewToken, Reused, 5000, 1));
    TestEqual(TEXT("Idempotent admission cannot double-count a permit"), Execution->GetStats().ActivePreparationPermits, 1);
    const double NewDue = Execution->Entries.FindChecked(NewToken).NextDue;
    Execution->Unregister(OldToken, Reused, OldRequest);
    TestTrue(TEXT("Old token/request unregister cannot remove the replacement"), Execution->Entries.Contains(NewToken));
    TestEqual(TEXT("Old unregister cannot return a replacement generation's permit"), Execution->GetStats().ActivePreparationPermits, 1);
    Execution->Tick(.1f);
    TestEqual(TEXT("Cancel/re-register does not refund the same frame work"), Execution->GetStats().Advances, FirstPass.Advances);
    TestEqual(TEXT("Old visit cannot overwrite a new generation due time"), Execution->Entries.FindChecked(NewToken).NextDue, NewDue);
    TestEqual(TEXT("Replacement request remains unprocessed in the original frame"), Reused->ArrivalRequest, uint64(5000));
    // Stop uses the same lifecycle path as world teardown and does not own or
    // claim cancellation of engine asynchronous streaming/queued Mover tasks.
    Execution->Stop();
    TestEqual(TEXT("World stop releases all logical records"), Execution->GetStats().Pending, 0);
    TestEqual(TEXT("World stop closes every authority logical permit before callbacks"), Execution->GetStats().ActivePreparationPermits, 0);
    for (const auto& Item : Scheduled)
    {
        TestEqual(TEXT("World stop detaches every component lease"), Item->ArrivalExecutionToken, uint64(0));
        Item->DestroyComponent();
    }
    Scheduled.Reset();

    return true;
}

#endif
