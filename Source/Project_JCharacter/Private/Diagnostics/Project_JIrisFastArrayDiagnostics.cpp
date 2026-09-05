#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JInventoryComponent.h"

#if !UE_BUILD_SHIPPING

#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJIrisFastArray, Log, All);

namespace Project_J::Diagnostics
{
	static void ForEachIrisFastArrayComponent(TFunctionRef<void(UWorld*, APlayerState*, UProject_JInventoryComponent*, UProject_JEquipmentManagerComponent*)> Visitor)
	{
		if (!GEngine)
		{
			return;
		}

		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			UWorld* World = WorldContext.World();
			if (!World || !World->IsGameWorld())
			{
				continue;
			}

			for (TActorIterator<APlayerState> PlayerStateIt(World); PlayerStateIt; ++PlayerStateIt)
			{
				APlayerState* PlayerState = *PlayerStateIt;
				UProject_JInventoryComponent* Inventory = PlayerState->FindComponentByClass<UProject_JInventoryComponent>();
				UProject_JEquipmentManagerComponent* Equipment = PlayerState->FindComponentByClass<UProject_JEquipmentManagerComponent>();
				if (!Inventory && !Equipment)
				{
					continue;
				}

				Visitor(World, PlayerState, Inventory, Equipment);
			}
		}
	}

	static void DumpIrisFastArrayState()
	{
		if (!GEngine)
		{
			UE_LOG(LogProjectJIrisFastArray, Warning, TEXT("IrisFastArray: GEngine is unavailable."));
			return;
		}

		int32 WorldCount = 0;
		int32 ComponentOwnerCount = 0;
		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			UWorld* World = WorldContext.World();
			if (World && World->IsGameWorld())
			{
				++WorldCount;
			}
		}

		ForEachIrisFastArrayComponent([&ComponentOwnerCount](UWorld* World, APlayerState* PlayerState, UProject_JInventoryComponent* Inventory, UProject_JEquipmentManagerComponent* Equipment)
		{
			++ComponentOwnerCount;
				UE_LOG(
					LogProjectJIrisFastArray,
					Display,
					TEXT("IrisFastArray World=%s NetMode=%d PlayerState=%s PlayerId=%d Inventory={%s} Equipment={%s}"),
					*GetNameSafe(World),
					static_cast<int32>(World->GetNetMode()),
					*GetNameSafe(PlayerState),
					PlayerState->GetPlayerId(),
					Inventory ? *Inventory->GetReplicationDiagnosticSummary() : TEXT("Absent"),
					Equipment ? *Equipment->GetReplicationDiagnosticSummary() : TEXT("Absent"));
		});

		UE_LOG(
			LogProjectJIrisFastArray,
			Display,
			TEXT("IrisFastArray complete: GameWorlds=%d ComponentOwners=%d. Inventory is OwnerOnly by policy; compare its server row only with the owning client row. Equipment is visible to all relevant PlayerStates."),
			WorldCount,
			ComponentOwnerCount);
	}

	static void ResetIrisFastArrayDeltaEvents()
	{
		int32 ResetComponentCount = 0;
		ForEachIrisFastArrayComponent([&ResetComponentCount](UWorld*, APlayerState*, UProject_JInventoryComponent* Inventory, UProject_JEquipmentManagerComponent* Equipment)
		{
			if (Inventory)
			{
				Inventory->ResetReplicationDiagnosticDeltaCounters();
				++ResetComponentCount;
			}
			if (Equipment)
			{
				Equipment->ResetReplicationDiagnosticDeltaCounters();
				++ResetComponentCount;
			}
		});
		UE_LOG(LogProjectJIrisFastArray, Display, TEXT("IrisFastArray delta event counters reset: Components=%d"), ResetComponentCount);
	}

	static void DumpIrisFastArrayDeltaEvents()
	{
		int32 ComponentOwnerCount = 0;
		ForEachIrisFastArrayComponent([&ComponentOwnerCount](UWorld* World, APlayerState* PlayerState, UProject_JInventoryComponent* Inventory, UProject_JEquipmentManagerComponent* Equipment)
		{
			++ComponentOwnerCount;
			UE_LOG(
				LogProjectJIrisFastArray,
				Display,
				TEXT("IrisFastArrayDelta World=%s NetMode=%d PlayerId=%d Inventory={%s} Equipment={%s}"),
				*GetNameSafe(World),
				static_cast<int32>(World->GetNetMode()),
				PlayerState->GetPlayerId(),
				Inventory ? *Inventory->GetReplicationDiagnosticDeltaSummary() : TEXT("Absent"),
				Equipment ? *Equipment->GetReplicationDiagnosticDeltaSummary() : TEXT("Absent"));
		});
		UE_LOG(LogProjectJIrisFastArray, Display, TEXT("IrisFastArray delta event dump complete: ComponentOwners=%d. Counters are client callback observations after the most recent reset."), ComponentOwnerCount);
	}

	static FAutoConsoleCommand DumpIrisFastArrayStateCommand(
		TEXT("ProjectJ.DumpIrisFastArrayState"),
		TEXT("Development-only: logs Inventory/Equipment FastArray state fingerprints for every PIE game world."),
		FConsoleCommandDelegate::CreateStatic(&DumpIrisFastArrayState),
		ECVF_Default);

	static FAutoConsoleCommand ResetIrisFastArrayDeltaEventsCommand(
		TEXT("ProjectJ.ResetIrisFastArrayDeltaEvents"),
		TEXT("Development-only: clears locally observed Iris FastArray add/change/remove callback counters in every PIE game world."),
		FConsoleCommandDelegate::CreateStatic(&ResetIrisFastArrayDeltaEvents),
		ECVF_Default);

	static FAutoConsoleCommand DumpIrisFastArrayDeltaEventsCommand(
		TEXT("ProjectJ.DumpIrisFastArrayDeltaEvents"),
		TEXT("Development-only: logs locally observed Iris FastArray add/change/remove callback counters in every PIE game world."),
		FConsoleCommandDelegate::CreateStatic(&DumpIrisFastArrayDeltaEvents),
		ECVF_Default);
}

#endif
