#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Project_JNPCCallbackFixture.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Combat/Project_JAttackDefinition.h"
#include "GameplayEffect.h"
#include "GameplayEffectAggregator.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

namespace ProjectJConfirmedHealthTests
{
struct FWorld
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    AProject_JNPCCallbackFixture* Source = nullptr;
    AProject_JNPCCallbackFixture* Target = nullptr;
    UProject_JAbilitySystemComponent* SourceASC = nullptr;
    UProject_JAbilitySystemComponent* TargetASC = nullptr;
    UProject_JAttributeSet* Stats = nullptr;
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Source = World->SpawnActor<AProject_JNPCCallbackFixture>(P); Target = World->SpawnActor<AProject_JNPCCallbackFixture>(P);
        World->InitializeActorsForPlay(FURL());
        World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
        SourceASC = CastChecked<UProject_JAbilitySystemComponent>(Source->GetAbilitySystemComponent());
        TargetASC = CastChecked<UProject_JAbilitySystemComponent>(Target->GetAbilitySystemComponent());
        SourceASC->InitAbilityActorInfo(Source, Source); TargetASC->InitAbilityActorInfo(Target, Target);
        Stats = Target->GetAttributeSet();
        Stats->InitMaxHealth(100); Stats->InitHealth(100);
    }
    ~FWorld()
    {
        Stats->OnConfirmedHealthGELoss.Clear(); Stats->OnHealthGEObservationFailed.Clear();
        Stats->ReleaseUnusedHealthGEObservationStorage();
        World->EndPlay(EEndPlayReason::LevelTransition); World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    }
    void Apply(const FGameplayAttribute& Attribute, float Amount)
    {
        const TStrongObjectPtr<UGameplayEffect> Effect(NewObject<UGameplayEffect>());
        Effect->DurationPolicy = EGameplayEffectDurationType::Instant;
        auto& Mod = Effect->Modifiers.AddDefaulted_GetRef(); Mod.Attribute = Attribute;
        Mod.ModifierOp = EGameplayModOp::Additive; Mod.ModifierMagnitude = FScalableFloat(Amount);
        auto Context = SourceASC->MakeEffectContext(); Context.AddInstigator(Source, Source);
        FGameplayEffectSpec Spec(Effect.Get(), Context, 1);
        SourceASC->ApplyGameplayEffectSpecToTarget(Spec, TargetASC);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJConfirmedHealthLossTest, "ProjectJ.Foundation.GAS.ConfirmedHealthGELoss",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJConfirmedHealthLossTest::RunTest(const FString&)
{
    using namespace ProjectJConfirmedHealthTests;
    {
        FWorld F;
        TestTrue(TEXT("Fresh native set supports an observation lease"), F.Stats->CanBeginHealthGEObservation());
        F.Apply(F.Stats->GetHealthAttribute(), -5);
        TestEqual(TEXT("Default OFF still applies real GE Health"), F.Stats->GetHealth(), 95.f);
        TestEqual(TEXT("Default OFF allocates no execution array"), F.Stats->GetHealthGEObservationCapacityForTest(), 0);
        TArray<FProject_JConfirmedHealthGELoss> Losses;
        const auto Handle = F.Stats->OnConfirmedHealthGELoss.AddLambda([&](const auto& Loss)
        {
            Losses.Add(Loss);
            TestTrue(TEXT("Native loss proves the original target/source ASC owner/avatar"),
                Loss.Proof.TargetASC.Get() == F.TargetASC && Loss.Proof.TargetOwner.Get() == F.Target
                && Loss.Proof.TargetAvatar.Get() == F.Target && Loss.Proof.SourceASC.Get() == F.SourceASC
                && Loss.Proof.SourceOwner.Get() == F.Source && Loss.Proof.SourceAvatar.Get() == F.Source
                && Loss.Proof.Instigator.Get() == F.Source && Loss.Proof.EffectCauser.Get() == F.Source
                && Loss.Proof.TargetEpoch == F.TargetASC->GetActorInfoRevision()
                && Loss.Proof.SourceEpoch == F.SourceASC->GetActorInfoRevision() && Loss.Proof.EffectDefinition.IsValid());
            TestFalse(TEXT("A new observer lease cannot begin in the middle of GE execution"), F.Stats->CanBeginHealthGEObservation());
        });
        F.Apply(F.Stats->GetHealthAttribute(), -10);
        TestEqual(TEXT("One real GE creates exactly one confirmed loss"), Losses.Num(), 1);
        TestEqual(TEXT("Clamped resource old value is exact"), Losses[0].OldValue, 95.f);
        TestEqual(TEXT("Clamped resource new value is exact"), Losses[0].NewValue, 85.f);
        F.Apply(F.Stats->GetHealthAttribute(), 5); F.Apply(F.Stats->GetHealthAttribute(), 0);
        F.TargetASC->SetNumericAttributeBase(F.Stats->GetHealthAttribute(), 70); F.Stats->InitHealth(65);
        F.Apply(F.Stats->GetMaxHealthAttribute(), -50);
        TestEqual(TEXT("Healing, no-op, direct setter/init and maximum reduction create no confirmed Health GE loss"), Losses.Num(), 1);
        F.Apply(F.Stats->GetHealthAttribute(), -1000);
        TestEqual(TEXT("Overkill creates one actual clamped loss"), Losses.Num(), 2);
        TestEqual(TEXT("Overkill credits the actual remaining Health only"), Losses.Last().OldValue - Losses.Last().NewValue, 50.f);
        TestEqual(TEXT("Completed GE scopes are released"), F.Stats->GetActiveHealthGEExecutionsForTest(), 0);
        TestEqual(TEXT("Ordinary execution has no observation failure"), F.Stats->GetHealthGEObservationFailures(), uint64(0));
        F.Stats->OnConfirmedHealthGELoss.Remove(Handle); F.Stats->ReleaseUnusedHealthGEObservationStorage();
        TestEqual(TEXT("Final observer retirement returns the reserved execution storage"), F.Stats->GetHealthGEObservationCapacityForTest(), 0);
    }
    {
        FWorld F;
        int32 Confirmed = 0; double ActualLoss = 0;
        const auto LossHandle = F.Stats->OnConfirmedHealthGELoss.AddLambda([&](const auto& Loss)
        { ++Confirmed; ActualLoss += double(Loss.OldValue) - Loss.NewValue; });
        bool bDirect = false, bNestedGE = false;
        const auto Legacy = F.TargetASC->GetGameplayAttributeValueChangeDelegate(F.Stats->GetHealthAttribute()).AddLambda([&](const FOnAttributeChangeData& Data)
        {
            if (!Data.GEModData) return;
            if (!bDirect) { bDirect = true; F.TargetASC->SetNumericAttributeBase(F.Stats->GetHealthAttribute(), F.Stats->GetHealth() - 7); }
            else if (!bNestedGE) { bNestedGE = true; F.Apply(F.Stats->GetHealthAttribute(), -3); }
        });
        F.Apply(F.Stats->GetHealthAttribute(), -10);
        TestTrue(TEXT("The real legacy Health callback exercised both nested direct setter and nested GE"), bDirect && bNestedGE);
        TestEqual(TEXT("Only the two actual GEs produce confirmed loss"), Confirmed, 2);
        TestEqual(TEXT("Nested direct Health change is not misattributed to inherited GEModData"), ActualLoss, 13.0);
        TestEqual(TEXT("All actual resource changes still apply"), F.Stats->GetHealth(), 80.f);
        TestEqual(TEXT("Nested executions pop exactly their own scopes"), F.Stats->GetActiveHealthGEExecutionsForTest(), 0);
        TestEqual(TEXT("Nested ordinary execution remains fully observed"), F.Stats->GetHealthGEObservationFailures(), uint64(0));
        F.TargetASC->GetGameplayAttributeValueChangeDelegate(F.Stats->GetHealthAttribute()).Remove(Legacy);
        F.Stats->OnConfirmedHealthGELoss.Remove(LossHandle);
    }
    {
        FWorld F;
        F.Stats->InitMaxHealth(1000); F.Stats->InitHealth(1000);
        int32 Confirmed = 0, Failed = 0, PeakDepth = 0;
        const auto Fault = F.Stats->OnHealthGEObservationFailed.AddLambda([&](const auto& Proof, auto Reason)
        {
            ++Failed;
            TestEqual(TEXT("Overflow is reported explicitly, after the suppressed scope unwinds"), Reason, EProject_JHealthGEObservationFailure::ScopeLimit);
            TestTrue(TEXT("Fault retains its original recipient proof"), Proof.TargetASC.Get() == F.TargetASC && Proof.TargetEpoch == F.TargetASC->GetActorInfoRevision());
            TestEqual(TEXT("Fault callback sees no suppressed frame still owned"), F.Stats->GetSuppressedHealthGEDepthForTest(), uint32(0));
        });
        const auto Loss = F.Stats->OnConfirmedHealthGELoss.AddLambda([&](const auto&)
        {
            ++Confirmed; PeakDepth = FMath::Max(PeakDepth, F.Stats->GetActiveHealthGEExecutionsForTest());
            const bool bFirst = Confirmed == 1;
            if (Confirmed <= F.Stats->MaxObservedHealthGEExecutions) F.Apply(F.Stats->GetHealthAttribute(), -1);
            if (bFirst) CollectGarbage(RF_NoFlags, true);
        });
        F.Apply(F.Stats->GetHealthAttribute(), -1);
        TestEqual(TEXT("Tracking memory is bounded by the explicit execution depth"), PeakDepth, F.Stats->MaxObservedHealthGEExecutions);
        TestEqual(TEXT("An overflow does not fabricate a confirmed loss"), Confirmed, F.Stats->MaxObservedHealthGEExecutions);
        TestEqual(TEXT("Observation pressure never rejects actual gameplay damage"), F.Stats->GetHealth(), 991.f);
        TestEqual(TEXT("Overflow is surfaced once per suppressed subtree"), Failed, 1);
        TestEqual(TEXT("All tracked and suppressed scopes are released"), F.Stats->GetActiveHealthGEExecutionsForTest(), 0);
        TestEqual(TEXT("Suppressed depth returns to zero"), F.Stats->GetSuppressedHealthGEDepthForTest(), uint32(0));
        F.Stats->OnConfirmedHealthGELoss.Remove(Loss); F.Stats->OnHealthGEObservationFailed.Remove(Fault);
    }
    {
        FWorld F;
        int32 Confirmed = 0, NativeCalls = 0;
        const auto Loss = F.Stats->OnConfirmedHealthGELoss.AddLambda([&](const auto& Data)
        {
            ++Confirmed; ++NativeCalls;
            const float Old = Data.OldValue, New = Data.NewValue;
            F.TargetASC->SetNumericAttributeBase(F.Stats->GetHealthAttribute(), F.Stats->GetHealth() - 2);
            CollectGarbage(RF_NoFlags, true);
            TestEqual(TEXT("Native observers retain immutable old value across callback GC"), Data.OldValue, Old);
            TestEqual(TEXT("Native observers retain immutable new value across direct nested writes"), Data.NewValue, New);
        });
        F.Apply(F.Stats->GetHealthAttribute(), -10);
        TestEqual(TEXT("Consuming before native callbacks prevents recursive direct-write observations"), NativeCalls, 1);
        TestEqual(TEXT("GC and nested direct Health modification do not create another GE event"), Confirmed, 1);
        TestEqual(TEXT("Real direct and GE Health changes remain visible"), F.Stats->GetHealth(), 88.f);
        TestEqual(TEXT("Native callback GC leaves no execution pin owned"), F.Stats->GetActiveHealthGEExecutionsForTest(), 0);
        F.Stats->OnConfirmedHealthGELoss.Remove(Loss);
    }
    {
        FWorld F;
        // Deliberately no caller TStrongObjectPtr for this transient instant definition.
        auto* Dynamic = NewObject<UGameplayEffect>(); Dynamic->DurationPolicy = EGameplayEffectDurationType::Instant;
        auto& Mod = Dynamic->Modifiers.AddDefaulted_GetRef(); Mod.Attribute = F.Stats->GetHealthAttribute();
        Mod.ModifierOp = EGameplayModOp::Additive; Mod.ModifierMagnitude = FScalableFloat(-4);
        const TWeakObjectPtr<UGameplayEffect> WeakDynamic(Dynamic);
        int32 DynamicEvents = 0;
        const auto DynamicObserver = F.Stats->OnConfirmedHealthGELoss.AddLambda([&](const auto& Loss)
        {
            ++DynamicEvents;
            TestTrue(TEXT("This case uses a transient instance rather than a class default effect"),
                Loss.Proof.EffectDefinition.IsValid() && WeakDynamic.IsValid()
                && Loss.Proof.EffectDefinition.Get() == WeakDynamic.Get() && !Loss.Proof.EffectDefinition->HasAnyFlags(RF_ClassDefaultObject));
            CollectGarbage(RF_NoFlags,true);
            TestTrue(TEXT("The observation scope pins the transient instant GE definition through native callback GC"),
                WeakDynamic.IsValid() && Loss.Proof.EffectDefinition.Get() == WeakDynamic.Get());
        });
        auto Context = F.SourceASC->MakeEffectContext(); Context.AddInstigator(F.Source,F.Source);
        FGameplayEffectSpec Spec(Dynamic,Context,1);
        F.SourceASC->ApplyGameplayEffectSpecToTarget(Spec,F.TargetASC);
        TestEqual(TEXT("A dynamic instant definition produces one real Health loss"),DynamicEvents,1);
        F.Stats->OnConfirmedHealthGELoss.Remove(DynamicObserver);

        const TStrongObjectPtr<const UProject_JAttackDefinition> Existing(LoadObject<UProject_JAttackDefinition>(nullptr,
            TEXT("/Game/DataAssetSets/AttackDefinition/Spellblade/DA_Attack_Spellblade_LMB1.DA_Attack_Spellblade_LMB1")));
        if(!TestTrue(TEXT("Existing authored melee data supplies its production GE class"),Existing && Existing->DamageEffect)) return false;
        const auto* ClassDefault = Existing->DamageEffect->GetDefaultObject<UGameplayEffect>();
        int32 CDOEvents = 0;
        const auto CDOObserver = F.Stats->OnConfirmedHealthGELoss.AddLambda([&](const auto& Loss)
        {
            ++CDOEvents;
            TestTrue(TEXT("The production class-default effect is distinguished from the dynamic case"),
                Loss.Proof.EffectDefinition.Get()==ClassDefault && ClassDefault->HasAnyFlags(RF_ClassDefaultObject));
            CollectGarbage(RF_NoFlags,true);
            TestTrue(TEXT("Class-default context remains readable across the same native callback GC"),Loss.Proof.EffectDefinition.Get()==ClassDefault);
        });
        FGameplayEffectSpec CDOSpec(ClassDefault,Context,1);
        F.SourceASC->ApplyGameplayEffectSpecToTarget(CDOSpec,F.TargetASC);
        TestEqual(TEXT("Existing authored damage GE produces one confirmed class-default loss"),CDOEvents,1);
        F.Stats->OnConfirmedHealthGELoss.Remove(CDOObserver);
    }
    {
        FWorld F;
        int32 Confirmed = 0, Failed = 0;
        const auto Loss = F.Stats->OnConfirmedHealthGELoss.AddLambda([&](const auto&) { ++Confirmed; });
        const auto Fault = F.Stats->OnHealthGEObservationFailed.AddLambda([&](const auto& Proof, auto Reason)
        {
            ++Failed;
            TestEqual(TEXT("A deferred execution is reported rather than attributed speculatively"), Reason, EProject_JHealthGEObservationFailure::UnobservedExecution);
            TestTrue(TEXT("Unobserved execution retains its recipient proof"), Proof.TargetASC.Get() == F.TargetASC);
            TestEqual(TEXT("The unobserved frame is popped before fault callbacks"), F.Stats->GetActiveHealthGEExecutionsForTest(), 0);
            TestFalse(TEXT("A fresh observer lease cannot hide an aggregate batch that is still pending"), F.Stats->CanBeginHealthGEObservation());
        });
        const TStrongObjectPtr<UGameplayEffect> Aggregated(NewObject<UGameplayEffect>());
        Aggregated->DurationPolicy = EGameplayEffectDurationType::Infinite;
        auto& Mod = Aggregated->Modifiers.AddDefaulted_GetRef(); Mod.Attribute = F.Stats->GetHealthAttribute();
        Mod.ModifierOp = EGameplayModOp::Additive; Mod.ModifierMagnitude = FScalableFloat(0);
        const auto Active = F.TargetASC->ApplyGameplayEffectToSelf(Aggregated.Get(), 1, F.TargetASC->MakeEffectContext());
        if (!TestTrue(TEXT("The real zero-value continuous GE creates the engine aggregator"), Active.IsValid())) return false;
        // Intentional native fault injection. UE diagnoses its own unconsumed ModData in this unsupported batching path.
        AddExpectedError(TEXT("CurrentModcallbackData was not consumed For attribute Health"), EAutomationExpectedErrorFlags::Contains, 1);
        {
            FScopedAggregatorOnDirtyBatch Batch;
            F.Apply(F.Stats->GetHealthAttribute(), -10);
        }
        TestEqual(TEXT("A deferred aggregate never fabricates a confirmed GE loss"), Confirmed, 0);
        TestEqual(TEXT("Deferred execution is surfaced once"), Failed, 1);
        TestEqual(TEXT("Deferred execution retains no observation scope"), F.Stats->GetActiveHealthGEExecutionsForTest(), 0);
        TestEqual(TEXT("The native consumer can observe the incomplete-coverage counter"), F.Stats->GetHealthGEObservationFailures(), uint64(1));
        F.TargetASC->RemoveActiveGameplayEffect(Active);
        F.Stats->OnConfirmedHealthGELoss.Remove(Loss); F.Stats->OnHealthGEObservationFailed.Remove(Fault);
    }
    return !HasAnyErrors();
}
#endif
