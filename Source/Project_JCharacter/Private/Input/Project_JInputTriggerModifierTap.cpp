#include "Input/Project_JInputTriggerModifierTap.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"

UProject_JInputTriggerModifierTap::UProject_JInputTriggerModifierTap()
{
	bShouldAlwaysTick = true;
}

ETriggerState UProject_JInputTriggerModifierTap::UpdateState_Implementation(
	const UEnhancedPlayerInput* PlayerInput, FInputActionValue Value, float)
{
	if (!PlayerInput || !ModifierKey.IsModifierKey())
	{
		bArmed = bChord = false;
		return ETriggerState::None;
	}
	if (const FKeyState* State = PlayerInput->GetKeyState(ModifierKey); State && State->bWasJustFlushed)
	{
		bArmed = bChord = false;
		return ETriggerState::None;
	}
	const auto* PC = Cast<APlayerController>(PlayerInput->GetOuter());
	const auto* Local = PC ? PC->GetLocalPlayer() : nullptr;
	if (Local && Local->ViewportClient && Local->ViewportClient->Viewport && !Local->ViewportClient->Viewport->HasFocus())
	{
		bArmed = bChord = false;
		return ETriggerState::None;
	}
	if (IsActuated(Value) && PlayerInput->WasJustPressed(ModifierKey))
	{
		bArmed = true;
		bChord = false;
	}
	if (bArmed)
	{
		// Public key-state APIs avoid depending on the protected key-state map.
		// Existing held movement and keyboard repeat are not a newly pressed chord.
		static const TArray<FKey> DigitalKeys = []
		{
			TArray<FKey> Keys; EKeys::GetAllKeys(Keys);
			Keys.RemoveAll([](const FKey& Key){ return Key == EKeys::AnyKey || !Key.IsValid() || Key.IsAnalog() || Key.IsGamepadKey(); });
			return Keys;
		}();
		for (const FKey& Key : DigitalKeys)
			if (Key != ModifierKey && (PlayerInput->WasJustPressed(Key) || (Key.IsModifierKey() && PlayerInput->IsPressed(Key))))
				bChord = true;
	}
	if (IsActuated(Value) && !PlayerInput->WasJustReleased(ModifierKey)) return ETriggerState::None;
	const bool bTrigger = bArmed && !bChord && PlayerInput->WasJustReleased(ModifierKey);
	bArmed = bChord = false;
	return bTrigger ? ETriggerState::Triggered : ETriggerState::None;
}
