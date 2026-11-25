#include "Game/Project_JCheatManager.h"
#include "Backend/Project_JNakamaServicePort.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Project_JAttributeSet.h"
#include "Project_JBaseCharacter.h"
#include "UI/Project_JMMOTestHarnessWidget.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJCheat, Log, All);

bool UProject_JCheatManager::CanExecuteAuthorityCheat() const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	const APlayerController* PC = GetOuterAPlayerController();
	return IsValid(PC) && PC->HasAuthority() && IsValid(GetWorld()) && !GetWorld()->bIsTearingDown;
#endif
}

void UProject_JCheatManager::PJ_Teleport(float X, float Y, float Z)
{
	if (!CanExecuteAuthorityCheat())
	{
		UE_LOG(LogProjectJCheat, Warning, TEXT("PJ_Teleport denied (requires non-Shipping authority)."));
		return;
	}
	if (!FMath::IsFinite(X) || !FMath::IsFinite(Y) || !FMath::IsFinite(Z))
	{
		return;
	}
	if (APawn* Pawn = GetOuterAPlayerController()->GetPawn())
	{
		Pawn->TeleportTo(FVector(X, Y, Z), Pawn->GetActorRotation(), false, true);
		UE_LOG(LogProjectJCheat, Display, TEXT("PJ_Teleport -> (%.1f, %.1f, %.1f)"), X, Y, Z);
	}
}

void UProject_JCheatManager::PJ_HealFull()
{
	if (!CanExecuteAuthorityCheat())
	{
		return;
	}
	if (auto* Char = Cast<AProject_JBaseCharacter>(GetOuterAPlayerController()->GetPawn()))
	{
		if (UProject_JAttributeSet* Attrs = Char->GetAttributeSet())
		{
			Attrs->SetHealth(Attrs->GetMaxHealth());
			Attrs->SetMana(Attrs->GetMaxMana());
			UE_LOG(LogProjectJCheat, Display, TEXT("PJ_HealFull -> HP=%.1f MP=%.1f"), Attrs->GetHealth(), Attrs->GetMana());
		}
	}
}

void UProject_JCheatManager::PJ_KillSelf()
{
	if (!CanExecuteAuthorityCheat())
	{
		return;
	}
	if (auto* Char = Cast<AProject_JBaseCharacter>(GetOuterAPlayerController()->GetPawn()))
	{
		if (UProject_JAttributeSet* Attrs = Char->GetAttributeSet())
		{
			Attrs->SetHealth(0.0f);
			UE_LOG(LogProjectJCheat, Display, TEXT("PJ_KillSelf applied."));
		}
	}
}

void UProject_JCheatManager::PJ_SimulateLag(int32 LatencyMs, int32 LossPercent)
{
#if !UE_BUILD_SHIPPING
	const int32 ClampedLag = FMath::Clamp(LatencyMs, 0, 2000);
	const int32 ClampedLoss = FMath::Clamp(LossPercent, 0, 50);
	if (IConsoleVariable* LagVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Net.PktLag")))
	{
		LagVar->Set(ClampedLag, ECVF_SetByConsole);
	}
	if (IConsoleVariable* LossVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Net.PktLoss")))
	{
		LossVar->Set(ClampedLoss, ECVF_SetByConsole);
	}
	UE_LOG(LogProjectJCheat, Display, TEXT("PJ_SimulateLag -> Lag=%dms Loss=%d%%"), ClampedLag, ClampedLoss);
#endif
}

void UProject_JCheatManager::PJ_SetBackendMode(int32 ModeIndex)
{
	if (!CanExecuteAuthorityCheat() || GetWorld()->GetNetMode() != NM_Standalone)
	{
		return;
	}
	UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	if (auto* Port = GI ? GI->GetSubsystem<UProject_JNakamaServicePortSubsystem>() : nullptr)
	{
		const uint8 Clamped = static_cast<uint8>(FMath::Clamp(ModeIndex, 0, 2));
		Port->SetRuntimeMode(static_cast<EProject_JBackendRuntimeMode>(Clamped));
		UE_LOG(LogProjectJCheat, Display, TEXT("PJ_SetBackendMode -> %d"), Clamped);
	}
}

void UProject_JCheatManager::PJ_DumpRuntimeMetrics()
{
#if !UE_BUILD_SHIPPING
	const APlayerController* PC = GetOuterAPlayerController();
	const UWorld* World = GetWorld();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const FVector Loc = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	const auto* Port = GI ? GI->GetSubsystem<UProject_JNakamaServicePortSubsystem>() : nullptr;
	UE_LOG(LogProjectJCheat, Display,
		TEXT("PJ_Metrics WorldTime=%.2f NetMode=%d Loc=(%.1f,%.1f,%.1f) Speed=%.1f BackendMode=%d"),
		World ? World->GetTimeSeconds() : 0.0,
		World ? static_cast<int32>(World->GetNetMode()) : -1,
		Loc.X, Loc.Y, Loc.Z,
		Pawn ? Pawn->GetVelocity().Size() : 0.0,
		Port ? static_cast<int32>(Port->GetRuntimeMode()) : -1);
#endif
}

void UProject_JCheatManager::PJ_ToggleTestUI()
{
#if !UE_BUILD_SHIPPING
	APlayerController* PC = GetOuterAPlayerController();
	if (!IsValid(PC) || !PC->IsLocalController())
	{
		return;
	}
	if (UProject_JMMOTestHarnessWidget* Existing = ActiveTestHarness.Get())
	{
		if (Existing->IsInViewport())
		{
			Existing->RemoveFromParent();
			ActiveTestHarness.Reset();
			UE_LOG(LogProjectJCheat, Display, TEXT("PJ_ToggleTestUI -> Hidden"));
			return;
		}
		ActiveTestHarness.Reset();
	}
	static const TSoftClassPtr<UProject_JMMOTestHarnessWidget> HarnessClass(
		FSoftObjectPath(TEXT("/Game/DataAssetSets/MMO/WBP_MMOTestHarness.WBP_MMOTestHarness_C")));
	UClass* WidgetClass = HarnessClass.LoadSynchronous();
	if (!WidgetClass) WidgetClass = UProject_JMMOTestHarnessWidget::StaticClass();
	if (auto* Widget = CreateWidget<UProject_JMMOTestHarnessWidget>(PC, WidgetClass); Widget && Widget->AddToPlayerScreen(100))
	{
		ActiveTestHarness = Widget;
		UE_LOG(LogProjectJCheat, Display, TEXT("PJ_ToggleTestUI -> Shown"));
	}
#endif
}
