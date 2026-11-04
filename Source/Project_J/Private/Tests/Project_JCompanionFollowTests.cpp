#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Companion/Project_JCompanionComponent.h"
#include "UI/Project_JInventoryViewModel.h"
#include "Companion/Project_JCompanionItemDefinition.h"
#include "Testing/Project_JAuthoredAnimationFixture.h"
#include "Components/Project_JCommandBudgetComponent.h"
#include "Components/Project_JNPCActionComponent.h"
#include "Components/Project_JInventoryComponent.h"
#include "System/Project_JNPCPathSubsystem.h"
#include "System/Project_JNPCDecisionSubsystem.h"
#include "World/Project_JWorldEventRegistry.h"
#include "Tests/Project_JMountLifecycleFixture.h"
#include "Project_JNPCCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "Engine/GameInstance.h"

namespace
{
class FOwnedCompanionFollow final : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    UWorld* World = nullptr;
    AProject_JPlayerState* State = nullptr;
    AProject_JPlayerCharacter* Player = nullptr;
    UProject_JCompanionComponent* Companion = nullptr;
    FGuid ItemId;
    double Started = FPlatformTime::Seconds();
    int32 Phase = 0, BaselineAgents = 0;
public:
    explicit FOwnedCompanionFollow(FAutomationTestBase* InTest) : Test(InTest)
    {
        World = UWorld::CreateWorld(EWorldType::Game, false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->InitializeActorsForPlay(FURL());
        World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
        auto* Controller = World->SpawnActor<APlayerController>();
        State = World->SpawnActor<AProject_JPlayerState>();
        State->SetOwner(Controller); Controller->SetPlayerState(State);
        Player = ProjectJAuthoredAnimationFixture::Spawn(World, Controller, State, FVector(0,0,500));
        Companion = State->GetCompanionComponent();
        if (!Test->TestNotNull(TEXT("Actual authored playable owner"), Player)
            || !Test->TestNotNull(TEXT("Default session companion consumer"), Companion)) return;
        auto* Definition = LoadObject<UProject_JCompanionItemDefinition>(nullptr,
            TEXT("/Game/DataAssetSets/Companion/DA_Companion_Follow_NPC_Test.DA_Companion_Follow_NPC_Test"));
        if (!Test->TestNotNull(TEXT("Actual authored owner-follow definition; create it before this asset integration test"), Definition)) return;
        Test->TestTrue(TEXT("Authored definition uses the runtime validation contract"), Definition->IsFollowDefinitionValid());
        const TStrongObjectPtr<UGameInstance> RegistryOwner(NewObject<UGameInstance>(GEngine));
        const TStrongObjectPtr<UProject_JCharacterDataSubsystem> Data(NewObject<UProject_JCharacterDataSubsystem>(RegistryOwner.Get()));
        Test->TestTrue(TEXT("Existing canonical definition loader reads current PrimaryAsset discovery"), Data->LoadPrimaryAssetDefinitions());
        Test->TestTrue(TEXT("Companion item is a canonical definition for the existing inventory snapshot/restore path"),
            Data->GetItemDefinition(Definition->ItemId, Definition->SchemaVersion) == Definition);
        Test->TestEqual(TEXT("Selected authored NPC class path remains explicit"), Definition->CompanionClass.ToSoftObjectPath().ToString(),
            FString(TEXT("/Game/NPCs/BP_NPC_Test.BP_NPC_Test_C")));
        ItemId = State->GetInventoryComponent()->AddItemDefinition(Definition).InstanceId;
        Test->TestTrue(TEXT("Session owns a real item instance"), ItemId.IsValid());
        auto* Budget = State->FindComponentByClass<UProject_JCommandBudgetComponent>();
        auto* Request = Companion->FindFunction(TEXT("ServerRequestCompanion"));
        if (!Budget || !Request) { Test->AddError(TEXT("Production session admission/RPC is missing")); return; }
        Test->TestTrue(TEXT("Production reliable server RPC"), Request->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer | FUNC_NetReliable));
        Budget->CommandsPerSecond = 1;
        Test->TestTrue(TEXT("Another domain exhausts this actual session"), Budget->TryConsume());
        struct FRequest { FGuid ItemInstanceId; } Params{ItemId};
        Companion->ProcessEvent(Request, &Params);
        Test->TestFalse(TEXT("Rejected external RPC starts no preparation"), Companion->HasPendingPreparation());
        Test->TestEqual(TEXT("Companion uses the shared session rejection accounting"), Budget->GetRejectedCount(), int64(1));
        BaselineAgents = World->GetSubsystem<UProject_JNPCDecisionSubsystem>()->GetAgentCount();
        const TStrongObjectPtr<UProject_JInventoryViewModel> Model(NewObject<UProject_JInventoryViewModel>(State));
        Model->Bind(State->GetInventoryComponent(), State->GetEquipmentManagerComponent());
        const UProject_JInventoryEntry* Entry = nullptr;
        for (const auto& Candidate : Model->InventoryEntries)
            if (Candidate && Candidate->Item.InstanceId == ItemId) { Entry = Candidate.Get(); break; }
        Test->TestTrue(TEXT("Actual inventory item activation queues its companion consumer"), Entry && Model->Activate(Entry));
        Test->TestTrue(TEXT("UI request reaches shared preparation"), Companion->HasPendingPreparation());
        Test->TestFalse(TEXT("A runtime companion request cannot leave the consumable-save spinner pending"), Model->bPending);
        const int32 Reservations = World->GetSubsystem<UProject_JWorldEventRegistry>()->GetStats().Preparations;
        Test->TestTrue(TEXT("Repeated same selection is idempotent"), Companion->UseCompanionAuthority(ItemId));
        Test->TestEqual(TEXT("Repeated command owns only one preparation reservation"),
            World->GetSubsystem<UProject_JWorldEventRegistry>()->GetStats().Preparations, Reservations);
        Test->TestNull(TEXT("Neither warm nor cold request spawns synchronously"), Companion->GetActiveCompanion());
    }
    ~FOwnedCompanionFollow()
    {
        World->EndPlay(EEndPlayReason::LevelTransition); World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    }
    bool Update() override
    {
        if (!Player || !Companion || !ItemId.IsValid() || Test->HasAnyErrors()) return true;
        if (FPlatformTime::Seconds() - Started > 30) { Test->AddError(TEXT("Owned authored companion lifecycle timed out")); return true; }
        // The first live actor is inspected before the next world tick dispatches its path.
        if (auto* NPC = Companion->GetActiveCompanion())
        {
            const TStrongObjectPtr<AProject_JNPCCharacter> KeepNPC(NPC);
            UProject_JNPCActionComponent* Action = nullptr;
            TInlineComponentArray<UProject_JNPCActionComponent*> Candidates(NPC);
            for (auto* Candidate : Candidates)
                if (Candidate->GetOwnershipToken() && Candidate->GetIntentTarget() == Player) { Action = Candidate; break; }
            if (!Test->TestNotNull(TEXT("Companion owns an actual NPC action consumer"), Action)) return true;
            Test->TestEqual(TEXT("Companion follows the authoritative gameplay Avatar"), Action->GetIntentTarget(), static_cast<AActor*>(Player));
            Test->TestFalse(TEXT("Companion owner is never a legal attack target"), Action->CanCommitToTarget(Player));
            Test->TestFalse(TEXT("Follow grants no attack ability"), Action->GetAttackAbilityHandle().IsValid());
            Test->TestEqual(TEXT("No hostile scoring worker/agent is registered"), World->GetSubsystem<UProject_JNPCDecisionSubsystem>()->GetAgentCount(), BaselineAgents);
            if (Phase == 0)
            {
                auto* Mount = World->SpawnActor<AProject_JMountLifecycleFixture>();
                Mount->SetActorLocation(Player->GetActorLocation());
                Test->TestTrue(TEXT("Actual mount possession succeeds"), Mount->TryMountRider(Player));
                Test->TestTrue(TEXT("PlayerState Pawn changed to mount"), State->GetPawn() == Mount);
                Action->UpdateAction();
                Test->TestTrue(TEXT("Normal mount retains the same companion"), Companion->GetActiveCompanion() == NPC);
                Test->TestEqual(TEXT("Follow still uses the attached rider's world position"), Action->GetIntentTarget(), static_cast<AActor*>(Player));
                Test->TestTrue(TEXT("Actual dismount succeeds"), Mount->DismountRider(true));
                Action->UpdateAction();
                Test->TestTrue(TEXT("Normal dismount/reinitialization retains companion"), Companion->GetActiveCompanion() == NPC);
                Mount->Destroy();
                auto* Replacement = World->SpawnActor<AActor>();
                State->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(State, Replacement);
                Action->UpdateAction();
                Test->TestNull(TEXT("Actual gameplay Avatar replacement retires its old follow lease"), Companion->GetActiveCompanion());
                State->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(State, Player);
            }
            else if (Phase == 1)
            {
                State->GetAbilitySystemComponent()->ApplyModToAttribute(UProject_JAttributeSet::GetHealthAttribute(), EGameplayModOp::Override, 0);
                Test->TestNull(TEXT("Owner death retires the companion immediately"), Companion->GetActiveCompanion());
                State->GetAbilitySystemComponent()->ApplyModToAttribute(UProject_JAttributeSet::GetHealthAttribute(), EGameplayModOp::Override, 100);
            }
            else if (Phase == 2)
            {
                NPC->GetAbilitySystemComponent()->ApplyModToAttribute(UProject_JAttributeSet::GetHealthAttribute(), EGameplayModOp::Override, 0);
                Test->TestNull(TEXT("Companion death retires only its runtime lease"), Companion->GetActiveCompanion());
            }
            else if (Phase == 3)
            {
                State->GetInventoryComponent()->SetItemInstanceLocked(ItemId, true);
                Test->TestNull(TEXT("Losing item eligibility retires the active companion"), Companion->GetActiveCompanion());
                State->GetInventoryComponent()->SetItemInstanceLocked(ItemId, false);
            }
            else
            {
                State->GetInventoryComponent()->RemoveItemInstance(ItemId);
                Test->TestNull(TEXT("Removing the owned item retires the companion"), Companion->GetActiveCompanion());
                Test->TestFalse(TEXT("A removed item cannot authorize another preparation"), Companion->UseCompanionAuthority(ItemId));
                Test->TestEqual(TEXT("All follow slots returned to the shared action scheduler"), World->GetSubsystem<UProject_JNPCDecisionSubsystem>()->GetActionCount(), 0);
                return true;
            }
            Test->TestTrue(TEXT("Retired lease destroys its actual authored NPC"), NPC->IsActorBeingDestroyed());
            Test->TestEqual(TEXT("Retired lease cancels its queued path"), World->GetSubsystem<UProject_JNPCPathSubsystem>()->GetRequestCount(), 0);
            ++Phase;
            Test->TestTrue(TEXT("The same eligible item can request a fresh runtime companion"), Companion->UseCompanionAuthority(ItemId));
        }
        World->Tick(LEVELTICK_All, 1.f / 60.f);
        const auto& Stats = World->GetSubsystem<UProject_JWorldEventRegistry>()->GetStats();
        Test->TestTrue(TEXT("All actual spawned companions obey the shared per-frame application bound"), Stats.LastTickSpawns <= UProject_JWorldEventRegistry::MaxSpawnsPerTick);
        Test->TestTrue(TEXT("All actual load requests obey the shared physical preparation bound"), Stats.Preparations <= UProject_JWorldEventRegistry::MaxPreparations);
        return Test->HasAnyErrors();
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJOwnedCompanionFollowTest, "ProjectJ.Companion.OwnedFollowLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJOwnedCompanionFollowTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FOwnedCompanionFollow(this));
    return true;
}
#endif
