#include "Audio/Project_JGameplayCue_BudgetedImpact.h"
#include "Audio/Project_JAudioBudgetSubsystem.h"
#include "Game/Project_JPlayerState.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Sound/SoundBase.h"

bool UProject_JGameplayCue_BudgetedImpact::OnExecute_Implementation(AActor* Target, const FGameplayCueParameters& Parameters) const
{
	UWorld* World = IsValid(Target) ? Target->GetWorld() : nullptr;
	if (!World || World->bIsTearingDown || World->GetNetMode() == NM_DedicatedServer || !ImpactSound) return false;
	auto* Budget = UGameInstance::GetSubsystem<UProject_JAudioBudgetSubsystem>(World->GetGameInstance());
	if (!Budget) return false;
	const auto* Caster = Cast<APawn>(Parameters.Instigator.Get());
	const auto* State = Caster ? Caster->GetPlayerState<AProject_JPlayerState>() : nullptr;
	EProject_JAudioGroup Group = EProject_JAudioGroup::OtherPlayerSFX;
	for (auto It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const auto* PC = It->Get();
		if (!PC || !PC->IsLocalPlayerController()) continue;
		if (Caster && PC->GetPawn() == Caster) { Group = EProject_JAudioGroup::LocalPlayer; break; }
		const auto* Listener = PC->GetPlayerState<AProject_JPlayerState>();
		if (State && Listener && !State->GetPartyId_Implementation().IsNone()
			&& State->GetPartyId_Implementation() == Listener->GetPartyId_Implementation())
			Group = EProject_JAudioGroup::PartySFX;
	}
	const bool bPlayed = Budget->PlayBudgetedSoundAtLocation(Target, ImpactSound, Parameters.Location, Group);
	UE_LOG(LogAudio, Verbose, TEXT("ProjectJ impact cue=%s sound=%s group=%d admitted=%d"), *GameplayCueTag.ToString(),
		*GetNameSafe(ImpactSound), static_cast<int32>(Group), bPlayed);
	return bPlayed;
}
