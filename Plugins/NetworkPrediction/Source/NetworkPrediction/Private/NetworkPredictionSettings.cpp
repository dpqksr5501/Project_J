// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkPredictionSettings.h"
#include "Engine/Engine.h"
#include "NetworkPredictionWorldManager.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/HUD.h"
#include "Engine/Canvas.h"
#include "Engine/LocalPlayer.h"
#include "Engine/LevelScriptActor.h"
#include "Containers/Ticker.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NetworkPredictionSettings)

#if WITH_EDITOR
void UNetworkPredictionSettingsObject::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	for (TObjectIterator<UNetworkPredictionWorldManager> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject))
		{
			It->SyncNetworkPredictionSettings(this);
		}
	}
};
#endif

namespace UE_NETWORK_PREDICTION
{
	struct FDevMenuState
	{
		bool bIsActive = false;
		bool bKeysBound = false;
		int32 ActiveHUD = INDEX_NONE;

		float LastMaxX = 0.f;
		float LastMaxY = 0.f;

		TArray<FNetworkPredictionDevHUD> DevHUDs;
	};

	namespace
	{
		TMap<TWeakObjectPtr<UWorld>, FDevMenuState> DevMenuStates;

		const TCHAR* const DevMenuKeyNames[] = { TEXT("zero"), TEXT("one"), TEXT("two"), TEXT("three"), TEXT("four"), TEXT("five"), TEXT("six"), TEXT("seven"), TEXT("eight"), TEXT("nine") };
		constexpr int32 NumDevMenuKeys = UE_ARRAY_COUNT(DevMenuKeyNames);

		FString MakeDevMenuKeyCommand(const int32 Choice)
		{
			return FString::Printf(TEXT("np2.DevMenu %d"), Choice);
		}

		UPlayerInput* GetDevMenuPlayerInput(UWorld* World)
		{
			const ULocalPlayer* const LocalPlayer = World ? World->GetFirstLocalPlayerFromController() : nullptr;
			return (LocalPlayer && LocalPlayer->PlayerController) ? LocalPlayer->PlayerController->PlayerInput.Get() : nullptr;
		}

		void BindDevMenuKeys(FDevMenuState& State, UWorld* World)
		{
			if (ULocalPlayer* LocalPlayer = World->GetFirstLocalPlayerFromController())
			{
				for (int32 Choice = 0; Choice < NumDevMenuKeys; ++Choice)
				{
					LocalPlayer->Exec(World, *FString::Printf(TEXT("setbind %s \"%s\""), DevMenuKeyNames[Choice], *MakeDevMenuKeyCommand(Choice)), *GLog);
				}
				State.bKeysBound = true;
			}
		}

		// setbind has no unbind form (an empty command still leaves a binding behind), so remove the bindings it added
		// directly and save, since setbind persisted them to the player input config.
		void UnbindDevMenuKeys(FDevMenuState& State, UWorld* World)
		{
			UPlayerInput* PlayerInput = GetDevMenuPlayerInput(World);
			const bool bLivePlayerInput = PlayerInput != nullptr;
			if (!PlayerInput)
			{
				// No live player input (e.g. the world is being torn down): edit the saved config through the class default
				// object instead, reloading first so bindings saved this session by other setbinds are kept.
				PlayerInput = UInputSettings::GetDefaultPlayerInputClass()->GetDefaultObject<UPlayerInput>();
				PlayerInput->ReloadConfig();
			}

			const int32 NumRemoved = PlayerInput->DebugExecBindings.RemoveAll([](const FKeyBind& Bind)
			{
				for (int32 Choice = 0; Choice < NumDevMenuKeys; ++Choice)
				{
					if (Bind.Key == FKey(DevMenuKeyNames[Choice]) && Bind.Command == MakeDevMenuKeyCommand(Choice)
						&& !Bind.Control && !Bind.Shift && !Bind.Alt && !Bind.Cmd)
					{
						return true;
					}
				}
				return false;
			});

			if (NumRemoved > 0)
			{
				PlayerInput->SaveConfig();
			}
			State.bKeysBound = false;

			UE_LOGF(LogNetworkPrediction, Verbose, "np2.DevMenu removed %d key bindings for world %ls (%ls).",
				NumRemoved, *GetNameSafe(World), bLivePlayerInput ? TEXT("live player input") : TEXT("saved config"));
		}

		void OnDevMenuWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
		{
			if (FDevMenuState* State = DevMenuStates.Find(World))
			{
				if (State->bKeysBound)
				{
					UnbindDevMenuKeys(*State, World);
				}
				DevMenuStates.Remove(World);
			}
		}

		// Binds or unbinds on the next tick: this can run from a binding whose command string UPlayerInput::Exec is still parsing,
		// and setbind or the unbind would change or free that string. World cleanup unbinds instead if the world goes away first.
		void SyncDevMenuKeyBindings(UWorld* World)
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld = TWeakObjectPtr<UWorld>(World)](float)
			{
				UWorld* const TickWorld = WeakWorld.Get();
				FDevMenuState* const State = TickWorld ? DevMenuStates.Find(TickWorld) : nullptr;
				if (State && State->bIsActive && !State->bKeysBound)
				{
					BindDevMenuKeys(*State, TickWorld);
				}
				else if (State && !State->bIsActive && State->bKeysBound)
				{
					UnbindDevMenuKeys(*State, TickWorld);
				}
				return false;
			}));
		}
	}

	FORCENOINLINE void DrawText(FDevMenuState& State, UCanvas* Canvas, const TCHAR* Str, float& X, float& Y, bool bEnabled=true)
	{
		float XL = 0.f;
		float YL = 0.f;

		if (bEnabled)
		{
			Canvas->SetDrawColor(FColor::White);
		}
		else
		{
			Canvas->SetDrawColor(FColor(150, 150, 150));
		}
		
		Canvas->StrLen(GEngine->GetMediumFont(), Str, XL, YL);		
		Y += Canvas->DrawText(GEngine->GetMediumFont(), Str, X, Y);
		
		State.LastMaxY = Y;
		State.LastMaxX = FMath::Max<float>(State.LastMaxX, X + XL);
	}

	FORCENOINLINE void DrawDevHUD(TMap<TWeakObjectPtr<UWorld>, FDevMenuState>& WorldHandlesMap, AHUD* HUD, UCanvas* Canvas)
	{
		FDevMenuState& State = WorldHandlesMap.FindOrAdd(HUD->GetWorld());
		if (!State.bIsActive)
		{
			return;
		}

		float YPos = Canvas->SizeY * 0.45f; //455.f; //16;
		float XPos = 24.f;
		float Pad = 4.f;

		FCanvasTileItem TileItem(FVector2D(XPos-Pad, YPos-Pad), FVector2D(State.LastMaxX - XPos + (Pad*2.f), State.LastMaxY - YPos + (Pad*2.f)), FLinearColor(0.05f, 0.05f, 0.05f, 0.25f));
		Canvas->DrawItem(TileItem);

		State.LastMaxX = 0.f;
		State.LastMaxY = 0.f;

		//DrawText(State, Canvas, TEXT("Dev HUD "), XPos, YPos);
		//DrawText(State, Canvas, TEXT(" "), XPos, YPos);

		if (State.ActiveHUD == INDEX_NONE)
		{
			for (int32 idx=0; idx < State.DevHUDs.Num(); ++idx)
			{
				const FNetworkPredictionDevHUD& DevHUD = State.DevHUDs[idx];
				const bool bEnabled = (!DevHUD.bRequireNotPIE || !GIsEditor) && (!DevHUD.bRequirePIE || GIsEditor);

				DrawText(State, Canvas, *FString::Printf(TEXT("[%d] %s"), idx+1, *DevHUD.HUDName), XPos, YPos, bEnabled);
			}

			DrawText(State, Canvas, TEXT(" "), XPos, YPos);
			DrawText(State, Canvas, TEXT("[0] HIDE Dev HUD"), XPos, YPos);
		}
		else if (State.DevHUDs.IsValidIndex(State.ActiveHUD))
		{
			const FNetworkPredictionDevHUD& DevHUD = State.DevHUDs[State.ActiveHUD];
			for (int32 idx=0; idx < DevHUD.Items.Num(); ++idx)
			{
				const FNetworkPredictionDevHUDItem& HUDItem = DevHUD.Items[idx];
				const bool bEnabled = (!HUDItem.bRequireNotPIE || !GIsEditor) && (!HUDItem.bRequirePIE || GIsEditor);
				DrawText(State, Canvas, *FString::Printf(TEXT("[%d] %s"), idx+1, *HUDItem.DisplayName), XPos, YPos, bEnabled);
			}

			DrawText(State, Canvas, TEXT(" "), XPos, YPos);
			DrawText(State, Canvas, TEXT("[0] Back"), XPos, YPos);
		}
		else
		{
			DrawText(State, Canvas, TEXT("???"), XPos, YPos);
		}		
	}

	FORCENOINLINE void DevMenu(const TArray< FString >& Args, UWorld* InWorld)
	{
		if (!InWorld)
		{
			UE_LOGF(LogNetworkPrediction, Warning, "np2.DevMenu needs a world.");
			return;
		}

		static FDelegateHandle WorldCleanupHandle;
		if (!WorldCleanupHandle.IsValid())
		{
			WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddStatic(&OnDevMenuWorldCleanup);
		}

		FDevMenuState& State = DevMenuStates.FindOrAdd(InWorld);

		int32 Choice = INDEX_NONE;
		if (Args.Num() > 0)
		{
			LexFromString(Choice, *Args[0]);
		}

		if (Choice == 0)
		{
			if (State.ActiveHUD == INDEX_NONE)
			{
				State.bIsActive = !State.bIsActive;
			}
			else
			{
				State.ActiveHUD = INDEX_NONE;
			}
			SyncDevMenuKeyBindings(InWorld);
			return;
		}

		if (State.DevHUDs.Num() <= 0)
		{
			const UNetworkPredictionSettingsObject* Settings = GetDefault<UNetworkPredictionSettingsObject>();
			check(Settings);
			State.DevHUDs = Settings->DevHUDs;

			ALevelScriptActor* LevelScript = InWorld->GetLevelScriptActor();
			FNetworkPredictionDevHUD& LevelScriptDevHUD = State.DevHUDs.InsertDefaulted_GetRef(0);
			LevelScriptDevHUD.HUDName = TEXT("Level Script");
			if (LevelScript)
			{
				for (TFieldIterator<UFunction> FunctionIt(LevelScript->GetClass(), EFieldIteratorFlags::ExcludeSuper); FunctionIt; ++FunctionIt)
				{
					UFunction* Func = *FunctionIt;
					if (Func->HasAnyFunctionFlags(EFunctionFlags::FUNC_UbergraphFunction | EFunctionFlags::FUNC_Event))
					{
						continue;
					}
					if (!Func->HasAllFunctionFlags(EFunctionFlags::FUNC_BlueprintCallable))
					{
						continue;
					}
					
					FString CommandStr = FString::Printf(TEXT("ServerExec \"ce \\\"%s\\\""), *Func->GetName());
					for (TFieldIterator<FProperty> It(Func); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
					{
						CommandStr += TEXT(" 0");
					}
					CommandStr += TEXT("\"");

					FNetworkPredictionDevHUDItem& Item = LevelScriptDevHUD.Items.AddDefaulted_GetRef();
					//Item.DisplayName = FString::Printf(TEXT("%s 0x%X"), *FunctionIt->GetName(), (uint32)Func->FunctionFlags);
					Item.DisplayName = FunctionIt->GetName();
					Item.ExecCommand = CommandStr;
					Item.bAutoBack = false;

					if (LevelScriptDevHUD.Items.Num() == 9)
					{
						break;
					}
				}
			}
		}

		if (!State.bIsActive)
		{
			State.bIsActive = true;
		}
		else if (State.ActiveHUD == INDEX_NONE)
		{
			if (State.DevHUDs.IsValidIndex(Choice-1))
			{
				if ((!State.DevHUDs[Choice-1].bRequirePIE || GIsEditor) && (!State.DevHUDs[Choice-1].bRequireNotPIE || !GIsEditor))
				{
					State.ActiveHUD = Choice-1;
				}
			}
		}
		else
		{
			if (State.DevHUDs.IsValidIndex(State.ActiveHUD))
			{
				const FNetworkPredictionDevHUD& DevHUD = State.DevHUDs[State.ActiveHUD];
				if (DevHUD.Items.IsValidIndex(Choice-1))
				{
					const FNetworkPredictionDevHUDItem& Item = DevHUD.Items[Choice-1];
					if ((!Item.bRequirePIE || GIsEditor) && (!Item.bRequireNotPIE || !GIsEditor))
					{
						if (ULocalPlayer* LocalPlayer = InWorld->GetFirstLocalPlayerFromController())
						{
							// The command can clean up this world or add another world's menu state, either of which invalidates State and Item.
							const FString ExecCommand = Item.ExecCommand;
							const bool bAutoBack = Item.bAutoBack;
							LocalPlayer->Exec(InWorld, *ExecCommand, *GLog);
							FDevMenuState* const StateAfterExec = DevMenuStates.Find(InWorld);
							if (bAutoBack && StateAfterExec)
							{
								StateAfterExec->ActiveHUD = INDEX_NONE;
							}
						}
					}
				}
			}
		}

		SyncDevMenuKeyBindings(InWorld);

		static FDelegateHandle Handle;
		if (Handle.IsValid())
		{
			return;
		}

		Handle = AHUD::OnHUDPostRender.AddLambda([](AHUD* HUD, UCanvas* Canvas)
		{
			DrawDevHUD(DevMenuStates, HUD, Canvas);
		});
	}

	void PrintDevMenu(UWorld* InWorld)
	{
		const FDevMenuState* const State = DevMenuStates.Find(InWorld);
		if (!State)
		{
			UE_LOGF(LogNetworkPrediction, Display, "np2.DevMenu has not been built for world %ls yet. Run np2.DevMenu in this world first.", *GetNameSafe(InWorld));
			return;
		}

		const FString ActiveHUDStr = State->DevHUDs.IsValidIndex(State->ActiveHUD)
			? FString::Printf(TEXT("[%d] %s"), State->ActiveHUD + 1, *State->DevHUDs[State->ActiveHUD].HUDName)
			: FString(TEXT("none"));

		UE_LOGF(LogNetworkPrediction, Display, "np2.DevMenu for world %ls: Shown: %ls, Keys bound: %ls, Active HUD: %ls",
			*GetNameSafe(InWorld), State->bIsActive ? TEXT("true") : TEXT("false"), State->bKeysBound ? TEXT("true") : TEXT("false"), *ActiveHUDStr);

		if (State->DevHUDs.Num() <= 0)
		{
			UE_LOGF(LogNetworkPrediction, Display, "  No HUDs listed yet; they are built by the first np2.DevMenu call other than np2.DevMenu 0.");
		}

		for (int32 HUDIdx = 0; HUDIdx < State->DevHUDs.Num(); ++HUDIdx)
		{
			const FNetworkPredictionDevHUD& DevHUD = State->DevHUDs[HUDIdx];
			UE_LOGF(LogNetworkPrediction, Display, "  [%d] %ls", HUDIdx + 1, *DevHUD.HUDName);

			for (int32 ItemIdx = 0; ItemIdx < DevHUD.Items.Num(); ++ItemIdx)
			{
				const FNetworkPredictionDevHUDItem& Item = DevHUD.Items[ItemIdx];
				UE_LOGF(LogNetworkPrediction, Display, "    [%d] %ls: %ls", ItemIdx + 1, *Item.DisplayName, *Item.ExecCommand);
			}
		}
	}

}

FAutoConsoleCommandWithWorldAndArgs DevMenuToggleCmd(TEXT("np2.DevMenu"), TEXT("Network Prediction dev HUD menu. While hidden, any call shows it. At the top level, N opens HUD N (1 is Level Script, then the project settings DevHUDs); inside a HUD, N runs item N. 0 goes back, or hides the menu at the top level. Showing the menu binds the number keys one..zero to np2.DevMenu (via setbind); hiding it removes those bindings."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray< FString >& Args, UWorld* InWorld)
{
	UE_NETWORK_PREDICTION::DevMenu(Args, InWorld);

}));

FAutoConsoleCommandWithWorld DevMenuPrintCmd(TEXT("np2.DevMenu.Print"), TEXT("Logs the np2.DevMenu state for this world: whether it is shown, the active HUD, and every HUD and item with the number that selects it and the item's exec command."),
	FConsoleCommandWithWorldDelegate::CreateStatic(&UE_NETWORK_PREDICTION::PrintDevMenu));
