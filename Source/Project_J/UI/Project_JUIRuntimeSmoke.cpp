#include "UI/Project_JPlayerUIComponent.h"
#include "UI/Project_JPlayerHUDWidget.h"
#include "UI/Project_JInventoryViewModel.h"
#include "UI/Project_JUILayoutSettings.h"
#include "UI/Project_JStatusEffects.h"
#include "UI/Project_JMinimapWidget.h"
#include "Game/Project_JPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "Inventory/Project_JConsumableDefinition.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "HAL/PlatformProperties.h"
#include "HAL/PlatformMisc.h"
#include "GameplayEffect.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Kismet/GameplayStatics.h"
#include "ProfilingDebugging/CsvProfiler.h"
CSV_DECLARE_CATEGORY_EXTERN(ProjectJUI);

void UProject_JPlayerUIComponent::PrepareRuntimeProfile()
{
#if !UE_BUILD_SHIPPING
	auto *PC = Cast<APlayerController>(GetOwner());
	if (!PC || !PC->IsLocalPlayerController() || !PC->HasAuthority() || GetNetMode() != NM_Standalone) return;
	RefreshSources();
	const bool bHeavy = FParse::Param(FCommandLine::Get(), TEXT("ProjectJUIWorkload"));
	if (bHeavy && BoundState.IsValid() && Screen && BoundASC.IsValid())
	{
		auto *Potion = LoadObject<UProject_JConsumableDefinition>(nullptr, TEXT("/Game/UI/Gameplay/DA_ProjectJHealthPotion.DA_ProjectJHealthPotion"));
		auto *Inventory = BoundState->GetInventoryComponent();
		if (Potion && Inventory)
			for (int32 Index = 0; Index < 1000; ++Index) Inventory->AddItemDefinition(Potion, 1);
		InventoryModel->Refresh();
		for (int32 Index = 0; Index < 24; ++Index)
		{
			auto *Effect = NewObject<UGameplayEffect>();
			Effect->DurationPolicy = EGameplayEffectDurationType::HasDuration;
			Effect->DurationMagnitude = FScalableFloat(30.f);
			auto &UI = Effect->FindOrAddComponent<UProject_JStatusEffectUIData>();
			UI.DisplayName = FText::Format(NSLOCTEXT("ProjectJUI", "LoadStatus", "검증 상태 {0}"), FText::AsNumber(Index + 1));
			BoundASC->ApplyGameplayEffectToSelf(Effect, 1, BoundASC->MakeEffectContext());
		}
		StatusEffects->Refresh();
		// Bind all ten item slots to exercise aggregated quantity and cooldown presentation.
		if (Potion)
			for (auto &Slot : HUDPreferences.Slots) { Slot.Kind = EProject_JQuickSlotKind::Item; Slot.ItemId = Potion->ItemId; Slot.InputTag = {}; }
		UpdateSkills();
		for (FName Window : {FName(TEXT("Bag")), FName(TEXT("Equipment")), FName(TEXT("Quests")), FName(TEXT("Settings"))}) ToggleWindow(Window);
		UE_LOG(LogTemp, Display, TEXT("PROJECT_J_UI_WORKLOAD rows=%d buffs=%d windows=4 slots=10"), InventoryModel->InventoryEntries.Num(), StatusEffects->States.Num());
	}
	// Both baseline and heavy cases start at the same point after startup. Capture setup spikes separately in logs.
	PC->ConsoleCommand(TEXT("csvprofile FRAMES=240"), false);
	GetWorld()->GetTimerManager().SetTimer(RuntimeSmokeTimer, this, &ThisClass::RunRuntimeSmoke, 8.f, false);
#endif
}

void UProject_JPlayerUIComponent::RunRuntimeSmoke()
{
#if !UE_BUILD_SHIPPING
	auto *PC = Cast<APlayerController>(GetOwner());
	if (!PC || !PC->IsLocalPlayerController() || !PC->HasAuthority() || GetNetMode() != NM_Standalone) return;
	RefreshSources();
	bool bSuccess = FPlatformProperties::RequiresCookedData() && Screen && Screen->ValidateRuntimeLayout() && BoundState.IsValid();
	auto *Potion = LoadObject<UProject_JConsumableDefinition>(nullptr, TEXT("/Game/UI/Gameplay/DA_ProjectJHealthPotion.DA_ProjectJHealthPotion"));
	bSuccess &= Potion && LoadClass<UProject_JItemWidget>(nullptr, TEXT("/Game/UI/WBP_ProjectJItemTile.WBP_ProjectJItemTile_C")) &&
		LoadObject<UProject_JMapDefinition>(nullptr, TEXT("/Game/UI/DA_ProjectJMap.DA_ProjectJMap"));
	if (bSuccess)
	{
		auto *Inventory = BoundState->GetInventoryComponent();
		int32 Before = 0;
		for (const auto &Item : Inventory->GetItemInstances()) if (Item.ItemDef == Potion) Before += Item.StackCount;
		auto Original = Inventory->AddItemDefinition(Potion, 10);
		InventoryModel->Refresh();
		auto *Entry = InventoryModel->InventoryEntries.FindByPredicate([&](const auto &E) { return E->Item.InstanceId == Original.InstanceId; });
		bSuccess &= Entry && InventoryModel->Split(Entry->Get(), 4);
		InventoryModel->Refresh();
		int32 Total = 0;
		for (const auto &Item : Inventory->GetItemInstances()) if (Item.ItemDef == Potion) Total += Item.StackCount;
		bSuccess &= Total == Before + 10;
		FText Reason;
		bSuccess &= RebindKey(0, EKeys::F5, Reason);
		auto *Settings = PC->GetLocalPlayer()->GetSubsystem<UProject_JUILayoutSettings>();
		bSuccess &= Settings->ReadKeys().Keys[0] == EKeys::F5 && Settings->FlushNow();
		auto *DiskSettings = Cast<UProject_JUILayoutSave>(UGameplayStatics::LoadGameFromSlot(
			FString::Printf(TEXT("ProjectJ_UI_Player_%d"), PC->GetLocalPlayer()->GetControllerId()), 0));
		bSuccess &= DiskSettings && DiskSettings->InputKeys.Keys[0] == EKeys::F5;
		ResetKeys();
		ToggleWindow(TEXT("Bag"));
		ToggleWindow(TEXT("Equipment"));
		bSuccess &= IsMenuOpen();
		SetMenuOpen(false);
		bSuccess &= !IsMenuOpen();
		auto *Effect = NewObject<UGameplayEffect>();
		Effect->DurationPolicy = EGameplayEffectDurationType::HasDuration;
		Effect->DurationMagnitude = FScalableFloat(3.f);
		auto &UI = Effect->FindOrAddComponent<UProject_JStatusEffectUIData>();
		UI.DisplayName = NSLOCTEXT("ProjectJUI", "SmokeStatus", "상태 검증");
		const auto Handle = BoundASC->ApplyGameplayEffectToSelf(Effect, 1, BoundASC->MakeEffectContext());
		StatusEffects->Refresh();
		bSuccess &= StatusEffects->States.ContainsByPredicate([&](const auto &State) { return State.Handle == Handle; });
		BoundASC->RemoveActiveGameplayEffect(Handle);
		StatusEffects->Refresh();
		bSuccess &= !StatusEffects->States.ContainsByPredicate([&](const auto &State) { return State.Handle == Handle; });
	}
	UE_LOG(LogTemp, Display, TEXT("PROJECT_J_UI_RUNTIME_SMOKE success=%d cooked=%d"), bSuccess, FPlatformProperties::RequiresCookedData());
	FPlatformMisc::RequestExitWithStatus(false, bSuccess ? 0 : 1);
#endif
}
