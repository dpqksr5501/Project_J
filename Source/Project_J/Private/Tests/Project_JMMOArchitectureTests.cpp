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
	TestEqual(TEXT("One observer projection"), Manager->PublicEquipmentArray.Items.Num(), 1);
	TestEqual(TEXT("Projection references same definition"), Manager->PublicEquipmentArray.Items[0].ItemDef.Get(), Definition);
	TestEqual(TEXT("Projection contains no item instance field"), FindFProperty<FProperty>(FProject_JPublicEquipmentItem::StaticStruct(), TEXT("ItemInstance")), static_cast<FProperty*>(nullptr));
	// Standalone test worlds have no NetDriver to initialize the class replication indices.
	Manager->GetClass()->SetUpRuntimeReplicationData();
	TArray<FLifetimeProperty> Props; Manager->GetLifetimeReplicatedProps(Props);
	const auto* PrivateProp = FindFProperty<FProperty>(Manager->GetClass(), TEXT("EquipmentArray"));
	const auto* PublicProp = FindFProperty<FProperty>(Manager->GetClass(), TEXT("PublicEquipmentArray"));
	bool PrivateOwnerOnly = false, PublicSkipOwner = false;
	for (const auto& Prop : Props)
	{
		if (Prop.RepIndex == PrivateProp->RepIndex) PrivateOwnerOnly = Prop.Condition == COND_OwnerOnly;
		if (Prop.RepIndex == PublicProp->RepIndex) PublicSkipOwner = Prop.Condition == COND_SkipOwner;
	}
	TestTrue(TEXT("Actual lifetime registration keeps private data owner-only"), PrivateOwnerOnly);
	TestTrue(TEXT("Owner receives one equipment event stream"), PublicSkipOwner);
	Manager->UnequipSlot(EProject_JEquipmentSlot::Weapon);
	TestTrue(TEXT("Unequip clears derived and authoritative slots together"), Manager->EquipmentArray.Items.IsEmpty() && Manager->PublicEquipmentArray.Items.IsEmpty());
	Manager->EquipItem(Definition);
	TestEqual(TEXT("Prototype path also publishes observer projection"), Manager->PublicEquipmentArray.Items.Num(), 1);
	Manager->DestroyComponent();
	TestNull(TEXT("Retirement detaches projection callbacks"), Manager->PublicEquipmentArray.OwnerComponent.Get());
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
	Social->BindPlayerState(Current.Get());
	TestTrue(TEXT("Replacement receives existing membership"), Current->GetPartyId_Implementation() == Party.GroupId);
	TestTrue(TEXT("Previous projection is revoked"), Old->GetPartyId_Implementation().IsNone());
	TestEqual(TEXT("Superseded session cannot leave"), Social->LeaveParty(Old.Get()).Failure, EProject_JSocialOperationFailure::StaleSession);
	TestEqual(TEXT("Superseded session cannot create another group"), Social->CreateGuild(Old.Get()).Failure, EProject_JSocialOperationFailure::StaleSession);
	TestFalse(TEXT("Trusted restore does not implicitly steal an active binding"), Social->RestoreMembership(Old.Get(), Party.GroupId, NAME_None));
	Social->UnbindPlayerState(Old.Get());
	TestTrue(TEXT("Late old-session unbind preserves replacement"), Social->LeaveParty(Current.Get()).bSucceeded);
	Social->UnbindPlayerState(Current.Get());
	TestEqual(TEXT("Logout of replacement cannot resurrect old writer"), Social->CreateParty(Old.Get()).Failure, EProject_JSocialOperationFailure::StaleSession);
	Social->BindPlayerState(Old.Get());
	TestTrue(TEXT("Explicit server binding can establish a new session"), Social->CreateParty(Old.Get()).bSucceeded);
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
#endif
