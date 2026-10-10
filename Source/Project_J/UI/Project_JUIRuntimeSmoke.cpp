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
		auto Original = Inventory->AddItemDefinition(Potion, 10);
		InventoryModel->Refresh();
		auto *Entry = InventoryModel->InventoryEntries.FindByPredicate([&](const auto &E) { return E->Item.InstanceId == Original.InstanceId; });
		bSuccess &= Entry && InventoryModel->Split(Entry->Get(), 4);
		InventoryModel->Refresh();
		int32 Total = 0;
		for (const auto &Item : Inventory->GetItemInstances()) if (Item.ItemDef == Potion) Total += Item.StackCount;
		bSuccess &= Total == 10;
		FText Reason;
		bSuccess &= RebindKey(0, EKeys::F5, Reason);
		bSuccess &= PC->GetLocalPlayer()->GetSubsystem<UProject_JUILayoutSettings>()->ReadKeys().Keys[0] == EKeys::F5;
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
