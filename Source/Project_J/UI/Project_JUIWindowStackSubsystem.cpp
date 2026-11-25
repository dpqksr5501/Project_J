#include "UI/Project_JUIWindowStackSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

void UProject_JUIWindowStackSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &ThisClass::OnWorldCleanup);
}

void UProject_JUIWindowStackSubsystem::Deinitialize()
{
	if (WorldCleanupHandle.IsValid())
	{
		FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
		WorldCleanupHandle.Reset();
	}
	WindowStack.Reset();
	Super::Deinitialize();
}

void UProject_JUIWindowStackSubsystem::OnWorldCleanup(UWorld* World, bool, bool)
{
	if (World && World->GetGameInstance() == GetGameInstance())
	{
		WindowStack.Reset();
	}
}

bool UProject_JUIWindowStackSubsystem::PushModalWindow(FName WindowId)
{
	if (WindowId.IsNone())
	{
		return false;
	}
	WindowStack.Remove(WindowId); // Bring to top if already open.
	WindowStack.Add(WindowId);
	return true;
}

FName UProject_JUIWindowStackSubsystem::HandleEscapeCloseTop()
{
	if (WindowStack.IsEmpty())
	{
		return NAME_None;
	}
	return WindowStack.Pop();
}

bool UProject_JUIWindowStackSubsystem::CloseWindow(FName WindowId)
{
	return WindowStack.Remove(WindowId) > 0;
}

void UProject_JUIWindowStackSubsystem::ApplyAccessibilitySettings(const FProject_JAccessibilitySettings& NewSettings)
{
	Accessibility = NewSettings;
	Accessibility.UIScale = FMath::Clamp(Accessibility.UIScale, 0.8f, 1.4f);
	Accessibility.OtherPlayerVFXOpacity = FMath::Clamp(Accessibility.OtherPlayerVFXOpacity, 0.0f, 1.0f);
}
