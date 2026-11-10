#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Project_JNPCCallbackFixture.h"
#include "Components/Project_JNPCActivationComponent.h"
#include "Components/Project_JNPCActionComponent.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "System/Project_JNPCDecisionSubsystem.h"
#include "Project_JAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "AIController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "System/Project_JTargetScoringSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#include "Combat/Project_JGameplayAbility_NPCAttack.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Animation/Project_JAnimNotifyState_MeleeHit.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Components/Project_JCombatPresentationComponent.h"
#include "Combat/Project_JCombatPresentationSet.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Engine/SkeletalMesh.h"
#include "Game/Project_JPlayerState.h"
#include "Project_JSpellbladeCharacter.h"
#include "NativeGameplayTags.h"
#include "Tests/Project_JMoverFrameFixture.h"
#include "UObject/UnrealType.h"
#include "GameplayEffect.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/DataValidation.h"

namespace ProjectJNPCCallbackTests
{
struct FWorld
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    AProject_JNPCCallbackFixture* NPC;
    UProject_JTargetScoringComponent* Scoring;
    UProject_JNPCActionComponent* Action;
    UProject_JNPCActivationComponent* Activation;
    UProject_JNPCDecisionSubsystem* Scheduler;
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        NPC = World->SpawnActor<AProject_JNPCCallbackFixture>();
        World->SpawnActor<AAIController>()->Possess(NPC);
        NPC->GetAbilitySystemComponent()->InitAbilityActorInfo(NPC, NPC);
        Scoring = NewObject<UProject_JTargetScoringComponent>(NPC); NPC->AddInstanceComponent(Scoring); Scoring->RegisterComponent();
        Action = NewObject<UProject_JNPCActionComponent>(NPC); NPC->AddInstanceComponent(Action); Action->RegisterComponent();
        Activation = NewObject<UProject_JNPCActivationComponent>(NPC); NPC->AddInstanceComponent(Activation); Activation->RegisterComponent();
        World->InitializeActorsForPlay(FURL());
        World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
        NPC->GetAttributeSet()->InitMaxHealth(100); NPC->GetAttributeSet()->InitHealth(100);
        Scheduler = World->GetSubsystem<UProject_JNPCDecisionSubsystem>();
    }
    ~FWorld()
    {
        World->EndPlay(EEndPlayReason::LevelTransition);
        World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    }
    AProject_JNPCCallbackFixture* Target(double X, int32 Team = 2)
    {
        auto* Target = World->SpawnActor<AProject_JNPCCallbackFixture>();
        Target->SetActorLocation(FVector(X, 0, 0)); Scheduler->RegisterTarget(Target, Team); return Target;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCCommitSupersededTest, "ProjectJ.Continuation.NPCCommitSuperseded",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCCommitSupersededTest::RunTest(const FString&)
{
    ProjectJNPCCallbackTests::FWorld F;
    auto* A = F.Target(100); auto* B = F.Target(150);
    TestTrue(TEXT("Server registration"), F.Scoring->StartBatchedNPCDecisions(1));
    TestTrue(TEXT("Movement consumer"), F.Action->StartActions(F.Scoring, {}));
    F.Scoring->OnQueryCompleted.Broadcast(A, 1.0);
    TestTrue(TEXT("Original target can commit"), F.Action->CanCommitToTarget(A));
    bool Fired = false;
    F.NPC->OnLifeQuery = [&] { Fired = true; F.Scoring->OnQueryCompleted.Broadcast(B, 1.0); };
    TestFalse(TEXT("An older target cannot commit after IsDead switches intent"), F.Action->CanCommitToTarget(A));
    TestTrue(TEXT("Actual interface event callback"), Fired);
    TestEqual(TEXT("Newer intent remains installed"), F.Action->GetIntentTarget(), static_cast<AActor*>(B));
    TestTrue(TEXT("Fresh commit to the newer target remains supported"), F.Action->CanCommitToTarget(B));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCLifeRetiredTest, "ProjectJ.Continuation.NPCLifeRetired",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCLifeRetiredTest::RunTest(const FString&)
{
    ProjectJNPCCallbackTests::FWorld F;
    auto* Target = F.Target(100);
    TestTrue(TEXT("Server registration"), F.Scoring->StartBatchedNPCDecisions(1));
    bool Fired = false;
    Target->OnLifeQuery = [&] { Fired = true; Target->Destroy(); };
    TestFalse(TEXT("A target retired during its life event is ineligible"), F.Scheduler->CanActOnTarget(F.Scoring, Target));
    TestTrue(TEXT("Actual target life callback"), Fired);
    TestTrue(TEXT("Original action joins the shared scheduler"), F.Action->StartActions(F.Scoring, {}));
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Peer = F.World->SpawnActor<AProject_JNPCCallbackFixture>(AProject_JNPCCallbackFixture::StaticClass(), FVector(500,0,0), FRotator::ZeroRotator, Params);
    F.World->SpawnActor<AAIController>()->Possess(Peer);
    Peer->GetAbilitySystemComponent()->InitAbilityActorInfo(Peer, Peer);
    auto* PeerScoring = NewObject<UProject_JTargetScoringComponent>(Peer); Peer->AddInstanceComponent(PeerScoring); PeerScoring->RegisterComponent();
    auto* PeerAction = NewObject<UProject_JNPCActionComponent>(Peer); Peer->AddInstanceComponent(PeerAction); PeerAction->RegisterComponent();
    TestTrue(TEXT("Peer scoring uses the same bounded service"), PeerScoring->StartBatchedNPCDecisions(1));
    TestTrue(TEXT("Peer action joins after the original"), PeerAction->StartActions(PeerScoring, {}));
    bool RemovedDuringCallback = false, RestartedDuringCallback = false;
    F.NPC->OnLifeQuery = [&]
    {
        RemovedDuringCallback = true;
        PeerAction->StopActions();
        RestartedDuringCallback = PeerAction->StartActions(PeerScoring, {});
        F.Action->StopActions();
        CollectGarbage(RF_NoFlags, true);
    };
    F.Scheduler->Tick(0);
    TestTrue(TEXT("Actual scheduled life callback removes/restarts peer then removes self with GC"), RemovedDuringCallback && RestartedDuringCallback);
    TestEqual(TEXT("Only the current peer remains after callback swap removals"), F.Scheduler->GetActionCount(), 1);
    TestTrue(TEXT("The surviving peer lookup is idempotent after moving into a removed slot"), F.Scheduler->RegisterAction(PeerAction));
    TestEqual(TEXT("Surviving peer is not registered twice"), F.Scheduler->GetActionCount(), 1);
    PeerAction->StopActions();
    TestEqual(TEXT("Final peer cancellation releases the moved lookup entry"), F.Scheduler->GetActionCount(), 0);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCGrantRegistrationTest, "ProjectJ.Continuation.NPCGrantRegistrationSuperseded",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCGrantRegistrationTest::RunTest(const FString&)
{
    ProjectJNPCCallbackTests::FWorld F;
    auto* Definition = NewObject<UProject_JNPCActivationDefinition>();
    Definition->AttackAbility = UProject_JNPCGrantCallbackFixture::StaticClass();
    const int32 Baseline = F.NPC->GetAbilitySystemComponent()->GetActivatableAbilities().Num();
    bool Fired = false, Restarted = false;
    F.NPC->OnGrant = [&]
    {
        Fired = true; F.Scoring->StopBatchedNPCDecisions(); Restarted = F.Scoring->StartBatchedNPCDecisions(2);
    };
    TestFalse(TEXT("Grant callback registration change supersedes old activation"), F.Activation->ActivateNPC(Definition, F.Scoring, F.Action, 1));
    TestTrue(TEXT("Actual GAS grant event ran"), Fired);
    TestTrue(TEXT("Newer registration succeeded"), Restarted);
    TestFalse(TEXT("Old activation cannot own the new registration's action"), F.Activation->HasOwnedActivation());
    TestEqual(TEXT("No leaked old activation grant"), F.NPC->GetAbilitySystemComponent()->GetActivatableAbilities().Num(), Baseline);
    TestEqual(TEXT("Old activation cannot start pursuit"), F.Action->GetActionState(), EProjectJNPCActionState::Disabled);
    auto* Target = F.Target(100, 1);
    TestTrue(TEXT("Rollback preserves the newer team2 registration"), F.Scheduler->CanActOnTarget(F.Scoring, Target));
    for (int32 Mode = 0; Mode < 3; ++Mode)
    {
        ProjectJNPCCallbackTests::FWorld EpochWorld;
        auto* EpochDefinition = NewObject<UProject_JNPCActivationDefinition>();
        EpochDefinition->AttackAbility = UProject_JNPCGrantCallbackFixture::StaticClass();
        auto* EpochASC = CastChecked<UProject_JAbilitySystemComponent>(EpochWorld.NPC->GetAbilitySystemComponent());
        const auto Foreign = EpochASC->GiveAbility(FGameplayAbilitySpec(EpochDefinition->AttackAbility));
        const uint64 BeforeEpoch = EpochASC->GetActorInfoRevision();
        bool CallbackRan = false, Refreshed = false;
        EpochWorld.NPC->OnGrant = [&]
        {
            CallbackRan = true;
            if (Mode == 0) { Refreshed = EpochASC->RefreshActorInfoForCurrentActor(EpochWorld.NPC); }
            else
            {
                if (Mode == 2) { EpochASC->ClearActorInfo(); }
                EpochASC->InitAbilityActorInfo(EpochWorld.NPC, EpochWorld.NPC);
            }
        };
        const bool Accepted = EpochWorld.Activation->ActivateNPC(EpochDefinition, EpochWorld.Scoring, EpochWorld.Action, 1);
        TestTrue(TEXT("Actual GAS grant callback ran for each context transition"), CallbackRan);
        TestEqual(TEXT("Only same-epoch metadata refresh may commit a new NPC action"), Accepted, Mode == 0);
        if (Mode == 0)
        {
            TestTrue(TEXT("Metadata refresh succeeds without replacing recipient epoch"), Refreshed);
            TestEqual(TEXT("Metadata refresh retains the existing epoch"), EpochASC->GetActorInfoRevision(), BeforeEpoch);
            EpochWorld.Activation->DeactivateNPC();
        }
        else { TestTrue(TEXT("Same-avatar initialization still creates a new epoch"), EpochASC->GetActorInfoRevision() != BeforeEpoch); }
        TestFalse(TEXT("Superseded or explicitly released activation owns no lease"), EpochWorld.Activation->HasOwnedActivation());
        TestEqual(TEXT("Retired activation cannot continue pursuit"), EpochWorld.Action->GetActionState(), EProjectJNPCActionState::Disabled);
        TestFalse(TEXT("Owned decision registration is released"), EpochWorld.Scoring->IsBatchedNPCDecisionRegistered());
        TestEqual(TEXT("Only the unrelated grant remains"), EpochASC->GetActivatableAbilities().Num(), 1);
        TestNotNull(TEXT("Context rollback preserves the unrelated grant"), EpochASC->FindAbilitySpecFromHandle(Foreign));
    }
    {
        ProjectJNPCCallbackTests::FWorld OverrideWorld;
        const TStrongObjectPtr<UProject_JNPCActivationDefinition> Legacy(NewObject<UProject_JNPCActivationDefinition>());
        TStrongObjectPtr<UProject_JNPCActivationDefinition> ChoiceA(NewObject<UProject_JNPCActivationDefinition>());
        const TStrongObjectPtr<UProject_JNPCActivationDefinition> ChoiceB(NewObject<UProject_JNPCActivationDefinition>());
        TStrongObjectPtr<UProject_JAttackDefinition> AttackA(NewObject<UProject_JAttackDefinition>());
        const TStrongObjectPtr<UProject_JAttackDefinition> AttackB(NewObject<UProject_JAttackDefinition>());
        Legacy->AttackAbility = UProject_JNPCGrantCallbackFixture::StaticClass();
        ChoiceA->AttackAbility = ChoiceB->AttackAbility = UProject_JGameplayAbility_NPCAttack::StaticClass();
        ChoiceA->AttackDefinitionOverride = AttackA.Get(); ChoiceB->AttackDefinitionOverride = AttackB.Get(); ChoiceB->AbilityLevel = 2;
        auto* ASC = CastChecked<UProject_JAbilitySystemComponent>(OverrideWorld.NPC->GetAbilitySystemComponent());
        const int32 Initial = ASC->GetActivatableAbilities().Num();
        const auto Foreign = ASC->GiveAbility(FGameplayAbilitySpec(Legacy->AttackAbility));
        const int32 WithForeign = ASC->GetActivatableAbilities().Num();
        ON_SCOPE_EXIT { if (IsValid(OverrideWorld.Activation) && !OverrideWorld.Activation->IsBeingDestroyed()) OverrideWorld.Activation->DeactivateNPC(); if (IsValid(ASC) && !ASC->IsBeingDestroyed() && ASC->FindAbilitySpecFromHandle(Foreign)) ASC->ClearAbility(Foreign); };
        TestNull(TEXT("Legacy activation has no implicit data override"), Legacy->AttackDefinitionOverride.Get());
        if (!TestTrue(TEXT("Null override retains normal legacy activation"), OverrideWorld.Activation->ActivateNPC(Legacy.Get(), OverrideWorld.Scoring, OverrideWorld.Action, 1))) return false;
        const auto LegacyGrant = OverrideWorld.Action->GetAttackAbilityHandle();
        TestTrue(TEXT("Legacy SourceObject remains the exact activation component"), ASC->FindAbilitySpecFromHandle(LegacyGrant) && ASC->FindAbilitySpecFromHandle(LegacyGrant)->SourceObject.Get() == OverrideWorld.Activation);
        OverrideWorld.Activation->DeactivateNPC();
        TestTrue(TEXT("Legacy cleanup retires only its own grant"), !ASC->FindAbilitySpecFromHandle(LegacyGrant) && ASC->FindAbilitySpecFromHandle(Foreign) && ASC->GetActivatableAbilities().Num() == WithForeign);
        Legacy->AttackDefinitionOverride = AttackA.Get();
        FDataValidationContext InvalidContext;
        TestEqual(TEXT("Supported generic ability cannot author an NPCAttack data override"), Legacy->IsDataValid(InvalidContext), EDataValidationResult::Invalid);
        bool InvalidGrantRan = false; OverrideWorld.NPC->OnGrant = [&] { InvalidGrantRan = true; };
        TestFalse(TEXT("Runtime rejects override on a supported non-NPCAttack before grant/registration"), OverrideWorld.Activation->ActivateNPC(Legacy.Get(), OverrideWorld.Scoring, OverrideWorld.Action, 1));
        TestTrue(TEXT("Invalid override leaves no activation work and never gives an ability"), !InvalidGrantRan && !OverrideWorld.Activation->HasOwnedActivation() &&
            !OverrideWorld.Scoring->IsBatchedNPCDecisionRegistered() && OverrideWorld.Action->GetActionState() == EProjectJNPCActionState::Disabled && ASC->GetActivatableAbilities().Num() == WithForeign);
        OverrideWorld.NPC->OnGrant = nullptr;
        Legacy->AttackAbility = nullptr;
        TestFalse(TEXT("An override cannot silently activate without an attack consumer"), OverrideWorld.Activation->ActivateNPC(Legacy.Get(), OverrideWorld.Scoring, OverrideWorld.Action, 1));
        Legacy->AttackDefinitionOverride = nullptr;
        FDataValidationContext ValidContext;
        TestEqual(TEXT("Typed NPCAttack override validates without inspecting a runtime mesh"), ChoiceA->IsDataValid(ValidContext), EDataValidationResult::Valid);
        if (!TestTrue(TEXT("Same actor admits definition A through the real activation grant"), OverrideWorld.Activation->ActivateNPC(ChoiceA.Get(), OverrideWorld.Scoring, OverrideWorld.Action, 1))) return false;
        const uint64 TokenA = OverrideWorld.Activation->GetOwnershipToken(); const auto GrantA = OverrideWorld.Action->GetAttackAbilityHandle();
        TestTrue(TEXT("NPCAttack Spec.SourceObject is the selected attack data"), ASC->FindAbilitySpecFromHandle(GrantA) && ASC->FindAbilitySpecFromHandle(GrantA)->SourceObject.Get() == AttackA.Get());
        const TWeakObjectPtr<UProject_JAttackDefinition> CapturedA(AttackA.Get());
        const TWeakObjectPtr<UProject_JNPCActivationDefinition> ReleasedDefinitionA(ChoiceA.Get());
        ChoiceA->AttackDefinitionOverride = AttackB.Get();
        TestFalse(TEXT("Changing the same definition cannot implicitly replace an owned grant"), OverrideWorld.Activation->ActivateNPC(ChoiceA.Get(), OverrideWorld.Scoring, OverrideWorld.Action, 1));
        ChoiceA.Reset(); AttackA.Reset();
        CollectGarbage(RF_NoFlags, true);
        TestTrue(TEXT("The idle owned grant pins its captured data after caller definition/data pins are released"), !ReleasedDefinitionA.IsValid() && CapturedA.IsValid() &&
            ASC->FindAbilitySpecFromHandle(GrantA) && ASC->FindAbilitySpecFromHandle(GrantA)->SourceObject.Get() == CapturedA.Get());
        TestFalse(TEXT("A different definition cannot implicitly replace an owned activation"), OverrideWorld.Activation->ActivateNPC(ChoiceB.Get(), OverrideWorld.Scoring, OverrideWorld.Action, 1));
        TestEqual(TEXT("Rejected replacement keeps the exact activation owner"), OverrideWorld.Activation->GetOwnershipToken(), TokenA);
        TestTrue(TEXT("Owner explicitly retires definition A"), OverrideWorld.Activation->DeactivateNPCIfOwned(TokenA));
        CollectGarbage(RF_NoFlags, true);
        TestFalse(TEXT("Retirement releases the selected data after exact grant cancellation/clear"), CapturedA.IsValid());
        TestTrue(TEXT("Definition retirement preserves the unrelated grant"), !ASC->FindAbilitySpecFromHandle(GrantA) && ASC->FindAbilitySpecFromHandle(Foreign));
        if (!TestTrue(TEXT("Same actor admits the selected definition B after retirement"), OverrideWorld.Activation->ActivateNPC(ChoiceB.Get(), OverrideWorld.Scoring, OverrideWorld.Action, 1))) return false;
        const uint64 TokenB = OverrideWorld.Activation->GetOwnershipToken(); const auto GrantB = OverrideWorld.Action->GetAttackAbilityHandle();
        TestTrue(TEXT("Replacement grant owns exactly definition B and its authored level"), TokenB != TokenA && ASC->FindAbilitySpecFromHandle(GrantB) &&
            ASC->FindAbilitySpecFromHandle(GrantB)->SourceObject.Get() == AttackB.Get() && ASC->FindAbilitySpecFromHandle(GrantB)->Level == 2);
        TestFalse(TEXT("Old activation token cannot release a selected replacement"), OverrideWorld.Activation->DeactivateNPCIfOwned(TokenA));
        ASC->InitAbilityActorInfo(OverrideWorld.NPC, OverrideWorld.NPC);
        TestTrue(TEXT("Real same-pointer ASC epoch transition retires the typed grant and exact action/registration"), !OverrideWorld.Activation->HasOwnedActivation() &&
            !ASC->FindAbilitySpecFromHandle(GrantB) && ASC->FindAbilitySpecFromHandle(Foreign) && !OverrideWorld.Scoring->IsBatchedNPCDecisionRegistered() && OverrideWorld.Action->GetActionState() == EProjectJNPCActionState::Disabled);
        if (!TestTrue(TEXT("Typed data activation remains reusable after epoch retirement"), OverrideWorld.Activation->ActivateNPC(ChoiceB.Get(), OverrideWorld.Scoring, OverrideWorld.Action, 1))) return false;
        const auto FinalGrant = OverrideWorld.Action->GetAttackAbilityHandle();
        const TStrongObjectPtr<UProject_JNPCActivationComponent> KeepActivation(OverrideWorld.Activation);
        OverrideWorld.Activation->DestroyComponent();
        TestTrue(TEXT("Component retirement releases typed data grant without touching an unrelated owner"), !ASC->FindAbilitySpecFromHandle(FinalGrant) &&
            ASC->FindAbilitySpecFromHandle(Foreign) && !OverrideWorld.Activation->HasOwnedActivation() && !OverrideWorld.Scoring->IsBatchedNPCDecisionRegistered() && ASC->GetActivatableAbilities().Num() == WithForeign);
        ASC->ClearAbility(Foreign);
        TestEqual(TEXT("Final explicit cleanup returns to the original grant count"), ASC->GetActivatableAbilities().Num(), Initial);
    }
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCCallbackGCTest, "ProjectJ.Continuation.NPCCallbackGC",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCCallbackGCTest::RunTest(const FString&)
{
    {
        ProjectJNPCCallbackTests::FWorld F;
        auto* Target = F.Target(100);
        F.Scoring->StartBatchedNPCDecisions(1); F.Action->StartActions(F.Scoring, {});
        F.Scoring->OnQueryCompleted.Broadcast(Target, 1.0);
        F.NPC->OnLifeQuery = [&] { F.Action->DestroyComponent(); CollectGarbage(RF_NoFlags, true); };
        TestFalse(TEXT("Actual action retirement and GC rejects a pending commit"), F.Action->CanCommitToTarget(Target));
    }
    {
        ProjectJNPCCallbackTests::FWorld F;
        auto* Definition = NewObject<UProject_JNPCActivationDefinition>();
        Definition->AttackAbility = UProject_JNPCGrantCallbackFixture::StaticClass();
        const int32 Before = F.NPC->GetAbilitySystemComponent()->GetActivatableAbilities().Num();
        F.NPC->OnGrant = [&] { F.Activation->DestroyComponent(); CollectGarbage(RF_NoFlags, true); };
        TestFalse(TEXT("Grant callback retirement and GC cannot acknowledge activation"), F.Activation->ActivateNPC(Definition, F.Scoring, F.Action, 1));
        TestEqual(TEXT("Retired activation releases its grant"), F.NPC->GetAbilitySystemComponent()->GetActivatableAbilities().Num(), Before);
        TestFalse(TEXT("Retired activation releases its registration"), F.Scoring->IsBatchedNPCDecisionRegistered());
    }
    {
        ProjectJNPCCallbackTests::FWorld F;
        auto* Definition = NewObject<UProject_JNPCActivationDefinition>();
        Definition->AttackAbility = UProject_JNPCGrantCallbackFixture::StaticClass();
        auto* ASC = F.NPC->GetAbilitySystemComponent();
        const auto Foreign = ASC->GiveAbility(FGameplayAbilitySpec(Definition->AttackAbility));
        TestTrue(TEXT("Normal grant activation"), F.Activation->ActivateNPC(Definition, F.Scoring, F.Action, 1));
        auto* Target = F.Target(100);
        F.Scoring->OnQueryCompleted.Broadcast(Target, 1.0); F.Action->UpdateAction();
        const auto Owned = F.Action->GetAttackAbilityHandle();
        TestTrue(TEXT("Real owned GAS attack is active"), ASC->FindAbilitySpecFromHandle(Owned)->IsActive());
        bool Fired = false, Restarted = false;
        const auto Callback = ASC->OnAbilityEnded.AddLambda([&](const FAbilityEndedData& Data)
        {
            if (Data.AbilitySpecHandle != Owned) { return; }
            Fired = true;
            F.Scoring->StopBatchedNPCDecisions(); Restarted = F.Scoring->StartBatchedNPCDecisions(2);
            F.Activation->DestroyComponent(); CollectGarbage(RF_NoFlags, true);
        });
        F.Activation->DeactivateNPC();
        TestTrue(TEXT("Actual Cancel/End callback executed"), Fired);
        TestTrue(TEXT("Newer callback registration remains active"), Restarted && F.Scoring->IsBatchedNPCDecisionRegistered());
        TestNull(TEXT("Only old activation grant is removed"), ASC->FindAbilitySpecFromHandle(Owned));
        TestNotNull(TEXT("Foreign grant survives callback GC cleanup"), ASC->FindAbilitySpecFromHandle(Foreign));
        ASC->OnAbilityEnded.Remove(Callback);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCActiveActorInfoTest, "ProjectJ.Continuation.NPCActiveActorInfoLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCActiveActorInfoTest::RunTest(const FString&)
{
    // A live GAS attack must end against its original recipient, before Init/Clear replaces it.
    for (int32 Mode = 0; Mode < 8; ++Mode)
    {
        ProjectJNPCCallbackTests::FWorld F;
        const TStrongObjectPtr<UProject_JNPCActivationComponent> KeepActivation(F.Activation);
        const TStrongObjectPtr<UProject_JNPCActivationDefinition> Definition(NewObject<UProject_JNPCActivationDefinition>());
        Definition->AttackAbility = UProject_JNPCGrantCallbackFixture::StaticClass();
        auto* ASC = CastChecked<UProject_JAbilitySystemComponent>(F.NPC->GetAbilitySystemComponent());
        const auto Foreign = ASC->GiveAbility(FGameplayAbilitySpec(Definition->AttackAbility));
        if (!TestTrue(TEXT("Actual NPC activation starts"), F.Activation->ActivateNPC(Definition.Get(), F.Scoring, F.Action, 1))) return false;
        auto* Target = F.Target(100);
        F.Scoring->OnQueryCompleted.Broadcast(Target, 1); F.Action->UpdateAction();
        const auto Owned = F.Action->GetAttackAbilityHandle();
        const auto* Spec = ASC->FindAbilitySpecFromHandle(Owned);
        if (!TestTrue(TEXT("An actual owned attack is active before replacement"), Spec && Spec->IsActive())) return false;
        auto* Replacement = F.World->SpawnActor<AActor>();
        int32 Ended = 0;
        bool CancelledOnOldRecipient = false;
        const uint64 PreviousEpoch = ASC->GetActorInfoRevision();
        const auto Handle = ASC->OnAbilityEnded.AddLambda([&](const FAbilityEndedData& Data)
        {
            if (Data.AbilitySpecHandle != Owned) return;
            ++Ended;
            CancelledOnOldRecipient = ASC->GetOwnerActor() == F.NPC && ASC->GetAvatarActor() == F.NPC;
            if (Mode == 4) ASC->InitAbilityActorInfo(F.NPC, F.NPC);
            if (Mode == 5) ASC->InitAbilityActorInfo(F.NPC, Replacement);
            if (Mode == 6) { F.Activation->DestroyComponent(); CollectGarbage(RF_NoFlags, true); }
            if (Mode == 7) { Replacement->Destroy(); CollectGarbage(RF_NoFlags, true); }
        });
        if (Mode == 0)
        {
            TestTrue(TEXT("Metadata refresh remains supported"), ASC->RefreshActorInfoForCurrentActor(F.NPC));
            TestTrue(TEXT("Metadata refresh preserves the owned activation"), F.Activation->HasOwnedActivation());
            TestTrue(TEXT("Metadata refresh preserves the active GAS attack"), ASC->FindAbilitySpecFromHandle(Owned)->IsActive());
            TestEqual(TEXT("Metadata refresh does not retire the recipient epoch"), ASC->GetActorInfoRevision(), PreviousEpoch);
            TestEqual(TEXT("Metadata refresh invokes no cancellation"), Ended, 0);
            F.Activation->DeactivateNPC();
        }
        else
        {
            if (Mode == 2 || Mode == 5) ASC->ClearActorInfo();
            else ASC->InitAbilityActorInfo(F.NPC, Mode == 3 || Mode == 4 || Mode == 7 ? Replacement : F.NPC);
            TestTrue(TEXT("Init/Clear advances the recipient epoch"), ASC->GetActorInfoRevision() != PreviousEpoch);
            AActor* Expected = Mode == 2 || Mode == 7 ? nullptr : (Mode == 3 || Mode == 5 ? Replacement : F.NPC);
            TestEqual(TEXT("The latest nested request retains ownership of the final recipient"), ASC->GetAvatarActor(), Expected);
        }
        TestEqual(TEXT("Only the owned attack ends, exactly once"), Ended, 1);
        TestTrue(TEXT("Cancellation observes the original GAS recipient"), CancelledOnOldRecipient);
        TestFalse(TEXT("Recipient retirement releases the activation lease"), F.Activation->HasOwnedActivation());
        TestEqual(TEXT("Recipient retirement stops its action consumer"), F.Action->GetActionState(), EProjectJNPCActionState::Disabled);
        TestFalse(TEXT("Recipient retirement releases its own scoring registration"), F.Scoring->IsBatchedNPCDecisionRegistered());
        TestEqual(TEXT("Recipient retirement leaves no scheduled action"), F.Scheduler->GetActionCount(), 0);
        TestNull(TEXT("Only the owned grant is removed"), ASC->FindAbilitySpecFromHandle(Owned));
        TestNotNull(TEXT("An unrelated grant survives recipient retirement and cancellation GC"), ASC->FindAbilitySpecFromHandle(Foreign));
        ASC->OnAbilityEnded.Remove(Handle);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCResultGCTest, "ProjectJ.Continuation.NPCResultBroadcastGC",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCResultGCTest::RunTest(const FString&)
{
    ProjectJNPCCallbackTests::FWorld F;
    auto* Target = F.Target(100);
    const FName OriginalName = Target->GetFName();
    const TStrongObjectPtr<UProject_JNPCResultCallbackFixture> A(NewObject<UProject_JNPCResultCallbackFixture>());
    const TStrongObjectPtr<UProject_JNPCResultCallbackFixture> B(NewObject<UProject_JNPCResultCallbackFixture>());
    int32 Calls = 0; bool RemainingArgumentReadable = false;
    const auto Callback = [&](AActor* Result)
    {
        ++Calls;
        if (Calls == 1) { Target->Destroy(); F.Scoring->DestroyComponent(); CollectGarbage(RF_NoFlags, true); }
        else { RemainingArgumentReadable = Result && Result->GetFName() == OriginalName; }
    };
    A->OnResult = Callback; B->OnResult = Callback;
    F.Scoring->OnQueryCompleted.AddDynamic(A.Get(), &UProject_JNPCResultCallbackFixture::Receive);
    F.Scoring->OnQueryCompleted.AddDynamic(B.Get(), &UProject_JNPCResultCallbackFixture::Receive);
    F.Scoring->Execution = EProject_JTargetScoringExecution::Serial;
    TestTrue(TEXT("Actual Core scoring request"), F.Scoring->RequestTargets({Target}));
    F.World->GetSubsystem<UProject_JTargetScoringSubsystem>()->Tick(0);
    TestEqual(TEXT("Both actual multicast listeners ran"), Calls, 2);
    TestTrue(TEXT("Broadcast argument is held for remaining listeners across GC"), RemainingArgumentReadable);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCPreparedBatchTest, "ProjectJ.Continuation.NPCPreparedBatchSuperseded",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCPreparedBatchTest::RunTest(const FString&)
{
    ProjectJNPCCallbackTests::FWorld F;
    F.Target(100);
    TestTrue(TEXT("First collector registration"), F.Scoring->StartBatchedNPCDecisions(1));
    auto* Peer = F.World->SpawnActor<AProject_JNPCCallbackFixture>();
    auto* PeerScoring = NewObject<UProject_JTargetScoringComponent>(Peer);
    Peer->AddInstanceComponent(PeerScoring); PeerScoring->RegisterComponent();
    TestTrue(TEXT("Second collector registration"), PeerScoring->StartBatchedNPCDecisions(1));
    bool Fired = false, Restarted = false;
    Peer->OnLifeQuery = [&]
    {
        Fired = true; F.Scoring->StopBatchedNPCDecisions(); Restarted = F.Scoring->StartBatchedNPCDecisions(2);
    };
    F.Scheduler->Tick(0);
    TestTrue(TEXT("Later agent's real interface callback ran"), Fired && Restarted);
    TestEqual(TEXT("Retired prepared query is removed before actual Core dispatch"), F.Scheduler->GetStats().SubmittedDecisions, uint64(1));
    TestEqual(TEXT("Only current peer query owns an outstanding decision"), F.Scheduler->GetOutstandingCount(), 1);
    TestEqual(TEXT("Restart preserves exactly two current registry agents"), F.Scheduler->GetAgentCount(), 2);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCReadyBatchRetirementTest, "ProjectJ.Continuation.NPCReadyBatchRetirement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCReadyBatchRetirementTest::RunTest(const FString&)
{
    for (const bool bStopWorld : {false, true})
    {
        ProjectJNPCCallbackTests::FWorld F;
        auto* Target = F.Target(100);
        const FName TargetName = Target->GetFName();
        auto* Peer = F.World->SpawnActor<AProject_JNPCCallbackFixture>();
        auto* PeerScoring = NewObject<UProject_JTargetScoringComponent>(Peer);
        Peer->AddInstanceComponent(PeerScoring); PeerScoring->RegisterComponent();
        const TStrongObjectPtr<UProject_JNPCResultCallbackFixture> First(NewObject<UProject_JNPCResultCallbackFixture>());
        const TStrongObjectPtr<UProject_JNPCResultCallbackFixture> Second(NewObject<UProject_JNPCResultCallbackFixture>());
        int32 FirstCalls = 0, PeerCalls = 0;
        bool bResultReadable = false, bRetiredBeforeGC = false;
        First->OnResult = [&](AActor* Result)
        {
            ++FirstCalls;
            TestEqual(TEXT("The current delivery releases its reservation before external code"), F.Scheduler->GetOutstandingCount(), 1);
            if (bStopWorld) F.World->EndPlay(EEndPlayReason::LevelTransition);
            else
            {
                // The peer already has a result in this same real worker batch.
                // Retire it, then the last producer, from the first result callback.
                PeerScoring->StopBatchedNPCDecisions();
                F.Scoring->StopBatchedNPCDecisions();
            }
            bRetiredBeforeGC = F.Scheduler->GetAgentCount() == 0 && F.Scheduler->GetOutstandingCount() == 0;
            CollectGarbage(RF_NoFlags, true);
            bResultReadable = Result && Result->GetFName() == TargetName;
        };
        Second->OnResult = [&](AActor*) { ++PeerCalls; };
        F.Scoring->OnQueryCompleted.AddDynamic(First.Get(), &UProject_JNPCResultCallbackFixture::Receive);
        PeerScoring->OnQueryCompleted.AddDynamic(Second.Get(), &UProject_JNPCResultCallbackFixture::Receive);
        if (!TestTrue(TEXT("Both actual decision producers register"), F.Scoring->StartBatchedNPCDecisions(1)
            && PeerScoring->StartBatchedNPCDecisions(1))) return false;
        auto* Service = F.World->GetSubsystem<UProject_JTargetScoringSubsystem>();
        const uint64 PreviousLaunched = Service->GetLaunchedTaskCount();
        F.Scheduler->Tick(0);
        if (!TestEqual(TEXT("Both queries use one actual shared batch"), F.Scheduler->GetStats().SubmittedBatches, uint64(1))
            || !TestEqual(TEXT("The actual batch contains both producers"), F.Scheduler->GetStats().SubmittedDecisions, uint64(2))) return false;
        // Pump only the existing Core service. The NPC FIFO cannot consume either
        // result until both results have been delivered by the actual worker.
        const double Deadline = FPlatformTime::Seconds() + 2;
        while (Service->GetPendingCount() && FPlatformTime::Seconds() < Deadline)
        {
            Service->Tick(0);
            FPlatformProcess::SleepNoStats(.001f);
        }
        if (!TestEqual(TEXT("The native worker batch completed before NPC delivery"), Service->GetPendingCount(), 0)
            || !TestEqual(TEXT("The ready FIFO retains both charged results"), F.Scheduler->GetOutstandingCount(), 2)
            || !TestEqual(TEXT("The actual worker root task was launched once"), Service->GetLaunchedTaskCount(), PreviousLaunched + 1)) return false;
        TestEqual(TEXT("No recipient callback ran during Core completion"), FirstCalls + PeerCalls, 0);
        F.Scheduler->Tick(0);
        TestEqual(TEXT("FIFO publishes the first admitted recipient once"), FirstCalls, 1);
        TestEqual(TEXT("The retired peer never receives its old ready result"), PeerCalls, 0);
        TestTrue(TEXT("Retirement closes queue reservations before callback-driven GC"), bRetiredBeforeGC);
        TestTrue(TEXT("The current callback argument remains readable after queue clear and GC"), bResultReadable);
        TestEqual(TEXT("Only the popped current result was visited"), F.Scheduler->GetStats().LastTickResults, 1);
        TestEqual(TEXT("No producer or ready reservation survives the callback retirement"), F.Scheduler->GetOutstandingCount(), 0);
        TestTrue(TEXT("Both real registrations are retired"), !F.Scoring->IsBatchedNPCDecisionRegistered()
            && !PeerScoring->IsBatchedNPCDecisionRegistered());
    }
    return !HasAnyErrors();
}
UE_DEFINE_GAMEPLAY_TAG_STATIC(CleaveContactTag, "ProjectJ.Tests.NPC.CleaveContact");
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCCleaveContactTest, "ProjectJ.NPCGameplay.CleaveContactLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCCleaveContactTest::RunTest(const FString&)
{
    const TStrongObjectPtr<const UProject_JAttackDefinition> Reference(LoadObject<UProject_JAttackDefinition>(nullptr,TEXT("/Game/DataAssetSets/AttackDefinition/Spellblade/DA_Attack_Spellblade_LMB1.DA_Attack_Spellblade_LMB1")));
    const TStrongObjectPtr<USkeletalMesh> MeshAsset(LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple")));
    if(!TestNotNull(TEXT("Existing working attack"),Reference.Get()) || !Reference->Montage || !Reference->DamageEffect || !TestNotNull(TEXT("Existing NPC mesh"),MeshAsset.Get())) return false;
    // Additional teleport/rotation/provider swap, then primary and legacy-default counterparts.
    for(int32 Case=0;Case<6;++Case)
    {
        ProjectJNPCCallbackTests::FWorld F;
        FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Primary=F.World->SpawnActor<AProject_JNPCCallbackFixture>(AProject_JNPCCallbackFixture::StaticClass(),FVector(100,-130,0),FRotator::ZeroRotator,P);
        if(!TestNotNull(TEXT("Primary remains outside the secondary-only physical query"),Primary)) return false;
        Primary->GetAttributeSet()->InitMaxHealth(100);Primary->GetAttributeSet()->InitHealth(100);F.Scheduler->RegisterTarget(Primary,2);
        AProject_JBaseCharacter* Victim=nullptr;AProject_JPlayerState* OldState=nullptr;AProject_JPlayerState* NewState=nullptr;
        const FVector ContactLocation(100,60,0);const FRotator ContactRotation=FRotator::ZeroRotator;
        if(Case==2 || Case==5)
        {
            OldState=F.World->SpawnActor<AProject_JPlayerState>(P);NewState=F.World->SpawnActor<AProject_JPlayerState>(P);
            auto* Player=F.World->SpawnActorDeferred<AProject_JSpellbladeCharacter>(AProject_JSpellbladeCharacter::StaticClass(),FTransform(ContactRotation,ContactLocation),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if(!TestNotNull(TEXT("Actual player-style provider Pawn"),Player) || !TestNotNull(TEXT("Original PlayerState"),OldState) || !TestNotNull(TEXT("Replacement PlayerState"),NewState)) return false;
            Player->SetPlayerState(OldState);Player->FinishSpawning(FTransform(ContactRotation,ContactLocation));Victim=Player;
            for(auto* State:{OldState,NewState})
            {
                auto* ASC=State->GetProjectJAbilitySystemComponent();ASC->AddAttributeSetSubobject(State->GetProjectJAttributeSet());ASC->InitAbilityActorInfo(State,Player);
                ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(),100);ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(),100);
            }
        }
        else
        {
            auto* NPC=F.World->SpawnActor<AProject_JNPCCallbackFixture>(AProject_JNPCCallbackFixture::StaticClass(),ContactLocation,ContactRotation,P);
            if(!TestNotNull(TEXT("Actual noncombat NPC victim"),NPC)) return false;
            NPC->GetAttributeSet()->InitMaxHealth(100);NPC->GetAttributeSet()->InitHealth(100);Victim=NPC;
        }
        Victim->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Victim->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Ignore);
        Primary->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Ignore);F.Scheduler->RegisterTarget(Victim,2);
        auto* OldASC=Cast<UProject_JAbilitySystemComponent>(Victim->GetAbilitySystemComponent());
        if(!TestNotNull(TEXT("Original recipient ASC"),OldASC)) return false;
        const uint64 OldEpoch=OldASC->GetActorInfoRevision();
        auto* Mesh=F.NPC->GetMesh();Mesh->SetSkeletalMesh(MeshAsset.Get());Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass());
        if(!TestNotNull(TEXT("Actual native montage instance"),Mesh->GetAnimInstance())) return false;
        auto* Hit=NewObject<UProject_JCombatHitValidationComponent>(F.NPC);F.NPC->AddInstanceComponent(Hit);Hit->RegisterComponent();
        const TStrongObjectPtr<UProject_JAttackDefinition> Definition(NewObject<UProject_JAttackDefinition>(F.NPC));
        Definition->AttackTag=CleaveContactTag;Definition->Montage=Reference->Montage;Definition->DamageEffect=Reference->DamageEffect;
        Definition->MovementPolicy=EProject_JAttackMovementPolicy::RootMotionMontage;Definition->NPCMaxMeleeTargets=Case>=4?1:2;Definition->HitSpec.TraceRadius=90;
        auto* SourceASC=F.NPC->GetAbilitySystemComponent();const auto Handle=SourceASC->GiveAbility(FGameplayAbilitySpec(UProject_JGameplayAbility_NPCAttack::StaticClass(),1,INDEX_NONE,Definition.Get()));
        auto* Ability=Cast<UProject_JGameplayAbility_NPCAttack>(SourceASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
        if(!TestNotNull(TEXT("Ordinary granted native NPC ability"),Ability) || !Ability->ConfigureHitEvent(CleaveContactTag)) return false;
        if(!F.Scoring->StartBatchedNPCDecisions(1) || !F.Action->StartActions(F.Scoring,Handle)) return false;
        F.Scoring->OnQueryCompleted.Broadcast(Case>=3?static_cast<AActor*>(Victim):Primary,1);ProjectJ::Tests::PublishMoverFrame(F.NPC,MOVE_Walking);F.Action->UpdateAction();
        if(!TestTrue(TEXT("Real current action activated its owned attack"),Ability->IsActive())) return false;
        const TStrongObjectPtr<UProject_JAnimNotifyState_MeleeHit> Notify(NewObject<UProject_JAnimNotifyState_MeleeHit>());
        FindFProperty<FNameProperty>(Notify->GetClass(),TEXT("SocketName"))->SetPropertyValue_InContainer(Notify.Get(),MeshAsset->GetRefSkeleton().GetBoneName(0));
        *FindFProperty<FStructProperty>(Notify->GetClass(),TEXT("HitEventTag"))->ContainerPtrToValuePtr<FGameplayTag>(Notify.Get())=CleaveContactTag;
        FAnimNotifyEventReference Event;Notify->NotifyBegin(Mesh,Definition->Montage,1,Event);
        bool Fired=false,OldTupleRetained=false;
        F.NPC->OnLifeQuery=[&]
        {
            Fired=true;
            if(Case==0 || Case==3 || Case==4) TestTrue(TEXT("Real same-range victim teleport succeeds"),Victim->TeleportTo(FVector(150,60,0),ContactRotation,false,true));
            else if(Case==1) TestTrue(TEXT("Real same-location rotation succeeds"),Victim->TeleportTo(ContactLocation,FRotator(0,45,0),false,true));
            else
            {
                CastChecked<AProject_JSpellbladeCharacter>(Victim)->SetPlayerState(NewState);
                NewState->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(NewState,Victim);
                OldTupleRetained=OldASC->GetOwnerActor()==OldState && OldASC->GetAvatarActor()==Victim && OldASC->GetActorInfoRevision()==OldEpoch;
            }
            CollectGarbage(RF_NoFlags);
        };
        Notify->NotifyTick(Mesh,Definition->Montage,1.f/60,Event);
        TestTrue(TEXT("Actual authority life callback ran while consuming the real secondary contact"),Fired);
        TestTrue(TEXT("Changed victim remains inside authored attack range"),FVector::DistSquared(F.NPC->GetActorLocation(),Victim->GetActorLocation())<FMath::Square(F.Action->AttackRange));
        TestEqual(TEXT("Stale contact never damages the captured old recipient"),OldASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()),100.f);
        if(Case==2 || Case==5)
        {
            TestTrue(TEXT("Actual provider switched while original ASC tuple stayed valid"),OldTupleRetained && Victim->GetAbilitySystemComponent()==NewState->GetProjectJAbilitySystemComponent());
            TestEqual(TEXT("Unvalidated replacement recipient receives no stale damage"),NewState->GetProjectJAttributeSet()->GetHealth(),100.f);
        }
        else TestTrue(TEXT("Victim returns through normal teleport for a fresh contact"),Victim->TeleportTo(ContactLocation,ContactRotation,false,true));
        Notify->NotifyTick(Mesh,Definition->Montage,1.f/60,Event);
        TestTrue(TEXT("A later fresh canonical contact can damage the current valid recipient"),Victim->GetAbilitySystemComponent()->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute())<100.f);
        if(Case==2 || Case==5) TestEqual(TEXT("Old PlayerState recipient remains untouched after fresh current damage"),OldASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()),100.f);
        Notify->NotifyEnd(Mesh,Definition->Montage,Event);F.Action->StopActions();SourceASC->ClearAbility(Handle);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCAttackIntentRetentionTest, "ProjectJ.NPCGameplay.AttackIntentRetention",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCAttackIntentRetentionTest::RunTest(const FString&)
{
    const TStrongObjectPtr<USkeletalMesh> MeshAsset(LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple")));
    const TStrongObjectPtr<UAnimMontage> Montage(LoadObject<UAnimMontage>(nullptr,TEXT("/Game/Anim_Assets/Great_Sword/Animations/Sword/Montage/AM_Greatsword_LMB1.AM_Greatsword_LMB1")));
    if(!TestNotNull(TEXT("Existing attack mesh"),MeshAsset.Get()) || !TestNotNull(TEXT("Existing attack montage"),Montage.Get())) return false;
    for(int32 Case=0;Case<5;++Case)
    {
        ProjectJNPCCallbackTests::FWorld F;
        FActorSpawnParameters P; P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* A=F.World->SpawnActor<AProject_JNPCCallbackFixture>(AProject_JNPCCallbackFixture::StaticClass(),FVector(100,0,0),FRotator::ZeroRotator,P);
        auto* B=F.World->SpawnActor<AProject_JNPCCallbackFixture>(AProject_JNPCCallbackFixture::StaticClass(),FVector(50,0,0),FRotator::ZeroRotator,P);
        if(!TestNotNull(TEXT("Original live target"),A) || !TestNotNull(TEXT("New advisory best"),B)) return false;
        for(auto* Target:{A,B})
        {
            Target->GetAttributeSet()->InitMaxHealth(100);Target->GetAttributeSet()->InitHealth(100);
            Target->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Ignore);
        }
        if(!F.Scheduler->RegisterTarget(A,2) || !F.Scheduler->RegisterTarget(B,1)) return false;
        auto* Mesh=F.NPC->GetMesh();Mesh->SetSkeletalMesh(MeshAsset.Get());Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass());
        if(!TestNotNull(TEXT("Real montage instance"),Mesh->GetAnimInstance())) return false;
        auto* Hit=NewObject<UProject_JCombatHitValidationComponent>(F.NPC);F.NPC->AddInstanceComponent(Hit);Hit->RegisterComponent();
        const TStrongObjectPtr<UProject_JAttackDefinition> Definition(NewObject<UProject_JAttackDefinition>(F.NPC));
        Definition->AttackTag=CleaveContactTag;Definition->Montage=Montage.Get();Definition->DamageEffect=UGameplayEffect::StaticClass();
        Definition->MovementPolicy=EProject_JAttackMovementPolicy::RootMotionMontage;
        auto* ASC=F.NPC->GetAbilitySystemComponent();const auto Handle=ASC->GiveAbility(FGameplayAbilitySpec(UProject_JGameplayAbility_NPCAttack::StaticClass(),1,INDEX_NONE,Definition.Get()));
        const auto* Spec=ASC->FindAbilitySpecFromHandle(Handle);
        auto* Ability=Spec?Cast<UProject_JGameplayAbility_NPCAttack>(Spec->GetPrimaryInstance()):nullptr;
        if(!TestNotNull(TEXT("Real granted attack"),Ability) || !Ability->ConfigureHitEvent(CleaveContactTag)) return false;
        if(!F.Scoring->StartBatchedNPCDecisions(1) || !F.Action->StartActions(F.Scoring,Handle)
            || !ProjectJ::Tests::PublishMoverFrame(F.NPC,MOVE_Walking)) return false;
        const auto PumpUntil=[&](TFunctionRef<bool()> Ready)
        {
            const double Deadline=FPlatformTime::Seconds()+2;
            while(!Ready() && FPlatformTime::Seconds()<Deadline)
            {
                F.Scheduler->Tick(0);F.World->GetSubsystem<UProject_JTargetScoringSubsystem>()->Tick(0);
                FPlatformProcess::SleepNoStats(.001f);
            }
            return Ready();
        };
        if(!TestTrue(TEXT("Actual decision completion activates the ordinary owned attack"),PumpUntil([&]{return Ability->IsActive()&&F.Action->GetIntentTarget()==A;}))) return false;
        TestTrue(TEXT("Actual shared hit authority owns the active montage"),Hit->GetActiveAttackDefinition()==Definition.Get() && Mesh->GetAnimInstance()->Montage_IsPlaying(Montage.Get()));
        if(Case==4)
        {
            auto* Service=F.World->GetSubsystem<UProject_JTargetScoringSubsystem>();
            if(!TestTrue(TEXT("Initial actual query resources drain before the expiry case"),PumpUntil([&]{return !Service->GetPendingCount()&&!F.Scheduler->GetOutstandingCount();}))) return false;
            const uint64 OwnedToken=F.Action->GetOwnershipToken(), Registration=F.Scoring->GetBatchRegistrationRevision();
            const uint64 BeforeSubmit=F.Scheduler->GetStats().SubmittedBatches;
            int32 HardInvalidations=0;
            const auto Invalidation=F.Scoring->OnContextInvalidated.AddLambda([&]{++HardInvalidations;});
            ON_SCOPE_EXIT { F.Scoring->OnContextInvalidated.Remove(Invalidation); };
            const double SubmitDeadline=FPlatformTime::Seconds()+2;
            while(F.Scheduler->GetStats().SubmittedBatches==BeforeSubmit && FPlatformTime::Seconds()<SubmitDeadline)
            {
                F.Scheduler->Tick(0); // The real cadence admits the next query; do not deliver Core yet.
                FPlatformProcess::SleepNoStats(.001f);
            }
            if(!TestTrue(TEXT("A subsequent real shared query owns one Core/NPC reservation"),
                F.Scheduler->GetStats().SubmittedBatches==BeforeSubmit+1 && Service->GetPendingCount()==1 && F.Scheduler->GetOutstandingCount()==1)) return false;
            Service->Tick(0); // Launch the ordinary native worker, with no fake status or completion.
            FPlatformProcess::SleepNoStats(.55f); // Actual delay crosses the unchanged .5s capture-age and .25s Core deadline.
            const double DeliveryDeadline=FPlatformTime::Seconds()+2;
            while(Service->GetPendingCount() && FPlatformTime::Seconds()<DeliveryDeadline)
            {
                Service->Tick(0); FPlatformProcess::SleepNoStats(.001f);
            }
            if(!TestTrue(TEXT("Expired real Core delivery leaves only its NPC ready reservation"),!Service->GetPendingCount()&&F.Scheduler->GetOutstandingCount()==1)) return false;
            const auto BeforeDiscard=F.Scheduler->GetStats().DiscardedDecisions;
            const auto BeforeNewBatches=F.Scheduler->GetStats().SubmittedBatches;
            const auto BeforeNewDecisions=F.Scheduler->GetStats().SubmittedDecisions;
            F.Scheduler->Tick(0);
            TestEqual(TEXT("The stale query is discarded once"),F.Scheduler->GetStats().DiscardedDecisions,BeforeDiscard+1);
            TestTrue(TEXT("Discarded query really exceeded the original capture-age deadline"),F.Scheduler->GetStats().OldestReadyResultMilliseconds>500);
            TestEqual(TEXT("Expiry does not broadcast semantic context revocation"),HardInvalidations,0);
            TestTrue(TEXT("Expiry preserves the exact registered action lease"),F.Action->GetOwnershipToken()==OwnedToken && F.Scoring->GetBatchRegistrationRevision()==Registration);
            TestEqual(TEXT("Expiry does not overwrite the committed live intent"),F.Action->GetIntentTarget(),static_cast<AActor*>(A));
            TestTrue(TEXT("The original owned GAS/montage/hit authority survives advisory expiry"),Ability->IsActive()&&Mesh->GetAnimInstance()->Montage_IsPlaying(Montage.Get())&&Hit->GetActiveAttackDefinition()==Definition.Get());
            TestNull(TEXT("The expired query publishes no selected target"),F.Scoring->GetLastScoredTarget());
            // The same scheduler tick may legitimately admit a fresh query. Only
            // those new submissions may remain charged, never the expired one.
            TestEqual(TEXT("Expired NPC reservation retires independently of normal new submissions"),F.Scheduler->GetOutstandingCount(),int32(F.Scheduler->GetStats().SubmittedDecisions-BeforeNewDecisions));
            TestEqual(TEXT("Expired Core job retires independently of normal new submissions"),Service->GetPendingCount(),int32(F.Scheduler->GetStats().SubmittedBatches-BeforeNewBatches));
            FPlatformProcess::SleepNoStats(static_cast<float>(F.Action->DecisionLifetime+.05)); // Preserve the real default3s bound; no new completion recharges it.
            F.Action->UpdateAction();
            TestFalse(TEXT("Ordinary decision lifetime still cancels a committed attack after expiry"),Ability->IsActive());
            TestNull(TEXT("Expired intent is eventually released by its unchanged lifetime"),F.Action->GetIntentTarget());
            TestNull(TEXT("Lifetime expiry still retires hit authority"),Hit->GetActiveAttackDefinition());
            TestEqual(TEXT("Lifetime enforcement does not need a fabricated hard context event"),HardInvalidations,0);
        }
        else if(Case==2)
        {
            if(!TestTrue(TEXT("Reentry target uses actual hostile membership"),F.Scheduler->RegisterTarget(B,2))) return false;
            const uint64 OldToken=F.Action->GetOwnershipToken();bool Fired=false,Restarted=false;
            A->OnLifeQuery=[&]
            {
                Fired=true;F.Action->StopActions();CollectGarbage(RF_NoFlags);
                Restarted=F.Action->StartActions(F.Scoring,Handle);F.Scoring->OnQueryCompleted.Broadcast(B,1);
            };
            F.Scoring->OnQueryCompleted.Broadcast(B,1);
            TestTrue(TEXT("Retention life query ran real Stop/GC/restart reentry"),Fired&&Restarted);
            TestTrue(TEXT("New action lease supersedes the old receipt"),F.Action->GetOwnershipToken()!=OldToken && F.Action->GetOwnershipToken()!=0);
            TestEqual(TEXT("Old retention callback cannot overwrite the new intent"),F.Action->GetIntentTarget(),static_cast<AActor*>(B));
            TestFalse(TEXT("Explicit stop still cancelled the old attack"),Ability->IsActive());
        }
        else
        {
            TestTrue(TEXT("New hostile eligibility admitted"),F.Scheduler->RegisterTarget(B,2));
            if(!TestTrue(TEXT("Actual worker completion publishes a different best"),PumpUntil([&]{return F.Scoring->GetLastScoredTarget()==B;}))) return false;
            TestEqual(TEXT("Live attack keeps its original intent despite the new best"),F.Action->GetIntentTarget(),static_cast<AActor*>(A));
            TestTrue(TEXT("Original GAS attack and hit authority remain active"),Ability->IsActive()&&Hit->GetActiveAttackDefinition()==Definition.Get());
            if(Case==0 || Case==3)
            {
                if(Case==3)
                {
                    F.Action->DecisionLifetime=.5;
                    FPlatformProcess::SleepNoStats(.55f); // No new completion: leave the existing selection's actual receipt expired.
                }
                Ability->EndAbility(Handle,ASC->AbilityActorInfo.Get(),Ability->GetCurrentActivationInfo(),true,false);
                TestFalse(TEXT("Ordinary natural end releases the attack"),Ability->IsActive());
                F.Action->UpdateAction();
                if(Case==0) TestEqual(TEXT("Central next-action update consumes the current newer best"),F.Action->GetIntentTarget(),static_cast<AActor*>(B));
                else TestNull(TEXT("Re-reading a stored result cannot recharge an expired decision receipt"),F.Action->GetIntentTarget());
                TestFalse(TEXT("Catch-up does not restart the old-target attack in the same update"),Ability->IsActive());
            }
            else
            {
                F.Scheduler->UnregisterTarget(A);F.Action->UpdateAction();
                TestFalse(TEXT("Unregistered current target still immediately cancels GAS"),Ability->IsActive());
                TestNull(TEXT("Invalid current intent is released"),F.Action->GetIntentTarget());
                TestNull(TEXT("Invalidation still retires hit authority"),Hit->GetActiveAttackDefinition());
            }
        }
        F.Action->StopActions();F.Scoring->StopBatchedNPCDecisions();ASC->ClearAbility(Handle);
    }
    AddInfo(TEXT("Actual Core decision completions + NPC action/GAS montage lifetime; the existing native ground fixture supplies the movement state. No world travel/Nav/throughput claim."));
    return !HasAnyErrors();
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCSnapshotRetiredTest, "ProjectJ.Continuation.NPCSnapshotRetired",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCSnapshotRetiredTest::RunTest(const FString&)
{
    for (int32 Mode = 0; Mode < 3; ++Mode)
    {
        ProjectJNPCCallbackTests::FWorld F;
        auto* Trigger = F.Target(100);
        int32 LaterLifeQueries = 0;
        for (int32 Index = 0; Index < 8; ++Index)
        {
            auto* Later = F.Target(200 + Index * 10);
            Later->OnLifeQuery = [&] { ++LaterLifeQueries; };
        }
        TestTrue(TEXT("Actual NPC is registered for the existing decision batch"), F.Scoring->StartBatchedNPCDecisions(1));
        bool Fired = false, Collected = false;
        Trigger->OnLifeQuery = [&]
        {
            Fired = true;
            if (Mode == 0) { F.Scheduler->RegisterTarget(Trigger, 1); }
            else if (Mode == 1) { F.World->BeginTearingDown(); }
            else
            {
                const int32 Before = F.Scheduler->GetTargetCount();
                F.Scheduler->UnregisterTarget(Trigger);
                TestEqual(TEXT("Life callback removes the currently captured registry row"), F.Scheduler->GetTargetCount(), Before - 1);
                TestTrue(TEXT("Life callback readmits that target through the real public API"), F.Scheduler->RegisterTarget(Trigger, 2));
                TestEqual(TEXT("Removal/readmission preserves bounded registry cardinality"), F.Scheduler->GetTargetCount(), Before);
                CollectGarbage(RF_NoFlags, true); Collected = true;
            }
        };
        for (int32 Attempt = 0; Attempt < 32 && !Fired; ++Attempt) { F.Scheduler->Tick(0); }
        TestTrue(TEXT("The first target executes its actual life callback"), Fired);
        TestEqual(TEXT("Retired capture stops before querying later targets"), LaterLifeQueries, 0);
        TestEqual(TEXT("Retired capture does not read a position after the callback fence"), F.Scheduler->GetStats().LastTickTargetPositionReads, 0);
        TestEqual(TEXT("Retired capture does not build an unused spatial index"), F.Scheduler->GetStats().LastTickSnapshotBuilds, 0);
        TestEqual(TEXT("Retired capture never enters the worker queue"), F.Scheduler->GetStats().SubmittedBatches, uint64(0));
        TestEqual(TEXT("Retired capture reserves no worker decision"), F.Scheduler->GetOutstandingCount(), 0);
        if (Mode == 2) TestTrue(TEXT("Registry compaction/readmission survives a real reentrant GC"), Collected && IsValid(Trigger));
        if (Mode != 1)
        {
            for (int32 Attempt = 0; Attempt < 32 && F.Scheduler->GetStats().SubmittedBatches == 0; ++Attempt) { F.Scheduler->Tick(0); }
            TestEqual(TEXT("Next valid capture queries every remaining hostile row"), LaterLifeQueries, 8);
            TestEqual(TEXT("Next valid pass still builds one shared snapshot"), F.Scheduler->GetStats().LastTickSnapshotBuilds, 1);
            TestEqual(TEXT("The valid next pass reaches the existing worker service"), F.Scheduler->GetStats().SubmittedBatches, uint64(1));
        }
        else
        {
            TestEqual(TEXT("Teardown releases the existing agent registry"), F.Scheduler->GetAgentCount(), 0);
            TestFalse(TEXT("Teardown cannot restart a decision producer"), F.Scoring->StartBatchedNPCDecisions(1));
        }
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCSnapshotContinuationTest, "ProjectJ.Continuation.NPCSnapshotContinuation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCSnapshotContinuationTest::RunTest(const FString&)
{
    ProjectJNPCCallbackTests::FWorld Capture127;
    auto* Capture127Prefix = Capture127.Target(100);
    auto* Capture127SuffixA = Capture127.Target(200);
    auto* Capture127SuffixB = Capture127.Target(300);
    auto* Capture127Service = Capture127.World->GetSubsystem<UProject_JTargetScoringSubsystem>();
    int32 Capture127PrefixCalls = 0, Capture127SuffixCalls = 0, Capture127Callbacks = 0;
    Capture127Prefix->OnLifeQuery = [&]
    {
        ++Capture127PrefixCalls;
        FPlatformProcess::SleepNoStats(.002f);
        // A second query of the prefix before submission must be observable.
        Capture127Prefix->OnLifeQuery = [&] { ++Capture127PrefixCalls; };
    };
    Capture127SuffixA->OnLifeQuery = [&] { ++Capture127SuffixCalls; };
    Capture127SuffixB->OnLifeQuery = [&] { ++Capture127SuffixCalls; };
    const TStrongObjectPtr<UProject_JNPCResultCallbackFixture> Capture127Receiver(NewObject<UProject_JNPCResultCallbackFixture>());
    Capture127Receiver->OnResult = [&](AActor* Capture127Result)
    {
        ++Capture127Callbacks;
        TestEqual(TEXT("Real worker result uses the preserved prefix values and exact target identity"), Capture127Result, static_cast<AActor*>(Capture127Prefix));
        Capture127.Scoring->StopBatchedNPCDecisions();
    };
    Capture127.Scoring->OnQueryCompleted.AddDynamic(Capture127Receiver.Get(), &UProject_JNPCResultCallbackFixture::Receive);
    Capture127.Scoring->Range = 1000;
    if (!TestTrue(TEXT("Actual continuation producer registers"), Capture127.Scoring->StartBatchedNPCDecisions(1))) return false;
    for (int32 Capture127Start = 0; Capture127Start < 32 && Capture127PrefixCalls == 0; ++Capture127Start) Capture127.Scheduler->Tick(0);
    TestEqual(TEXT("First actual life callback crosses the real 1ms capture budget"), Capture127PrefixCalls, 1);
    TestEqual(TEXT("The first slice never queries a suffix row"), Capture127SuffixCalls, 0);
    TestEqual(TEXT("The first slice captures precisely its completed prefix row"), Capture127.Scheduler->GetStats().LastTickTargetPositionReads, 1);
    TestTrue(TEXT("A partial capture is pending and not queryable"), Capture127.Scheduler->GetStats().bCapturePending && !Capture127.Scheduler->GetStats().bCaptureReady);
    TestEqual(TEXT("Continuation cursor retains the completed prefix"), Capture127.Scheduler->GetStats().CaptureNextIndex, 1);
    TestEqual(TEXT("The initial target order has three bounded rows"), Capture127.Scheduler->GetStats().CaptureTargetCount, 3);
    TestEqual(TEXT("An incomplete prefix cannot build the spatial index"), Capture127.Scheduler->GetStats().LastTickSnapshotBuilds, 0);
    TestEqual(TEXT("An incomplete prefix is never consumed by a spatial query"), Capture127.Scheduler->GetStats().LastTickCandidateVisits, 0);
    TestEqual(TEXT("An incomplete prefix cannot prepare worker candidate values"), Capture127.Scheduler->GetStats().LastTickSnapshotValues, 0);
    TestEqual(TEXT("An incomplete prefix cannot submit Core work"), Capture127.Scheduler->GetStats().SubmittedBatches, uint64(0));
    TestEqual(TEXT("An incomplete prefix owns no Core or NPC reservation"), Capture127Service->GetPendingCount() + Capture127.Scheduler->GetOutstandingCount(), 0);
    // It remains eligible, but re-reading its position would make suffix A win.
    Capture127Prefix->SetActorLocation(FVector(500, 0, 0));
    int32 Capture127Builds = 0, Capture127PositionReads = 1;
    for (int32 Capture127Attempt = 0; Capture127Attempt < 32 && Capture127.Scheduler->GetStats().SubmittedBatches == 0; ++Capture127Attempt)
    {
        Capture127.Scheduler->Tick(0);
        Capture127Builds += Capture127.Scheduler->GetStats().LastTickSnapshotBuilds;
        Capture127PositionReads += Capture127.Scheduler->GetStats().LastTickTargetPositionReads;
    }
    TestEqual(TEXT("Continuation never calls the completed prefix again"), Capture127PrefixCalls, 1);
    TestEqual(TEXT("Continuation queries each actual suffix once"), Capture127SuffixCalls, 2);
    TestEqual(TEXT("All capture slices perform one position read per target"), Capture127PositionReads, 3);
    TestEqual(TEXT("Only the complete capture builds one spatial index"), Capture127Builds, 1);
    if (!TestEqual(TEXT("The complete capture submits exactly one real worker batch"), Capture127.Scheduler->GetStats().SubmittedBatches, uint64(1))) return false;
    TestEqual(TEXT("Exactly one due source owns the real worker decision"), Capture127.Scheduler->GetStats().SubmittedDecisions, uint64(1));
    const double Capture127Deadline = FPlatformTime::Seconds() + 2;
    while (Capture127Service->GetPendingCount() && FPlatformTime::Seconds() < Capture127Deadline)
    {
        Capture127Service->Tick(0); FPlatformProcess::SleepNoStats(.001f);
    }
    TestEqual(TEXT("The actual Core worker completes"), Capture127Service->GetPendingCount(), 0);
    TestEqual(TEXT("Core completion does not bypass the NPC result consumer"), Capture127Callbacks, 0);
    TestEqual(TEXT("The completed decision remains owned until NPC consumption"), Capture127.Scheduler->GetOutstandingCount(), 1);
    for (int32 Capture127Apply = 0; Capture127Apply < 32 && !Capture127Callbacks; ++Capture127Apply) Capture127.Scheduler->Tick(0);
    TestEqual(TEXT("The same due source consumes one actual worker result"), Capture127Callbacks, 1);
    TestEqual(TEXT("Recipient retirement leaves no worker reservation"), Capture127.Scheduler->GetOutstandingCount(), 0);
    Capture127Prefix->OnLifeQuery = nullptr;
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCSnapshotContinuationRetiredTest, "ProjectJ.Continuation.NPCSnapshotContinuationRetired",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCSnapshotContinuationRetiredTest::RunTest(const FString&)
{
    // Four actual lifetime boundaries, not combinations of settings or injected state.
    for (int32 Retire127Mode = 0; Retire127Mode < 4; ++Retire127Mode)
    {
        ProjectJNPCCallbackTests::FWorld Retire127;
        auto* Retire127Prefix = Retire127.Target(100);
        auto* Retire127Suffix = Retire127.Target(200);
        auto* Retire127Service = Retire127.World->GetSubsystem<UProject_JTargetScoringSubsystem>();
        int32 Retire127PrefixCalls = 0, Retire127SuffixCalls = 0, Retire127Callbacks = 0;
        Retire127Prefix->OnLifeQuery = [&]
        {
            ++Retire127PrefixCalls; FPlatformProcess::SleepNoStats(.002f);
            Retire127Prefix->OnLifeQuery = [&] { ++Retire127PrefixCalls; };
        };
        Retire127Suffix->OnLifeQuery = [&] { ++Retire127SuffixCalls; };
        const TStrongObjectPtr<UProject_JNPCResultCallbackFixture> Retire127Receiver(NewObject<UProject_JNPCResultCallbackFixture>());
        AActor* Retire127Expected = Retire127Mode == 0 || Retire127Mode == 3 ? Retire127Suffix : Retire127Prefix;
        Retire127Receiver->OnResult = [&](AActor* Retire127Result)
        {
            ++Retire127Callbacks;
            TestEqual(TEXT("Only the fresh current registration/capture may publish"), Retire127Result, Retire127Expected);
            Retire127.Scoring->StopBatchedNPCDecisions();
        };
        Retire127.Scoring->OnQueryCompleted.AddDynamic(Retire127Receiver.Get(), &UProject_JNPCResultCallbackFixture::Receive);
        Retire127.Scoring->Range = 1000;
        if (!TestTrue(TEXT("Partial retirement producer registers"), Retire127.Scoring->StartBatchedNPCDecisions(1))) return false;
        for (int32 Retire127Start = 0; Retire127Start < 32 && Retire127PrefixCalls == 0; ++Retire127Start) Retire127.Scheduler->Tick(0);
        if (!TestTrue(TEXT("Retirement starts from a real one-row partial capture"), Retire127PrefixCalls == 1 && Retire127SuffixCalls == 0 &&
            Retire127.Scheduler->GetStats().bCapturePending && Retire127.Scheduler->GetStats().CaptureNextIndex == 1)) return false;
        TestEqual(TEXT("Partial retirement has no spatial index or Core submission"), Retire127.Scheduler->GetStats().LastTickSnapshotBuilds, 0);
        TestEqual(TEXT("Partial retirement has no Core or NPC reservation"), Retire127Service->GetPendingCount() + Retire127.Scheduler->GetOutstandingCount(), 0);
        if (Retire127Mode == 0)
        {
            const uint64 Retire127Revision = Retire127.Scheduler->GetTargetRegistryRevision();
            Retire127.Scheduler->UnregisterTarget(Retire127Prefix);
            if (!TestTrue(TEXT("Registry readmission between slices succeeds"), Retire127.Scheduler->RegisterTarget(Retire127Prefix, 2))) return false;
            Retire127Prefix->SetActorLocation(FVector(600, 0, 0));
            CollectGarbage(RF_NoFlags, true);
            TestTrue(TEXT("Actual readmission/GC changes the capture's registry revision"), Retire127.Scheduler->GetTargetRegistryRevision() != Retire127Revision && IsValid(Retire127Prefix));
        }
        else if (Retire127Mode == 1)
        {
            Retire127.Scoring->StopBatchedNPCDecisions();
            TestEqual(TEXT("Stopping the producer releases the partial's source registration"), Retire127.Scheduler->GetAgentCount(), 0);
            if (!TestTrue(TEXT("A new registration may replace a partial producer"), Retire127.Scoring->StartBatchedNPCDecisions(1))) return false;
        }
        else if (Retire127Mode == 2)
        {
            Retire127.World->EndPlay(EEndPlayReason::Quit); Retire127.Scheduler->Tick(0);
            TestEqual(TEXT("World EndPlay closes the partial source registry"), Retire127.Scheduler->GetAgentCount(), 0);
            TestFalse(TEXT("World EndPlay prevents readmission"), Retire127.Scoring->StartBatchedNPCDecisions(1));
            TestEqual(TEXT("World EndPlay never submits the incomplete capture"), Retire127.Scheduler->GetStats().SubmittedBatches, uint64(0));
            TestEqual(TEXT("World EndPlay leaves no worker reservation"), Retire127Service->GetPendingCount() + Retire127.Scheduler->GetOutstandingCount(), 0);
            TestEqual(TEXT("World EndPlay cannot publish a partial result"), Retire127Callbacks, 0);
            Retire127Prefix->OnLifeQuery = nullptr; Retire127Suffix->OnLifeQuery = nullptr;
            continue;
        }
        else
        {
            Retire127Prefix->SetActorLocation(FVector(600, 0, 0));
            FPlatformProcess::SleepNoStats(.55f);
        }
        int32 Retire127Builds = 0;
        for (int32 Retire127Attempt = 0; Retire127Attempt < 32 && Retire127.Scheduler->GetStats().SubmittedBatches == 0; ++Retire127Attempt)
        {
            Retire127.Scheduler->Tick(0);
            Retire127Builds += Retire127.Scheduler->GetStats().LastTickSnapshotBuilds;
        }
        TestEqual(TEXT("Fresh capture re-queries the formerly captured prefix"), Retire127PrefixCalls, 2);
        TestEqual(TEXT("Only the fresh capture queries its suffix"), Retire127SuffixCalls, 1);
        TestEqual(TEXT("Only the fresh complete capture builds a spatial index"), Retire127Builds, 1);
        TestEqual(TEXT("Data-only partial invalidation is not a discarded worker decision"), Retire127.Scheduler->GetStats().DiscardedDecisions, uint64(0));
        if (Retire127Mode == 3) TestEqual(TEXT("Between-slice age expires the original data-only capture once"), Retire127.Scheduler->GetStats().CaptureExpired, uint64(1));
        if (!TestEqual(TEXT("Fresh recovery submits one actual Core batch"), Retire127.Scheduler->GetStats().SubmittedBatches, uint64(1))) return false;
        const double Retire127Deadline = FPlatformTime::Seconds() + 2;
        while (!Retire127Callbacks && FPlatformTime::Seconds() < Retire127Deadline)
        {
            Retire127Service->Tick(0); Retire127.Scheduler->Tick(0); FPlatformProcess::SleepNoStats(.001f);
        }
        TestEqual(TEXT("One fresh real-worker completion recovers after partial retirement"), Retire127Callbacks, 1);
        TestEqual(TEXT("Fresh recovery's recipient closes every reservation"), Retire127.Scheduler->GetOutstandingCount(), 0);
        Retire127Prefix->OnLifeQuery = nullptr; Retire127Suffix->OnLifeQuery = nullptr;
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCCaptureAgeTest, "ProjectJ.Continuation.NPCSnapshotCaptureAge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCCaptureAgeTest::RunTest(const FString&)
{
    // Exercise actual interface delays and the real worker; no clock or queued-result injection.
    for (int32 Case = 0; Case < 3; ++Case)
    {
        ProjectJNPCCallbackTests::FWorld F;
        auto* Target = F.Target(100);
        auto* Service = F.World->GetSubsystem<UProject_JTargetScoringSubsystem>();
        const TStrongObjectPtr<UProject_JNPCResultCallbackFixture> Receiver(NewObject<UProject_JNPCResultCallbackFixture>());
        int32 Callbacks = 0;
        bool bRecovering = false, bCaptureHook = false, bEligibilityHook = false;
        Receiver->OnResult = [&](AActor* Result)
        {
            ++Callbacks;
            TestEqual(TEXT("The actual result preserves target identity"), Result, static_cast<AActor*>(Target));
            if (bRecovering) F.Scoring->StopBatchedNPCDecisions();
        };
        F.Scoring->OnQueryCompleted.AddDynamic(Receiver.Get(), &UProject_JNPCResultCallbackFixture::Receive);
        if (!TestTrue(TEXT("Actual age scenario registers"), F.Scoring->StartBatchedNPCDecisions(1))) return false;
        if (Case < 2)
        {
            F.NPC->OnLifeQuery = [&]
            {
                bCaptureHook = true;
                FPlatformProcess::SleepNoStats(Case == 0 ? .55f : .30f);
            };
        }
        F.Scheduler->Tick(0);
        for (int32 CaptureStart = 0; Case < 2 && CaptureStart < 32 && !bCaptureHook; ++CaptureStart) F.Scheduler->Tick(0);
        if (Case < 2 && !TestTrue(TEXT("The producer's actual life callback delayed collection"), bCaptureHook)) return false;
        // A budgeted partial capture may legitimately finish in another Tick;
        // its timestamp must still precede the original delayed callback.
        for (int32 CaptureAttempt = 0; Case != 0 && CaptureAttempt < 32 && F.Scheduler->GetStats().SubmittedBatches == 0; ++CaptureAttempt)
        {
            F.Scheduler->Tick(0);
        }
        if (Case == 0)
        {
            TestEqual(TEXT("Expired preparation does not submit a Core batch"), F.Scheduler->GetStats().SubmittedBatches, uint64(0));
            TestEqual(TEXT("Expired preparation owns no Core or NPC reservation"), Service->GetPendingCount() + F.Scheduler->GetOutstandingCount(), 0);
            TestEqual(TEXT("Data-only preparation expiry has its own capture statistic"), F.Scheduler->GetStats().CaptureExpired, uint64(1));
        }
        else
        {
            if (!TestEqual(TEXT("A still-fresh preparation submits one real batch"), F.Scheduler->GetStats().SubmittedBatches, uint64(1))) return false;
            const double CoreStarted = FPlatformTime::Seconds();
            while (Service->GetPendingCount() && FPlatformTime::Seconds() - CoreStarted < 2)
            {
                Service->Tick(0);
                FPlatformProcess::SleepNoStats(.001f);
            }
            if (!TestEqual(TEXT("The real worker completed before the delayed NPC consumer"), Service->GetPendingCount(), 0)
                || !TestTrue(TEXT("Worker completion precedes its unchanged Core deadline"), FPlatformTime::Seconds() - CoreStarted < .25)
                || !TestEqual(TEXT("The NPC FIFO still owns the completed result"), F.Scheduler->GetOutstandingCount(), 1)) return false;
            if (Case == 1) FPlatformProcess::SleepNoStats(.25f);
            else Target->OnLifeQuery = [&] { bEligibilityHook = true; FPlatformProcess::SleepNoStats(.55f); };
            F.Scheduler->Tick(0);
            if (Case == 1) TestTrue(TEXT("Ready age includes collection before Core submission"), F.Scheduler->GetStats().OldestReadyResultMilliseconds > 500);
            else TestTrue(TEXT("The final eligibility callback really crossed the age limit"), bEligibilityHook);
        }
        TestEqual(TEXT("Expired values do not publish a target"), Callbacks, 0);
        TestNull(TEXT("Expiry clears the selected target"), F.Scoring->GetLastScoredTarget());
        TestEqual(TEXT("Only an actually submitted expired decision is discarded"), F.Scheduler->GetStats().DiscardedDecisions, Case == 0 ? uint64(0) : uint64(1));
        F.Scoring->StopBatchedNPCDecisions();
        TestEqual(TEXT("Retirement closes any normal follow-up reservation"), F.Scheduler->GetOutstandingCount(), 0);
        bRecovering = true; Callbacks = 0;
        if (!TestTrue(TEXT("A fresh producer may register after expiry"), F.Scoring->StartBatchedNPCDecisions(1))) return false;
        const double RecoveryDeadline = FPlatformTime::Seconds() + 2;
        while (!Callbacks && FPlatformTime::Seconds() < RecoveryDeadline)
        {
            F.Scheduler->Tick(0); Service->Tick(0);
            FPlatformProcess::SleepNoStats(.001f);
        }
        TestEqual(TEXT("A new fresh real-worker result still publishes once"), Callbacks, 1);
        TestEqual(TEXT("Fresh callback retirement leaves no reservation"), F.Scheduler->GetOutstandingCount(), 0);
        AddInfo(FString::Printf(TEXT("NPCSnapshotCaptureAge case=%d discarded=%llu fresh_callbacks=%d"), Case, F.Scheduler->GetStats().DiscardedDecisions, Callbacks));
    }
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCCombatAssemblyContractTest, "ProjectJ.NPCGameplay.CombatAssemblyContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCCombatAssemblyContractTest::RunTest(const FString&)
{
    const TStrongObjectPtr<UProject_JNPCActivationDefinition> Definition(NewObject<UProject_JNPCActivationDefinition>());
    Definition->AttackAbility = UProject_JNPCGrantCallbackFixture::StaticClass();
    {
        ProjectJNPCCallbackTests::FWorld F;
        auto* ASC = F.NPC->GetAbilitySystemComponent();
        const int32 BaselineGrants = ASC->GetActivatableAbilities().Num();
        ON_SCOPE_EXIT { F.NPC->OnASCQuery = nullptr; F.NPC->OnGrant = nullptr; F.NPC->StopCombatActions(); };
        TestNull(TEXT("Native fixture starts without hit support"), F.NPC->FindComponentByClass<UProject_JCombatHitValidationComponent>());
        TestNull(TEXT("Native fixture starts without presentation support"), F.NPC->FindComponentByClass<UProject_JCombatPresentationComponent>());
        if (!TestTrue(TEXT("Public helper assembles missing combat support before grant"), F.NPC->StartCombatActions(Definition.Get(), 1))) return false;
        auto* Hit = F.NPC->FindComponentByClass<UProject_JCombatHitValidationComponent>();
        auto* Presentation = F.NPC->FindComponentByClass<UProject_JCombatPresentationComponent>();
        if (!TestNotNull(TEXT("Actual helper-created hit component"), Hit) || !TestNotNull(TEXT("Actual helper-created presentation component"), Presentation)) return false;
        TestTrue(TEXT("Both capabilities are registered, begun, replicated and tickless"), Hit->IsRegistered() && Hit->HasBegunPlay() && Hit->GetIsReplicated()
            && Presentation->IsRegistered() && Presentation->HasBegunPlay() && Presentation->GetIsReplicated()
            && !Hit->PrimaryComponentTick.bCanEverTick && !Presentation->PrimaryComponentTick.bCanEverTick);
        const auto FirstGrant = F.Action->GetAttackAbilityHandle();
        TestTrue(TEXT("Ordinary activation owns its real GAS grant"), FirstGrant.IsValid() && ASC->FindAbilitySpecFromHandle(FirstGrant));
        F.NPC->StopCombatActions();
        TestFalse(TEXT("Normal Stop retires only the helper activation"), F.NPC->HasCombatActions());
        TestTrue(TEXT("Normal Stop keeps both actual component instances"), Hit->IsRegistered() && Presentation->IsRegistered()
            && F.NPC->FindComponentByClass<UProject_JCombatHitValidationComponent>() == Hit
            && F.NPC->FindComponentByClass<UProject_JCombatPresentationComponent>() == Presentation);
        TestTrue(TEXT("Stopped owned grant is removed"), !ASC->FindAbilitySpecFromHandle(FirstGrant) && ASC->GetActivatableAbilities().Num() == BaselineGrants);
        const TStrongObjectPtr<UProject_JCombatPresentationSet> AuthoredSet(NewObject<UProject_JCombatPresentationSet>());
        Presentation->BasePresentationSet = AuthoredSet.Get();
        const FName Marker(TEXT("Assembly.AuthoredSetting")); Hit->ComponentTags.Add(Marker);
        Hit->ResetSpatialHistory(); const uint64 SpatialRevision = Hit->GetSpatialRevision();
        if (!TestTrue(TEXT("Second public activation reuses retained capabilities"), F.NPC->StartCombatActions(Definition.Get(), 1))) return false;
        TInlineComponentArray<UProject_JCombatHitValidationComponent*> Hits(F.NPC);
        TInlineComponentArray<UProject_JCombatPresentationComponent*> Presentations(F.NPC);
        TestTrue(TEXT("Reuse has no duplicate component and preserves authored settings/native history"), Hits.Num() == 1 && Hits[0] == Hit
            && Presentations.Num() == 1 && Presentations[0] == Presentation && Presentation->BasePresentationSet == AuthoredSet.Get()
            && Hit->ComponentTags.Contains(Marker) && Hit->GetSpatialRevision() == SpatialRevision);
        F.NPC->StopCombatActions();
        TestTrue(TEXT("Repeated stop leaves idle support without owned action or grant"), !F.NPC->HasCombatActions()
            && !F.Activation->HasOwnedActivation() && F.Action->GetActionState() == EProjectJNPCActionState::Disabled
            && F.Scheduler->GetActionCount() == 0 && ASC->GetActivatableAbilities().Num() == BaselineGrants);
    }
    // Both capability types must reject ambiguous or deliberately unregistered authored objects.
    for (int32 Case = 0; Case < 4; ++Case)
    {
        ProjectJNPCCallbackTests::FWorld F;
        auto* ASC = F.NPC->GetAbilitySystemComponent();
        const int32 BaselineGrants = ASC->GetActivatableAbilities().Num();
        const bool bHit = Case < 2, bDuplicate = (Case % 2) == 0;
        UActorComponent* First = bHit ? static_cast<UActorComponent*>(NewObject<UProject_JCombatHitValidationComponent>(F.NPC))
            : static_cast<UActorComponent*>(NewObject<UProject_JCombatPresentationComponent>(F.NPC));
        F.NPC->AddInstanceComponent(First);
        UActorComponent* Second = nullptr;
        if (bDuplicate)
        {
            First->RegisterComponent();
            Second = bHit ? static_cast<UActorComponent*>(NewObject<UProject_JCombatHitValidationComponent>(F.NPC))
                : static_cast<UActorComponent*>(NewObject<UProject_JCombatPresentationComponent>(F.NPC));
            F.NPC->AddInstanceComponent(Second); Second->RegisterComponent();
        }
        bool bGrantRan = false; F.NPC->OnGrant = [&] { bGrantRan = true; };
        ON_SCOPE_EXIT { F.NPC->OnGrant = nullptr; F.NPC->StopCombatActions(); };
        TestFalse(TEXT("Public helper rejects ambiguous/unregistered authored capability"), F.NPC->StartCombatActions(Definition.Get(), 1));
        TInlineComponentArray<UProject_JCombatHitValidationComponent*> Hits(F.NPC);
        TInlineComponentArray<UProject_JCombatPresentationComponent*> Presentations(F.NPC);
        TestTrue(TEXT("Refusal preserves exact authored objects/registration and creates no missing counterpart"), IsValid(First)
            && First->IsRegistered() == bDuplicate && (!Second || (IsValid(Second) && Second->IsRegistered()))
            && (bHit ? Hits.Num() : Presentations.Num()) == (bDuplicate ? 2 : 1) && (bHit ? Presentations.Num() : Hits.Num()) == 0);
        TestTrue(TEXT("Refusal publishes no GAS/action/decision ownership"), !bGrantRan && !F.NPC->HasCombatActions()
            && !F.Activation->HasOwnedActivation() && !F.Scoring->IsBatchedNPCDecisionRegistered()
            && F.Scheduler->GetActionCount() == 0 && ASC->GetActivatableAbilities().Num() == BaselineGrants);
    }
    {
        ProjectJNPCCallbackTests::FWorld F;
        auto* ASC = F.NPC->GetAbilitySystemComponent();
        const int32 BaselineGrants = ASC->GetActivatableAbilities().Num();
        const TStrongObjectPtr<UProject_JAttackDefinition> ForeignAttack(NewObject<UProject_JAttackDefinition>());
        ForeignAttack->AttackTag = CleaveContactTag;
        TWeakObjectPtr<UProject_JCombatHitValidationComponent> ClaimedHit;
        TWeakObjectPtr<UProject_JCombatPresentationComponent> ClaimedPresentation;
        bool bClaimed = false, bGrantRan = false;
        TFunction<void()> Probe;
        Probe = [&]
        {
            auto* Hit = F.NPC->FindComponentByClass<UProject_JCombatHitValidationComponent>();
            auto* Presentation = F.NPC->FindComponentByClass<UProject_JCombatPresentationComponent>();
            if (!Hit || !Presentation || !Hit->HasBegunPlay() || !Presentation->HasBegunPlay()) { F.NPC->OnASCQuery = Probe; return; }
            bClaimed = true; ClaimedHit = Hit; ClaimedPresentation = Presentation;
            Hit->BeginAttackNode(CleaveContactTag, ForeignAttack.Get());
            Presentation->BeginAttackPresentation(CleaveContactTag);
        };
        F.NPC->OnASCQuery = Probe; F.NPC->OnGrant = [&] { bGrantRan = true; };
        ON_SCOPE_EXIT
        {
            F.NPC->OnASCQuery = nullptr; F.NPC->OnGrant = nullptr; F.NPC->StopCombatActions();
            if (ClaimedHit.IsValid()) ClaimedHit->EndAttack();
            if (ClaimedPresentation.IsValid()) ClaimedPresentation->EndAttackPresentation();
        };
        TestFalse(TEXT("Actual virtual ASC callback after registration can claim support and supersede admission"), F.NPC->StartCombatActions(Definition.Get(), 1));
        TestTrue(TEXT("Callback really observed both begun components before any grant"), bClaimed && !bGrantRan);
        TestTrue(TEXT("Rollback preserves foreign node/presentation ownership and their exact objects"), ClaimedHit.IsValid() && ClaimedPresentation.IsValid()
            && ClaimedHit->IsRegistered() && ClaimedPresentation->IsRegistered() && ClaimedHit->GetActiveAttackDefinition() == ForeignAttack.Get()
            && ClaimedHit->GetActiveAttackNodeTag() == CleaveContactTag && ClaimedPresentation->GetActiveAttackTag() == CleaveContactTag);
        TestTrue(TEXT("Foreign claim leaves no helper ownership"), !F.NPC->HasCombatActions() && !F.Activation->HasOwnedActivation()
            && !F.Scoring->IsBatchedNPCDecisionRegistered() && F.Scheduler->GetActionCount() == 0
            && ASC->GetActivatableAbilities().Num() == BaselineGrants);
    }
    for (int32 Mode = 0; Mode < 2; ++Mode)
    {
        ProjectJNPCCallbackTests::FWorld F;
        auto* ASC = CastChecked<UProject_JAbilitySystemComponent>(F.NPC->GetAbilitySystemComponent());
        const auto ForeignGrant = ASC->GiveAbility(FGameplayAbilitySpec(Definition->AttackAbility));
        const int32 BaselineGrants = ASC->GetActivatableAbilities().Num();
        const uint64 BeforeEpoch = ASC->GetActorInfoRevision();
        bool bGrantRan = false, bSawCapabilities = false;
        F.NPC->OnGrant = [&]
        {
            bGrantRan = true;
            auto* Hit = F.NPC->FindComponentByClass<UProject_JCombatHitValidationComponent>();
            auto* Presentation = F.NPC->FindComponentByClass<UProject_JCombatPresentationComponent>();
            bSawCapabilities = Hit && Hit->IsRegistered() && Presentation && Presentation->IsRegistered();
            if (Mode == 0 && Presentation) Presentation->DestroyComponent();
            else if (Mode == 1) ASC->InitAbilityActorInfo(F.NPC, F.NPC);
            CollectGarbage(RF_NoFlags, true);
        };
        ON_SCOPE_EXIT
        {
            F.NPC->OnASCQuery = nullptr; F.NPC->OnGrant = nullptr; F.NPC->StopCombatActions();
            if (ASC->FindAbilitySpecFromHandle(ForeignGrant)) ASC->ClearAbility(ForeignGrant);
        };
        TestFalse(TEXT("Actual GAS grant callback component destruction or recipient rebind rejects helper publication"), F.NPC->StartCombatActions(Definition.Get(), 1));
        TestTrue(TEXT("Real OnGive callback sees registered support before changing it and GC"), bGrantRan && bSawCapabilities);
        TestTrue(TEXT("Rollback clears only the attempted owned grant/action/registration"), !F.NPC->HasCombatActions()
            && !F.Activation->HasOwnedActivation() && !F.Scoring->IsBatchedNPCDecisionRegistered()
            && F.Action->GetActionState() == EProjectJNPCActionState::Disabled && F.Scheduler->GetActionCount() == 0
            && ASC->GetActivatableAbilities().Num() == BaselineGrants && ASC->FindAbilitySpecFromHandle(ForeignGrant));
        TestNull(TEXT("Unclaimed newly-created hit support is retired after failed admission"), F.NPC->FindComponentByClass<UProject_JCombatHitValidationComponent>());
        TestNull(TEXT("Unclaimed newly-created presentation support is retired after failed admission"), F.NPC->FindComponentByClass<UProject_JCombatPresentationComponent>());
        if (Mode == 1) TestTrue(TEXT("Rollback preserves the callback's newer actor-info epoch/recipient"), ASC->GetActorInfoRevision() > BeforeEpoch
            && ASC->GetOwnerActor() == F.NPC && ASC->GetAvatarActor() == F.NPC);
    }
    return !HasAnyErrors();
}
#endif
