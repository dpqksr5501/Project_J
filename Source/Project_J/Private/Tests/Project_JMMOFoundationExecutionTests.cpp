#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "HAL/PlatformProcess.h"
#include <limits>
#include "Misc/DataValidation.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "Game/Project_JPlayerState.h"
#include "Components/Project_JCommandBudgetComponent.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "GameFramework/Pawn.h"
#include "Inventory/Project_JConsumableDefinition.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "KinematicActorSimulation.h"
#include "MovementModeStateMachine.h"
#include "MoverComponent.h"
#include "Tests/Project_JMountLifecycleFixture.h"
#include "Project_JNPCCharacter.h"
#include "AIController.h"
#include "Components/Project_JNPCActionComponent.h"
#include "Components/Project_JNPCActivationComponent.h"
#include "Combat/Project_JGameplayAbility_NPCAttack.h"
#include "System/Project_JNPCDecisionSubsystem.h"
#include "System/Project_JNPCPathSubsystem.h"
#include "Project_JAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Project_JMMOArchitectureReceiver.h"
#include "Project_JNPCCallbackFixture.h"
#include "UObject/GarbageCollection.h"
#include "World/Project_JWorldEncounter.h"
#include "World/Project_JWorldEventRegistry.h"
#include "Optimization/Project_JTargetSpatialSnapshot.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"

namespace ProjectJFoundationTests
{
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
struct FWorld
{
    UWorld* World;
    explicit FWorld(EWorldType::Type Type = EWorldType::Game) : World(UWorld::CreateWorld(Type, false))
    { GEngine->CreateNewWorldContext(Type).SetCurrentWorld(World); }
    void Start()
    {
        World->InitializeActorsForPlay(FURL());
        World->GetWorldSettings()->NotifyBeginPlay();
        World->GetWorldSettings()->NotifyMatchStarted();
    }
    ~FWorld()
    {
        World->EndPlay(EEndPlayReason::LevelTransition);
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
    }
};

class FPreparationLifecycle final : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    FWorld Fixture;
    TArray<TWeakObjectPtr<AProject_JWorldEncounter>> Events;
    TStrongObjectPtr<AProject_JWorldEncounter> Cancelled;
    TWeakObjectPtr<AProject_JWorldEncounter> WithoutGround;
    TWeakObjectPtr<AActor> GroundPawnBlocker;
    double Started = FPlatformTime::Seconds();
    bool bPrepared = false, bDeathCasesStarted = false, bResolutionRetiredOwner = false;
    TArray<TStrongObjectPtr<UProject_JEncounterResolutionReceiver>> Resolutions;
    int32 ComponentAccepted = 0, ComponentCompleted = 0;
public:
    explicit FPreparationLifecycle(FAutomationTestBase* InTest) : Test(InTest)
    {
        auto* World = Fixture.World;
        auto* Ground = World->SpawnActor<AActor>();
        auto* Box = NewObject<UBoxComponent>(Ground);
        Ground->SetRootComponent(Box); Ground->AddInstanceComponent(Box);
        Box->SetBoxExtent(FVector(25000, 25000, 50)); Box->SetMobility(EComponentMobility::Static);
        Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Box->SetCollisionObjectType(ECC_WorldStatic);
        Box->SetCollisionResponseToAllChannels(ECR_Block);
        Ground->SetActorLocation(FVector(0, 0, -50)); Box->RegisterComponent();
        const auto AddSupport = [World](FVector Location, FVector Extent, ECollisionChannel ObjectType)
        {
            auto* Actor = World->SpawnActor<AActor>();
            auto* Collision = NewObject<UBoxComponent>(Actor);
            Actor->SetRootComponent(Collision); Actor->AddInstanceComponent(Collision);
            Collision->SetBoxExtent(Extent); Collision->SetMobility(EComponentMobility::Movable);
            Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Collision->SetCollisionObjectType(ObjectType);
            Collision->SetCollisionResponseToAllChannels(ECR_Block);
            Actor->SetActorLocation(Location); Collision->RegisterComponent();
            return Actor;
        };
        // Pawn responses block WorldStatic too, but cannot certify support. A dynamic
        // platform remains intentional support; do not replace the channel query with a static-only filter.
        GroundPawnBlocker = AddSupport(FVector(0,0,3000), FVector(25000,25000,50), ECC_Pawn);
        AddSupport(FVector(8000,0,1000), FVector(500,500,50), ECC_WorldDynamic);
        AddSupport(FVector(50000,0,1000), FVector(500,500,50), ECC_Pawn);
        auto* Controller = World->SpawnActor<APlayerController>(); Controller->Possess(World->SpawnActor<APawn>());
        for (int32 Index = 0; Index < 5; ++Index)
        {
            auto* Event = World->SpawnActor<AProject_JWorldEncounter>();
            Event->EncounterId = FName(*FString::Printf(TEXT("Foundation.AsyncEvent.%d"), Index));
            Event->NPCClass = Index == 4 ? AProject_JNPCCallbackFixture::StaticClass() : AProject_JNPCCharacter::StaticClass();
            Resolutions.Emplace(NewObject<UProject_JEncounterResolutionReceiver>());
            Event->OnResolved.AddDynamic(Resolutions.Last().Get(), &UProject_JEncounterResolutionReceiver::Resolved);
            Event->SetActorLocation(FVector(Index * 2000, 0, 0));
            Events.Add(Event);
        }
        auto* Unready = World->SpawnActor<AProject_JWorldEncounter>();
        Unready->EncounterId = TEXT("Foundation.AsyncEvent.PawnOnly");
        Unready->NPCClass = AProject_JNPCCharacter::StaticClass();
        Unready->SetActorLocation(FVector(50000,0,0)); WithoutGround = Unready;
        Cancelled.Reset(Events[0].Get());
        Fixture.Start();
    }
    bool Update() override
    {
        if (FPlatformTime::Seconds() - Started > 20) { Test->AddError(TEXT("Real event preparation/spawn lifecycle timed out")); return true; }
        auto* World = Fixture.World;
        World->Tick(LEVELTICK_All, bPrepared ? 1.f / 60.f : 1.1f);
        auto* Registry = World->GetSubsystem<UProject_JWorldEventRegistry>();
        const auto& Stats = Registry->GetStats();
        Test->TestTrue(TEXT("Actual ready-load applications respect the spawn bound"), Stats.LastTickSpawns <= Registry->MaxSpawnsPerTick);
        Test->TestTrue(TEXT("Actual async reservations remain bounded"), Stats.Preparations <= Registry->MaxPreparations);
        if (!bPrepared)
        {
            bPrepared = true;
            FHitResult Unfiltered;
            Test->TestTrue(TEXT("Real Pawn collision object blocks the original ground channel"),
                World->LineTraceSingleByChannel(Unfiltered, FVector(0,0,5000), FVector(0,0,-15000), ECC_WorldStatic));
            Test->TestTrue(TEXT("Original query would project onto the Pawn blocker"), Unfiltered.GetActor() == GroundPawnBlocker.Get());
            Cancelled->Destroy(); // Retire one real consumer while its preparation or activation is live.
            const auto NativeNPC = FSoftObjectPath(AProject_JNPCCharacter::StaticClass());
            for (int32 Index = 0; Index < Registry->MaxPreparations + 2; ++Index)
            {
                auto* Owner = NewObject<USceneComponent>(GroundPawnBlocker.Get());
                GroundPawnBlocker->AddInstanceComponent(Owner); Owner->RegisterComponent();
                if (Registry->RequestComponentPreparation(Owner, NativeNPC, [this](uint64, bool bCompleted)
                    { Test->TestTrue(TEXT("Shared component preparation completes without timeout"), bCompleted); ++ComponentCompleted; })) ++ComponentAccepted;
            }
            Test->TestTrue(TEXT("Encounter and component owners share the same physical cap"), Registry->GetStats().Preparations <= Registry->MaxPreparations);
            return false;
        }
        if (bDeathCasesStarted)
        {
            if (Resolutions[3]->Calls == 0 || Resolutions[4]->Calls == 0) return false;
            Test->TestEqual(TEXT("Destroy-before-poll emits exactly one successful resolution"), Resolutions[1]->Calls, 1);
            Test->TestEqual(TEXT("Healthy destruction never becomes a successful resolution"), Resolutions[2]->Calls, 0);
            Test->TestEqual(TEXT("Ordinary polled death still resolves once"), Resolutions[3]->Calls, 1);
            Test->TestEqual(TEXT("Nested destroy inside IsDead resolves once"), Resolutions[4]->Calls, 1);
            Test->TestTrue(TEXT("Resolution callback may retire its encounter owner and collect garbage"), bResolutionRetiredOwner && !Events[4].IsValid());
            WithoutGround->Destroy();
            for (const auto& Event : Events) if (Event.IsValid()) Event->Destroy();
            for (TActorIterator<AProject_JNPCCharacter> It(World); It; ++It)
                Test->TestTrue(TEXT("Encounter retirement leaves no live owned NPC"), It->IsActorBeingDestroyed());
            Test->TestFalse(TEXT("Retired encounters leave no idle scheduler tick"), Registry->IsTickable());
            return true;
        }
        int32 Spawned = 0;
        for (int32 Index = 1; Index < Events.Num(); ++Index)
            if (const auto* Event = Events[Index].Get(); Event && Event->SpawnRevision > 0) ++Spawned;
        if (Spawned < 4 || ComponentCompleted < ComponentAccepted || !WithoutGround.IsValid() || WithoutGround->Phase != EProject_JEncounterPhase::Cooldown) return false;
        Test->TestTrue(TEXT("Actual registered component consumer used shared preparation"), ComponentAccepted > 0);
        Test->TestEqual(TEXT("Pawn-only location never becomes a successful spawn"), WithoutGround->SpawnRevision, 0);
        for (TActorIterator<AProject_JNPCCharacter> It(World); It; ++It)
        {
            const auto* Encounter = Cast<AProject_JWorldEncounter>(It->GetOwner());
            if (!Encounter || It->IsActorBeingDestroyed()) continue;
            const double Height = It->GetActorLocation().Z;
            if (FMath::IsNearlyEqual(Encounter->GetActorLocation().X, 8000.0))
                Test->TestTrue(TEXT("Existing blocking dynamic platform remains usable support"), Height > 900 && Height < 2000);
            else Test->TestTrue(TEXT("Pawn blocker cannot replace the ready static world floor"), Height > -100 && Height < 500);
        }
        Test->TestTrue(TEXT("Real async completions exercised the apply queue"), Stats.SpawnApplications >= 4);
        Test->TestEqual(TEXT("Completed and cancelled consumers release all preparation slots"), Stats.Preparations, 0);
        Test->AddInfo(FString::Printf(TEXT("WorldEventLifecycle spawned=%d applications=%llu peak_preparations=%d"), Spawned, Stats.SpawnApplications, Stats.PeakPreparations));
        TArray<TStrongObjectPtr<AProject_JNPCCharacter>> SpawnedNPCs;
        for (int32 Index = 1; Index < Events.Num(); ++Index)
        {
            AProject_JNPCCharacter* Found = nullptr;
            for (TActorIterator<AProject_JNPCCharacter> It(World); It; ++It)
                if (It->GetOwner() == Events[Index].Get() && !It->IsActorBeingDestroyed()) { Found = *It; break; }
            if (!Found) { Test->AddError(TEXT("Expected actual prepared NPC for resolution regression")); return true; }
            SpawnedNPCs.Emplace(Found);
        }
        const double Now = World->GetTimeSeconds();
        SpawnedNPCs[0]->GetAbilitySystemComponent()->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 0);
        Test->TestTrue(TEXT("Dead NPC is externally destroyed before the next registry poll"), SpawnedNPCs[0]->Destroy());
        Test->TestTrue(TEXT("Death-triggered destruction preserves successful cooldown and spawn identity"),
            Resolutions[1]->Calls == 1 && Resolutions[1]->LastId == Events[1]->EncounterId && Resolutions[1]->LastRevision == Events[1]->SpawnRevision &&
            Events[1]->Phase == EProject_JEncounterPhase::Cooldown && FMath::IsNearlyEqual(Events[1]->NextSpawnServerTime, Now + Events[1]->RespawnSeconds));
        Test->TestTrue(TEXT("Healthy NPC destruction remains failure cleanup"), SpawnedNPCs[1]->GetAttributeSet()->GetHealth() > 0 && SpawnedNPCs[1]->Destroy());
        Test->TestTrue(TEXT("Healthy cleanup uses existing ten-second retry, with no resolution"), Resolutions[2]->Calls == 0 &&
            Events[2]->Phase == EProject_JEncounterPhase::Cooldown && FMath::IsNearlyEqual(Events[2]->NextSpawnServerTime, Now + 10.0));
        SpawnedNPCs[2]->GetAbilitySystemComponent()->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 0);
        auto* CallbackNPC = CastChecked<AProject_JNPCCallbackFixture>(SpawnedNPCs[3].Get());
        CallbackNPC->bReportedDead = true;
        CallbackNPC->OnLifeQuery = [CallbackNPC] { CallbackNPC->Destroy(); };
        Resolutions[4]->OnResolution = [this]
        {
            bResolutionRetiredOwner = Events[4].IsValid() && Events[4]->Destroy();
            CollectGarbage(RF_NoFlags, true);
        };
        bDeathCasesStarted = true;
        return false;
    }
};

class FEncounterDemandLifecycle final : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    FWorld Fixture;
    TWeakObjectPtr<APawn> Observer;
    TWeakObjectPtr<AActor> Ground;
    TWeakObjectPtr<AProject_JWorldEncounter> Event, ChangedDefinition;
    TStrongObjectPtr<UProject_JEncounterResolutionReceiver> Resolution;
    double Started = FPlatformTime::Seconds();
    int32 Phase = 0, ComponentCompleted = 0;
    uint64 ComponentToken = 0;

    int32 OwnedNPCs(AProject_JWorldEncounter* Owner) const
    {
        int32 Count = 0;
        for (TActorIterator<AProject_JNPCCharacter> It(Fixture.World); It; ++It)
            if (It->GetOwner() == Owner && !It->IsActorBeingDestroyed()) ++Count;
        return Count;
    }
public:
    explicit FEncounterDemandLifecycle(FAutomationTestBase* InTest) : Test(InTest),
        Resolution(NewObject<UProject_JEncounterResolutionReceiver>())
    {
        auto* World = Fixture.World;
        auto* Support = World->SpawnActor<AActor>(); Ground = Support;
        auto* Box = NewObject<UBoxComponent>(Support);
        Support->SetRootComponent(Box); Support->AddInstanceComponent(Box);
        Box->SetBoxExtent(FVector(25000,25000,50)); Box->SetMobility(EComponentMobility::Static);
        Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Box->SetCollisionObjectType(ECC_WorldStatic);
        Box->SetCollisionResponseToAllChannels(ECR_Block);
        Support->SetActorLocation(FVector(0,0,-50)); Box->RegisterComponent();
        auto* Pawn = World->SpawnActor<APawn>(); Observer = Pawn;
        auto* Root = NewObject<USceneComponent>(Pawn);
        Pawn->SetRootComponent(Root); Pawn->AddInstanceComponent(Root); Root->RegisterComponent();
        Pawn->SetActorLocation(FVector(0,0,200));
        World->SpawnActor<APlayerController>()->Possess(Pawn);
        auto* Encounter = World->SpawnActor<AProject_JWorldEncounter>(); Event = Encounter;
        Encounter->EncounterId = TEXT("Foundation.Demand.Recipient");
        Encounter->NPCClass = AProject_JNPCCharacter::StaticClass(); Encounter->ActivationRadius = 1000;
        Encounter->OnResolved.AddDynamic(Resolution.Get(), &UProject_JEncounterResolutionReceiver::Resolved);
        auto* Reconfigured = World->SpawnActor<AProject_JWorldEncounter>(); ChangedDefinition = Reconfigured;
        Reconfigured->EncounterId = TEXT("Foundation.Demand.ClassChanged");
        Reconfigured->NPCClass = AProject_JNPCCharacter::StaticClass(); Reconfigured->SetActorLocation(FVector(4000,0,0));
        Reconfigured->OnResolved.AddDynamic(Resolution.Get(), &UProject_JEncounterResolutionReceiver::Resolved);
        // The replacement class is already available; an old A request must not authorize spawning B.
        AProject_JNPCCallbackFixture::StaticClass();
        Fixture.Start();
    }
    bool Update() override
    {
        if (FPlatformTime::Seconds()-Started > 20) { Test->AddError(TEXT("Actual encounter demand cancellation/recovery timed out")); return true; }
        auto* World = Fixture.World; auto* Encounter = Event.Get(); auto* Pawn = Observer.Get();
        auto* Registry = World->GetSubsystem<UProject_JWorldEventRegistry>();
        if (!IsValid(Encounter) || !IsValid(Pawn) || !Registry) { Test->AddError(TEXT("Demand lifecycle lost its actual owner/observer/registry")); return true; }
        if (Phase == 0)
        {
            auto* Reconfigured = ChangedDefinition.Get();
            if (!Reconfigured) { Test->AddError(TEXT("Class-change owner retired before initial preparation")); return true; }
            Test->AddInfo(FString::Printf(TEXT("NativePreparingWarmup scenario=demand step=0 phase=%d changed_phase=%d game_time=%.6f max_undilated=%.6f"),
                int32(Encounter->Phase), int32(Reconfigured->Phase), World->GetTimeSeconds(), World->GetWorldSettings()->MaxUndilatedFrameTime));
            // Normal world deltas may be clamped. Observe the native hash stagger
            // in this Update without advancing the global streamable delegate ticker.
            for (int32 Step = 0; Step < 16 && (Encounter->Phase == EProject_JEncounterPhase::Dormant || Reconfigured->Phase == EProject_JEncounterPhase::Dormant); ++Step)
            {
                World->Tick(LEVELTICK_All, .1f);
                Test->AddInfo(FString::Printf(TEXT("NativePreparingWarmup scenario=demand step=%d phase=%d changed_phase=%d game_time=%.6f max_undilated=%.6f"),
                    Step+1, int32(Encounter->Phase), int32(Reconfigured->Phase), World->GetTimeSeconds(), World->GetWorldSettings()->MaxUndilatedFrameTime));
                if ((Encounter->Phase != EProject_JEncounterPhase::Dormant && Encounter->Phase != EProject_JEncounterPhase::Preparing)
                    || (Reconfigured->Phase != EProject_JEncounterPhase::Dormant && Reconfigured->Phase != EProject_JEncounterPhase::Preparing)) break;
            }
            if (!Test->TestEqual(TEXT("Natural world ticks reach the first demand preparation"), Encounter->Phase, EProject_JEncounterPhase::Preparing)
                || !Test->TestTrue(TEXT("Independent class-change case enters actual preparation"), Reconfigured && Reconfigured->Phase == EProject_JEncounterPhase::Preparing)) return true;
            Test->TestEqual(TEXT("Preparation has not published a spawn"), Encounter->SpawnRevision, 0);
            Reconfigured->NPCClass = AProject_JNPCCallbackFixture::StaticClass();
            Test->TestTrue(TEXT("Rooted observer physically leaves the activation radius"), Pawn->SetActorLocation(FVector(20000,0,200)));
            auto* Component = NewObject<USceneComponent>(Ground.Get());
            Ground->AddInstanceComponent(Component); Component->RegisterComponent();
            ComponentToken = Registry->RequestComponentPreparation(Component, FSoftObjectPath(AProject_JNPCCharacter::StaticClass()),
                [this](uint64 Token, bool bCompleted)
                {
                    Test->TestTrue(TEXT("Generic component preparation keeps its token and succeeds without nearby demand"), Token == ComponentToken && bCompleted);
                    ++ComponentCompleted;
                });
            if (!Test->TestTrue(TEXT("Generic registered component shares actual preparation admission"), ComponentToken != 0)) return true;
            // Real cache expiry; do not rewrite the snapshot, native ready flag or any deadline.
            FPlatformProcess::Sleep(.55f); Phase = 1;
            return false;
        }
        World->Tick(LEVELTICK_All, Phase == 2 || Phase == 3 ? 1.1f : 1.f/60.f);
        Test->TestTrue(TEXT("Demand lifecycle keeps shared native preparation capacity"), Registry->GetStats().Preparations <= Registry->MaxPreparations);
        if (Phase == 1)
        {
            if (!Test->TestEqual(TEXT("Absent demand never publishes the original prepared NPC"), Encounter->SpawnRevision, 0)
                || !Test->TestEqual(TEXT("Absent demand leaves no owned NPC"), OwnedNPCs(Encounter), 0)
                || !Test->TestEqual(TEXT("Cancelled preparation never resolves defeat"), Resolution->Calls, 0)) return true;
            auto* Reconfigured = ChangedDefinition.Get();
            if (!Reconfigured) { Test->AddError(TEXT("Class-change owner retired before observation")); return true; }
            if (!Test->TestEqual(TEXT("An old loaded class cannot authorize the changed definition"), Reconfigured->SpawnRevision, 0)
                || !Test->TestEqual(TEXT("Definition replacement creates no stale-owned NPC"), OwnedNPCs(Reconfigured), 0)) return true;
            if (Encounter->Phase != EProject_JEncounterPhase::Cooldown || Reconfigured->Phase != EProject_JEncounterPhase::Cooldown
                || ComponentCompleted == 0 || Registry->GetStats().Preparations != 0) return false;
            Test->TestEqual(TEXT("Generic component completes exactly once while encounter demand is absent"), ComponentCompleted, 1);
            bool bFound = true;
            Test->TestTrue(TEXT("Observer departure is known through the refreshed shared snapshot"), Registry->TryQueryNearbyPlayer(FVector::ZeroVector, 1000, bFound));
            Test->TestFalse(TEXT("Refreshed snapshot proves this encounter has no nearby observer"), bFound);
            Test->TestTrue(TEXT("Changed-definition case retires through its public actor lifetime"), Reconfigured->Destroy());
            Test->TestTrue(TEXT("Same rooted observer returns normally"), Pawn->SetActorLocation(FVector(0,0,200)));
            FPlatformProcess::Sleep(.55f); Phase = 2;
            return false;
        }
        if (Phase == 2)
        {
            if (Encounter->Phase != EProject_JEncounterPhase::Active) return false;
            Test->TestEqual(TEXT("Same encounter recovers through its existing game-time cooldown"), Encounter->SpawnRevision, 1);
            Test->TestEqual(TEXT("Recovery creates exactly one current owned NPC"), OwnedNPCs(Encounter), 1);
            Test->TestEqual(TEXT("Recovery and class retirement emit no defeat resolution"), Resolution->Calls, 0);
            Test->TestTrue(TEXT("Observer can leave an already active encounter"), Pawn->SetActorLocation(FVector(20000,0,200)));
            FPlatformProcess::Sleep(.55f); Phase = 3;
            return false;
        }
        Test->TestEqual(TEXT("Lost observer demand does not despawn an already active encounter"), Encounter->Phase, EProject_JEncounterPhase::Active);
        Test->TestEqual(TEXT("Active demand loss preserves the actual spawn revision"), Encounter->SpawnRevision, 1);
        Test->TestEqual(TEXT("Active demand loss preserves its current NPC"), OwnedNPCs(Encounter), 1);
        Test->TestEqual(TEXT("All non-defeat paths keep resolution silent"), Resolution->Calls, 0);
        Test->TestTrue(TEXT("Normal encounter EndPlay retires the recovered NPC"), Encounter->Destroy());
        Test->TestEqual(TEXT("Public retirement leaves no live NPC owned by the retired encounter"), OwnedNPCs(Encounter), 0);
        Test->TestEqual(TEXT("Completed lifecycle leaves no physical preparation reservation"), Registry->GetStats().Preparations, 0);
        return true;
    }
};

// This is spawn/command admission and lifetime coverage. Actual Recast/Mover reach
// is exercised by ProjectJ.NPCAction.NativePointNavigation, not this nav-less world.
class FEncounterNavigationLifecycle final : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    FWorld Fixture;
    TWeakObjectPtr<AProject_JWorldEncounter> Event, Invalid;
    TStrongObjectPtr<UProject_JEncounterResolutionReceiver> Resolution;
    double Started = FPlatformTime::Seconds();
    int32 Stops = 0;
public:
    explicit FEncounterNavigationLifecycle(FAutomationTestBase* InTest) : Test(InTest),
        Resolution(NewObject<UProject_JEncounterResolutionReceiver>())
    {
        auto* World = Fixture.World;
        auto* Ground = World->SpawnActor<AActor>();
        auto* Box = NewObject<UBoxComponent>(Ground);
        Ground->SetRootComponent(Box); Ground->AddInstanceComponent(Box);
        Box->SetBoxExtent(FVector(10000,10000,50)); Box->SetMobility(EComponentMobility::Static);
        Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Box->SetCollisionObjectType(ECC_WorldStatic);
        Box->SetCollisionResponseToAllChannels(ECR_Block);
        Ground->SetActorLocation(FVector(0,0,-50)); Box->RegisterComponent();
        auto* Observer = World->SpawnActor<APawn>();
        auto* Root = NewObject<USceneComponent>(Observer);
        Observer->SetRootComponent(Root); Observer->AddInstanceComponent(Root); Root->RegisterComponent();
        Observer->SetActorLocation(FVector(0,0,200));
        World->SpawnActor<APlayerController>()->Possess(Observer);
        auto* Encounter = World->SpawnActor<AProject_JWorldEncounter>(); Event = Encounter;
        Encounter->EncounterId = TEXT("Foundation.Navigation.Initial");
        Encounter->NPCClass = AProject_JNPCCharacter::StaticClass(); Encounter->ActivationRadius = 1000;
        Encounter->bUseInitialNavigation = true;
        Encounter->InitialNavigationDestination = FVector(600,0,0); Encounter->InitialNavigationAcceptanceRadius = 60;
        Encounter->OnResolved.AddDynamic(Resolution.Get(), &UProject_JEncounterResolutionReceiver::Resolved);
        auto* Bad = World->SpawnActor<AProject_JWorldEncounter>(); Invalid = Bad;
        Bad->EncounterId = TEXT("Foundation.Navigation.Invalid"); Bad->NPCClass = AProject_JNPCCharacter::StaticClass();
        Bad->bUseInitialNavigation = true; Bad->InitialNavigationAcceptanceRadius = 0;
        Fixture.Start();
    }
    ~FEncounterNavigationLifecycle()
    {
        // Retire any admitted callback while this latent command and its fields are still alive.
        if (auto* Encounter = Event.Get(); IsValid(Encounter) && !Encounter->IsActorBeingDestroyed()) Encounter->Destroy();
    }
    bool Update() override
    {
        if (FPlatformTime::Seconds()-Started > 15) { Test->AddError(TEXT("Encounter initial-navigation admission/lifetime timed out")); return true; }
        auto* World = Fixture.World;
        const TStrongObjectPtr<AProject_JWorldEncounter> Encounter(Event.Get());
        if (!IsValid(Encounter.Get()) || !Invalid.IsValid()) { Test->AddError(TEXT("Initial-navigation fixture lost its event owner")); return true; }
        World->Tick(LEVELTICK_All, .1f);
        Test->TestEqual(TEXT("Invalid enabled navigation is disabled before spawning"), Invalid->Phase, EProject_JEncounterPhase::Disabled);
        Test->TestEqual(TEXT("Invalid authoring never publishes a spawn"), Invalid->SpawnRevision, 0);
        if (Encounter->Phase == EProject_JEncounterPhase::Dormant || Encounter->Phase == EProject_JEncounterPhase::Preparing) return false;
        if (!Test->TestEqual(TEXT("A valid finite command admits the real encounter spawn"), Encounter->Phase, EProject_JEncounterPhase::Active)) return true;
        AProject_JNPCCharacter* Found = nullptr;
        int32 Count = 0;
        for (TActorIterator<AProject_JNPCCharacter> It(World); It; ++It)
        {
            if (!It->IsActorBeingDestroyed()) { Found = *It; ++Count; }
        }
        if (!Test->TestEqual(TEXT("The isolated opt-in encounter publishes exactly one NPC"), Count, 1) || !Found) return true;
        const TStrongObjectPtr<AProject_JNPCCharacter> NPC(Found);
        const TStrongObjectPtr<AAIController> AI(Cast<AAIController>(NPC->GetController()));
        const TStrongObjectPtr<UProject_JNPCActionComponent> Action(NPC->FindComponentByClass<UProject_JNPCActionComponent>());
        if (!Test->TestTrue(TEXT("Spawned neutral NPC has its actual native controller and registered action"), IsValid(AI.Get()) &&
            AI->GetPawn() == NPC.Get() && AI->GetWorld() == World && IsValid(Action.Get()) && Action->IsRegistered() && Action->GetOwner() == NPC.Get())) return true;
        Test->TestTrue(TEXT("Native possession retains its standard pawn-to-controller Owner edge without a cycle"), NPC->GetOwner() == AI.Get() && AI->GetOwner() != NPC.Get());
        Test->TestEqual(TEXT("Command admission advances the spawn revision once"), Encounter->SpawnRevision, 1);
        Test->TestNull(TEXT("Neutral admission creates no hostile scoring writer"), Action->GetScoringSource());
        Test->TestFalse(TEXT("Neutral admission creates no attack grant"), Action->GetAttackAbilityHandle().IsValid());
        NPC->StopNavigation();
        Test->TestTrue(TEXT("Explicit stop preserves the same spawned NPC/controller for combat promotion"),
            !NPC->IsActorBeingDestroyed() && NPC->GetController() == AI.Get() && AI->GetPawn() == NPC.Get());
        Test->TestEqual(TEXT("Navigation cancellation is independent of encounter lifetime"), Encounter->Phase, EProject_JEncounterPhase::Active);
        Test->TestEqual(TEXT("Navigation cancellation does not create a new spawn"), Encounter->SpawnRevision, 1);
        Test->TestEqual(TEXT("Navigation failure/cancel never fabricates defeat"), Resolution->Calls, 0);
        if (!Test->TestTrue(TEXT("A fresh command reuses the helper-owned controller/action before final retirement"),
            NPC->StartNavigationTo(FVector(800,100,0), 60, [this](bool Arrived)
            { ++Stops; Test->TestFalse(TEXT("Encounter retirement cancels rather than certifies arrival"), Arrived); }))) return true;
        Test->TestTrue(TEXT("The replacement command retains actual action identity"), NPC->FindComponentByClass<UProject_JNPCActionComponent>() == Action.Get());
        Test->TestTrue(TEXT("Public encounter retirement owns final cleanup"), Encounter->Destroy());
        Test->TestEqual(TEXT("Final actor retirement reports the admitted cancellation once"), Stops, 1);
        Test->TestTrue(TEXT("Retirement destroys only the helper's same-NPC controller"), AI->IsActorBeingDestroyed());
        Test->TestTrue(TEXT("Retirement destroys the event's actual spawned NPC"), NPC->IsActorBeingDestroyed());
        Test->TestEqual(TEXT("Final retirement remains distinct from defeat"), Resolution->Calls, 0);
        Test->TestEqual(TEXT("Final retirement returns the real shared action slot"), World->GetSubsystem<UProject_JNPCDecisionSubsystem>()->GetActionCount(), 0);
        Test->TestEqual(TEXT("Final retirement returns the real shared path ownership"), World->GetSubsystem<UProject_JNPCPathSubsystem>()->GetRequestCount(), 0);
        Test->TestEqual(TEXT("Completed/retired spawn has no preparation reservation"), World->GetSubsystem<UProject_JWorldEventRegistry>()->GetStats().Preparations, 0);
        return true;
    }
};

// Real Encounter -> grouped async preparation -> NPC capability admission. The
// transient definitions are native data values, not new content or fake completion.
class FEncounterCombatLifecycle final : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    FWorld Fixture;
    TWeakObjectPtr<AProject_JWorldEncounter> Event, Changed, Conflict;
    TStrongObjectPtr<UProject_JNPCActivationDefinition> Definition, Replacement;
    TWeakObjectPtr<UProject_JNPCActivationDefinition> InitialDefinition;
    TStrongObjectPtr<UProject_JEncounterResolutionReceiver> Resolution;
    double Started = FPlatformTime::Seconds();
    int32 Stage = 0;
public:
    explicit FEncounterCombatLifecycle(FAutomationTestBase* InTest) : Test(InTest),
        Definition(NewObject<UProject_JNPCActivationDefinition>()), Replacement(NewObject<UProject_JNPCActivationDefinition>()),
        Resolution(NewObject<UProject_JEncounterResolutionReceiver>())
    {
        Definition->AttackAbility = UProject_JGameplayAbility_NPCAttack::StaticClass();
        Replacement->AttackAbility = UProject_JGameplayAbility_NPCAttack::StaticClass();
        InitialDefinition = Definition.Get();
        auto* World = Fixture.World;
        auto* Ground = World->SpawnActor<AActor>();
        auto* Box = NewObject<UBoxComponent>(Ground);
        Ground->SetRootComponent(Box); Ground->AddInstanceComponent(Box);
        Box->SetBoxExtent(FVector(10000,10000,50)); Box->SetMobility(EComponentMobility::Static);
        Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Box->SetCollisionObjectType(ECC_WorldStatic);
        Box->SetCollisionResponseToAllChannels(ECR_Block);
        Ground->SetActorLocation(FVector(0,0,-50)); Box->RegisterComponent();
        auto* Observer = World->SpawnActor<APawn>();
        auto* Root = NewObject<USceneComponent>(Observer);
        Observer->SetRootComponent(Root); Observer->AddInstanceComponent(Root); Root->RegisterComponent();
        Observer->SetActorLocation(FVector(0,0,200)); World->SpawnActor<APlayerController>()->Possess(Observer);
        const auto Add = [&](FName Id, FVector Location)
        {
            auto* Encounter = World->SpawnActor<AProject_JWorldEncounter>();
            Encounter->EncounterId = Id; Encounter->SetActorLocation(Location);
            Encounter->NPCClass = AProject_JNPCCharacter::StaticClass(); Encounter->ActivationRadius = 10000;
            Encounter->CombatDefinition = Definition.Get(); Encounter->CombatTeamId = 1;
            Encounter->OnResolved.AddDynamic(Resolution.Get(), &UProject_JEncounterResolutionReceiver::Resolved);
            return Encounter;
        };
        Event = Add(TEXT("Foundation.Combat.Producer"), FVector::ZeroVector);
        Changed = Add(TEXT("Foundation.Combat.DefinitionChanged"), FVector(4000,0,0));
        auto* Invalid = Add(TEXT("Foundation.Combat.NavigationConflict"), FVector(8000,0,0)); Conflict = Invalid;
        Invalid->bUseInitialNavigation = true;
        FDataValidationContext ConflictContext;
        Test->TestEqual(TEXT("Initial navigation and combat cannot silently chain"), Invalid->IsDataValid(ConflictContext), EDataValidationResult::Invalid);
        Fixture.Start();
    }
    ~FEncounterCombatLifecycle()
    {
        // Dynamic resolution bindings and native capability state retire before our fields.
        for (const auto& Weak : {Event, Changed, Conflict})
            if (auto* Owner = Weak.Get(); IsValid(Owner) && !Owner->IsActorBeingDestroyed()) Owner->Destroy();
    }
    bool Update() override
    {
        if (FPlatformTime::Seconds()-Started > 15) { Test->AddError(TEXT("Encounter grouped combat admission/lifetime timed out")); return true; }
        auto* World = Fixture.World;
        const TStrongObjectPtr<AProject_JWorldEncounter> Encounter(Event.Get()), Reconfigured(Changed.Get());
        auto* Registry = World->GetSubsystem<UProject_JWorldEventRegistry>();
        if (!IsValid(Encounter.Get()) || !IsValid(Reconfigured.Get()) || !Conflict.IsValid() || !Registry)
        { Test->AddError(TEXT("Combat producer fixture lost its actual event/registry")); return true; }
        if (Stage == 0)
        {
            // Keep the same already-resident dependencies but drive their normal
            // next-tick streamable callback, not a synthetic ready flag.
            for (int32 Step = 0; Step < 16 && (Encounter->Phase == EProject_JEncounterPhase::Dormant || Reconfigured->Phase == EProject_JEncounterPhase::Dormant); ++Step)
                World->Tick(LEVELTICK_All, .1f);
            if (!Test->TestEqual(TEXT("Combat producer enters actual group preparation"), Encounter->Phase, EProject_JEncounterPhase::Preparing)
                || !Test->TestEqual(TEXT("Independent definition-change request is also preparing"), Reconfigured->Phase, EProject_JEncounterPhase::Preparing)) return true;
            Test->TestEqual(TEXT("Two dependency groups use two physical preparation slots, not four"), Registry->GetStats().Preparations, 2);
            Test->TestEqual(TEXT("Invalid combat/navigation policy stays disabled"), Conflict->Phase, EProject_JEncounterPhase::Disabled);
            Test->TestEqual(TEXT("Invalid policy never publishes a spawn"), Conflict->SpawnRevision, 0);
            Reconfigured->CombatDefinition = Replacement.Get();
            Test->TestFalse(TEXT("The resident source definition is not rooted/standalone by the fixture"), Definition->IsRooted() || Definition->HasAnyFlags(RF_Standalone));
            Definition.Reset();
            CollectGarbage(RF_NoFlags);
            if (!Test->TestTrue(TEXT("The real shared streamable handles keep a resident soft definition alive through pending GC"), InitialDefinition.IsValid())) return true;
            // The GC observation above proves pending handle ownership. This pin
            // supports the later explicit stop/restart using the same native data.
            Definition.Reset(InitialDefinition.Get());
            Stage = 1; return false;
        }
        World->Tick(LEVELTICK_All, .1f);
        if (Encounter->Phase == EProject_JEncounterPhase::Dormant || Encounter->Phase == EProject_JEncounterPhase::Preparing
            || Reconfigured->Phase == EProject_JEncounterPhase::Preparing || Registry->GetStats().Preparations) return false;
        if (!Test->TestEqual(TEXT("The current loaded combat definition admits a real spawn"), Encounter->Phase, EProject_JEncounterPhase::Active)) return true;
        Test->TestEqual(TEXT("An old group cannot authorize a changed definition"), Reconfigured->Phase, EProject_JEncounterPhase::Cooldown);
        Test->TestEqual(TEXT("Changed definition never publishes its old request"), Reconfigured->SpawnRevision, 0);
        AProject_JNPCCharacter* Found = nullptr; int32 NPCs = 0;
        for (TActorIterator<AProject_JNPCCharacter> It(World); It; ++It) if (!It->IsActorBeingDestroyed()) { Found = *It; ++NPCs; }
        if (!Test->TestEqual(TEXT("Only the qualified producer owns a live spawn in this isolated world"), NPCs, 1) || !Found) return true;
        const TStrongObjectPtr<AProject_JNPCCharacter> NPC(Found);
        const TStrongObjectPtr<AAIController> AI(Cast<AAIController>(NPC->GetController()));
        const TStrongObjectPtr<UProject_JNPCActivationComponent> Activation(NPC->FindComponentByClass<UProject_JNPCActivationComponent>());
        const TStrongObjectPtr<UProject_JNPCActionComponent> Action(NPC->FindComponentByClass<UProject_JNPCActionComponent>());
        const TStrongObjectPtr<UProject_JTargetScoringComponent> Scoring(NPC->FindComponentByClass<UProject_JTargetScoringComponent>());
        const TStrongObjectPtr<UAbilitySystemComponent> ASC(NPC->GetAbilitySystemComponent());
        if (!Test->TestTrue(TEXT("The actual native producer installed its controller/activation/scoring/action"),
            IsValid(AI.Get()) && AI->GetPawn() == NPC.Get() && NPC->GetOwner() == AI.Get() && AI->GetOwner() != NPC.Get() &&
            IsValid(Activation.Get()) && IsValid(Action.Get()) && IsValid(Scoring.Get()) && IsValid(ASC.Get()) && NPC->HasCombatActions() &&
            Activation->HasOwnedActivation() && Scoring->IsBatchedNPCDecisionRegistered() && Action->GetOwnershipToken() != 0)) return true;
        Test->TestNotNull(TEXT("The prepared definition creates the real GAS attack grant"), ASC->FindAbilitySpecFromClass(UProject_JGameplayAbility_NPCAttack::StaticClass()));
        Test->TestEqual(TEXT("Combat uses the existing shared decision registration"), World->GetSubsystem<UProject_JNPCDecisionSubsystem>()->GetAgentCount(), 1);
        Test->TestEqual(TEXT("Combat uses the existing shared action slot"), World->GetSubsystem<UProject_JNPCDecisionSubsystem>()->GetActionCount(), 1);
        NPC->StopCombatActions();
        Test->TestTrue(TEXT("Explicit combat stop releases only the capability, keeping the same NPC/controller"),
            !NPC->HasCombatActions() && !NPC->IsActorBeingDestroyed() && NPC->GetController() == AI.Get() && AI->GetPawn() == NPC.Get());
        Test->TestNull(TEXT("Explicit capability retirement releases its actual grant"), ASC->FindAbilitySpecFromClass(UProject_JGameplayAbility_NPCAttack::StaticClass()));
        Test->TestEqual(TEXT("Combat deactivation is not encounter defeat"), Encounter->Phase, EProject_JEncounterPhase::Active);
        Test->TestEqual(TEXT("Capability retirement preserves spawn identity"), Encounter->SpawnRevision, 1);
        Test->TestEqual(TEXT("No preparation/retirement path fabricates defeat"), Resolution->Calls, 0);
        if (!Test->TestTrue(TEXT("The same native capability can be explicitly re-admitted before final retirement"), NPC->StartCombatActions(Definition.Get(), 1))) return true;
        Test->TestTrue(TEXT("Public encounter retirement owns final spawn cleanup"), Encounter->Destroy());
        Test->TestTrue(TEXT("EndPlay retires the exact capability and same-NPC controller"), !Activation->HasOwnedActivation() && AI->IsActorBeingDestroyed() && NPC->IsActorBeingDestroyed());
        Test->TestNull(TEXT("Final retirement releases the re-admitted grant"), ASC->FindAbilitySpecFromClass(UProject_JGameplayAbility_NPCAttack::StaticClass()));
        Test->TestEqual(TEXT("Final retirement leaves no hostile decision registration"), World->GetSubsystem<UProject_JNPCDecisionSubsystem>()->GetAgentCount(), 0);
        Test->TestEqual(TEXT("Final retirement leaves no action registration"), World->GetSubsystem<UProject_JNPCDecisionSubsystem>()->GetActionCount(), 0);
        Test->TestEqual(TEXT("All grouped preparation reservations return normally"), Registry->GetStats().Preparations, 0);
        Test->TestEqual(TEXT("Stop/EndPlay never turn into defeat resolution"), Resolution->Calls, 0);
        return true;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJWorldEventPreparationTest, "ProjectJ.Foundation.WorldEvents.PreparationLifecycle", ProjectJFoundationTests::Flags)
bool FProjectJWorldEventPreparationTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(ProjectJFoundationTests::FPreparationLifecycle(this));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJEncounterCombatAdmissionTest, "ProjectJ.Foundation.WorldEvents.CombatAdmission", ProjectJFoundationTests::Flags)
bool FProjectJEncounterCombatAdmissionTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(ProjectJFoundationTests::FEncounterCombatLifecycle(this));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJEncounterInitialNavigationTest, "ProjectJ.Foundation.WorldEvents.InitialNavigation", ProjectJFoundationTests::Flags)
bool FProjectJEncounterInitialNavigationTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(ProjectJFoundationTests::FEncounterNavigationLifecycle(this));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJEncounterDemandLifecycleTest, "ProjectJ.Foundation.WorldEvents.DemandLifecycle", ProjectJFoundationTests::Flags)
bool FProjectJEncounterDemandLifecycleTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(ProjectJFoundationTests::FEncounterDemandLifecycle(this));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMoverDestructionOrderTest, "ProjectJ.Foundation.Mover.DestructionOrder", ProjectJFoundationTests::Flags)
bool FProjectJMoverDestructionOrderTest::RunTest(const FString&)
{
    const TStrongObjectPtr<UMoverComponent> Mover(NewObject<UMoverComponent>());
    for (int32 Order = 0; Order < 2; ++Order)
    {
        const TStrongObjectPtr<UKinematicActorSimulation> Simulation(NewObject<UKinematicActorSimulation>(Mover.Get()));
        UKinematicActorSimulation::FInitParams Params;
        Params.MoverComp = Mover.Get();
        Simulation->Init(Params);
        auto* FSM = FindObjectFast<UMovementModeStateMachine>(Simulation.Get(), TEXT("KinematicActorStateMachine"));
        if (!TestNotNull(TEXT("Actual Mover state machine"), FSM)) return false;
        TArray<UObject*> Before;
        GetObjectsWithOuter(Simulation.Get(), Before, EGetObjectsFlags::None);
        if (Order == 0)
        {
            FSM->ConditionalBeginDestroy();
            Simulation->Uninit(); // Used to dereference the FSM's already-cleared transition.
        }
        Simulation->ConditionalBeginDestroy();
        TArray<UObject*> After;
        GetObjectsWithOuter(Simulation.Get(), After, EGetObjectsFlags::None);
        TestFalse(TEXT("GC teardown never creates replacement default modes"),
            After.ContainsByPredicate([&](UObject* Object) { return !Before.Contains(Object); }));
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCommittedDeltaTest, "ProjectJ.Foundation.Persistence.CommittedDeltaIdentity", ProjectJFoundationTests::Flags)
bool FProjectJCommittedDeltaTest::RunTest(const FString&)
{
    ProjectJFoundationTests::FWorld F;
    auto* Owner = F.World->SpawnActor<APawn>();
    auto* Inventory = NewObject<UProject_JInventoryComponent>(Owner);
    auto* Equipment = NewObject<UProject_JEquipmentManagerComponent>(Owner);
    Owner->AddInstanceComponent(Inventory); Inventory->RegisterComponent();
    Owner->AddInstanceComponent(Equipment); Equipment->RegisterComponent();
    auto* Definition = NewObject<UProject_JConsumableDefinition>(Owner);
    Definition->ItemId = TEXT("Foundation.Consumable"); Definition->MaxStackCount = 99;
    TArray<FProject_JItemInstanceData> Values;
    for (int32 Index = 0; Index < 512; ++Index)
    {
        auto& Item = Values.AddDefaulted_GetRef();
        Item.InstanceId = FGuid::NewGuid(); Item.ItemDef = Definition; Item.BagOrder = Index;
    }
    Inventory->InstallPersistentItems(Values);
    TMap<FGuid, TPair<int32, int32>> Keys;
    for (const auto& E : Inventory->InventoryArray.Items) Keys.Add(E.ItemInstance.InstanceId, {E.ReplicationID, E.ReplicationKey});
    const int32 ArrayKey = Inventory->InventoryArray.ArrayReplicationKey;
    Inventory->InstallPersistentItems(Values);
    TestEqual(TEXT("An unchanged commit does not dirty the bag"), Inventory->InventoryArray.ArrayReplicationKey, ArrayKey);
    Values[17].StackCount = 2;
    Inventory->InstallPersistentItems(Values);
    for (const auto& E : Inventory->InventoryArray.Items)
    {
        const auto Old = Keys.FindChecked(E.ItemInstance.InstanceId);
        TestEqual(TEXT("All item replication identities survive a one-item update"), E.ReplicationID, Old.Key);
        TestEqual(TEXT("Only the changed item gets a new replication key"), E.ReplicationKey,
            Old.Value + (E.ItemInstance.InstanceId == Values[17].InstanceId ? 1 : 0));
    }
    const FGuid Surviving = Values.Last().InstanceId;
    Values.RemoveAt(0);
    Inventory->InstallPersistentItems(Values);
    const auto* Survivor = Inventory->InventoryArray.Items.FindByPredicate([&](const auto& E) { return E.ItemInstance.InstanceId == Surviving; });
    TestTrue(TEXT("Removal preserves the moved survivor's replication identity"), Survivor && Survivor->ReplicationID == Keys.FindChecked(Surviving).Key);

    auto* Weapon = NewObject<UProject_JEquipmentItemDefinition>(Owner);
    Weapon->ItemId = TEXT("Foundation.Weapon"); Weapon->EquipmentSlot = EProject_JEquipmentSlot::Weapon;
    FProject_JEquipmentArrayItem Slot;
    Slot.ItemDef = Weapon; Slot.Slot = EProject_JEquipmentSlot::Weapon;
    Slot.ItemInstance.ItemDef = Weapon; Slot.ItemInstance.InstanceId = FGuid::NewGuid();
    auto* Runtime = NewObject<UProject_JEquipmentRuntimeComponent>(Owner);
    Owner->AddInstanceComponent(Runtime); Runtime->RegisterComponent(); Runtime->BindToEquipmentManager(Equipment);
    Equipment->InstallPersistentEquipment({Slot}); Equipment->PublishPersistentEquipment({});
    const int32 PrivateId = Equipment->EquipmentArray.Items[0].ReplicationID;
    const int32 PublicKey = Runtime->GetPublicEquipment().ArrayReplicationKey;
    const auto BeforeLevel = Equipment->EquipmentArray.Items;
    Slot.ItemInstance.ItemLevel = 2;
    Equipment->InstallPersistentEquipment({Slot}); Equipment->PublishPersistentEquipment(BeforeLevel);
    TestEqual(TEXT("Private equipment retains its slot identity"), Equipment->EquipmentArray.Items[0].ReplicationID, PrivateId);
    TestEqual(TEXT("Private level changes do not dirty observer equipment"), Runtime->GetPublicEquipment().ArrayReplicationKey, PublicKey);
    const auto BeforeUnequip = Equipment->EquipmentArray.Items;
    Equipment->InstallPersistentEquipment({}); Equipment->PublishPersistentEquipment(BeforeUnequip);
    TestEqual(TEXT("Unequip removes private state"), Equipment->EquipmentArray.Items.Num(), 0);
    TestEqual(TEXT("Unequip removes observer projection"), Runtime->GetPublicEquipment().Items.Num(), 0);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMountSessionAdmissionTest, "ProjectJ.Foundation.Mount.SessionAdmission", ProjectJFoundationTests::Flags)
bool FProjectJMountSessionAdmissionTest::RunTest(const FString&)
{
    ProjectJFoundationTests::FWorld F;
    auto* State = F.World->SpawnActor<AProject_JPlayerState>();
    auto* Controller = F.World->SpawnActor<APlayerController>();
    auto* Mount = F.World->SpawnActor<AProject_JMountLifecycleFixture>();
    Controller->SetPlayerState(State); Controller->Possess(Mount);
    auto* Budget = State->FindComponentByClass<UProject_JCommandBudgetComponent>();
    Budget->CommandsPerSecond = 1;
    double Next = 0;
    TestTrue(TEXT("Possessed mount consumes the persistent PlayerState budget"), UProject_JCommandBudgetComponent::AdmitClientCommand(Mount, Next, 0.2));
    double OtherDomain = 0;
    TestFalse(TEXT("Changing domain cannot bypass the session budget"), UProject_JCommandBudgetComponent::AdmitClientCommand(Mount, OtherDomain, 0.2));
    TestEqual(TEXT("Rejected command is observable on PlayerState"), Budget->GetRejectedCount(), int64(1));
    Next = FPlatformTime::Seconds() + 60;
    TestFalse(TEXT("A domain throttle applies to trusted NPC fixtures too"), UProject_JCommandBudgetComponent::AdmitClientCommand(F.World->SpawnActor<AActor>(), Next, 0.2));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJWorldEventExecutionTest, "ProjectJ.Foundation.WorldEvents.BoundedExecution", ProjectJFoundationTests::Flags)
bool FProjectJWorldEventExecutionTest::RunTest(const FString&)
{
    {
        ProjectJFoundationTests::FWorld Demand;
        auto* Controller = Demand.World->SpawnActor<APlayerController>();
        auto* Pawn = Demand.World->SpawnActor<APawn>();
        auto* Root = NewObject<USceneComponent>(Pawn);
        Pawn->SetRootComponent(Root); Pawn->AddInstanceComponent(Root); Root->RegisterComponent();
        Controller->Possess(Pawn);
        auto* Encounter = Demand.World->SpawnActor<AProject_JWorldEncounter>();
        Encounter->EncounterId = TEXT("Foundation.ScheduleCancellation");
        Encounter->NPCClass = AProject_JNPCCharacter::StaticClass(); Encounter->ActivationRadius = 1000;
        Demand.Start();
        auto* Registry = Demand.World->GetSubsystem<UProject_JWorldEventRegistry>();
        bool bFound = false;
        TestTrue(TEXT("Shared observer query distinguishes complete known-near demand"), Registry->TryQueryNearbyPlayer(FVector::ZeroVector, 1000, bFound));
        TestTrue(TEXT("Actual possessed observer is present"), bFound);
        bFound = true;
        TestFalse(TEXT("Invalid nearby query reports unknown rather than known absence"), Registry->TryQueryNearbyPlayer(FVector::ZeroVector, -1, bFound));
        TestFalse(TEXT("Unknown query clears a stale found output"), bFound);
        const auto ReportInitialSchedule = [&](int32 Step)
        {
            const auto* Entry = Registry->Owners.Find(Encounter->EncounterId);
            AddInfo(FString::Printf(TEXT("NativePreparingWarmup scenario=schedule step=%d phase=%d game_time=%.6f next_update=%.6f max_undilated=%.6f"),
                Step, int32(Encounter->Phase), Demand.World->GetTimeSeconds(), Entry ? Entry->NextUpdate : std::numeric_limits<double>::quiet_NaN(),
                Demand.World->GetWorldSettings()->MaxUndilatedFrameTime));
        };
        ReportInitialSchedule(0);
        for (int32 Step = 0; Step < 16 && Encounter->Phase == EProject_JEncounterPhase::Dormant; ++Step)
        {
            Demand.World->Tick(LEVELTICK_All, .1f);
            ReportInitialSchedule(Step+1);
        }
        if (!TestEqual(TEXT("Native request reaches Preparing before schedule closes"), Encounter->Phase, EProject_JEncounterPhase::Preparing)) return false;
        uint64 Token = 0;
        for (const auto& Pair : Registry->Preparations) if (Pair.Value.Owner.Get() == Encounter) { Token = Pair.Key; break; }
        if (!TestTrue(TEXT("Actual preparing consumer has a native reservation token"), Token != 0)) return false;
        const FDateTime Later = FDateTime::UtcNow() + FTimespan::FromDays(1);
        Encounter->bUseSchedule = true; Encounter->StartUtc = Later; Encounter->EndUtc = Later + FTimespan::FromHours(1);
        FPlatformProcess::Sleep(1.05f);
        const auto* Before = Registry->Preparations.Find(Token);
        if (!TestNotNull(TEXT("Preparation remains owned before its monotonic demand wake"), Before)) return false;
        const bool bReadyBeforeCancellation = Before->bReady;
        const auto* NativeHandle = Before->Handle.Get();
        TestTrue(TEXT("Existing monotonic preparation wake becomes runnable"), Registry->IsTickable());
        for (int32 Attempt = 0; Encounter->Phase == EProject_JEncounterPhase::Preparing && Attempt < 4; ++Attempt) Registry->Tick(0);
        TestEqual(TEXT("Closed schedule retires actual preparation without waiting for load completion"), Encounter->Phase, EProject_JEncounterPhase::Cooldown);
        TestEqual(TEXT("Schedule cancellation never publishes a spawn"), Encounter->SpawnRevision, 0);
        const auto* After = Registry->Preparations.Find(Token);
        if (!bReadyBeforeCancellation)
        {
            TestTrue(TEXT("Nonready native load keeps its reservation after consumer cancellation"), After && After->Handle.Get() == NativeHandle && NativeHandle);
            TestTrue(TEXT("Nonready reservation detaches only its obsolete consumer/callback"), After && !After->Owner.IsValid() && !After->Completion);
        }
        else TestTrue(TEXT("Already-ready cancellation releases its completed reservation"), After == nullptr);
        AddInfo(FString::Printf(TEXT("NativeScheduleCancellation token=%llu ready_before=%d reservation_after=%d"), Token, bReadyBeforeCancellation, After != nullptr));
    }
    {
        ProjectJFoundationTests::FWorld EmptyCoverage; EmptyCoverage.Start();
        bool bFound = true;
        auto* Registry = EmptyCoverage.World->GetSubsystem<UProject_JWorldEventRegistry>();
        TestTrue(TEXT("Complete empty observer population is a successful query"), Registry->TryQueryNearbyPlayer(FVector::ZeroVector, 1000, bFound));
        TestFalse(TEXT("Complete empty observer population is known absent"), bFound);
    }
    {
        ProjectJ::TargetScoring::FTargetSpatialSnapshot Spatial;
        TestTrue(TEXT("Kilometre-scale cells build through the existing spatial index"),
            Spatial.Build({{0, FVector(149999, 0, 0), 1}, {1, FVector(150001, 0, 0), 1},
                {2, FVector(0, 0, 150001), 1}, {3, FVector(-150000, 0, 0), 1}}, 100000));
        TArray<int32> Indices; ProjectJ::TargetScoring::FSpatialQueryStats Query;
        TestTrue(TEXT("Event-scale radius remains queryable"), Spatial.Query(FVector::ZeroVector, 150000, 0, Indices, Query));
        TestEqual(TEXT("Exact 3D radius includes only the inside and boundary players"), Indices.Num(), 2);
        TestFalse(TEXT("Event radius uses bounded spatial cells instead of a full scan"), Query.bLinearFallback);
    }
    ProjectJFoundationTests::FWorld F;
    auto* Controller = F.World->SpawnActor<APlayerController>();
    auto* Pawn = F.World->SpawnActor<APawn>();
    Controller->Possess(Pawn);
    TArray<AProject_JWorldEncounter*> Events;
    for (int32 Index = 0; Index < 32; ++Index)
    {
        auto* E = F.World->SpawnActor<AProject_JWorldEncounter>();
        E->EncounterId = FName(*FString::Printf(TEXT("Foundation.Event.%d"), Index));
        E->NPCClass = AProject_JNPCCharacter::StaticClass();
        Events.Add(E);
    }
    F.Start();
    auto* Registry = F.World->GetSubsystem<UProject_JWorldEventRegistry>();
    for (auto& Pair : Registry->Owners) Pair.Value.NextUpdate = -1;
    for (int32 Step = 0; Step < 16; ++Step)
    {
        Registry->Tick(0);
        TestTrue(TEXT("Per-tick event updates are bounded"), Registry->GetStats().LastTickUpdates <= Registry->MaxUpdatesPerTick);
        TestTrue(TEXT("Per-tick spawn applications are bounded"), Registry->GetStats().LastTickSpawns <= Registry->MaxSpawnsPerTick);
        TestTrue(TEXT("Physical preparation reservations remain bounded"), Registry->GetStats().Preparations <= Registry->MaxPreparations);
    }
    TestTrue(TEXT("Actual encounter updates run through the shared scheduler"), Registry->GetStats().Updates > 0);
    TestEqual(TEXT("Nearby-player capture is shared across the event burst"), Registry->GetStats().SnapshotBuilds, uint64(1));
    TestTrue(TEXT("Preparation pressure is explicitly deferred"), Registry->GetStats().PreparationDeferrals > 0);
    for (auto* E : Events)
        if (E->Phase == EProject_JEncounterPhase::Preparing)
        {
            const int32 Count = Registry->GetStats().Preparations;
            TestEqual(TEXT("An event cannot reserve duplicate preparation slots"), Registry->RequestPreparation(E, E->NPCClass.ToSoftObjectPath()), uint64(0));
            TestEqual(TEXT("Rejected duplicate preserves actual slot accounting"), Registry->GetStats().Preparations, Count);
            break;
        }
    FDataValidationContext TemplateContext;
    TestEqual(TEXT("Reusable encounter templates do not require instance IDs"), GetDefault<AProject_JWorldEncounter>()->IsDataValid(TemplateContext), EDataValidationResult::Valid);
    const FDateTime Start(2026, 10, 26, 0, 0, 0);
    auto* Event = Events[0]; Event->bUseSchedule = true; Event->StartUtc = Start; Event->EndUtc = Start + FTimespan::FromHours(1);
    TestFalse(TEXT("Scheduled event stays closed before UTC start"), Event->IsScheduleOpen(Start - FTimespan::FromSeconds(1)));
    TestTrue(TEXT("UTC start is inclusive"), Event->IsScheduleOpen(Start));
    TestFalse(TEXT("UTC end is exclusive"), Event->IsScheduleOpen(Event->EndUtc));
    Registry->OnWorldEndPlay(*F.World);
    TestFalse(TEXT("World retirement stops event execution"), Registry->IsTickable());
    TestEqual(TEXT("World retirement releases native preparation handles"), Registry->GetStats().Preparations, 0);
    TestFalse(TEXT("Retired registry rejects new owners"), Registry->Register(TEXT("Late"), Event));
    {
        ProjectJFoundationTests::FWorld Scheduled;
        TArray<AProject_JWorldEncounter*> Future;
        const FDateTime FutureStart = FDateTime::UtcNow() + FTimespan::FromDays(1);
        Scheduled.World->TimeSeconds = 0.9; // Force a split sweep across a whole-second heartbeat.
        for (int32 Index = 0; Index < 1024; ++Index)
        {
            auto* Waiting = Scheduled.World->SpawnActor<AProject_JWorldEncounter>();
            Waiting->EncounterId = FName(*FString::Printf(TEXT("Foundation.Future.%d"), Index));
            Waiting->NPCClass = AProject_JNPCCharacter::StaticClass(); Waiting->bUseSchedule = true;
            Waiting->StartUtc = FutureStart; Waiting->EndUtc = FutureStart + FTimespan::FromHours(1);
            Future.Add(Waiting);
        }
        Scheduled.Start();
        auto* Sleeping = Scheduled.World->GetSubsystem<UProject_JWorldEventRegistry>();
        double FirstDue = TNumericLimits<double>::Max(), LastDue = 0;
        for (const auto& Pair : Sleeping->Owners)
        { FirstDue = FMath::Min(FirstDue, Pair.Value.NextUpdate); LastDue = FMath::Max(LastDue, Pair.Value.NextUpdate); }
        TestTrue(TEXT("Future owners retain their existing initial hash stagger"), LastDue - FirstDue > 0.5);
        const auto AdvanceSchedule = [&]
        {
            Scheduled.World->Tick(LEVELTICK_All, 1.0f / 60.0f);
            Sleeping->Tick(0);
            TestTrue(TEXT("Advancing schedule sweep keeps the existing visit bound"), Sleeping->GetStats().LastTickVisits <= Sleeping->MaxVisitsPerTick);
        };
        AdvanceSchedule();
        TestTrue(TEXT("Unvisited future owners cannot be hidden behind a cached deadline"), Sleeping->IsTickable());
        for (int32 Step = 0; Sleeping->IsTickable() && Step < 120; ++Step) AdvanceSchedule();
        TestFalse(TEXT("1024 staggered future owners converge to sleep as world time advances"), Sleeping->IsTickable());
        TestEqual(TEXT("Closed schedules consume no gameplay update slots"), Sleeping->GetStats().Updates, uint64(0));
        TestEqual(TEXT("Future-only waiting does not request an observer snapshot"), Sleeping->GetStats().SnapshotBuilds, uint64(0));
        TestTrue(TEXT("UTC start is never cached as an unbounded game-time wait"),
            Sleeping->NextEventWake <= Scheduled.World->GetTimeSeconds() + Sleeping->UpdateIntervalSeconds);
        const double BeforeHeartbeat = Sleeping->NextEventWake;
        for (int32 Step = 0; Scheduled.World->GetTimeSeconds() <= BeforeHeartbeat && Step < 64; ++Step)
            Scheduled.World->Tick(LEVELTICK_All, 0.1f);
        for (int32 Step = 0; Sleeping->IsTickable() && Step < 120; ++Step) AdvanceSchedule();
        TestTrue(TEXT("Existing cadence wakes and recomputes the next bounded deadline"), Sleeping->NextEventWake > BeforeHeartbeat);
        TestFalse(TEXT("Heartbeat finishes without leaving idle per-frame work"), Sleeping->IsTickable());
        Scheduled.World->TimeSeconds -= 2.0;
        TestTrue(TEXT("Game-time rollback wakes a cached event deadline"), Sleeping->IsTickable());
        for (int32 Step = 0; Sleeping->IsTickable() && Step < 120; ++Step) AdvanceSchedule();
        TestFalse(TEXT("Rollback converges without waiting for the old clock era"), Sleeping->IsTickable());
        const double FiniteTime = Scheduled.World->GetTimeSeconds();
        Scheduled.World->TimeSeconds = std::numeric_limits<double>::quiet_NaN();
        TestTrue(TEXT("Nonfinite game time cannot hide a cached event deadline"), Sleeping->IsTickable());
        Sleeping->Tick(0);
        Scheduled.World->TimeSeconds = FiniteTime;
        for (int32 Step = 0; Sleeping->IsTickable() && Step < 120; ++Step) AdvanceSchedule();
        TestFalse(TEXT("Finite clock recovery restores bounded sleep"), Sleeping->IsTickable());
        auto* Reconfigured = Future[0];
        Reconfigured->StartUtc = FDateTime::UtcNow() - FTimespan::FromHours(1);
        Reconfigured->EndUtc = FDateTime::UtcNow() + FTimespan::FromHours(1);
        TestTrue(TEXT("Explicit same-owner schedule re-registration is accepted"), Sleeping->Register(Reconfigured->EncounterId, Reconfigured));
        TestTrue(TEXT("Explicit schedule change wakes existing execution"), Sleeping->IsTickable());
        for (int32 Step = 0; Step < 120 && Sleeping->GetStats().Updates == 0; ++Step) AdvanceSchedule();
        TestTrue(TEXT("Open schedule uses the existing actual encounter update path"), Sleeping->GetStats().Updates > 0);
        TestEqual(TEXT("Existing observer consumer wakes only for the open schedule"), Sleeping->GetStats().SnapshotBuilds, uint64(1));
        Reconfigured->EndUtc = FDateTime::UtcNow() - FTimespan::FromMinutes(1);
        Sleeping->Register(Reconfigured->EncounterId, Reconfigured);
        for (int32 Step = 0; Sleeping->IsTickable() && Step < 120; ++Step) AdvanceSchedule();
        TestFalse(TEXT("Expired dormant schedule returns to bounded sleep"), Sleeping->IsTickable());
        for (auto* Waiting : Future) Waiting->Destroy();
        TestFalse(TEXT("Retired scheduled owners leave no cached wake or tick"), Sleeping->IsTickable());
    }
    {
        ProjectJFoundationTests::FWorld Preview(EWorldType::GamePreview);
        auto* PreviewEvent = Preview.World->SpawnActor<AProject_JWorldEncounter>();
        PreviewEvent->EncounterId = TEXT("Preview"); PreviewEvent->NPCClass = AProject_JNPCCharacter::StaticClass();
        Preview.Start();
        TestEqual(TEXT("Unsupported preview worlds disable events without a scheduler"), PreviewEvent->Phase, EProject_JEncounterPhase::Disabled);
    }
    {
        ProjectJFoundationTests::FWorld Churn;
        TArray<AProject_JWorldEncounter*> Owners;
        for (int32 Index = 0; Index < 8; ++Index)
        {
            auto* ChurnEvent = Churn.World->SpawnActor<AProject_JWorldEncounter>();
            ChurnEvent->EncounterId = FName(*FString::Printf(TEXT("VisitChurn.%d"), Index));
            ChurnEvent->NPCClass = AProject_JNPCCharacter::StaticClass(); Owners.Add(ChurnEvent);
        }
        Churn.Start();
        auto* Indexed = Churn.World->GetSubsystem<UProject_JWorldEventRegistry>();
        auto CheckSlots = [&]()
        {
            for (int32 Slot = 0; Slot < Indexed->VisitOrder.Num(); ++Slot)
                TestEqual(TEXT("Every active identity has its current dense slot"),
                    Indexed->Owners.FindChecked(Indexed->VisitOrder[Slot]).VisitIndex, Slot);
            for (const auto& Pair : Indexed->Owners)
                if (Pair.Value.VisitIndex != INDEX_NONE)
                    TestEqual(TEXT("Every derived slot resolves its identity"), Indexed->VisitOrder[Pair.Value.VisitIndex], Pair.Key);
        };
        CheckSlots();
        Indexed->Cursor = Indexed->VisitOrder.Num() - 1;
        const FName Next = Indexed->VisitOrder[Indexed->Cursor];
        Indexed->Unregister(Owners[0]->EncounterId, Owners[0]);
        TestEqual(TEXT("Swap removal preserves the next consumer even when the last slot moves"),
            Indexed->VisitOrder[Indexed->Cursor], Next);
        CheckSlots();
        auto* NextOwner = Cast<AProject_JWorldEncounter>(Indexed->Owners.FindChecked(Next).Owner.Get());
        NextOwner->Phase = EProject_JEncounterPhase::Disabled;
        TestTrue(TEXT("Explicit disable retains its authority identity"), Indexed->Register(Next, NextOwner));
        TestEqual(TEXT("Disabled identity has no runnable slot"), Indexed->Owners.FindChecked(Next).VisitIndex, INDEX_NONE);
        CheckSlots();
        NextOwner->Phase = EProject_JEncounterPhase::Dormant;
        TestTrue(TEXT("Re-enable installs one slot"), Indexed->Register(Next, NextOwner));
        const int32 Count = Indexed->VisitOrder.Num();
        TestTrue(TEXT("Repeated enable remains idempotent"), Indexed->Register(Next, NextOwner));
        TestEqual(TEXT("Repeated enable cannot add a duplicate visit"), Indexed->VisitOrder.Num(), Count);
        Indexed->Unregister(Next, Owners[0]);
        TestTrue(TEXT("An obsolete owner cannot remove the current indexed identity"), Indexed->Owners.Contains(Next));
        CheckSlots();
        for (auto* ChurnEvent : Owners) ChurnEvent->Destroy();
        TestEqual(TEXT("Burst EndPlay returns all runnable slots"), Indexed->VisitOrder.Num(), 0);
        TestEqual(TEXT("Burst EndPlay returns all authority identities"), Indexed->Owners.Num(), 0);
        TestEqual(TEXT("Empty cursor is normalized"), Indexed->Cursor, 0);
    }
    {
        ProjectJFoundationTests::FWorld Empty;
        auto* Placeholder = Empty.World->SpawnActor<AProject_JWorldEncounter>(); Placeholder->EncounterId = TEXT("Placeholder");
        auto* InvalidConfigured = Empty.World->SpawnActor<AProject_JWorldEncounter>();
        InvalidConfigured->EncounterId = TEXT("InvalidConfigured"); InvalidConfigured->NPCClass = AProject_JNPCCharacter::StaticClass();
        InvalidConfigured->ActivationRadius = 0;
        auto* InvalidSchedule = Empty.World->SpawnActor<AProject_JWorldEncounter>();
        InvalidSchedule->EncounterId = TEXT("InvalidSchedule"); InvalidSchedule->NPCClass = AProject_JNPCCharacter::StaticClass();
        InvalidSchedule->bUseSchedule = true; InvalidSchedule->EndUtc = InvalidSchedule->StartUtc;
        Empty.Start();
        auto* Idle = Empty.World->GetSubsystem<UProject_JWorldEventRegistry>();
        TestEqual(TEXT("Configured invalid owners enter Disabled before scheduling admission"), InvalidConfigured->Phase, EProject_JEncounterPhase::Disabled);
        TestEqual(TEXT("Invalid schedules also remain Disabled"), InvalidSchedule->Phase, EProject_JEncounterPhase::Disabled);
        TestFalse(TEXT("All disabled owners reserve IDs without per-frame work"), Idle->IsTickable());
        TestEqual(TEXT("Disabled configurations create no visit slots"), Idle->VisitOrder.Num(), 0);
        TestFalse(TEXT("Disabled encounters still reserve their unique identity"), Idle->Register(Placeholder->EncounterId, Empty.World->SpawnActor<AActor>()));
        TestFalse(TEXT("Configured disabled identity cannot be stolen"), Idle->Register(InvalidConfigured->EncounterId, Empty.World->SpawnActor<AActor>()));
        TestEqual(TEXT("Disabled owner cannot reserve physical preparation"), Idle->RequestPreparation(InvalidConfigured, InvalidConfigured->NPCClass.ToSoftObjectPath()), uint64(0));
        InvalidConfigured->ActivationRadius = 150000; InvalidConfigured->Phase = EProject_JEncounterPhase::Dormant;
        TestTrue(TEXT("Same authority owner can reconcile explicit repair through existing registration"), Idle->Register(InvalidConfigured->EncounterId, InvalidConfigured));
        TestTrue(TEXT("Repaired owner becomes runnable"), Idle->IsTickable());
        TestTrue(TEXT("Repeated same-owner registration remains idempotent"), Idle->Register(InvalidConfigured->EncounterId, InvalidConfigured));
        TestEqual(TEXT("Repaired owner owns exactly one visit slot"), Idle->VisitOrder.Num(), 1);
        const FName RepairedId = InvalidConfigured->EncounterId;
        InvalidConfigured->Destroy();
        TestFalse(TEXT("EndPlay unregister returns the repaired owner's identity"), Idle->Owners.Contains(RepairedId));
        TestFalse(TEXT("Remaining disabled identities do not keep execution ticking"), Idle->IsTickable());
        auto* Replacement = Empty.World->SpawnActor<AActor>();
        TestTrue(TEXT("Retired ID can be claimed by its next owner"), Idle->Register(RepairedId, Replacement));
        Idle->Unregister(RepairedId, InvalidConfigured);
        TestTrue(TEXT("Stale old-owner unregister cannot remove the replacement"), Idle->Owners.FindChecked(RepairedId).Owner.Get() == Replacement);
        TestTrue(TEXT("Other disabled identity remains reserved"), Idle->Owners.Contains(Placeholder->EncounterId));
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJWorldEventFairChurnTest, "ProjectJ.Foundation.WorldEvents.FairChurn", ProjectJFoundationTests::Flags)
bool FProjectJWorldEventFairChurnTest::RunTest(const FString&)
{
    ProjectJFoundationTests::FWorld F;
    constexpr int32 Population = 256;
    const FDateTime FutureStart = FDateTime::UtcNow() + FTimespan::FromDays(1);
    auto Spawn = [&](int32 Serial)
    {
        auto* Event = F.World->SpawnActorDeferred<AProject_JWorldEncounter>(AProject_JWorldEncounter::StaticClass(),
            FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Event) return Event;
        Event->EncounterId = FName(*FString::Printf(TEXT("FairEvent.%d"), Serial));
        Event->NPCClass = AProject_JNPCCharacter::StaticClass();
        Event->bUseSchedule = true; Event->StartUtc = FutureStart; Event->EndUtc = FutureStart + FTimespan::FromHours(1);
        Event->FinishSpawning(FTransform::Identity);
        return Event;
    };
    AProject_JWorldEncounter* Protected = nullptr;
    for (int32 Index = 0; Index < Population; ++Index)
    {
        auto* Event = Spawn(Index);
        if (!TestNotNull(TEXT("Real authority event"), Event)) return false;
        if (Index == 0) Protected = Event;
    }
    F.Start();
    auto* Registry = F.World->GetSubsystem<UProject_JWorldEventRegistry>();
    Protected->bUseSchedule = false; Protected->Phase = EProject_JEncounterPhase::Cooldown;
    Protected->NextSpawnServerTime = TNumericLimits<double>::Max(); // No load/spawn in this scheduler test.
    Registry->Owners.FindChecked(Protected->EncounterId).NextUpdate = -1;
    // A genuine worst-position survivor: the old numeric traversal visits the
    // latter half, then swaps its next (last) consumer into the just-visited slot.
    Registry->Cursor = Population - 1 - Registry->MaxVisitsPerTick;
    const auto CheckRing = [&]
    {
        TestEqual(TEXT("Churn preserves the live admission bound"), Registry->VisitOrder.Num(), Population);
        TSet<FName> Seen;
        FName Id = Registry->VisitOrder[Registry->Cursor];
        const FName First = Id;
        for (int32 Visit = 0; Visit < Population; ++Visit)
        {
            const auto* Entry = Registry->Owners.Find(Id);
            if (!TestTrue(TEXT("Each linked identity remains a registered runnable owner"), Entry && Entry->VisitIndex != INDEX_NONE)) return;
            TestFalse(TEXT("One sweep cannot revisit a peer"), Seen.Contains(Id)); Seen.Add(Id);
            TestEqual(TEXT("Dense storage resolves the linked identity"), Registry->VisitOrder[Entry->VisitIndex], Id);
            TestEqual(TEXT("Forward and backward links agree"), Registry->Owners.FindChecked(Entry->NextVisit).PreviousVisit, Id);
            Id = Entry->NextVisit;
        }
        TestEqual(TEXT("A sweep returns exactly to its current frontier"), Id, First);
    };
    CheckRing();
    int32 Visits = 0, Churns = 0;
    bool bVisited = false;
    for (int32 Step = 0; Step < Population + 16 && Visits < Population && !bVisited; ++Step)
    {
        const FName FirstVisited = Registry->VisitOrder[Registry->Cursor];
        Registry->Tick(0);
        TestTrue(TEXT("The existing visit budget is retained"), Registry->Stats.LastTickVisits <= Registry->MaxVisitsPerTick);
        TestTrue(TEXT("The existing update budget is retained"), Registry->Stats.LastTickUpdates <= Registry->MaxUpdatesPerTick);
        const int32 ThisVisitCount = Registry->Stats.LastTickVisits;
        Visits += ThisVisitCount;
        bVisited = Registry->Owners.FindChecked(Protected->EncounterId).NextUpdate > 0;
        if (bVisited || !ThisVisitCount) continue;
        auto* Retiring = Cast<AProject_JWorldEncounter>(Registry->Owners.FindChecked(FirstVisited).Owner.Get());
        if (!TestTrue(TEXT("Only a visited peer retires"), IsValid(Retiring) && Retiring != Protected)) return false;
        Retiring->Destroy(); // Actual EndPlay unregisters its slot before the new admission.
        if (!TestNotNull(TEXT("Actual new owner registers during churn"), Spawn(Population + Churns++))) return false;
        CheckRing();
    }
    // The observation is at the end of a bounded Tick, not an invented exact
    // per-visit timestamp. A last Tick may straddle the Population visit horizon.
    TestTrue(TEXT("A due surviving event is observed by the Tick covering the original-population visit horizon"), bVisited);
    TestTrue(TEXT("The actual unregister/re-register trigger was exercised"), Churns > 0);
    TestEqual(TEXT("Closed scheduled peers do not force observer capture"), Registry->Stats.SnapshotBuilds, uint64(0));
    TestEqual(TEXT("Only the due protected cooldown consumes gameplay update work"), Registry->Stats.Updates, uint64(1));
    AddInfo(FString::Printf(TEXT("WorldEventFairChurn population=%d observedTickVisits=%d churns=%d"), Population, Visits, Churns));
    Registry->OnWorldEndPlay(*F.World);
    TestEqual(TEXT("Teardown detaches all linked and dense owners"), Registry->VisitOrder.Num(), 0);
    TestFalse(TEXT("Teardown leaves no scheduled work"), Registry->IsTickable());
    return !HasAnyErrors();
}
#endif
