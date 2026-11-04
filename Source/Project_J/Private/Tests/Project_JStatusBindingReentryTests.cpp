#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "UI/Project_JStatusEffects.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJStatusBindingReentryTest, "ProjectJ.RuntimeOwnership.StatusBindingReentry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJStatusBindingReentryTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Owner = World->SpawnActor<AActor>();
    auto MakeASC = [&] { auto* ASC = NewObject<UAbilitySystemComponent>(Owner); ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Owner, Owner); return ASC; };
    auto* Previous = MakeASC(); auto* Outer = MakeASC(); auto* Nested = MakeASC();
    TStrongObjectPtr<UProject_JStatusEffectModel> Model(NewObject<UProject_JStatusEffectModel>());
    Model->Bind(Previous);
    bool bRebound = false;
    const auto Delegate = Model->OnChanged.AddLambda([&] {
        if (!bRebound) { bRebound = true; Model->Bind(Nested); }
    });
    Model->Bind(Outer);
    Model->OnChanged.Remove(Delegate);
    TestTrue(TEXT("Real status publication reenters binding"), bRebound);
    TestTrue(TEXT("Latest nested binding owns the source"), Model->ASC.Get() == Nested);
    TestFalse(TEXT("Older bind cannot leave an extra subscription"), Outer->OnActiveGameplayEffectAddedDelegateToSelf.IsBoundToObject(Model.Get()));
    Model->Unbind();
    for (auto* ASC : {Previous, Outer, Nested})
    { TestFalse(TEXT("All acquired effect subscriptions retire"), ASC->OnActiveGameplayEffectAddedDelegateToSelf.IsBoundToObject(Model.Get())); }
    TStrongObjectPtr<UGameplayEffect> Observed(NewObject<UGameplayEffect>());
    Observed->DurationPolicy = EGameplayEffectDurationType::Infinite;
    Observed->FindOrAddComponent<UProject_JStatusEffectUIData>().DisplayName = FText::FromString(TEXT("Observed status"));
    auto ObservedHandle = Previous->ApplyGameplayEffectToSelf(Observed.Get(), 1, Previous->MakeEffectContext());
    Model->Bind(Previous);
    bool bEffectRebound = false;
    const auto EffectDelegate = Model->OnChanged.AddLambda([&] {
        if (!bEffectRebound) { bEffectRebound = true; Model->Bind(Nested); }
    });
    ObservedHandle = Previous->SetActiveGameplayEffectInhibit(MoveTemp(ObservedHandle), true, true);
    Model->OnChanged.Remove(EffectDelegate);
    TestTrue(TEXT("Native per-effect publication can replace the binding"), bEffectRebound && Model->ASC.Get() == Nested);
    TestTrue(TEXT("Retired per-effect subscriptions are detached before nested publication"), Model->ObservedEffects.IsEmpty());
    if (auto* OldDelegate = Previous->OnGameplayEffectInhibitionChangedDelegate(ObservedHandle))
        TestFalse(TEXT("Old in-progress effect delegate does not retain its model subscription"), OldDelegate->IsBoundToObject(Model.Get()));
    Model->Unbind();
    TStrongObjectPtr<UProject_JStatusEffectModel> GCModel(NewObject<UProject_JStatusEffectModel>());
    TWeakObjectPtr<UProject_JStatusEffectModel> WeakModel(GCModel.Get());
    GCModel->Bind(Previous);
    // The fixture deliberately collects during normal GAS application. Keep its
    // transient definition and source alive, as production authored data is.
    const TStrongObjectPtr<UAbilitySystemComponent> KeepPrevious(Previous);
    TStrongObjectPtr<UGameplayEffect> Effect(NewObject<UGameplayEffect>());
    Effect->DurationPolicy = EGameplayEffectDurationType::Infinite;
    Effect->FindOrAddComponent<UProject_JStatusEffectUIData>().DisplayName = FText::FromString(TEXT("GC status"));
    bool bAliveDuringCallback = false;
    GCModel->OnChanged.AddLambda([&] { GCModel.Reset(); CollectGarbage(RF_NoFlags); bAliveDuringCallback = WeakModel.IsValid(); });
    Previous->ApplyGameplayEffectToSelf(Effect.Get(), 1, Previous->MakeEffectContext());
    TestTrue(TEXT("Status model survives collection during its own publication"), bAliveDuringCallback);
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("Publication does not retain the model after completion"), WeakModel.IsValid());
    return true;
}
#endif
