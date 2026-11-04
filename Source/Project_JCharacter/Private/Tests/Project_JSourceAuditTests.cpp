#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "NativeGameplayTags.h"
#include "Tests/Project_JPlayerMaturityFixtures.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Tests/Project_JInternalTestReceiver.h"
#include "Inventory/Project_JConsumableDefinition.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(AuditAttackA, "ProjectJ.Tests.SourceAudit.AttackA");
UE_DEFINE_GAMEPLAY_TAG_STATIC(AuditAttackB, "ProjectJ.Tests.SourceAudit.AttackB");

namespace
{
struct FAuditWorld
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    FAuditWorld() { GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World); }
    ~FAuditWorld() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
};
constexpr auto AuditFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAuditEquipmentNetworkTest, "ProjectJ.SourceAudit.EquipmentNetwork", AuditFlags)
bool FProjectJAuditEquipmentNetworkTest::RunTest(const FString&)
{
    FAuditWorld Scope;
    auto* Owner = Scope.World->SpawnActor<AActor>();
    auto* Inventory = NewObject<UProject_JInventoryComponent>(Owner); Inventory->RegisterComponent();
    auto* Manager = NewObject<UProject_JEquipmentManagerComponent>(Owner); Manager->RegisterComponent();
    auto* Definition = NewObject<UProject_JEquipmentItemDefinition>();
    Definition->EquipmentSlot = EProject_JEquipmentSlot::Weapon;
    const auto Item = Inventory->AddItemDefinition(Definition);
    // Malformed new requests still consume the same network admission budget.
    for (int32 Index = 0; Index < 20; ++Index)
        Manager->ServerRequestEquipmentChange_Implementation(FGuid::NewGuid(), FGuid::NewGuid(), EProject_JEquipmentSlot::Weapon, false);
    Manager->ServerRequestEquipItemInstanceById_Implementation(Item.InstanceId);
    TestNull(TEXT("Legacy equip cannot bypass the new RPC budget"), Manager->GetEquippedItemInSlot(EProject_JEquipmentSlot::Weapon));
    // Trusted server mutations remain independent of network throttling.
    TestTrue(TEXT("Internal equip remains available"), Manager->TryEquipItemInstanceById(Item.InstanceId).bSucceeded);
    Manager->ServerRequestUnequipSlot_Implementation(EProject_JEquipmentSlot::Weapon);
    TestNotNull(TEXT("Slot-only legacy request cannot remove an unidentified replacement"), Manager->GetEquippedItemInSlot(EProject_JEquipmentSlot::Weapon));
    Manager->RequestWindowStart = -1;
    const auto Replacement = Inventory->AddItemDefinition(Definition);
    TestTrue(TEXT("Server replaces the occupied slot"), Manager->TryEquipItemInstanceById(Replacement.InstanceId).bSucceeded);
    TestFalse(TEXT("Correlated stale removal is rejected"), Manager->ExecuteEquipmentRequest(Item.InstanceId, EProject_JEquipmentSlot::Weapon, true).bSucceeded);
    FProject_JItemInstanceData Current; Manager->GetEquippedItemInstance(EProject_JEquipmentSlot::Weapon, Current);
    TestEqual(TEXT("Stale removal preserves the replacement identity"), Current.InstanceId, Replacement.InstanceId);
    TestTrue(TEXT("Current correlated removal succeeds"), Manager->ExecuteEquipmentRequest(Replacement.InstanceId, EProject_JEquipmentSlot::Weapon, true).bSucceeded);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAuditSSRExecutionTest, "ProjectJ.SourceAudit.SSRExecution", AuditFlags)
bool FProjectJAuditSSRExecutionTest::RunTest(const FString&)
{
    FAuditWorld Scope;
    auto* Owner = Scope.World->SpawnActor<AActor>();
    auto* Hit = NewObject<UProject_JCombatHitValidationComponent>(Owner); Hit->RegisterComponent();
    Scope.World->Tick(LEVELTICK_All, .1f);
    Hit->BeginAttackNode(AuditAttackA, nullptr, 17); Hit->SetHitWindowOpen(true);
    Hit->RecordAuthoritativeTrace(FVector(10, 0, 0), FVector(20, 0, 0));
    const float FirstTime = Scope.World->GetTimeSeconds();
    FVector Start, End; bool bOpen = false;
    TestTrue(TEXT("First A has a real server trace"), Hit->FindAuthoritativeTraceAtTime(FirstTime, 17, AuditAttackA, Start, End, bOpen));
    Scope.World->Tick(LEVELTICK_All, .01f);
    Hit->BeginAttackNode(AuditAttackB, nullptr, 17);
    Scope.World->Tick(LEVELTICK_All, .01f);
    Hit->BeginAttackNode(AuditAttackA, nullptr, 17); Hit->SetHitWindowOpen(true);
    Hit->RecordAuthoritativeTrace(FVector(100, 0, 0), FVector(120, 0, 0));
    TestFalse(TEXT("Second A rejects first A timestamp even inside history past tolerance"),
        Hit->FindAuthoritativeTraceAtTime(FirstTime, 17, AuditAttackA, Start, End, bOpen));
    TestTrue(TEXT("Second A accepts its current trace"),
        Hit->FindAuthoritativeTraceAtTime(Scope.World->GetTimeSeconds(), 17, AuditAttackA, Start, End, bOpen));
    TestEqual(TEXT("Current geometry belongs to second A"), Start.X, 100.);
    TestTrue(TEXT("Current hit window remains available"), bOpen);
    Hit->EndAttack();
    TestFalse(TEXT("Ended execution cannot supply damage geometry"),
        Hit->FindAuthoritativeTraceAtTime(Scope.World->GetTimeSeconds(), 17, AuditAttackA, Start, End, bOpen));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAuditBagUnequipTest, "ProjectJ.SourceAudit.BagUnequip", AuditFlags)
bool FProjectJAuditBagUnequipTest::RunTest(const FString&)
{
    for (auto Operation : {EProject_JBagOperation::Split, EProject_JBagOperation::Merge, EProject_JBagOperation::Swap})
    {
        FAuditWorld Scope;
        auto* Owner = Scope.World->SpawnActor<AActor>();
        auto* Inventory = NewObject<UProject_JInventoryComponent>(Owner); Inventory->RegisterComponent();
        auto* Manager = NewObject<UProject_JEquipmentManagerComponent>(Owner); Manager->RegisterComponent();
        auto* Weapon = NewObject<UProject_JEquipmentItemDefinition>(); Weapon->EquipmentSlot = EProject_JEquipmentSlot::Weapon;
        const auto Equipped = Inventory->AddItemDefinition(Weapon);
        TestTrue(TEXT("Inventory-backed weapon equips"), Manager->TryEquipItemInstanceById(Equipped.InstanceId).bSucceeded);
        auto* Stack = NewObject<UProject_JConsumableDefinition>(); Stack->MaxStackCount = 10;
        const auto Source = Inventory->AddItemDefinition(Stack, 4);
        const auto Target = Inventory->AddItemDefinition(Stack, 2);
        auto* Receiver = NewObject<UProject_JInternalTestReceiver>(); Receiver->bMutate = false;
        int32 Callbacks = 0;
        Receiver->OnInventoryChanged = [&] {
            ++Callbacks;
            Manager->UnequipSlot(EProject_JEquipmentSlot::Weapon);
        };
        Inventory->OnItemChanged.AddDynamic(Receiver, &UProject_JInternalTestReceiver::OnChanged);
        FProject_JBagRequest Request; Request.Operation = Operation; Request.Source = Source.InstanceId;
        Request.ExpectedSourceCount = Source.StackCount; Request.ExpectedSourceOrder = Source.BagOrder;
        Request.Count = Operation == EProject_JBagOperation::Swap ? 0 : 1;
        if (Operation != EProject_JBagOperation::Split)
        {
            Request.Target = Target.InstanceId;
            Request.ExpectedTargetCount = Target.StackCount; Request.ExpectedTargetOrder = Target.BagOrder;
        }
        TestEqual(TEXT("Bag transaction succeeds"), Inventory->TryBagChange(Request), EProject_JBagResult::Success);
        TestTrue(TEXT("Real synchronous bag notification ran"), Callbacks > 0);
        TestNotNull(TEXT("Failed inventory unlock preserves equipment slot"), Manager->GetEquippedItemInSlot(EProject_JEquipmentSlot::Weapon));
        FProject_JItemInstanceData Current; Inventory->FindItemInstance(Equipped.InstanceId, Current);
        TestTrue(TEXT("Preserved slot agrees with inventory equipped lock"), Current.bIsLocked && Current.bIsEquipped);
        Inventory->OnItemChanged.RemoveAll(Receiver);
        Manager->UnequipSlot(EProject_JEquipmentSlot::Weapon);
        TestNull(TEXT("Unequip succeeds after bag guard exits"), Manager->GetEquippedItemInSlot(EProject_JEquipmentSlot::Weapon));
        TestFalse(TEXT("Successful unequip releases inventory lock"), Inventory->IsItemInstanceLocked(Equipped.InstanceId));
        // Definition-only equipment has no inventory lock to release.
        auto* PrototypeOwner = Scope.World->SpawnActor<AActor>();
        auto* Prototype = NewObject<UProject_JEquipmentManagerComponent>(PrototypeOwner); Prototype->RegisterComponent();
        Prototype->EquipItem(Weapon); Prototype->UnequipSlot(EProject_JEquipmentSlot::Weapon);
        TestNull(TEXT("Definition-only prototype still unequips without inventory"), Prototype->GetEquippedItemInSlot(EProject_JEquipmentSlot::Weapon));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAuditConsumableContinuationTest, "ProjectJ.SourceAudit.ConsumableContinuation", AuditFlags)
bool FProjectJAuditConsumableContinuationTest::RunTest(const FString&)
{
    for (int32 Count : {1, 2}) for (int32 Mode = 0; Mode < 10; ++Mode)
    {
        FAuditWorld Scope;
        auto* Owner = Scope.World->SpawnActor<AProject_JPlayerMaturityASCFixture>();
        const auto MakeASC = [Owner, Mode] {
            UAbilitySystemComponent* ASC = Mode == 9 ? NewObject<UAbilitySystemComponent>(Owner) : NewObject<UProject_JAbilitySystemComponent>(Owner);
            ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Owner, Owner);
            ASC->AddAttributeSetSubobject(NewObject<UProject_JAttributeSet>(Owner));
            ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(), 100.f);
            ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 40.f);
            ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxManaAttribute(), 100.f);
            return ASC;
        };
        auto* ASC = MakeASC(); auto* Replacement = MakeASC(); Owner->CurrentASC = ASC;
        const TStrongObjectPtr<UAbilitySystemComponent> KeepReplacement(Replacement);
        const auto* ProjectASC = Cast<UProject_JAbilitySystemComponent>(ASC);
        const uint64 OriginalRevision = ProjectASC ? ProjectASC->GetActorInfoRevision() : 0;
        auto* OtherActor = Scope.World->SpawnActor<AActor>();
        if (Mode == 4) ASC->InitAbilityActorInfo(Owner, OtherActor);
        if (Mode == 5) ASC->InitAbilityActorInfo(OtherActor, Owner);
        auto* Inventory = NewObject<UProject_JInventoryComponent>(Owner); Inventory->RegisterComponent();
        auto* Definition = NewObject<UProject_JConsumableDefinition>(); Definition->MaxStackCount = 10; Definition->RestoreHealth = 20.f;
        const auto Item = Inventory->AddItemDefinition(Definition, Count);
        auto* Receiver = NewObject<UProject_JInternalTestReceiver>(); Receiver->bMutate = false;
        const TStrongObjectPtr<UProject_JInternalTestReceiver> KeepReceiver(Receiver);
        bool bCallbackRan = false, bRefreshedMetadata = false;
        const auto Callback = [&] {
            bCallbackRan = true;
            if (Mode == 0) Inventory->DestroyComponent();
            else if (Mode == 1) Owner->Destroy();
            else if (Mode == 2) Owner->CurrentASC = Replacement;
            else if (Mode == 3) ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 0.f);
            else if (Mode == 4 || Mode == 5) OtherActor->Destroy();
            else if (Mode == 6 || Mode == 9) ASC->InitAbilityActorInfo(Owner, Owner);
            else if (Mode == 7) { ASC->ClearActorInfo(); ASC->InitAbilityActorInfo(Owner, Owner); }
            else if (Mode == 8) bRefreshedMetadata = CastChecked<UProject_JAbilitySystemComponent>(ASC)->RefreshActorInfoForCurrentActor(Owner);
            if (Mode == 6 || Mode == 7) CollectGarbage(RF_NoFlags);
        };
        Receiver->OnInventoryChanged = Callback; Receiver->OnInventoryRemoved = Callback;
        Inventory->OnItemChanged.AddDynamic(Receiver, &UProject_JInternalTestReceiver::OnChanged);
        Inventory->OnItemRemoved.AddDynamic(Receiver, &UProject_JInternalTestReceiver::OnRemoved);
        int32 Applied = 0;
        ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda([&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle) { ++Applied; });
        const bool bShouldSucceed = Mode == 8 || Mode == 9;
        TestEqual(TEXT("Only same-epoch metadata refresh and legacy generic tuple continuation succeed"), Inventory->TryUseItem(Item.InstanceId),
            bShouldSucceed ? EProject_JItemUseResult::Success : EProject_JItemUseResult::Unavailable);
        TestTrue(TEXT("Partial/final consumption notification ran"), bCallbackRan);
        TestEqual(TEXT("Retired recipients get no effect; current recipients get exactly one"), Applied, bShouldSucceed ? 1 : 0);
        TestEqual(TEXT("Only an admitted current recipient is restored"), ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()),
            bShouldSucceed ? 60.f : Mode == 3 ? 0.f : 40.f);
        TestEqual(TEXT("Replacement ASC remains unchanged"), Replacement->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()), 40.f);
        if (bShouldSucceed) TestTrue(TEXT("Current recipient retains its normal recovery cooldown"), Inventory->GetUseCooldownRemaining() > 0);
        else TestEqual(TEXT("Aborted continuation creates no recovery cooldown"), Inventory->GetUseCooldownRemaining(), 0.f);
        if (Mode == 6 || Mode == 7)
        {
            TestTrue(TEXT("Same-pair Init/Clear+Init advances the existing project epoch"), ProjectASC && ProjectASC->GetActorInfoRevision() > OriginalRevision);
            TestTrue(TEXT("The failing epoch case deliberately preserves all three pointers"), Owner->CurrentASC == ASC && ASC->GetOwnerActor() == Owner && ASC->GetAvatarActor() == Owner);
        }
        if (Mode == 8)
        {
            TestTrue(TEXT("Actual metadata refresh succeeds"), bRefreshedMetadata);
            TestEqual(TEXT("Metadata refresh preserves the current project epoch"), ProjectASC->GetActorInfoRevision(), OriginalRevision);
        }
        if (Mode == 9) TestNull(TEXT("Generic ASC compatibility does not invent a project epoch requirement"), ProjectASC);
        FProject_JItemInstanceData Remaining;
        TestEqual(TEXT("Published consumption is not rolled back or duplicated"), Inventory->FindItemInstance(Item.InstanceId, Remaining), Count == 2);
        if (Count == 2) TestEqual(TEXT("Partial consumption retains exactly one item"), Remaining.StackCount, 1);
    }
    for (int32 Count : {1, 2}) for (bool bClear : {false, true})
    {
        FAuditWorld Scope;
        auto* Owner = Scope.World->SpawnActor<AProject_JPlayerMaturityASCFixture>();
        auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Owner); ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Owner, Owner);
        Owner->CurrentASC = ASC; ASC->AddAttributeSetSubobject(NewObject<UProject_JAttributeSet>(Owner));
        ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(), 100.f);
        ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 40.f);
        ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxManaAttribute(), 100.f);
        auto* Inventory = NewObject<UProject_JInventoryComponent>(Owner); Inventory->RegisterComponent();
        auto* Definition = NewObject<UProject_JConsumableDefinition>(Owner); Definition->MaxStackCount = 10; Definition->RestoreHealth = 20;
        const auto Item = Inventory->AddItemDefinition(Definition, Count);
        TestTrue(TEXT("Current project recipient has normal persistent-use eligibility before transition"), Inventory->CanUsePersistentItem(Item.InstanceId));
        bool bSawChanging = false, bSawOldTuple = false, bPersistentEligible = true;
        EProject_JItemUseResult DuringChange = EProject_JItemUseResult::Success;
        const auto Changing = ASC->OnActorInfoChanging.AddLambda([&](AActor*, AActor*)
        {
            bSawChanging = ASC->IsActorInfoChanging();
            bSawOldTuple = ASC->GetOwnerActor() == Owner && ASC->GetAvatarActor() == Owner;
            DuringChange = Inventory->TryUseItem(Item.InstanceId);
            bPersistentEligible = Inventory->CanUsePersistentItem(Item.InstanceId);
        });
        if (bClear) ASC->ClearActorInfo(); else ASC->InitAbilityActorInfo(Owner, Owner);
        ASC->OnActorInfoChanging.Remove(Changing);
        TestTrue(TEXT("The real transition delegate exposes changing=true while the old tuple is still installed"), bSawChanging && bSawOldTuple);
        TestEqual(TEXT("Transition rejects direct use before consuming"), DuringChange, EProject_JItemUseResult::Unavailable);
        TestFalse(TEXT("The same transition closes durable-use admission"), bPersistentEligible);
        FProject_JItemInstanceData Remaining;
        TestTrue(TEXT("Transition admission leaves the item instance present"), Inventory->FindItemInstance(Item.InstanceId, Remaining));
        TestEqual(TEXT("Transition admission preserves the complete stack"), Remaining.StackCount, Count);
        TestEqual(TEXT("Transition admission applies no recovery"), ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()), 40.f);
        TestEqual(TEXT("Transition admission creates no cooldown"), Inventory->GetUseCooldownRemaining(), 0.f);
    }
    return true;
}
#endif
