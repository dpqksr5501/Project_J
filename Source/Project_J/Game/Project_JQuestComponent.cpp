#include "Game/Project_JQuestComponent.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"

UProject_JQuestComponent::UProject_JQuestComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
}
void UProject_JQuestComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty> &OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UProject_JQuestComponent, States, COND_OwnerOnly);
}
void UProject_JQuestComponent::BeginPlay()
{
	Super::BeginPlay();
	if (Definitions.IsEmpty())
		if (auto *Def = LoadObject<UProject_JQuestDefinition>(
				nullptr, TEXT("/Game/UI/Gameplay/DA_ProjectJFirstSteps.DA_ProjectJFirstSteps")))
			Definitions.Add(Def);
	if (GetOwner()->HasAuthority())
	{
		for (const UProject_JQuestDefinition *Def : Definitions)
			if (Def && Def->bTravelObjective)
				Accept(Def->QuestId);
	}
}
void UProject_JQuestComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorld()->GetTimerManager().ClearTimer(TravelTimer);
	OnChanged.Clear();
	Super::EndPlay(Reason);
}
UProject_JQuestDefinition *UProject_JQuestComponent::FindDefinition(FName Id) const
{
	UProject_JQuestDefinition *Match = nullptr;
	for (UProject_JQuestDefinition *Def : Definitions)
		if (Def && Def->QuestId == Id)
		{
			if (Match)
				return nullptr;
			Match = Def;
		}
	return Match && !Id.IsNone() && Match->RequiredCount > 0 && Match->RequiredCount <= 1000000 &&
				   !Match->ObjectiveEvent.IsNone() && Match->RewardExperience >= 0
			   ? Match
			   : nullptr;
}
void UProject_JQuestComponent::Publish()
{
	GetOwner()->ForceNetUpdate();
	OnChanged.Broadcast();
}
void UProject_JQuestComponent::OnRep_States()
{
	OnChanged.Broadcast();
}
bool UProject_JQuestComponent::Accept(FName Id)
{
	if (!GetOwner()->HasAuthority() || bChanging || States.Num() >= 128 || !FindDefinition(Id) ||
		States.ContainsByPredicate([Id](const auto &State) { return State.QuestId == Id; }))
		return false;
	FProject_JQuestState State;
	State.QuestId = Id;
	States.Add(State);
	if (FindDefinition(Id)->bTravelObjective && !TravelTimer.IsValid())
		GetWorld()->GetTimerManager().SetTimer(TravelTimer, this, &ThisClass::SampleTravel, 0.5f, true);
	Publish();
	return true;
}
void UProject_JQuestComponent::RecordObjective(FName Event, int32 Count)
{
	if (!GetOwner()->HasAuthority() || bChanging || Event.IsNone() || Count <= 0 || Count > 1000000)
		return;
	bool bChanged = false;
	for (auto &State : States)
		if (const auto *Def = FindDefinition(State.QuestId);
			Def && Def->ObjectiveEvent == Event && !State.bClaimed && State.Count < Def->RequiredCount)
		{
			State.Count += FMath::Min(Count, Def->RequiredCount - State.Count);
			bChanged = true;
		}
	if (bChanged)
		Publish();
}
bool UProject_JQuestComponent::Claim(FName Id)
{
	if (!GetOwner()->HasAuthority() || bChanging)
		return false;
	auto *State = States.FindByPredicate([Id](const auto &Value) { return Value.QuestId == Id; });
	const auto *Def = FindDefinition(Id);
	auto *Progression = GetOwner()->FindComponentByClass<UProject_JProgressionComponent>();
	if (!State || !Def || State->bClaimed || State->Count < Def->RequiredCount || !Progression)
		return false;
	TGuardValue<bool> Guard(bChanging, true);
	// Commit the claim before reward delegates fire. Repeated/reentrant requests cannot pay twice.
	State->bClaimed = true;
	State->bTracked = false;
	if (Def->RewardExperience > 0 && Progression->GetNextLevelExperience() > 0 &&
		!Progression->GrantExperience(Def->RewardExperience))
	{
		State->bClaimed = false;
		State->bTracked = true;
		return false;
	}
	Publish();
	return true;
}
bool UProject_JQuestComponent::AllowRequest()
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (RequestWindow < 0 || Now - RequestWindow >= 1)
	{
		RequestWindow = Now;
		RequestCount = 0;
	}
	return ++RequestCount <= 20;
}
void UProject_JQuestComponent::RequestClaim(FName Id)
{
	ServerClaim(Id);
}
void UProject_JQuestComponent::ServerClaim_Implementation(FName Id)
{
	if (AllowRequest())
		Claim(Id);
}
void UProject_JQuestComponent::RequestTracking(FName Id, bool bTracked)
{
	ServerTracking(Id, bTracked);
}
void UProject_JQuestComponent::ServerTracking_Implementation(FName Id, bool bTracked)
{
	if (!AllowRequest() || bChanging)
		return;
	if (auto *State = States.FindByPredicate([Id](const auto &Value) { return Value.QuestId == Id; });
		State && !State->bClaimed)
	{
		State->bTracked = bTracked;
		Publish();
	}
}
void UProject_JQuestComponent::SampleTravel()
{
	bool bActive = false;
	for (const auto &State : States)
		if (const auto *Def = FindDefinition(State.QuestId);
			Def && Def->bTravelObjective && !State.bClaimed && State.Count < Def->RequiredCount)
			bActive = true;
	if (!bActive)
	{
		GetWorld()->GetTimerManager().ClearTimer(TravelTimer);
		return;
	}
	auto *PS = Cast<APlayerState>(GetOwner());
	auto *Pawn = PS ? PS->GetPawn() : nullptr;
	if (!Pawn)
	{
		TravelPawn.Reset();
		return;
	}
	const FVector Position = Pawn->GetActorLocation();
	if (TravelPawn.Get() == Pawn)
	{
		const double Distance = FVector::Dist2D(Position, LastLocation);
		// Count sampled horizontal movement; ignore large teleport/respawn jumps.
		if (Distance > 0 && Distance < 1000 && !Pawn->GetVelocity().IsNearlyZero())
		{
			TravelRemainder += Distance / 100.;
			const int32 Count = FMath::FloorToInt(TravelRemainder);
			TravelRemainder -= Count;
			if (Count > 0)
			{
				TSet<FName> Events;
				for (const UProject_JQuestDefinition *Def : Definitions)
					if (Def && Def->bTravelObjective)
						Events.Add(Def->ObjectiveEvent);
				for (FName Event : Events)
					RecordObjective(Event, Count);
			}
		}
	}
	else
		TravelRemainder = 0;
	TravelPawn = Pawn;
	LastLocation = Position;
}
FProject_JQuestSnapshot UProject_JQuestComponent::CaptureSnapshot() const
{
	FProject_JQuestSnapshot Snapshot;
	Snapshot.States = States;
	return Snapshot;
}
bool UProject_JQuestComponent::RestoreSnapshot(const FProject_JQuestSnapshot &Snapshot)
{
	if (!GetOwner()->HasAuthority() || bChanging || !States.IsEmpty() || Snapshot.Version != 1 ||
		Snapshot.States.Num() > 128)
		return false;
	TSet<FName> Seen;
	for (const auto &State : Snapshot.States)
	{
		const auto *Def = FindDefinition(State.QuestId);
		if (!Def || Seen.Contains(State.QuestId) || State.Count < 0 || State.Count > Def->RequiredCount ||
			(State.bClaimed && State.Count < Def->RequiredCount))
			return false;
		Seen.Add(State.QuestId);
	}
	States = Snapshot.States;
	if (!States.IsEmpty())
		GetWorld()->GetTimerManager().SetTimer(TravelTimer, this, &ThisClass::SampleTravel, 0.5f, true);
	Publish();
	return true;
}
