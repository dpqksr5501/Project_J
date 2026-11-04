#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Project_JNPCCallbackFixture.h"
#include "Components/Project_JNPCActivationComponent.h"
#include "Components/Project_JNPCActionComponent.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "System/Project_JNPCDecisionSubsystem.h"
#include "System/Project_JTargetScoringSubsystem.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "GameplayEffect.h"
#include "AIController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"

namespace ProjectJDamageThreatTests
{
struct FWorld
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    AProject_JNPCCallbackFixture* NPC = nullptr;
    AProject_JNPCCallbackFixture* Near = nullptr;
    AProject_JNPCCallbackFixture* Far = nullptr;
    AProject_JNPCCallbackFixture* Friendly = nullptr;
    UProject_JNPCActivationComponent* Activation = nullptr;
    UProject_JNPCActionComponent* Action = nullptr;
    UProject_JTargetScoringComponent* Scoring = nullptr;
    UProject_JNPCDecisionSubsystem* Scheduler = nullptr;
    UProject_JTargetScoringSubsystem* Service = nullptr;
    TStrongObjectPtr<UProject_JNPCActivationDefinition> Definition{NewObject<UProject_JNPCActivationDefinition>()};
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        NPC = Spawn(0); Near = Spawn(100); Far = Spawn(150); Friendly = Spawn(125);
        World->SpawnActor<AAIController>()->Possess(NPC);
        for (auto* Actor : {NPC, Near, Far, Friendly}) Actor->GetAbilitySystemComponent()->InitAbilityActorInfo(Actor, Actor);
        auto* Source = NewObject<UProject_JTargetScoringComponent>(NPC); NPC->AddInstanceComponent(Source); Source->RegisterComponent(); Scoring = Source;
        Scoring->Range = 1000; Scoring->DirectionWeight = 0;
        Action = NewObject<UProject_JNPCActionComponent>(NPC); NPC->AddInstanceComponent(Action); Action->RegisterComponent();
        Activation = NewObject<UProject_JNPCActivationComponent>(NPC); NPC->AddInstanceComponent(Activation); Activation->RegisterComponent();
        World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
        for (auto* Actor : {NPC, Near, Far, Friendly}) { Actor->GetAttributeSet()->InitMaxHealth(1000); Actor->GetAttributeSet()->InitHealth(1000); }
        Scheduler = World->GetSubsystem<UProject_JNPCDecisionSubsystem>(); Service = World->GetSubsystem<UProject_JTargetScoringSubsystem>();
        Scheduler->RegisterTarget(Near, 2); Scheduler->RegisterTarget(Far, 2); Scheduler->RegisterTarget(Friendly, 1);
    }
    ~FWorld()
    {
        NPC->GetAttributeSet()->OnConfirmedHealthGELoss.Clear();
        Activation->DeactivateNPC(); World->EndPlay(EEndPlayReason::LevelTransition);
        World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    }
    AProject_JNPCCallbackFixture* Spawn(double X)
    {
        FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        return World->SpawnActor<AProject_JNPCCallbackFixture>(AProject_JNPCCallbackFixture::StaticClass(), FVector(X,0,0), FRotator::ZeroRotator, P);
    }
    bool Activate(bool bEnabled)
    {
        Definition->bUseDamageThreat = bEnabled;
        return Activation->ActivateNPC(Definition.Get(), Scoring, Action, 1);
    }
    void Apply(AProject_JNPCCallbackFixture* Source, float Amount, FGameplayAttribute Attribute = UProject_JAttributeSet::GetHealthAttribute())
    {
        const TStrongObjectPtr<UGameplayEffect> GE(NewObject<UGameplayEffect>());
        GE->DurationPolicy = EGameplayEffectDurationType::Instant;
        auto& Modifier = GE->Modifiers.AddDefaulted_GetRef(); Modifier.Attribute = Attribute;
        Modifier.ModifierOp = EGameplayModOp::Additive; Modifier.ModifierMagnitude = FScalableFloat(Amount);
        auto* ASC = Source->GetAbilitySystemComponent(); auto Context = ASC->MakeEffectContext(); Context.AddInstigator(Source,Source);
        FGameplayEffectSpec Spec(GE.Get(),Context,1); ASC->ApplyGameplayEffectSpecToTarget(Spec,NPC->GetAbilitySystemComponent());
    }
    bool Until(AActor* Expected)
    {
        const double Deadline = FPlatformTime::Seconds() + 2.5;
        while (FPlatformTime::Seconds() < Deadline)
        {
            Scheduler->Tick(0); Service->Tick(0);
            if (Scoring->GetLastScoredTarget() == Expected && Action->GetIntentTarget() == Expected) return true;
            FPlatformProcess::SleepNoStats(.001f);
        }
        return false;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCDamageThreatTest, "ProjectJ.NPCGameplay.DamageThreatLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCDamageThreatTest::RunTest(const FString&)
{
    using namespace ProjectJDamageThreatTests;
    {
        FWorld F;
        TestTrue(TEXT("The default policy activates the existing NPC runtime"), F.Activate(false));
        F.Apply(F.Far,-50);
        TestTrue(TEXT("Default OFF keeps ordinary nearest-target scoring and action consumption"), F.Until(F.Near));
        TestEqual(TEXT("Default OFF binds no Health GE observation"), F.NPC->GetAttributeSet()->GetHealthGEObservationCapacityForTest(), 0);
        TestFalse(TEXT("Default OFF creates no threat policy"), F.Activation->IsDamageThreatActive());
    }
    {
        FWorld F;
        TestTrue(TEXT("Explicit opt-in starts the threat lease"), F.Activate(true));
        F.Scheduler->Tick(0);
        const uint64 QueryRevision = F.Scoring->ContextRevision;
        const auto Pending = F.Scoring->PendingToken;
        TestTrue(TEXT("The real unchanged worker query is pending before damage"), Pending.IsValid());
        F.Apply(F.Near,-10); F.Apply(F.Far,-30);
        TestEqual(TEXT("Damage accumulation does not invalidate an in-flight query"), F.Scoring->ContextRevision, QueryRevision);
        TestEqual(TEXT("Damage does not cancel/reissue the current worker token"), F.Scoring->PendingToken.Value, Pending.Value);
        TestTrue(TEXT("Greater actual Health loss outranks a nearer source through the real worker/action path"), F.Until(F.Far));
        F.Apply(F.Near,-25);
        TestTrue(TEXT("Repeated real losses accumulate before the next action"), F.Until(F.Near));
        F.Apply(F.Far,-5);
        TestTrue(TEXT("Equal threat preserves the existing distance/direction tie policy"), F.Until(F.Near));
        const uint64 Accepted = F.Activation->GetDamageThreatStats().AcceptedLosses;
        F.Apply(F.Friendly,-50); F.Apply(F.NPC,-20); F.Apply(F.Far,10);
        F.NPC->GetAbilitySystemComponent()->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(),900);
        F.NPC->GetAttributeSet()->InitHealth(850);
        F.Apply(F.Far,-50,UProject_JAttributeSet::GetMaxHealthAttribute());
        TestEqual(TEXT("Friendly/self/healing/direct/max changes add no damage threat"), F.Activation->GetDamageThreatStats().AcceptedLosses, Accepted);
        TestEqual(TEXT("Only registered valid hostile sources occupy storage"), F.Activation->GetDamageThreatStats().Sources, 2);
        F.Activation->DeactivateNPC();
        TestEqual(TEXT("Retirement releases all threat records"), F.Activation->GetDamageThreatStats().Sources, 0);
        TestEqual(TEXT("Retirement releases the observed execution storage"), F.NPC->GetAttributeSet()->GetHealthGEObservationCapacityForTest(), 0);
        F.Apply(F.Far,-5);
        TestEqual(TEXT("A retired lease observes no further Health losses"), F.Activation->GetDamageThreatStats().AcceptedLosses, Accepted);
    }
    {
        FWorld F;
        TestTrue(TEXT("Fresh threat epoch activates"), F.Activate(true));
        F.Apply(F.Near,-20); F.Apply(F.Far,-10);
        TestTrue(TEXT("Old current recipient is the initial threat leader"), F.Until(F.Near));
        EProject_JNPCUpdateBudgetTier Tier; double Interval = 0;
        F.Scheduler->GetAgentDecisionBudget(F.Scoring,Tier,Interval);
        FPlatformProcess::SleepNoStats(float(Interval+.01));
        F.Scheduler->Tick(0);
        TestTrue(TEXT("A real threat-filtered snapshot is awaiting delivery"), F.Scoring->bRequestedDamageThreat && F.Scoring->PendingToken.IsValid());
        const TStrongObjectPtr<UProject_JNPCResultCallbackFixture> Receiver(NewObject<UProject_JNPCResultCallbackFixture>());
        bool bPublishedOldEpoch = false;
        Receiver->OnResult = [&](AActor* Actor) { bPublishedOldEpoch |= Actor == F.Near; };
        F.Scoring->OnQueryCompleted.AddDynamic(Receiver.Get(),&UProject_JNPCResultCallbackFixture::Receive);
        auto* SourceASC = F.Near->GetAbilitySystemComponent(); SourceASC->InitAbilityActorInfo(F.Near,F.Near);
        TestTrue(TEXT("An old winner proof cannot deliver against a replacement source epoch"), F.Until(F.Far));
        TestFalse(TEXT("The old prepared source tuple was never published"), bPublishedOldEpoch);
        F.Scoring->OnQueryCompleted.RemoveDynamic(Receiver.Get(),&UProject_JNPCResultCallbackFixture::Receive);
        F.Apply(F.Near,-15);
        FProject_JNPCTargetThreat New;
        TestTrue(TEXT("A new epoch can contribute a fresh actual loss"), F.Activation->ReadDamageThreat(F.Near,New));
        TestEqual(TEXT("New recipient epoch never inherits the old cumulative threat"), New.Value,15.0);
        TestTrue(TEXT("A fresh source tuple reaches the actual action consumer"),F.Until(F.Near));
    }
    {
        FWorld F; F.Definition->MaxDamageThreatSources=1;
        TestTrue(TEXT("A deliberately bounded prototype lease starts"),F.Activate(true));
        F.Apply(F.Near,-10); F.Apply(F.Far,-100);
        TestTrue(TEXT("Source saturation explicitly degrades priority"),F.Activation->GetDamageThreatStats().bDegraded);
        TestEqual(TEXT("Count saturation is recorded once"),F.Activation->GetDamageThreatStats().SourceLimitFailures,uint64(1));
        F.Apply(F.Far,-5);F.Apply(F.Near,-5);
        TestEqual(TEXT("Further damage does not rescan/retrigger count saturation"),F.Activation->GetDamageThreatStats().SourceLimitFailures,uint64(1));
        TestTrue(TEXT("Declared degradation uses the original distance/direction path"),F.Until(F.Near));
        F.Activation->DeactivateNPC();F.Definition->MaxDamageThreatSources=8;
        TestTrue(TEXT("Only a new activation lease re-enables threat priority"),F.Activate(true));
        F.Apply(F.Far,-20);
        TestTrue(TEXT("A new bounded lease can consume greater fresh threat"),F.Until(F.Far));
        F.Activation->DeactivateNPC();F.Definition->MaxDamageThreatPerSource=5;
        TestTrue(TEXT("A low explicit value limit is a valid prototype configuration"),F.Activate(true));
        F.Apply(F.Far,-10);
        TestEqual(TEXT("Value saturation is explicit"),F.Activation->GetDamageThreatStats().Reason,EProject_JNPCThreatDegradation::ValueLimit);
        TestTrue(TEXT("Value saturation does not prevent authoritative damage"),F.NPC->GetAttributeSet()->GetHealth()<1000);
    }
    {
        FWorld F;
        TestTrue(TEXT("Observation fault scenario starts a real threat consumer"),F.Activate(true));
        int32 Nested=0;
        const auto Recursive=F.NPC->GetAttributeSet()->OnConfirmedHealthGELoss.AddLambda([&](const auto&)
        { if(++Nested<=UProject_JAttributeSet::MaxObservedHealthGEExecutions) F.Apply(F.Far,-1); });
        F.Apply(F.Far,-1);
        TestTrue(TEXT("The actual observation overflow reaches the current threat lease"),F.Activation->GetDamageThreatStats().bDegraded);
        TestEqual(TEXT("Incomplete native coverage cannot remain an exact threat policy"),F.Activation->GetDamageThreatStats().Reason,EProject_JNPCThreatDegradation::ObservationFailure);
        TestEqual(TEXT("Fault degradation releases the partial map"),F.Activation->GetDamageThreatStats().Sources,0);
        F.NPC->GetAttributeSet()->OnConfirmedHealthGELoss.Remove(Recursive);
        TestTrue(TEXT("Fault degradation preserves the existing gameplay selection path"),F.Until(F.Near));
    }
    {
        FWorld F;
        TestTrue(TEXT("Current recipient cleanup scenario starts"),F.Activate(true));
        F.Apply(F.Near,-10);
        auto* ASC=F.NPC->GetAbilitySystemComponent(); ASC->InitAbilityActorInfo(F.NPC,F.NPC);
        TestFalse(TEXT("Recipient replacement retires the entire owned threat/action/grant lease"),F.Activation->HasOwnedActivation());
        TestEqual(TEXT("Recipient replacement clears all threat sources"),F.Activation->GetDamageThreatStats().Sources,0);
        TestFalse(TEXT("Recipient replacement releases the scoring registration"),F.Scoring->IsBatchedNPCDecisionRegistered());
        TestEqual(TEXT("Recipient replacement releases the native observation storage"),F.NPC->GetAttributeSet()->GetHealthGEObservationCapacityForTest(),0);
        TestTrue(TEXT("A clean new recipient may admit a fresh opt-in activation"),F.Activate(true));
        F.Apply(F.Far,-20);
        TestTrue(TEXT("Fresh recipient selection has no old source bias"),F.Until(F.Far));
    }
    {
        FWorld F;
        TestTrue(TEXT("Callback retirement scenario starts"),F.Activate(true));
        bool bRetired=false;
        F.NPC->OnLifeQuery=[&]
        {
            bRetired=true; F.Activation->DeactivateNPC(); CollectGarbage(RF_NoFlags,true);
        };
        F.Apply(F.Far,-10);
        TestTrue(TEXT("The real eligibility life query reentered activation retirement and GC"),bRetired);
        TestFalse(TEXT("An in-flight damage observer cannot restore a retired activation"),F.Activation->HasOwnedActivation());
        TestEqual(TEXT("Retired observer publishes no late threat source"),F.Activation->GetDamageThreatStats().Sources,0);
        TestEqual(TEXT("Retirement during a GE releases storage at its matching Post"),F.NPC->GetAttributeSet()->GetHealthGEObservationCapacityForTest(),0);
    }
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCThreatSnapshotAgeTest, "ProjectJ.NPCGameplay.DamageThreatSnapshotAge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCThreatSnapshotAgeTest::RunTest(const FString&)
{
    ProjectJDamageThreatTests::FWorld F;
    if (!TestTrue(TEXT("The existing threat consumer activates"), F.Activate(true))) return false;
    F.Apply(F.Near, -10);
    const TStrongObjectPtr<UProject_JNPCResultCallbackFixture> Receiver(NewObject<UProject_JNPCResultCallbackFixture>());
    int32 Callbacks = 0;
    Receiver->OnResult = [&](AActor*) { ++Callbacks; };
    F.Scoring->OnQueryCompleted.AddDynamic(Receiver.Get(), &UProject_JNPCResultCallbackFixture::Receive);
    F.Scheduler->Tick(0);
    const double CoreStarted = FPlatformTime::Seconds();
    while (F.Service->GetPendingCount() && FPlatformTime::Seconds() - CoreStarted < 2)
    {
        F.Service->Tick(0);
        FPlatformProcess::SleepNoStats(.001f);
    }
    if (!TestEqual(TEXT("The actual threat worker completed"), F.Service->GetPendingCount(), 0)
        || !TestTrue(TEXT("Completion precedes the unchanged Core deadline"), FPlatformTime::Seconds() - CoreStarted < .25)
        || !TestEqual(TEXT("The real threat result waits in the NPC FIFO"), F.Scheduler->GetOutstandingCount(), 1)) return false;
    bool bGetterRan = false;
    F.Near->OnASCQuery = [&] { bGetterRan = true; FPlatformProcess::SleepNoStats(.55f); };
    F.Scheduler->Tick(0);
    TestTrue(TEXT("The final damage-threat source ASC getter actually delayed publication"), bGetterRan);
    TestEqual(TEXT("A result expiring inside component validation never publishes"), Callbacks, 0);
    TestNull(TEXT("Expired component validation clears selection"), F.Scoring->GetLastScoredTarget());
    TestNull(TEXT("The action consumer receives no expired intent"), F.Action->GetIntentTarget());
    TestEqual(TEXT("The delayed result has already released its reservation"), F.Scheduler->GetOutstandingCount(), 0);
    TestTrue(TEXT("A fresh threat result still reaches the existing action consumer"), F.Until(F.Near));
    TestEqual(TEXT("Only the fresh threat result publishes"), Callbacks, 1);
    return !HasAnyErrors();
}
#endif
