#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Gathering/Project_JGatheringNode.h"
#include "Gathering/Project_JGatheringSubsystem.h"
#include "Backend/Project_JLocalProfileSubsystem.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JCommandBudgetComponent.h"
#include "Components/BoxComponent.h"
#include "Game/Project_JPlayerState.h"
#include "Game/Project_JQuestComponent.h"
#include "Project_JSpellbladeCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Inventory/Project_JConsumableDefinition.h"
#include "UI/Project_JInventoryViewModel.h"
#include "Tests/Project_JSourceAuditPhase2UIReceiver.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
AProject_JGatheringNode* SpawnNode(UWorld* World, FName Id)
{
    auto* Node = World->SpawnActorDeferred<AProject_JGatheringNode>(AProject_JGatheringNode::StaticClass(),
        FTransform(FVector(150,0,100)), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Node) return nullptr;
    Node->NodeId = Id; Node->YieldItemId = TEXT("HealthPotion"); Node->CooldownSeconds = .1;
    Node->FinishSpawning(FTransform(FVector(150,0,100))); return Node;
}
class FGatheringExecution final : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    TStrongObjectPtr<UGameInstance> GI;
    UWorld* World;
    UProject_JLocalProfileSubsystem* Profiles;
    AProject_JPlayerState* Player;
    AProject_JSpellbladeCharacter* Avatar = nullptr;
    AProject_JGatheringNode* Node = nullptr;
    UProject_JItemDefinition* Health;
    TStrongObjectPtr<UProject_JQuestDefinition> Quest;
    int32 GatherCount() const
    {
        const auto Snapshot = Player->FindComponentByClass<UProject_JQuestComponent>()->CaptureSnapshot();
        const auto* State = Snapshot.States.FindByPredicate([this](const auto& S) { return S.QuestId == Quest->QuestId; });
        return State ? State->GetObjectiveCount(TEXT("Gather")) : -1;
    }
    TStrongObjectPtr<UProject_JInventoryViewModel> Model;
    TStrongObjectPtr<UProject_JSourceAuditPhase2UIReceiver> AddedObserver, ChangedObserver;
    TWeakObjectPtr<UProject_JInventoryComponent> ObservedInventory;
    FGuid MainStack, IncompatibleStack, RemainderStack;
    int32 EventsAdded = 0, EventsChanged = 0;
    int64 GrantRevision = 0, FailureRevision = 0;
    TArray<FProject_JItemInstanceData> BeforeFailure;
    void ObserveInventory()
    {
        auto* Inventory = Player->GetInventoryComponent(); ObservedInventory = Inventory;
        AddedObserver.Reset(NewObject<UProject_JSourceAuditPhase2UIReceiver>(GI.Get()));
        ChangedObserver.Reset(NewObject<UProject_JSourceAuditPhase2UIReceiver>(GI.Get()));
        // Reuse existing dynamic-delegate receiver. Rearm its one-shot latch in
        // each callback so duplicate publications are counted, never hidden.
        AddedObserver->OnConsumption = [this] { ++EventsAdded; AddedObserver->bConsumedCallback = false; };
        ChangedObserver->OnConsumption = [this] { ++EventsChanged; ChangedObserver->bConsumedCallback = false; };
        Inventory->OnItemAdded.AddDynamic(AddedObserver.Get(), &UProject_JSourceAuditPhase2UIReceiver::OnItem);
        Inventory->OnItemChanged.AddDynamic(ChangedObserver.Get(), &UProject_JSourceAuditPhase2UIReceiver::OnItem);
    }
    int32 Step = 0;
    bool Done = false, Success = false;
    double Started = FPlatformTime::Seconds();
    void Check(bool Value, const TCHAR* Message) { Test->TestTrue(Message, Value); }
    int32 Count() const
    {
        int32 Result = 0;
        for (const auto& Item : Player->GetInventoryComponent()->GetItemInstances()) if (Item.ItemDef == Health) Result += Item.StackCount;
        return Result;
    }
    void Possess()
    {
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* PC = World->SpawnActor<APlayerController>(Params);
        Avatar = World->SpawnActor<AProject_JSpellbladeCharacter>(FVector(0,0,100), FRotator::ZeroRotator, Params);
        Player->SetOwner(PC); PC->SetPlayerState(Player); Avatar->SetPlayerState(Player); PC->Possess(Avatar);
        Model.Reset(NewObject<UProject_JInventoryViewModel>(Player)); Model->Bind(Player->GetInventoryComponent(), Player->GetEquipmentManagerComponent());
    }
public:
    FGatheringExecution(FAutomationTestBase* InTest, UGameInstance* Instance, AProject_JPlayerState* State, UProject_JItemDefinition* Item)
        : Test(InTest), GI(Instance), World(Instance->GetWorld()), Profiles(Instance->GetSubsystem<UProject_JLocalProfileSubsystem>()), Player(State), Health(Item)
    {
        // The movement backend keeps simulating during cooldowns. Give this
        // interaction fixture real support rather than resetting its actor pose.
        auto* GroundActor = World->SpawnActor<AActor>();
        auto* Ground = NewObject<UBoxComponent>(GroundActor);
        GroundActor->SetRootComponent(Ground); GroundActor->AddInstanceComponent(Ground);
        Ground->SetBoxExtent(FVector(2000,2000,10));
        Ground->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Ground->SetCollisionObjectType(ECC_WorldStatic);
        Ground->SetCollisionResponseToAllChannels(ECR_Block);
        Ground->SetWorldLocation(FVector(0,0,-10)); Ground->RegisterComponent();
        Quest.Reset(NewObject<UProject_JQuestDefinition>(Instance)); Quest->QuestId = TEXT("OwnedGatheringCompound");
        FProject_JQuestObjectiveDefinition Gather; Gather.ObjectiveId = TEXT("Gather"); Gather.Kind = EProject_JQuestObjectiveKind::GatherItem;
        Gather.TargetId = TEXT("HealthPotion"); Gather.RequiredCount = 10; // Leave headroom to detect a duplicated grant delta.
        FProject_JQuestObjectiveDefinition Craft; Craft.ObjectiveId = TEXT("Craft"); Craft.Kind = EProject_JQuestObjectiveKind::CraftRecipe;
        Craft.TargetId = TEXT("Prototype.Alchemy.HealthToMana");
        Quest->Objectives = {Gather, Craft};
        Player->FindComponentByClass<UProject_JQuestComponent>()->EnsureDefinitionsResolved();
        Player->FindComponentByClass<UProject_JQuestComponent>()->Definitions.Add(Quest.Get());
    }
    ~FGatheringExecution()
    {
        if (auto* Inventory = ObservedInventory.Get())
        {
            if (AddedObserver) Inventory->OnItemAdded.RemoveAll(AddedObserver.Get());
            if (ChangedObserver) Inventory->OnItemChanged.RemoveAll(ChangedObserver.Get());
        }
        if (AddedObserver) AddedObserver->OnConsumption = {};
        if (ChangedObserver) ChangedObserver->OnConsumption = {};
        if (Model) Model->Unbind();
        if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
        GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    }
    bool Update() override
    {
        if (FPlatformTime::Seconds() - Started > 45) { Test->AddError(TEXT("Gathering execution exceeded45s")); return true; }
        auto* Ledger = World->GetSubsystem<UProject_JGatheringSubsystem>();
        if (Step == 0)
        {
            Profiles->OpenProfile(Player, TEXT("GatheringExecution"), [this](bool OK) { Done = true; Success = OK; }); ++Step; return false;
        }
        if (Step == 1)
        {
            if (!Done) return false;
            if (!Success) { Test->AddError(TEXT("Owned gathering profile did not open")); return true; }
            Player->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(Player, nullptr);
            Player->FindComponentByClass<UProject_JProgressionComponent>()->InitializeDefaults(nullptr, nullptr, 1);
            ++Step; return false;
        }
        if (Step == 2)
        {
            if (Profiles->IsCheckpointPending(Player)) return false;
            auto* Quests = Player->FindComponentByClass<UProject_JQuestComponent>();
            if (!Quests->States.ContainsByPredicate([this](const auto& S) { return S.QuestId == Quest->QuestId; }))
            {
                if (!Quests->Accept(Quest->QuestId)) { Test->AddError(TEXT("Compound acceptance was not admitted after initialization drained")); return true; }
                return false;
            }
            Possess(); Node = SpawnNode(World, TEXT("Native.Gathering.Stable01"));
            if (!Node || !Avatar) { Test->AddError(TEXT("Gathering actor/player fixture did not spawn")); return true; }
            ++Step; return false;
        }
        if (Step == 3)
        {
            if (Profiles->IsCheckpointPending(Player)) return false;
            auto* RPC = Avatar->FindFunction(TEXT("ServerTryInteract"));
            auto* Budget = Player->FindComponentByClass<UProject_JCommandBudgetComponent>();
            if (!RPC || !Budget) { Test->AddError(TEXT("Existing interaction admission is unavailable")); return true; }
            Budget->CommandsPerSecond = 1; Check(Budget->TryConsume(), TEXT("Another domain consumes the session quota"));
            Avatar->ProcessEvent(RPC, nullptr);
            Check(Node->GetPhase() == EProject_JGatheringPhase::Available && Ledger->GetStats().Accepted == 0,
                TEXT("Rejected shared-quota RPC cannot reserve a node or grant a reward"));
            Check(Avatar->TeleportTo(FVector(1000,0,100), Avatar->GetActorRotation()), TEXT("Normal teleport prepares the remote range probe"));
            Check(!Node->TryGather(Avatar), TEXT("Direct server calls recheck gathering range"));
            Check(Avatar->TeleportTo(FVector(0,0,100), Avatar->GetActorRotation()), TEXT("Normal teleport restores the interaction probe"));
            auto* Wall = World->SpawnActor<AActor>(); auto* Blocker = NewObject<UBoxComponent>(Wall);
            Wall->SetRootComponent(Blocker); Wall->AddInstanceComponent(Blocker); Blocker->SetBoxExtent(FVector(5,50,100));
            Blocker->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Blocker->SetCollisionObjectType(ECC_WorldStatic);
            Blocker->SetCollisionResponseToAllChannels(ECR_Ignore); Blocker->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
            Blocker->RegisterComponent(); Wall->SetActorLocation(FVector(75,0,100));
            Check(!Node->TryGather(Avatar), TEXT("Direct server calls enforce an actual blocking visibility trace"));
            Blocker->SetCollisionEnabled(ECollisionEnabled::NoCollision); Wall->Destroy();
            Budget->CommandsPerSecond = 8;
            Avatar->ProcessEvent(RPC, nullptr);
            Check(Node->GetPhase() == EProject_JGatheringPhase::Pending && Count() == 0 && GatherCount() == 0,
                TEXT("Actual reflected interaction reserves before one Add commit and publishes no early item"));
            const FName Stable = Node->NodeId;
            Check(Node->Destroy(), TEXT("Real node Actor unload/Destroy succeeds while its grant is pending"));
            Node = nullptr; CollectGarbage(RF_NoFlags, true); Node = SpawnNode(World, Stable);
            Check(Node && Node->GetPhase() == EProject_JGatheringPhase::Pending && !Node->TryGather(Avatar),
                TEXT("Reloading the same stable node cannot reuse a pending resource"));
            auto* Duplicate = SpawnNode(World, Stable);
            Check(Duplicate && Duplicate->GetPhase() == EProject_JGatheringPhase::Disabled && !Duplicate->TryGather(Avatar),
                TEXT("A second live actor cannot own the same NodeId"));
            if (Duplicate) Duplicate->Destroy();
            ++Step; return false;
        }
        if (Step == 4)
        {
            if (Profiles->IsCheckpointPending(Player)) return false;
            Check(Node->GetPhase() == EProject_JGatheringPhase::Cooldown && Count() == 1 && GatherCount() == 1 && Ledger->GetStats().Committed == 1,
                TEXT("One durable commit grants one canonical item and objective progress together on the reloaded Actor"));
            World->Tick(LEVELTICK_All, .001f);
            int32 Visible = 0; for (const auto& Entry : Model->InventoryEntries) if (Entry && Entry->Item.ItemDef == Health) Visible += Entry->Item.StackCount;
            Check(Visible == 1 && !Node->CanInteract_Implementation(Avatar), TEXT("Existing inventory event/UI receives the committed reward while cooldown rejects interaction"));
            World->Tick(LEVELTICK_All, .11f);
            Check(IProject_JInteractable::Execute_CanInteract(Node, Avatar), TEXT("Normal ground preserves the next gathering cycle's legal range"));
            Check(Node->GetPhase() == EProject_JGatheringPhase::Available, TEXT("Cooldown expires by clock comparison without a node tick or timer"));
            Player->GetAbilitySystemComponent()->ApplyModToAttribute(UProject_JAttributeSet::GetHealthAttribute(), EGameplayModOp::Override, 0);
            Check(!Node->TryGather(Avatar), TEXT("A dead current owner cannot admit a fresh gathering grant"));
            Player->GetAbilitySystemComponent()->ApplyModToAttribute(UProject_JAttributeSet::GetHealthAttribute(), EGameplayModOp::Override, 100);
            Check(Node->TryGather(Avatar), TEXT("A new resource cycle admits a newly issued operation"));
            Model->Unbind(); Model.Reset(); Profiles->Detach(Player); Check(Player->Destroy(), TEXT("Owner logout/Destroy occurs before durable callback"));
            Check(Avatar->Destroy(), TEXT("The retired gameplay Avatar is also destroyed before reconnect"));
            Avatar = nullptr; Player = nullptr; CollectGarbage(RF_NoFlags, true); ++Step; return false;
        }
        if (Step == 5)
        {
            if (Node->GetPhase() == EProject_JGatheringPhase::Pending) return false;
            Check(Node->GetPhase() == EProject_JGatheringPhase::Cooldown && Ledger->GetStats().Committed == 2,
                TEXT("Accepted grant drains after owner retirement and consumes its original node cycle once"));
            Player = World->SpawnActor<AProject_JPlayerState>(); Done = Success = false;
            Player->FindComponentByClass<UProject_JQuestComponent>()->EnsureDefinitionsResolved();
        Player->FindComponentByClass<UProject_JQuestComponent>()->Definitions.Add(Quest.Get());
            Profiles->OpenProfile(Player, TEXT("GatheringExecution"), [this](bool OK) { Done = true; Success = OK; }); ++Step; return false;
        }
        if (Step == 6)
        {
            if (!Done) return false;
            Check(Success && Count() == 2 && GatherCount() == 2, TEXT("Reconnect restores both exact item/objective grants without re-granting the retired owner's cycle"));
            if (!Success) return true;
            Possess(); ++Step; return false;
        }
        if (Step == 7)
        {
            if (Profiles->IsCheckpointPending(Player)) return false;
            const auto Items = Player->GetInventoryComponent()->GetItemInstances();
            const auto* Stack = Items.FindByPredicate([this](const auto& V) { return V.ItemDef == Health && V.ItemLevel == 1; });
            if (!Test->TestTrue(TEXT("Two actual gathers restored one compatible stack, not two identities"), Stack && Stack->StackCount == 2)) return true;
            MainStack = Stack->InstanceId;
            int32 Rows = 0; for (const auto& Item : Items) if (Item.ItemDef == Health) ++Rows;
            if (!Test->TestEqual(TEXT("Compatible gathering avoids an extra persistent row"), Rows, 1)) return true;
            ObserveInventory();
            World->Tick(LEVELTICK_All, .11f);
            if (!Test->TestTrue(TEXT("Normal ground preserves the full-merge cycle's legal range"), IProject_JInteractable::Execute_CanInteract(Node, Avatar))) return true;
            EventsAdded = EventsChanged = 0;
            if (!Test->TestTrue(TEXT("Actual node admits the full-merge cycle"), Node->TryGather(Avatar))) return true;
            ++Step; return false;
        }
        if (Step == 8)
        {
            if (Profiles->IsCheckpointPending(Player)) return false;
            const auto Items = Player->GetInventoryComponent()->GetItemInstances();
            const auto* Stack = Items.FindByPredicate([this](const auto& V) { return V.InstanceId == MainStack; });
            Check(Stack && Stack->StackCount == 3 && Count() == 3 && GatherCount() == 3 && Node->GetPhase() == EProject_JGatheringPhase::Cooldown,
                TEXT("Full merge preserves existing GUID and applies the full objective delta in one grant"));
            Check(EventsAdded == 0 && EventsChanged == 1, TEXT("Full merge publishes one OnItemChanged and no invented OnItemAdded"));
            EventsAdded = EventsChanged = 0;
            Check(!Player->GetInventoryComponent()->AddItemDefinition(Health,1,2).InstanceId.IsValid(),
                TEXT("Explicit level2 Add still awaits native confirmation"));
            ++Step; return false;
        }
        if (Step == 9)
        {
            if (Profiles->IsCheckpointPending(Player)) return false;
            const auto Items = Player->GetInventoryComponent()->GetItemInstances();
            const auto* Distinct = Items.FindByPredicate([this](const auto& V) { return V.ItemDef == Health && V.ItemLevel == 2; });
            if (!Test->TestTrue(TEXT("Explicit Add creates a distinct instance even beside a compatible item definition"),
                Distinct && Distinct->InstanceId != MainStack && Distinct->StackCount == 1 && EventsAdded == 1 && EventsChanged == 0)) return true;
            IncompatibleStack = Distinct->InstanceId;
            const auto* Main = Items.FindByPredicate([this](const auto& V) { return V.InstanceId == MainStack; });
            if (!Main) { Test->AddError(TEXT("Main stack missing")); return true; }
            if (Main->StackCount != Health->MaxStackCount-1)
                if (!Test->TestTrue(TEXT("Normal persistent stack command prepares one free space"),
                    Player->GetInventoryComponent()->SetItemStackCount(MainStack,Health->MaxStackCount-1))) return true;
            ++Step; return false;
        }
        if (Step == 10)
        {
            if (Profiles->IsCheckpointPending(Player)) return false;
            World->Tick(LEVELTICK_All,.11f);
            if (!Test->TestTrue(TEXT("Normal ground preserves partial merge's legal range"), IProject_JInteractable::Execute_CanInteract(Node, Avatar))) return true;
            Node->YieldCount = 2; EventsAdded = EventsChanged = 0;
            GrantRevision = Profiles->GetCommittedRevision(Player);
            Profiles->LoseNextCommitAcknowledgementForTest(Player);
            if (!Test->TestTrue(TEXT("Actual partial grant with lost commit acknowledgement is admitted"),Node->TryGather(Avatar))) return true;
            ++Step; return false;
        }
        if (Step == 11)
        {
            if (Profiles->IsCheckpointPending(Player)) return false;
            const auto Items = Player->GetInventoryComponent()->GetItemInstances();
            const auto* Main = Items.FindByPredicate([this](const auto& V){return V.InstanceId==MainStack;});
            const auto* Other = Items.FindByPredicate([this](const auto& V){return V.InstanceId==IncompatibleStack;});
            const auto* Remainder = Items.FindByPredicate([this](const auto& V){return V.ItemDef==Health && V.ItemLevel==1 && V.InstanceId!=MainStack;});
            if (!Test->TestTrue(TEXT("Partial grant fills the old stack, skips earlier level2 stack, and creates only its remainder"),
                Main && Main->StackCount==Health->MaxStackCount && Other && Other->StackCount==1 && Remainder && Remainder->StackCount==1)) return true;
            RemainderStack=Remainder->InstanceId;
            Check(Count()==Health->MaxStackCount+2 && GatherCount()==5 && Ledger->GetStats().Committed==4
                && Profiles->GetCommittedRevision(Player)==GrantRevision+1 && Node->GetPhase()==EProject_JGatheringPhase::Cooldown,
                TEXT("Lost ACK retries one exact receipt: full amount/objective and cooldown commit exactly once"));
            Check(EventsAdded==1 && EventsChanged==1, TEXT("Partial grant publishes one changed stack and one remainder row once"));
            if (Remainder->StackCount!=Health->MaxStackCount-1)
                if (!Test->TestTrue(TEXT("Persistent stack command prepares a partial-plus-new failure"),
                    Player->GetInventoryComponent()->SetItemStackCount(RemainderStack,Health->MaxStackCount-1))) return true;
            ++Step; return false;
        }
        if (Step == 12)
        {
            if (Profiles->IsCheckpointPending(Player)) return false;
            World->Tick(LEVELTICK_All,.11f);
            Check(IProject_JInteractable::Execute_CanInteract(Node, Avatar),TEXT("Normal ground preserves storage failure's legal range"));
            BeforeFailure=Player->GetInventoryComponent()->GetItemInstances();
            FailureRevision=Profiles->GetCommittedRevision(Player); EventsAdded=EventsChanged=0;
            Profiles->FailCommandWritesForTest(Player);
            if (!Test->TestTrue(TEXT("Storage failure is admitted after a partial merge/new-row plan"),Node->TryGather(Avatar))) return true;
            ++Step; return false;
        }
        if (Step == 13)
        {
            if (Profiles->IsCheckpointPending(Player)) return false;
            Check(Player->GetInventoryComponent()->GetItemInstances()==BeforeFailure && GatherCount()==5
                && Profiles->GetCommittedRevision(Player)==FailureRevision && EventsAdded==0 && EventsChanged==0
                && Node->GetPhase()==EProject_JGatheringPhase::Available && Ledger->GetStats().ProvenAbsent==1,
                TEXT("Failed partial/new grant preserves all quantities, identities, objectives, events and revision before ProvenAbsent releases the node"));
            const uint64 Accepted=Ledger->GetStats().Accepted;
            Check(!Node->TryGather(Avatar) && Ledger->GetStats().Accepted==Accepted,
                TEXT("Faulted provider cannot reuse or resubmit the old operation"));
            return true;
        }
        return false;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGatheringExecutionTest, "ProjectJ.MMO.Gathering.ExclusiveDurableLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJGatheringExecutionTest::RunTest(const FString&)
{
    // Value-boundary probes exercise identity, late completion and bounded reclamation without native I/O.
    {
        auto* World = UWorld::CreateWorld(EWorldType::Game, false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
        World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
        auto* Ledger = World->GetSubsystem<UProject_JGatheringSubsystem>();
        auto* Node = SpawnNode(World, TEXT("Native.Gathering.Generation"));
        if (!TestNotNull(TEXT("Real node fixture"), Node)) return false;
        auto& FirstSlot = Ledger->Nodes.FindChecked(Node->NodeId);
        const FGuid FirstRequest = FGuid::NewGuid();
        const uint64 First = Ledger->Reserve(Node, FirstSlot.Registration, FirstRequest);
        FProject_JProfileDurableResult Result; Result.Request = FirstRequest; Result.Outcome = EProject_JProfileDurableOutcome::ProvenAbsent;
        Ledger->Complete(Node->NodeId, First, FirstRequest, Result);
        TestTrue(TEXT("Proven absent operation is terminal before availability"), Ledger->Nodes.FindChecked(Node->NodeId).bTerminal && Node->GetPhase() == EProject_JGatheringPhase::Available);
        auto& Current = Ledger->Nodes.FindChecked(Node->NodeId);
        const FGuid SecondRequest = FGuid::NewGuid(); const uint64 Second = Ledger->Reserve(Node, Current.Registration, SecondRequest);
        Result.Outcome = EProject_JProfileDurableOutcome::Committed;
        Ledger->Complete(Node->NodeId, First, FirstRequest, Result);
        TestTrue(TEXT("Late old completion cannot consume or release a newer operation"), Node->GetPhase() == EProject_JGatheringPhase::Pending);
        Result.Request = SecondRequest; Result.Outcome = EProject_JProfileDurableOutcome::Unknown; Result.ReceiptStatus = ProjectJ::MMO::ECommitStatus::Expired;
        Ledger->Complete(Node->NodeId, Second, SecondRequest, Result);
        const FName Id = Node->NodeId; Node->Destroy(); CollectGarbage(RF_NoFlags, true); Node = SpawnNode(World, Id);
        TestTrue(TEXT("Unknown/Expired reservation survives actor GC/reload and has no timeout release"), Node && Node->GetPhase() == EProject_JGatheringPhase::Unresolved);
        Ledger->OnWorldEndPlay(*World);
        TestEqual(TEXT("World teardown discards nonpersistent cooldown/resource state"), Ledger->GetTrackedCount(), 0);
    }
    {
        auto* World = UWorld::CreateWorld(EWorldType::Game, false); GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
        auto* Ledger = World->GetSubsystem<UProject_JGatheringSubsystem>(); const double Now = World->GetTimeSeconds();
        for (int32 Index = 0; Index < Ledger->MaxTrackedNodes; ++Index)
        {
            const FName Id(*FString::Printf(TEXT("Native.Gathering.Capacity.%d"), Index));
            auto& Slot = Ledger->Nodes.Add(Id); Slot.Phase = EProject_JGatheringPhase::Cooldown; Slot.NextAvailable = Now + 100;
            Slot.ReclaimIndex = Ledger->ReclaimOrder.Add(Id);
        }
        auto* Node = SpawnNode(World, TEXT("Native.Gathering.Capacity.New"));
        TestEqual(TEXT("Live logical cooldown capacity rejects new identity without eviction"), Ledger->RegisterNode(Node), uint64(0));
        Ledger->Nodes.FindChecked(Ledger->ReclaimOrder.Last()).NextAvailable = Now - 1;
        uint64 Registered = 0;
        for (int32 Attempt = 0; Attempt <= Ledger->MaxTrackedNodes / Ledger->MaxReclaimsPerAdmission && !Registered; ++Attempt)
        { Registered = Ledger->RegisterNode(Node); TestTrue(TEXT("Each reclamation admission visits at most its fixed slice"), Ledger->GetStats().LastReclaimVisits <= Ledger->MaxReclaimsPerAdmission); }
        TestTrue(TEXT("Bounded round-robin eventually reclaims an expired ID beyond the first slice"), Registered != 0 && Ledger->GetTrackedCount() == Ledger->MaxTrackedNodes);
    }
    auto* GI = NewObject<UGameInstance>(GEngine); GI->InitializeStandalone(); auto* World = GI->GetWorld(); bool Queued = false;
    ON_SCOPE_EXIT { if (!Queued) { if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit); GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); } };
    auto* Data = GI->GetSubsystem<UProject_JCharacterDataSubsystem>();
    auto* Health = LoadObject<UProject_JConsumableDefinition>(nullptr, TEXT("/Game/UI/Gameplay/DA_ProjectJHealthPotion.DA_ProjectJHealthPotion"));
    if (!TestTrue(TEXT("Actual canonical gathering yield is registered"), Health && Data->LoadPrimaryAssetDefinitions() && Data->GetItemDefinition(Health->ItemId, Health->SchemaVersion) == Health)) return false;
    UE_LOG(LogTemp, Display, TEXT("GatherStack actual canonical HealthPotion MaxStackCount=%d"), Health->MaxStackCount);
    if (!TestTrue(TEXT("Canonical gathering yield supports this full/partial merge fixture without a hardcoded stack maximum"),
        Health->MaxStackCount >= 3 && Health->MaxStackCount <= 1000000)) return false;
    auto* Profiles = GI->GetSubsystem<UProject_JLocalProfileSubsystem>();
    Profiles->DatabasePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Validation/GatheringExecution") / FGuid::NewGuid().ToString() / TEXT("Profiles.sqlite"));
    auto* Player = World->SpawnActor<AProject_JPlayerState>();
    World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
    ADD_LATENT_AUTOMATION_COMMAND(FGatheringExecution(this, GI, Player, Health)); Queued = true;
    return true;
}
#endif
