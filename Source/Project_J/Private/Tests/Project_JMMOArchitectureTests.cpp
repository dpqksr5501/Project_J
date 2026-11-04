#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Game/Project_JPlayerState.h"
#include "Game/Project_JQuestComponent.h"
#include "Components/Project_JCommandBudgetComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Inventory/Project_JConsumableDefinition.h"
#include "Backend/Project_JGatewaySubsystem.h"
#include "Backend/Project_JGatewayResponseBuffer.h"
#include "MMO/Coordination.h"
#include "Social/Project_JSocialSubsystem.h"
#include "Tests/Project_JMMOArchitectureReceiver.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Net/UnrealNetwork.h"
#include "Project_JSpellbladeCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "UI/Project_JPlayerHUDWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/VerticalBox.h"

namespace
{
constexpr auto MMOArchitectureTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
struct FArchitectureWorld
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FArchitectureWorld() { GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World); }
	~FArchitectureWorld()
	{
		if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSharedBudgetTest, "ProjectJ.MMO.Architecture.SharedCommandBudget", MMOArchitectureTestFlags)
bool FProjectJSharedBudgetTest::RunTest(const FString&)
{
	FArchitectureWorld Scope;
	Scope.World->InitializeActorsForPlay(FURL());
	Scope.World->GetWorldSettings()->NotifyBeginPlay();
	Scope.World->GetWorldSettings()->NotifyMatchStarted();
	auto* State = Scope.World->SpawnActor<AProject_JPlayerState>();
	auto* Budget = State->FindComponentByClass<UProject_JCommandBudgetComponent>();
	if (!TestNotNull(TEXT("Session owns a common budget"), Budget)) return false;
	Budget->CommandsPerSecond = 3;
	State->GetEquipmentManagerComponent()->RequestEquipmentChange(FGuid::NewGuid(), FGuid(), EProject_JEquipmentSlot::None, false);
	State->GetInventoryComponent()->RequestBagChange(FGuid::NewGuid(), {});
	State->FindComponentByClass<UProject_JQuestComponent>()->RequestTracking(TEXT("UnknownQuest"), true);
	TestFalse(TEXT("Different real command entry points consume the same budget, even when invalid"), Budget->TryConsume());
	auto* Controller = Scope.World->SpawnActor<APlayerController>();
	Controller->PlayerState = State;
	auto* Pawn = Scope.World->SpawnActor<APawn>();
	Controller->Possess(Pawn);
	TestFalse(TEXT("Avatar commands cannot obtain a second budget"), UProject_JCommandBudgetComponent::AdmitClientCommand(Pawn));
	Controller->UnPossess();
	auto* Replacement = Scope.World->SpawnActor<APawn>();
	Controller->Possess(Replacement);
	TestFalse(TEXT("Possession replacement preserves the session limit"), UProject_JCommandBudgetComponent::AdmitClientCommand(Replacement));
	TestEqual(TEXT("Rejected commands counted without per-request logging"), Budget->GetRejectedCount(), int64(3));
	State->Destroy();
	TestFalse(TEXT("Retired budget cannot admit a continuation"), Budget->TryConsume());
	auto* OtherState = Scope.World->SpawnActor<AProject_JPlayerState>();
	TestTrue(TEXT("Independent session is not throttled"), UProject_JCommandBudgetComponent::AdmitClientCommand(OtherState));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCommandReplayTest, "ProjectJ.MMO.Architecture.InventoryCommandIdentity", MMOArchitectureTestFlags)
bool FProjectJCommandReplayTest::RunTest(const FString&)
{
	FArchitectureWorld Scope;
	auto* State = Scope.World->SpawnActor<AProject_JPlayerState>();
	auto* Inventory = State->GetInventoryComponent();
	TStrongObjectPtr<UProject_JMMOArchitectureReceiver> Receiver(NewObject<UProject_JMMOArchitectureReceiver>());
	Inventory->OnBagCompleted.AddDynamic(Receiver.Get(), &UProject_JMMOArchitectureReceiver::Bagged);
	Inventory->OnUseCompleted.AddDynamic(Receiver.Get(), &UProject_JMMOArchitectureReceiver::Used);
	auto* Definition = NewObject<UProject_JConsumableDefinition>(State);
	Definition->MaxStackCount = 20;
	const auto Item = Inventory->AddItemDefinition(Definition, 8);
	FProject_JBagRequest Split;
	Split.Source = Item.InstanceId; Split.Count = 2; Split.ExpectedSourceCount = 8; Split.ExpectedSourceOrder = Item.BagOrder;
	const auto RequestId = FGuid::NewGuid();
	Inventory->ServerBagChange_Implementation(RequestId, Split);
	TestEqual(TEXT("Real split commits"), Receiver->BagResult, EProject_JBagResult::Success);
	Inventory->ServerBagChange_Implementation(RequestId, Split);
	TestEqual(TEXT("Same request returns original receipt despite changed observed count"), Receiver->BagResult, EProject_JBagResult::Success);
	FProject_JItemInstanceData Current; Inventory->FindItemInstance(Item.InstanceId, Current);
	TestEqual(TEXT("Replay cannot split twice"), Current.StackCount, 6);
	Split.Count = 3;
	Inventory->ServerBagChange_Implementation(RequestId, Split);
	TestEqual(TEXT("Same request ID with different command is rejected"), Receiver->BagResult, EProject_JBagResult::Invalid);
	Inventory->FindItemInstance(Item.InstanceId, Current);
	TestEqual(TEXT("Changed-payload rejection preserves inventory"), Current.StackCount, 6);
	TestEqual(TEXT("Receipt remains bound to original payload"), Inventory->BagReceipts[RequestId].Request.Count, 2);
	const auto UseId = FGuid::NewGuid();
	Inventory->ServerUseItem_Implementation(UseId, FGuid());
	TestEqual(TEXT("Invalid use is recorded"), Receiver->UseResult, EProject_JItemUseResult::Invalid);
	Inventory->ServerUseItem_Implementation(UseId, Item.InstanceId);
	TestEqual(TEXT("Use receipt cannot be retargeted"), Receiver->UseResult, EProject_JItemUseResult::Invalid);
	TestFalse(TEXT("Original receipt target was not replaced"), Inventory->UseReceipts[UseId].InstanceId.IsValid());
	TestEqual(TEXT("Every admitted RPC replies once"), Receiver->Replies, 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCommandReplyAdmissionTest, "ProjectJ.MMO.Architecture.CommandReplyAdmission", MMOArchitectureTestFlags)
bool FProjectJCommandReplyAdmissionTest::RunTest(const FString&)
{
    FArchitectureWorld Scope;
    Scope.World->InitializeActorsForPlay(FURL());
    Scope.World->GetWorldSettings()->NotifyBeginPlay();
    Scope.World->GetWorldSettings()->NotifyMatchStarted();
    const auto BindReplies = [](AProject_JPlayerState* Owner, UProject_JMMOArchitectureReceiver* Observer)
    {
        Owner->GetInventoryComponent()->OnUseCompleted.AddDynamic(Observer, &UProject_JMMOArchitectureReceiver::Used);
        Owner->GetInventoryComponent()->OnBagCompleted.AddDynamic(Observer, &UProject_JMMOArchitectureReceiver::Bagged);
        Owner->GetEquipmentManagerComponent()->OnRequestCompleted.AddDynamic(Observer, &UProject_JMMOArchitectureReceiver::Equipped);
    };
    auto* Owner = Scope.World->SpawnActor<AProject_JPlayerState>();
    auto* Inventory = Owner->GetInventoryComponent();
    auto* Equipment = Owner->GetEquipmentManagerComponent();
    auto* Budget = Owner->FindComponentByClass<UProject_JCommandBudgetComponent>();
    const TStrongObjectPtr<UProject_JMMOArchitectureReceiver> Receiver(NewObject<UProject_JMMOArchitectureReceiver>());
    BindReplies(Owner, Receiver.Get());
    const TStrongObjectPtr<UProject_JConsumableDefinition> Potion(NewObject<UProject_JConsumableDefinition>());
    Potion->ItemId = TEXT("CommandReplyPotion"); Potion->MaxStackCount = 20;
    const auto Source = Inventory->AddItemDefinition(Potion.Get(), 8);
    const TStrongObjectPtr<UProject_JEquipmentItemDefinition> Weapon(NewObject<UProject_JEquipmentItemDefinition>());
    Weapon->ItemId = TEXT("CommandReplyWeapon"); Weapon->EquipmentSlot = EProject_JEquipmentSlot::Weapon;
    const auto OwnedWeapon = Inventory->AddItemDefinition(Weapon.Get());
    if (!TestTrue(TEXT("Real owner contains the source items before admission exhaustion"), Source.IsValid() && OwnedWeapon.IsValid())) return false;
    FProject_JBagRequest Split;
    Split.Source = Source.InstanceId; Split.Count = 2; Split.ExpectedSourceCount = Source.StackCount; Split.ExpectedSourceOrder = Source.BagOrder;
    const auto BeforeItems = Inventory->GetItemInstances();
    const auto BeforeEquipment = Equipment->CapturePersistentEquipment();
    const float BeforeHealth = Owner->GetProjectJAttributeSet()->GetHealth();
    Budget->CommandsPerSecond = 1;
    if (!TestTrue(TEXT("Consume the real owner budget before issuing rejected commands"), Budget->TryConsume())) return false;
    for (int32 Index = 0; Index < 8; ++Index)
    {
        Inventory->RequestUseItem(FGuid::NewGuid(), Source.InstanceId);
        Inventory->RequestBagChange(FGuid::NewGuid(), Split);
        Equipment->ServerRequestEquipmentChange_Implementation(FGuid::NewGuid(), OwnedWeapon.InstanceId, EProject_JEquipmentSlot::Weapon, false);
    }
    TestEqual(TEXT("Unadmitted actual handlers publish no new completion callbacks"), Receiver->Replies, 0);
    TestTrue(TEXT("Unadmitted requests create no bag/use receipts"), Inventory->BagReceipts.IsEmpty() && Inventory->UseReceipts.IsEmpty());
    TestTrue(TEXT("Unadmitted requests preserve actual inventory values"), Inventory->GetItemInstances() == BeforeItems);
    TestEqual(TEXT("Unadmitted equipment never installs a slot"), Equipment->CapturePersistentEquipment().Num(), BeforeEquipment.Num());
    TestEqual(TEXT("Unadmitted use cannot heal the owner"), Owner->GetProjectJAttributeSet()->GetHealth(), BeforeHealth);
    TestEqual(TEXT("All three handlers use the same real admission rejection counter"), Budget->GetRejectedCount(), int64(24));
    const auto DirectResult = Equipment->ExecuteEquipmentRequest(OwnedWeapon.InstanceId, EProject_JEquipmentSlot::Weapon, false);
    TestEqual(TEXT("Direct/legacy execution retains its RateLimited operation result"), DirectResult.Failure, EProject_JEquipmentOperationFailure::RateLimited);

    auto* Independent = Scope.World->SpawnActor<AProject_JPlayerState>();
    const TStrongObjectPtr<UProject_JMMOArchitectureReceiver> IndependentReplies(NewObject<UProject_JMMOArchitectureReceiver>());
    BindReplies(Independent, IndependentReplies.Get());
    auto* IndependentBudget = Independent->FindComponentByClass<UProject_JCommandBudgetComponent>();
    IndependentBudget->CommandsPerSecond = 4;
    Independent->GetInventoryComponent()->RequestUseItem(FGuid::NewGuid(), FGuid::NewGuid());
    Independent->GetInventoryComponent()->RequestBagChange(FGuid::NewGuid(), {});
    Independent->GetEquipmentManagerComponent()->ServerRequestEquipmentChange_Implementation(FGuid::NewGuid(), FGuid(), EProject_JEquipmentSlot::None, false);
    TestEqual(TEXT("An independent admitted owner receives each ordinary invalid-command result once"), IndependentReplies->Replies, 3);
    TestEqual(TEXT("Admitted equipment preserves invalid-slot semantics"), IndependentReplies->EquipmentResult.Failure, EProject_JEquipmentOperationFailure::InvalidSlot);
    TestTrue(TEXT("Three handlers spent three credits, not a wrapper plus executor double charge"), IndependentBudget->TryConsume());
    TestFalse(TEXT("The independent fourth credit exhausts that owner only"), IndependentBudget->TryConsume());

    auto* Throttled = Scope.World->SpawnActor<AProject_JPlayerState>();
    const TStrongObjectPtr<UProject_JMMOArchitectureReceiver> ThrottledReplies(NewObject<UProject_JMMOArchitectureReceiver>());
    BindReplies(Throttled, ThrottledReplies.Get());
    auto* DomainBudget = Throttled->FindComponentByClass<UProject_JCommandBudgetComponent>();
    DomainBudget->CommandsPerSecond = 100;
    for (int32 Index = 0; Index < 21; ++Index)
    {
        Throttled->GetInventoryComponent()->RequestUseItem(FGuid::NewGuid(), FGuid::NewGuid());
        Throttled->GetInventoryComponent()->RequestBagChange(FGuid::NewGuid(), {});
        Throttled->GetEquipmentManagerComponent()->ServerRequestEquipmentChange_Implementation(FGuid::NewGuid(), FGuid(), EProject_JEquipmentSlot::None, false);
    }
    TestEqual(TEXT("Shared-admitted domain throttles still reply once per handler call"), ThrottledReplies->Replies, 63);
    TestTrue(TEXT("All three domain20/s throttles retain RateLimited feedback"), ThrottledReplies->UseResult == EProject_JItemUseResult::RateLimited
        && ThrottledReplies->BagResult == EProject_JBagResult::RateLimited
        && ThrottledReplies->EquipmentResult.Failure == EProject_JEquipmentOperationFailure::RateLimited);
    TestEqual(TEXT("Domain throttle test never exhausted common admission"), DomainBudget->GetRejectedCount(), int64(0));

    auto* PersistentOwner = Scope.World->SpawnActor<AProject_JPlayerState>();
    auto* PersistentEquipment = PersistentOwner->GetEquipmentManagerComponent();
    const auto PersistentItem = PersistentOwner->GetInventoryComponent()->AddItemDefinition(Weapon.Get());
    const TStrongObjectPtr<UProject_JMMOArchitectureReceiver> PersistentReplies(NewObject<UProject_JMMOArchitectureReceiver>());
    BindReplies(PersistentOwner, PersistentReplies.Get());
    auto* PersistentBudget = PersistentOwner->FindComponentByClass<UProject_JCommandBudgetComponent>();
    PersistentBudget->CommandsPerSecond = 1;
    int32 Submissions = 0;
    FProject_JPersistentCompletion PendingCompletion;
    PersistentEquipment->SubmitPersistentEdit = [&](const FProject_JPersistentEdit&, FProject_JPersistentCompletion Complete)
    { ++Submissions; PendingCompletion = MoveTemp(Complete); return true; };
    ON_SCOPE_EXIT { PersistentEquipment->SubmitPersistentEdit = {}; PendingCompletion = {}; };
    // Observe the existing persistence port's callback contract; no DB commit or installed durable state is simulated.
    PersistentEquipment->ServerRequestEquipmentChange_Implementation(FGuid::NewGuid(), PersistentItem.InstanceId, EProject_JEquipmentSlot::Weapon, false);
    TestTrue(TEXT("An admitted persistent request returns the original Pending result"), Submissions == 1 && PendingCompletion
        && PersistentReplies->EquipmentReplies == 1 && PersistentReplies->EquipmentResult.Failure == EProject_JEquipmentOperationFailure::Pending);
    TestFalse(TEXT("The persistent request consumed its one shared credit"), PersistentBudget->TryConsume());
    if (PendingCompletion)
    {
        auto Complete = MoveTemp(PendingCompletion);
        Complete(true, true);
    }
    TestTrue(TEXT("Already-admitted final completion is delivered even after shared exhaustion"), PersistentReplies->EquipmentReplies == 2
        && PersistentReplies->EquipmentResult.bSucceeded && Submissions == 1);
    TestEqual(TEXT("Completion delivery never consumes or rejects an extra command credit"), PersistentBudget->GetRejectedCount(), int64(1));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJEquipmentPrivacyTest, "ProjectJ.MMO.Architecture.EquipmentProjection", MMOArchitectureTestFlags)
bool FProjectJEquipmentPrivacyTest::RunTest(const FString&)
{
	FArchitectureWorld Scope;
	auto* State = Scope.World->SpawnActor<AProject_JPlayerState>();
	auto* Manager = State->GetEquipmentManagerComponent();
	auto* Definition = NewObject<UProject_JEquipmentItemDefinition>(State);
	Definition->EquipmentSlot = EProject_JEquipmentSlot::Weapon;
	const auto Item = State->GetInventoryComponent()->AddItemDefinition(Definition);
	TestTrue(TEXT("Owned equipment commits"), Manager->TryEquipItemInstanceById(Item.InstanceId).bSucceeded);
	TestEqual(TEXT("One authoritative equipment slot"), Manager->GetPrivateEquipmentCount(), 1);
	TestEqual(TEXT("Projection contains no item instance field"), FindFProperty<FProperty>(FProject_JPublicEquipmentItem::StaticStruct(), TEXT("ItemInstance")), static_cast<FProperty*>(nullptr));
	// Standalone test worlds have no NetDriver to initialize the class replication indices.
	Manager->GetClass()->SetUpRuntimeReplicationData();
	TArray<FLifetimeProperty> Props; Manager->GetLifetimeReplicatedProps(Props);
	const auto* PrivateProp = FindFProperty<FProperty>(Manager->GetClass(), TEXT("EquipmentArray"));
	bool PrivateOwnerOnly = false;
	for (const auto& Prop : Props)
		if (Prop.RepIndex == PrivateProp->RepIndex) PrivateOwnerOnly = Prop.Condition == COND_OwnerOnly;
	TestTrue(TEXT("Actual lifetime registration keeps private data owner-only"), PrivateOwnerOnly);
	TestNull(TEXT("No unused global equipment mirror remains on PlayerState"),
		FindFProperty<FProperty>(Manager->GetClass(), TEXT("PublicEquipmentArray")));
	Scope.World->InitializeActorsForPlay(FURL());
	Scope.World->GetWorldSettings()->NotifyBeginPlay();
	Scope.World->GetWorldSettings()->NotifyMatchStarted();
	auto* Avatar = Scope.World->SpawnActor<APawn>();
	auto* Runtime = NewObject<UProject_JEquipmentRuntimeComponent>(Avatar);
	Avatar->AddInstanceComponent(Runtime); Runtime->RegisterComponent();
	Runtime->BindToEquipmentManager(Manager);
	TestEqual(TEXT("Late Avatar binding publishes the committed slot"), Runtime->GetPublicEquipment().Items.Num(), 1);
	Runtime->GetClass()->SetUpRuntimeReplicationData();
	TArray<FLifetimeProperty> RuntimeProps; Runtime->GetLifetimeReplicatedProps(RuntimeProps);
	const auto* RuntimePublic = FindFProperty<FProperty>(Runtime->GetClass(), TEXT("PublicEquipment"));
	bool PublicSkipOwner = false;
	for (const auto& Prop : RuntimeProps)
		if (Prop.RepIndex == RuntimePublic->RepIndex) PublicSkipOwner = Prop.Condition == COND_SkipOwner;
	TestTrue(TEXT("Current Pawn sends one public stream only to observers"), PublicSkipOwner);

	// Local-role consumer/lifetime fixture; actual socket cohort covers Iris roles.
	auto* ObserverState = Scope.World->SpawnActor<AProject_JPlayerState>();
	auto* ObserverPawn = Scope.World->SpawnActor<AProject_JSpellbladeCharacter>();
	ObserverState->SetRole(ROLE_SimulatedProxy); ObserverPawn->SetRole(ROLE_SimulatedProxy);
	ObserverPawn->SetPlayerState(ObserverState);
	auto* Observer = ObserverPawn->FindComponentByClass<UProject_JEquipmentRuntimeComponent>();
	if (!TestNotNull(TEXT("Actual player owns one presentation Runtime"), Observer)) return false;
	Observer->PublicEquipment.Items = Runtime->GetPublicEquipment().Items;
	Observer->OnPublicEquipmentReceived();
	TestEqual(TEXT("Remote snapshot resolves without private equipment"), Observer->GetPresentedItemInSlot(EProject_JEquipmentSlot::Weapon), Definition);
	TestEqual(TEXT("Remote Blueprint slot query uses current Avatar"), ObserverState->GetEquipmentManagerComponent()->GetEquippedItemInSlot(EProject_JEquipmentSlot::Weapon), Definition);
	const uint64 VisualRevision = Observer->GetWeaponRevision();
	Observer->OnPublicEquipmentReceived(); Observer->BindToEquipmentManager(nullptr);
	TestEqual(TEXT("Unchanged resolution/binding preserves visual lease"), Observer->GetWeaponRevision(), VisualRevision);
	ObserverPawn->SetPlayerState(nullptr);
	TestTrue(TEXT("No current Avatar means no remote manager item payload"), ObserverState->GetEquipmentManagerComponent()->GetAllEquippedItems().IsEmpty());
	TestEqual(TEXT("Detached Avatar retains its own visual snapshot"), Observer->GetPresentedItemInSlot(EProject_JEquipmentSlot::Weapon), Definition);
	Observer->PublicEquipment.Items[0].ItemDef = nullptr; Observer->OnPublicEquipmentReceived();
	TestNull(TEXT("Unresolved replacement revokes old item immediately"), Observer->GetPresentedItemInSlot(EProject_JEquipmentSlot::Weapon));
	Observer->PublicEquipment.Items[0].ItemDef = Definition; Observer->OnPublicEquipmentReceived();
	TestTrue(TEXT("Reference repair installs new visual generation"), Observer->GetWeaponRevision() > VisualRevision);
	const auto Handle = Observer->WeaponRevoked.AddLambda([Observer, Definition]
	{
		Observer->PublicEquipment.Items.Reset();
		auto& Nested = Observer->PublicEquipment.Items.AddDefaulted_GetRef();
		Nested.Slot = EProject_JEquipmentSlot::Head; Nested.ItemDef = Definition;
		Observer->OnPublicEquipmentReceived();
	});
	Observer->PublicEquipment.Items.Reset(); Observer->OnPublicEquipmentReceived();
	TestEqual(TEXT("Reentrant complete snapshot supersedes outer removal"), Observer->GetPresentedItemInSlot(EProject_JEquipmentSlot::Head), Definition);
	Observer->WeaponRevoked.Remove(Handle);
	// Receive a new complete public snapshot while Owner still resolves locally;
	// only Owner's later RepNotify must wake the already received remote state.
	auto* LocalController = Scope.World->SpawnActor<APlayerController>(); LocalController->SetAsLocalPlayerController();
	Observer->PublicEquipment.Items.Reset();
	auto& BeforeSwitch = Observer->PublicEquipment.Items.AddDefaulted_GetRef();
	BeforeSwitch.Slot = EProject_JEquipmentSlot::Weapon; BeforeSwitch.ItemDef = Definition;
	Observer->OnPublicEquipmentReceived();
	auto* IncomingState = Scope.World->SpawnActor<AProject_JPlayerState>();
	auto* Incoming = IncomingState->GetEquipmentManagerComponent(); Incoming->EquipItem(Definition);
	const auto RetireIncoming = Observer->WeaponRevoked.AddLambda([Incoming] { Incoming->DestroyComponent(); });
	ObserverPawn->SetOwner(LocalController); Observer->BindToEquipmentManager(Incoming);
	TestNull(TEXT("Cleanup cannot seed a target manager retired by its callback"), Observer->BoundEquipmentManager);
	TestTrue(TEXT("Target retirement leaves no stale item after role switch"), Observer->RuntimeItems.IsEmpty());
	Observer->WeaponRevoked.Remove(RetireIncoming);
	Observer->BindToEquipmentManager(Manager);
	TestEqual(TEXT("Owner bridge consumes private manager"), Observer->GetPresentedItemInSlot(EProject_JEquipmentSlot::Weapon), Definition);
	Observer->PublicEquipment.Items.Reset(); Observer->OnPublicEquipmentReceived();
	TestEqual(TEXT("Locally owned Avatar ignores public arrival"), Observer->GetPresentedItemInSlot(EProject_JEquipmentSlot::Weapon), Definition);
	ObserverPawn->SetOwner(nullptr);
	ObserverPawn->ProcessEvent(ObserverPawn->FindFunctionChecked(TEXT("OnRep_Owner")), nullptr);
	TestNull(TEXT("Owner-only RepNotify applies already received empty public snapshot"), Observer->GetPresentedItemInSlot(EProject_JEquipmentSlot::Weapon));
	Observer->DestroyComponent();
	TestTrue(TEXT("Remote retirement releases all items"), Observer->RuntimeItems.IsEmpty());
	TestNull(TEXT("Retirement detaches FastArray completion"), Observer->PublicEquipment.OwnerRuntime.Get());
	Manager->UnequipSlot(EProject_JEquipmentSlot::Weapon);
	TestTrue(TEXT("Unequip clears authority and current Avatar transport"), Manager->EquipmentArray.Items.IsEmpty() && Runtime->GetPublicEquipment().Items.IsEmpty());
	Manager->EquipItem(Definition);
	TestEqual(TEXT("Prototype path publishes current Avatar transport"), Runtime->GetPublicEquipment().Items.Num(), 1);
	Manager->DestroyComponent();
	TestTrue(TEXT("Manager retirement revokes bound Avatar transport"), Runtime->GetPublicEquipment().Items.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSocialSessionTest, "ProjectJ.MMO.Architecture.SocialSessionRevocation", MMOArchitectureTestFlags)
bool FProjectJSocialSessionTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));
	TStrongObjectPtr<UProject_JSocialSubsystem> Social(NewObject<UProject_JSocialSubsystem>(GI.Get()));
	TStrongObjectPtr<AProject_JPlayerState> Old(NewObject<AProject_JPlayerState>()), Current(NewObject<AProject_JPlayerState>());
	const auto Account = FProject_JAccountId::NewId(); const auto Character = FProject_JCharacterId::NewId();
	Old->SetIdentity(Account, Character); Current->SetIdentity(Account, Character);
	const auto Party = Social->CreateParty(Old.Get());
	TestTrue(TEXT("Initial memory party is real"), Party.bSucceeded);
	TestEqual(TEXT("Real party creation projects the owner roster"), Old->GetPartyRoster().Num(), 1);
	Social->BindPlayerState(Current.Get());
	TestTrue(TEXT("Replacement receives existing membership"), Current->GetPartyId_Implementation() == Party.GroupId);
	TestTrue(TEXT("Previous projection is revoked"), Old->GetPartyId_Implementation().IsNone());
	TestTrue(TEXT("Previous owner roster is revoked too"), Old->GetPartyRoster().IsEmpty());
	TestEqual(TEXT("Current session receives the real member summary"), Current->GetPartyRoster().Num(), 1);
	TestEqual(TEXT("Superseded session cannot leave"), Social->LeaveParty(Old.Get()).Failure, EProject_JSocialOperationFailure::StaleSession);
	TestEqual(TEXT("Superseded session cannot create another group"), Social->CreateGuild(Old.Get()).Failure, EProject_JSocialOperationFailure::StaleSession);
	TestFalse(TEXT("Trusted restore does not implicitly steal an active binding"), Social->RestoreMembership(Old.Get(), Party.GroupId, NAME_None));
	Social->UnbindPlayerState(Old.Get());
	TestTrue(TEXT("Late old-session unbind preserves replacement"), Social->LeaveParty(Current.Get()).bSucceeded);
	Social->UnbindPlayerState(Current.Get());
	TestEqual(TEXT("Logout of replacement cannot resurrect old writer"), Social->CreateParty(Old.Get()).Failure, EProject_JSocialOperationFailure::StaleSession);
	Social->BindPlayerState(Old.Get());
	TestTrue(TEXT("Explicit server binding can establish a new session"), Social->CreateParty(Old.Get()).bSucceeded);

	// Keep every superseded actor alive: session fencing must not depend on GC
	// or on whether a newer session remains in the online-character index.
	TArray<TStrongObjectPtr<AProject_JPlayerState>> Superseded;
	AProject_JPlayerState* Bound = Old.Get();
	for (int32 Index = 0; Index < 32; ++Index)
	{
		TStrongObjectPtr<AProject_JPlayerState> Replacement(NewObject<AProject_JPlayerState>());
		Replacement->SetIdentity(Account, Character);
		Social->BindPlayerState(Replacement.Get());
		TestTrue(TEXT("Replacement keeps the actual existing party projection"), !Replacement->GetPartyId_Implementation().IsNone());
		TestEqual(TEXT("Each superseded writer is immediately fenced"), Social->CreateGuild(Bound).Failure, EProject_JSocialOperationFailure::StaleSession);
		Superseded.Add(MoveTemp(Replacement));
		Bound = Superseded.Last().Get();
	}
	Social->UnbindPlayerState(Bound);
	TestEqual(TEXT("Original writer stays revoked after all replacement logouts"), Social->CreateGuild(Old.Get()).Failure, EProject_JSocialOperationFailure::StaleSession);
	TestEqual(TEXT("Previously detached writer stays revoked through reconnect churn"), Social->CreateGuild(Current.Get()).Failure, EProject_JSocialOperationFailure::StaleSession);
	for (const auto& Previous : Superseded)
	{
		TestEqual(TEXT("Every retained old session remains fenced with no current binding"), Social->CreateGuild(Previous.Get()).Failure, EProject_JSocialOperationFailure::StaleSession);
	}
	Social->BindPlayerState(Old.Get());
	TestTrue(TEXT("Explicit rebinding reopens only the selected session after churn"), Social->LeaveParty(Old.Get()).bSucceeded);
	TestEqual(TEXT("Rebinding the original does not reopen its last replacement"), Social->CreateGuild(Bound).Failure, EProject_JSocialOperationFailure::StaleSession);

	FProject_JSocialGroupSnapshot Snapshot;
	TestFalse(TEXT("Invalid group kind never aliases Guild"), Social->GetGroupSnapshot(static_cast<EProject_JSocialGroupKind>(255), Party.GroupId, Snapshot));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGatewayBoundsTest, "ProjectJ.MMO.Architecture.GatewayBounds", MMOArchitectureTestFlags)
bool FProjectJGatewayBoundsTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));
	TStrongObjectPtr<UProject_JGatewaySubsystem> Gateway(NewObject<UProject_JGatewaySubsystem>(GI.Get()));
	Gateway->GatewayUrl = TEXT("http://127.0.0.1/"); Gateway->MaxRequestBytes = 4;
	Gateway->RequestTracker = MakeShared<ProjectJ::MMO::FRequestTracker, ESPMode::ThreadSafe>(1);
	FGuid Ticket; Gateway->RequestTracker->Begin(Ticket);
	int32 Replies = 0;
	for (const auto& Route : {TEXT("../admin"), TEXT("https://outside/path"), TEXT("//outside"), TEXT("%2e%2e/admin"), TEXT("valid?query"), TEXT("valid\r\nHeader")})
		Gateway->DispatchTrackedRequest(Route, TEXT("{}"), {}, [&](const auto& Result) {
			++Replies; TestEqual(TEXT("Route rejected before saturated admission"), Result.FailureKind, EProject_JBackendFailureKind::InvalidRequest);
		});
	Gateway->DispatchTrackedRequest(TEXT("valid"), TEXT("가나다"), {}, [&](const auto& Result) {
		++Replies; TestEqual(TEXT("UTF-8 byte size, not TCHAR count, limits request"), Result.FailureKind, EProject_JBackendFailureKind::InvalidRequest);
	});
	Gateway->DispatchTrackedRequest(TEXT("valid"), TEXT("1234"), {}, [&](const auto& Result) {
		++Replies; TestEqual(TEXT("Exact byte boundary reaches normal admission"), Result.FailureKind, EProject_JBackendFailureKind::Overloaded);
	});
	FProject_JBackendRequestContext Invalid; Invalid.Intent = static_cast<EProject_JBackendRequestIntent>(255);
	TestFalse(TEXT("Unknown intent cannot downgrade to query"), Invalid.IsValidForDispatch());
	TestEqual(TEXT("One completion per rejection"), Replies, 8);
	TestTrue(TEXT("Preflight creates no HTTP requests"), Gateway->ActiveRequests.IsEmpty());
	FProject_JGatewayResponseBuffer Buffer(4); ANSICHAR First[] = {'a','b'}, Second[] = {'c','d'}, Extra = 'e';
	int64 Length = 2; Buffer.Receive(First, Length); Length = 2; Buffer.Receive(Second, Length);
	FString Body; TestTrue(TEXT("Exact-size streamed response is accepted"), Buffer.Read(Body));
	TestEqual(TEXT("Chunked bytes combine once"), Body, FString(TEXT("abcd")));
	Length = 1; Buffer.Receive(&Extra, Length);
	TestEqual(TEXT("Excess chunk reports receive failure"), Length, int64(0));
	TestFalse(TEXT("Partial business payload is never returned as success"), Buffer.Read(Body));
	TestTrue(TEXT("Oversize body is discarded"), Body.IsEmpty());
	Gateway->Deinitialize();
	return true;
}


/** One coarse real producer/authority/UI workload; ordinary GAS and direct reset completion are distinct. */
class FProjectJPartyRosterExecutionCommand final : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    FArchitectureWorld Scope;
    TStrongObjectPtr<UGameInstance> GI;
    TStrongObjectPtr<UProject_JSocialSubsystem> Social;
    TStrongObjectPtr<UProject_JPlayerHUDWidget> HUD;
    AProject_JPlayerState* Leader = nullptr;
    AProject_JPlayerState* Peer = nullptr;
    AProject_JPlayerState* Replacement = nullptr;
    AProject_JPlayerState* Latest = nullptr;
    AProject_JPlayerState* Witness = nullptr;
    int32 ReentryMode = 0;
    bool bNestedSourceUpdateRan = false, bReentrantLeaveRan = false;
    AProject_JSpellbladeCharacter* Avatar = nullptr;
    UVerticalBox* Box = nullptr;
    FName Party;
    FGuid PeerId;
    FDelegateHandle HUDHandle, ReentryHandle, GASHandle, DefaultsHandle;
    int32 Phase = 0, WaitTicks = 0, GASResets = 0, DefaultsPublished = 0;
    uint64 Revision = 0;
    double Started = FPlatformTime::Seconds();
    const FProject_JPartyMemberSummary* ReadPeer(TArray<FProject_JPartyMemberSummary>& Rows) const
    {
        Rows = Leader->GetPartyRoster();
        return Rows.FindByPredicate([&](const auto& Row) { return Row.CharacterId == PeerId; });
    }
    bool WaitForHealth(int32 Permille)
    {
        TArray<FProject_JPartyMemberSummary> Rows; const auto* Row = ReadPeer(Rows);
        return Row && Row->bHealthKnown && Row->HealthPermille == Permille;
    }
public:
    explicit FProjectJPartyRosterExecutionCommand(FAutomationTestBase* InTest) : Test(InTest),
        GI(NewObject<UGameInstance>(GEngine)), Social(NewObject<UProject_JSocialSubsystem>(GI.Get()))
    {
        Scope.World->InitializeActorsForPlay(FURL());
        Scope.World->GetWorldSettings()->NotifyBeginPlay(); Scope.World->GetWorldSettings()->NotifyMatchStarted();
        Leader = Scope.World->SpawnActor<AProject_JPlayerState>(); Peer = Scope.World->SpawnActor<AProject_JPlayerState>();
        Leader->SetIdentity(FProject_JAccountId::NewId(), FProject_JCharacterId::NewId());
        Peer->SetIdentity(FProject_JAccountId::NewId(), FProject_JCharacterId::NewId()); PeerId = Peer->GetCharacterId().Value;
        Leader->SetPlayerName(TEXT("Leader")); Peer->SetPlayerName(TEXT("Peer"));
        Party = Social->CreateParty(Leader).GroupId;
        Test->TestTrue(TEXT("Peer enters the real authoritative party"), Social->JoinParty(Peer, Party).bSucceeded);
        Test->TestEqual(TEXT("Ordered party projection has two member values"), Leader->GetPartyRoster().Num(), 2);
        Leader->GetClass()->SetUpRuntimeReplicationData();
        TArray<FLifetimeProperty> Props; Leader->GetLifetimeReplicatedProps(Props);
        const auto* Roster = FindFProperty<FProperty>(Leader->GetClass(), TEXT("PartyRoster"));
        bool OwnerOnly = false;
        for (const auto& Prop : Props) if (Roster && Prop.RepIndex == Roster->RepIndex) OwnerOnly = Prop.Condition == COND_OwnerOnly;
        Test->TestTrue(TEXT("Actual lifetime registration keeps the roster owner-only"), OwnerOnly);
        auto* PC = Scope.World->SpawnActor<APlayerController>(); Peer->SetOwner(PC); PC->SetPlayerState(Peer);
        Avatar = Scope.World->SpawnActorDeferred<AProject_JSpellbladeCharacter>(AProject_JSpellbladeCharacter::StaticClass(),
            FTransform(FVector(0,0,500)), PC, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Avatar->SetPlayerState(Peer); Avatar->FinishSpawning(FTransform(FVector(0,0,500))); PC->Possess(Avatar);
        HUD.Reset(NewObject<UProject_JPlayerHUDWidget>(PC)); HUD->Initialize();
        Box = HUD->WidgetTree->ConstructWidget<UVerticalBox>();
        auto* PartyBox = FindFProperty<FObjectPropertyBase>(HUD->GetClass(), TEXT("PartyBox"));
        if (Test->TestNotNull(TEXT("Actual native party panel binding exists"), PartyBox)) PartyBox->SetObjectPropertyValue_InContainer(HUD.Get(), Box);
        HUDHandle = Leader->OnPartyRosterChanged.AddLambda([this] { HUD->PresentPartyRoster(Leader->GetPartyRoster()); });
        HUD->PresentPartyRoster(Leader->GetPartyRoster());
        Revision = Leader->GetPartyRosterRevision();
        auto* ASC = Peer->GetProjectJAbilitySystemComponent();
        for (float Value : {25.f,35.f,45.f}) ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), Value);
        Test->TestEqual(TEXT("A health burst does not synchronously fan out intermediate rows"), Leader->GetPartyRosterRevision(), Revision);
    }
    ~FProjectJPartyRosterExecutionCommand()
    {
        if (Leader) { Leader->OnPartyRosterChanged.Remove(HUDHandle); Leader->OnPartyRosterChanged.Remove(ReentryHandle); }
        if (Peer)
        {
            Peer->GetProjectJAbilitySystemComponent()->GetGameplayAttributeValueChangeDelegate(UProject_JAttributeSet::GetHealthAttribute()).Remove(GASHandle);
            Peer->GetProjectJAttributeSet()->OnInitializedResources.Remove(DefaultsHandle);
        }
        if (Social) Social->Deinitialize();
    }
    bool Update() override
    {
        if (FPlatformTime::Seconds() - Started > 12) { Test->AddError(TEXT("Party producer/consumer workload exceeded12s")); return true; }
        Scope.World->GetTimerManager().Tick(.11f);
        if (Phase == 0 && WaitForHealth(450))
        {
            Test->TestEqual(TEXT("Actual HUD consumes the two member rows"), HUD->GetPresentedPartyMemberCount(), 2);
            Revision = Leader->GetPartyRosterRevision();
            Peer->GetProjectJAbilitySystemComponent()->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(),45.001f);
            WaitTicks = 0; Phase = 1;
        }
        else if (Phase == 1 && ++WaitTicks > 3)
        {
            Test->TestEqual(TEXT("Unchanged quantized health does not dirty the roster"), Leader->GetPartyRosterRevision(), Revision);
            GASHandle = Peer->GetProjectJAbilitySystemComponent()->GetGameplayAttributeValueChangeDelegate(UProject_JAttributeSet::GetHealthAttribute()).AddLambda([this](const FOnAttributeChangeData&) { ++GASResets; });
            DefaultsHandle = Peer->GetProjectJAttributeSet()->OnInitializedResources.AddLambda([this] { ++DefaultsPublished; });
            Avatar->InitializeDefaultAttributes(true); // Actual protected producer, no test-only production setter.
            Test->TestEqual(TEXT("Direct Init reset bypasses ordinary GAS change notifications"), GASResets, 0);
            Test->TestEqual(TEXT("Completed direct producer emits exactly one explicit signal"), DefaultsPublished, 1);
            Phase = 2;
        }
        else if (Phase == 2 && WaitForHealth(1000))
        {
            Social->UnbindPlayerState(Peer);
            TArray<FProject_JPartyMemberSummary> Rows; const auto* Offline = ReadPeer(Rows);
            Test->TestTrue(TEXT("Unbind preserves ordered membership without stale resource data"), Offline && !Offline->bAvailableHere && !Offline->bHealthKnown && Offline->DisplayName.IsEmpty());
            Test->TestFalse(TEXT("Detached source releases direct-init observation"), Peer->GetProjectJAttributeSet()->OnInitializedResources.IsBoundToObject(Peer));
            Replacement = Scope.World->SpawnActor<AProject_JPlayerState>();
            Replacement->SetIdentity(Peer->GetAccountId(), Peer->GetCharacterId()); Replacement->SetPlayerName(TEXT("Replacement"));
            Social->BindPlayerState(Replacement);
            auto* ASC = Replacement->GetProjectJAbilitySystemComponent();
            ASC->AddAttributeSetSubobject(Replacement->GetProjectJAttributeSet());
            ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(), 200);
            ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 80);
            Peer->GetProjectJAbilitySystemComponent()->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 1);
            Test->TestEqual(TEXT("Retired session cannot mutate current party"), Social->LeaveParty(Peer).Failure, EProject_JSocialOperationFailure::StaleSession);
            Phase = 3;
        }
        else if (Phase == 3 && WaitForHealth(400))
        {
            Witness = Scope.World->SpawnActor<AProject_JPlayerState>();
            Witness->SetIdentity(FProject_JAccountId::NewId(), FProject_JCharacterId::NewId()); Witness->SetPlayerName(TEXT("Witness"));
            Test->TestTrue(TEXT("A third current recipient witnesses same-revision reentrancy"), Social->JoinParty(Witness, Party).bSucceeded);
            FProject_JSocialGroupSnapshot Group; Social->GetGroupSnapshot(EProject_JSocialGroupKind::Party, Party, Group);
            while (Group.Members.Num() < ProjectJMaxPartyRosterMembers) Group.Members.Add(FGuid::NewGuid());
            ++Group.Revision;
            Test->TestTrue(TEXT("Trusted ordered snapshot projects eight real/offline member values"), Social->RestoreGroupSnapshot(EProject_JSocialGroupKind::Party, Group));
            Test->TestEqual(TEXT("Actual native panel supports exactly eight rows"), HUD->GetPresentedPartyMemberCount(), 8);
            Test->TestEqual(TEXT("Panel owns a bounded eight reusable row widgets"), Box->GetChildrenCount(), 8);
            auto TooLarge = Group; ++TooLarge.Revision; TooLarge.Members.Add(FGuid::NewGuid());
            Test->TestFalse(TEXT("A ninth party member cannot silently truncate owner projection"), Social->RestoreGroupSnapshot(EProject_JSocialGroupKind::Party, TooLarge));
            ReentryHandle = Leader->OnPartyRosterChanged.AddLambda([this]
            {
                const int32 Action = ReentryMode; ReentryMode = 0;
                if (Action == 1) Social->UnbindPlayerState(Replacement);
                else if (Action == 2)
                {
                    Latest = Scope.World->SpawnActor<AProject_JPlayerState>();
                    Latest->SetIdentity(Replacement->GetAccountId(), Replacement->GetCharacterId()); Latest->SetPlayerName(TEXT("Newest"));
                    auto* ASC = Latest->GetProjectJAbilitySystemComponent(); ASC->AddAttributeSetSubobject(Latest->GetProjectJAttributeSet());
                    ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(), 200);
                    ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 40);
                    Social->BindPlayerState(Latest);
                }
                else if (Action == 3)
                {
                    bNestedSourceUpdateRan = true;
                    Latest->SetPlayerName(TEXT("After nested publication"));
                    Latest->GetProjectJAbilitySystemComponent()->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 80);
                    Social->BindPlayerState(Leader);
                }
                else if (Action == 4)
                {
                    bReentrantLeaveRan = true;
                    Test->TestTrue(TEXT("Local consumer may retire a member during publication"), Social->LeaveParty(Latest).bSucceeded);
                }
            });
            auto WitnessRow = [this]()
            {
                const auto Rows = Witness->GetPartyRoster();
                const auto* Row = Rows.FindByPredicate([&](const auto& Item) { return Item.CharacterId == PeerId; });
                return Row ? *Row : FProject_JPartyMemberSummary();
            };
            const int64 SameRevision = Group.Revision;
            Replacement->SetPlayerName(TEXT("Trigger unbind")); ReentryMode = 1; Social->BindPlayerState(Leader);
            Test->TestFalse(TEXT("A remaining recipient keeps nested offline state despite unchanged group revision"), WitnessRow().bAvailableHere);
            Social->GetGroupSnapshot(EProject_JSocialGroupKind::Party, Party, Group);
            Test->TestEqual(TEXT("The unbind test actually keeps membership revision unchanged"), Group.Revision, SameRevision);
            Social->BindPlayerState(Replacement);
            Replacement->SetPlayerName(TEXT("Trigger replacement")); ReentryMode = 2; Social->BindPlayerState(Leader);
            Test->TestEqual(TEXT("A remaining recipient keeps the nested replacement name"), WitnessRow().DisplayName, FString(TEXT("Newest")));
            Test->TestEqual(TEXT("A remaining recipient keeps the nested replacement health"), WitnessRow().HealthPermille, 200);
            Social->GetGroupSnapshot(EProject_JSocialGroupKind::Party, Party, Group);
            Test->TestEqual(TEXT("The replacement test actually keeps membership revision unchanged"), Group.Revision, SameRevision);
            if (!Latest) return true;
            Latest->SetPlayerName(TEXT("Before nested publication"));
            ReentryMode = 3; Phase = 4;
            Latest->GetProjectJAbilitySystemComponent()->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 120);
        }
        else if (Phase == 4 && bNestedSourceUpdateRan)
        {
            const auto Rows = Witness->GetPartyRoster();
            const auto* Row = Rows.FindByPredicate([&](const auto& Item) { return Item.CharacterId == PeerId; });
            Test->TestTrue(TEXT("Same-flush single-row fanout cannot overwrite newer metadata/health from a nested reconcile"),
                Row && Row->DisplayName == TEXT("After nested publication") && Row->HealthPermille == 400);
            ReentryMode = 4; Phase = 5;
            Latest->GetProjectJAbilitySystemComponent()->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 60);
        }
        else if (Phase == 5 && bReentrantLeaveRan)
        {
            Test->TestTrue(TEXT("Removed member owner roster stays cleared after reentrant fanout"), Latest->GetPartyRoster().IsEmpty());
            Test->TestEqual(TEXT("The newer seven-member roster is not overwritten"), Leader->GetPartyRoster().Num(), 7);
            Test->TestEqual(TEXT("Health/delta updates reuse the bounded original row widgets"), Box->GetChildrenCount(), 8);
            Test->TestEqual(TEXT("Native panel consumes the final current roster"), HUD->GetPresentedPartyMemberCount(), 7);
            Revision = Leader->GetPartyRosterRevision();
            Latest->GetProjectJAbilitySystemComponent()->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 50);
            Social->Deinitialize();
            WaitTicks = 0; Phase = 6;
        }
        else if (Phase == 6 && ++WaitTicks > 3)
        {
            Test->TestEqual(TEXT("Released source/timer cannot publish after leave and subsystem retirement"), Leader->GetPartyRosterRevision(), Revision);
            HUD->PresentPartyRoster({});
            Test->TestEqual(TEXT("Clearing the consumer collapses its party presentation"), HUD->GetPresentedPartyMemberCount(), 0);
            return true;
        }
        return false;
    }
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPartyRosterExecutionTest, "ProjectJ.MMO.Social.PartyRosterExecution", MMOArchitectureTestFlags)
bool FProjectJPartyRosterExecutionTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FProjectJPartyRosterExecutionCommand(this));
    return true;
}

#endif
