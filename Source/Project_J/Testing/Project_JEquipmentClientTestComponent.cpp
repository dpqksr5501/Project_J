// Copyright Project J. All Rights Reserved.
#include "Testing/Project_JEquipmentClientTestComponent.h"

#include "AbilitySystemComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JInventoryComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Game/Project_JPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJEquipmentClientTest, Log, All);

namespace
{
const FSoftObjectPath TestItemPath(TEXT("/Game/DataAssetSets/Animation_Profiles/Equip/DA_Greatsword_Equip.DA_Greatsword_Equip"));
}

UProject_JEquipmentClientTestComponent::UProject_JEquipmentClientTestComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

bool UProject_JEquipmentClientTestComponent::IsTestWorld() const
{
#if WITH_EDITOR && !UE_BUILD_SHIPPING
	return GetWorld() && GetWorld()->WorldType == EWorldType::PIE && !GetWorld()->bIsTearingDown;
#else
	return false;
#endif
}

AProject_JPlayerState* UProject_JEquipmentClientTestComponent::GetTestPlayerState() const
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	return PC ? PC->GetPlayerState<AProject_JPlayerState>() : nullptr;
}

void UProject_JEquipmentClientTestComponent::Execute(const FString& Action)
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!IsTestWorld() || !PC || !PC->IsLocalController())
	{
		UE_LOG(LogProjectJEquipmentClientTest, Display, TEXT("Rejected: run EquipmentClientTest in the owning PIE player console."));
		return;
	}
	if (Action.Equals(TEXT("prepare"), ESearchCase::IgnoreCase)) { ServerPrepare(); return; }
	if (Action.Equals(TEXT("stop"), ESearchCase::IgnoreCase)) { ServerStop(); return; }
	if (Action.Equals(TEXT("dump"), ESearchCase::IgnoreCase))
	{
		LogState(TEXT("LocalSnapshot"));
		ServerInspect();
		return;
	}
	AProject_JPlayerState* PS = GetTestPlayerState();
	UProject_JInventoryComponent* Inventory = PS ? PS->GetInventoryComponent() : nullptr;
	UProject_JEquipmentManagerComponent* Equipment = PS ? PS->GetEquipmentManagerComponent() : nullptr;
	FProject_JItemInstanceData Item;
	if (!Inventory || !Equipment || !ClientItemId.IsValid() || !Inventory->FindItemInstance(ClientItemId, Item))
	{
		UE_LOG(LogProjectJEquipmentClientTest, Display, TEXT("Not ready: prepare first, then wait for the inventory ID to replicate. Use dump."));
		return;
	}
	if (Action.Equals(TEXT("equip"), ESearchCase::IgnoreCase))
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_EquipmentClientTest_RequestEquip);
		TRACE_BOOKMARK(TEXT("EquipmentClientTest RequestEquip ID=%s"), *ClientItemId.ToString());
		Equipment->RequestEquipItemInstanceById(ClientItemId);
		LogState(TEXT("EquipRequested_NotAcknowledged"));
	}
	else if (Action.Equals(TEXT("unequip"), ESearchCase::IgnoreCase))
	{
		// Do not intentionally remove another item when the user changed gear outside this fixture.
		if (!Item.bIsEquipped) { LogState(TEXT("TestItemNotEquipped")); return; }
		TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_EquipmentClientTest_RequestUnequip);
		TRACE_BOOKMARK(TEXT("EquipmentClientTest RequestUnequip ID=%s"), *ClientItemId.ToString());
		Equipment->RequestUnequipSlot(EProject_JEquipmentSlot::Weapon);
		LogState(TEXT("UnequipRequested_NotAcknowledged"));
	}
	else
	{
		UE_LOG(LogProjectJEquipmentClientTest, Display, TEXT("Usage: EquipmentClientTest prepare|equip|unequip|dump|stop"));
	}
}

void UProject_JEquipmentClientTestComponent::ServerPrepare_Implementation()
{
	if (!IsTestWorld() || !GetOwner()->HasAuthority()) { return; }
	const double Now = FPlatformTime::Seconds();
	if (Now - LastPrepareTime < 1.0) { return; }
	LastPrepareTime = Now;
	if (CreatedItemId.IsValid()) { ClientPrepared(CreatedItemId); return; }
	if (bPreparing) { return; }
	AProject_JPlayerState* PS = GetTestPlayerState();
	if (!PS || !PS->GetInventoryComponent()) { LogState(TEXT("PrepareRejected_NoPlayerState")); return; }
	CreatedFor = PS;
	bPreparing = true;
	const uint32 RequestGeneration = ++Generation;
	// One fixed asset, one outstanding request and one fixture item per controller. No client asset paths.
	GetWorld()->GetTimerManager().SetTimer(ExpiryTimer, this, &ThisClass::Cleanup, 300.0f, false);
	LoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(TestItemPath,
		FStreamableDelegate::CreateWeakLambda(this, [this, RequestGeneration]()
		{
			if (RequestGeneration != Generation || !IsTestWorld()) { return; }
			bPreparing = false;
			AProject_JPlayerState* LoadedPS = CreatedFor.Get();
			auto* Definition = Cast<UProject_JEquipmentItemDefinition>(TestItemPath.ResolveObject());
			if (!Definition || !LoadedPS || LoadedPS != GetTestPlayerState() || !LoadedPS->GetInventoryComponent())
			{
				UE_LOG(LogProjectJEquipmentClientTest, Warning, TEXT("Prepare failed: asset or PlayerState unavailable."));
				Cleanup();
				return;
			}
			TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_EquipmentClientTest_ServerPrepare);
			CreatedItemId = LoadedPS->GetInventoryComponent()->AddItemDefinition(Definition).InstanceId;
			LoadedPS->ForceNetUpdate();
			LogState(TEXT("ServerPrepared"));
			ClientPrepared(CreatedItemId);
			LoadHandle.Reset();
		}));
	if (!LoadHandle) { Cleanup(); }
}

void UProject_JEquipmentClientTestComponent::ClientPrepared_Implementation(FGuid InstanceId)
{
	if (!IsTestWorld()) { return; }
	ClientItemId = InstanceId;
	LogState(InstanceId.IsValid() ? TEXT("IDReceived_InventoryMayStillBePending") : TEXT("FixtureStopped"));
}

void UProject_JEquipmentClientTestComponent::ServerInspect_Implementation()
{
	if (!IsTestWorld()) { return; }
	const double Now = FPlatformTime::Seconds();
	if (Now - LastInspectTime < 0.5) { return; }
	LastInspectTime = Now;
	LogState(TEXT("ServerSnapshot"));
}

void UProject_JEquipmentClientTestComponent::ServerStop_Implementation()
{
	if (IsTestWorld()) { Cleanup(); }
}

void UProject_JEquipmentClientTestComponent::Cleanup()
{
	++Generation;
	bPreparing = false;
	if (LoadHandle) { LoadHandle->CancelHandle(); LoadHandle.Reset(); }
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(ExpiryTimer); }
	if (!GetOwner() || !GetOwner()->HasAuthority()) { return; }
	if (IsTestWorld())
	{
		AProject_JPlayerState* PS = CreatedFor.Get();
		UProject_JInventoryComponent* Inventory = PS ? PS->GetInventoryComponent() : nullptr;
		FProject_JItemInstanceData Item;
		if (Inventory && Inventory->FindItemInstance(CreatedItemId, Item))
		{
			if (Item.bIsEquipped && PS->GetEquipmentManagerComponent())
			{
				PS->GetEquipmentManagerComponent()->UnequipSlot(EProject_JEquipmentSlot::Weapon);
			}
			if (!Inventory->RemoveItemInstance(CreatedItemId))
			{
				UE_LOG(LogProjectJEquipmentClientTest, Warning, TEXT("Cleanup could not remove test ID=%s; retaining identity for manual stop retry."), *CreatedItemId.ToString());
				return;
			}
			PS->ForceNetUpdate();
		}
		ClientPrepared(FGuid());
	}
	CreatedItemId.Invalidate();
	CreatedFor.Reset();
}

void UProject_JEquipmentClientTestComponent::LogState(const TCHAR* Stage) const
{
	const AProject_JPlayerState* PS = GetTestPlayerState();
	const UProject_JInventoryComponent* Inventory = PS ? PS->GetInventoryComponent() : nullptr;
	const UProject_JEquipmentManagerComponent* Equipment = PS ? PS->GetEquipmentManagerComponent() : nullptr;
	const FGuid Id = GetOwner()->HasAuthority() ? CreatedItemId : ClientItemId;
	FProject_JItemInstanceData Item;
	const bool bFound = Inventory && Inventory->FindItemInstance(Id, Item);
	FGameplayTagContainer Tags;
	if (PS && PS->GetAbilitySystemComponent()) { PS->GetAbilitySystemComponent()->GetOwnedGameplayTags(Tags); }
	UE_LOG(LogProjectJEquipmentClientTest, Display,
		TEXT("%s World=%s NetMode=%d Authority=%d Owner=%s ID=%s Found=%d Equipped=%d Locked=%d Equipment={%s} Deltas={%s} Tags={%s}"),
		Stage, *GetNameSafe(GetWorld()), int32(GetNetMode()), GetOwner()->HasAuthority(), *GetNameSafe(GetOwner()),
		*Id.ToString(), bFound, Item.bIsEquipped, Item.bIsLocked,
		Equipment ? *Equipment->GetReplicationDiagnosticSummary() : TEXT("None"),
		Equipment ? *Equipment->GetReplicationDiagnosticDeltaSummary() : TEXT("None"), *Tags.ToStringSimple());
	TRACE_BOOKMARK(TEXT("EquipmentClientTest %s NetMode=%d ID=%s Found=%d Equipped=%d"), Stage, int32(GetNetMode()), *Id.ToString(), bFound, Item.bIsEquipped);
}

void UProject_JEquipmentClientTestComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Cleanup();
	Super::EndPlay(Reason);
}

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJEquipmentClientFixtureTest, "ProjectJ.EquipmentClient.FixtureIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJEquipmentClientFixtureTest::RunTest(const FString&)
{
	// Local lifecycle/ownership regression only; no net driver or RPC transport is exercised here.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	auto* PC = World->SpawnActor<APlayerController>();
	auto* PS = World->SpawnActor<AProject_JPlayerState>();
	PC->SetPlayerState(PS);
	auto* Fixture = NewObject<UProject_JEquipmentClientTestComponent>(PC);
	PC->AddInstanceComponent(Fixture);
	Fixture->RegisterComponent();
	TestFalse(TEXT("Non-PIE world cannot grant fixture items"), Fixture->IsTestWorld());
	Fixture->ServerPrepare_Implementation();
	TestFalse(TEXT("Rejected prepare does not schedule loading"), Fixture->bPreparing);
	World->WorldType = EWorldType::PIE;
	TestTrue(TEXT("PIE fixture is opt-in eligible"), Fixture->IsTestWorld());
	auto* Inventory = PS->GetInventoryComponent();
	auto* Equipment = PS->GetEquipmentManagerComponent();
	auto* Definition = NewObject<UProject_JEquipmentItemDefinition>(World);
	const FGuid TestId = Inventory->AddItemDefinition(Definition).InstanceId;
	const FGuid UserId = Inventory->AddItemDefinition(Definition).InstanceId;
	TestTrue(TEXT("Two distinct inventory identities created"), TestId.IsValid() && UserId.IsValid() && TestId != UserId);
	Fixture->CreatedFor = PS;
	Fixture->CreatedItemId = TestId;
	TestTrue(TEXT("Fixture item can be equipped through authoritative inventory lookup"), Equipment->TryEquipItemInstanceById(TestId).bSucceeded);
	TestTrue(TEXT("User can replace it with another instance of the same definition"), Equipment->TryEquipItemInstanceById(UserId).bSucceeded);
	Fixture->Cleanup();
	FProject_JItemInstanceData UserItem;
	TestFalse(TEXT("Cleanup removes only the fixture identity"), Inventory->HasItemInstance(TestId));
	TestTrue(TEXT("Unrelated inventory identity survives"), Inventory->FindItemInstance(UserId, UserItem));
	TestTrue(TEXT("Unrelated equipped weapon stays equipped"), UserItem.bIsEquipped);
	TestFalse(TEXT("Fixture releases its identity"), Fixture->CreatedItemId.IsValid());

	const FGuid EquippedTestId = Inventory->AddItemDefinition(Definition).InstanceId;
	Fixture->CreatedFor = PS;
	Fixture->CreatedItemId = EquippedTestId;
	TestTrue(TEXT("Second fixture equip succeeds"), Equipment->TryEquipItemInstanceById(EquippedTestId).bSucceeded);
	Fixture->Cleanup();
	TestFalse(TEXT("Equipped fixture is unlocked and removed on stop"), Inventory->HasItemInstance(EquippedTestId));
	TestTrue(TEXT("Prior user item remains in inventory"), Inventory->HasItemInstance(UserId));
	Fixture->ServerPrepare_Implementation();
	TestTrue(TEXT("Prepare schedules a bounded asynchronous load"), Fixture->bPreparing);
	const uint32 RequestGeneration = Fixture->Generation;
	Fixture->Cleanup();
	TestTrue(TEXT("Stop invalidates pending completion generation"), Fixture->Generation > RequestGeneration);
	TestFalse(TEXT("Stop clears pending load"), Fixture->bPreparing || Fixture->LoadHandle.IsValid());
	TestFalse(TEXT("Stop clears expiry timer"), World->GetTimerManager().TimerExists(Fixture->ExpiryTimer));
	World->bIsTearingDown = true;
	TestFalse(TEXT("Teardown rejects prepare"), Fixture->IsTestWorld());
	PC->SetPlayerState(nullptr);
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}
#endif
