#include "Components/Project_JCommandBudgetComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Controller.h"

UProject_JCommandBudgetComponent::UProject_JCommandBudgetComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UProject_JCommandBudgetComponent::AdmitClientCommand(AActor* Source)
{
	check(IsInGameThread());
	if (!IsValid(Source) || Source->IsActorBeingDestroyed() || !Source->HasAuthority()) return false;
	AActor* BudgetOwner = Source;
	if (const auto* Pawn = Cast<APawn>(Source))
	{
		if (auto* State = Pawn->GetPlayerState()) BudgetOwner = State;
		else if (Pawn->IsPlayerControlled()) return false;
	}
	if (auto* Budget = BudgetOwner->FindComponentByClass<UProject_JCommandBudgetComponent>())
		return Budget->TryConsume();
	// NPCs and non-player fixtures have no session budget. Their domain limits remain.
	return !Cast<APlayerState>(BudgetOwner);
}

bool UProject_JCommandBudgetComponent::TryConsume()
{
	check(IsInGameThread());
	if (bClosed || IsBeingDestroyed() || !IsValid(GetOwner()) || GetOwner()->IsActorBeingDestroyed() ||
		!GetOwner()->HasAuthority() || !GetWorld() || GetWorld()->bIsTearingDown) return false;
	const double Now = FPlatformTime::Seconds();
	if (WindowStart < 0 || Now - WindowStart >= 1.0) { WindowStart = Now; Used = 0; }
	if (Used >= FMath::Clamp(CommandsPerSecond, 1, 10000))
	{
		RejectedCount = FMath::Min(RejectedCount, MAX_int64 - 1) + 1;
		return false;
	}
	++Used;
	return true;
}

void UProject_JCommandBudgetComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	bClosed = true;
	Super::EndPlay(Reason);
}
