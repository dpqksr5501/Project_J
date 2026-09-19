#include "CharacterClass/Project_JProgressionComponent.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "Net/UnrealNetwork.h"

namespace
{
bool IsStyleReady(const UProject_JCombatStyleDefinition* Style) { return !Style || Style->IsRuntimeReady(); }
void GrantSets(UAbilitySystemComponent* ASC, UObject* Owner, const TArray<TObjectPtr<UProject_JAbilitySet>>& Sets,
	const FString& Prefix, FProject_JAbilitySet_GrantedHandles& Handles)
{
	for (const UProject_JAbilitySet* Set : Sets)
	{
		if (Set) Set->GiveToAbilitySystem(ASC, &Handles, Owner, FName(*(Prefix + TEXT(".") + Set->GetPathName())));
	}
}
void GrantStyle(UAbilitySystemComponent* ASC, UObject* Owner, const UProject_JCombatStyleDefinition* Style,
	FProject_JAbilitySet_GrantedHandles& Handles)
{
	if (Style) GrantSets(ASC, Owner, Style->GetRuntimeAbilitySets(), TEXT("CombatStyle.") + Style->GetPathName(), Handles);
}
}

UProject_JProgressionComponent::UProject_JProgressionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UProject_JProgressionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UProject_JProgressionComponent, State);
	DOREPLIFETIME_CONDITION(UProject_JProgressionComponent, AcquiredAdvancements, COND_OwnerOnly);
}

UAbilitySystemComponent* UProject_JProgressionComponent::GetASC() const
{
	const auto* Interface = Cast<IAbilitySystemInterface>(GetOwner());
	return Interface ? Interface->GetAbilitySystemComponent() : nullptr;
}

bool UProject_JProgressionComponent::CanMutate() const
{
	check(IsInGameThread());
	return !bChanging && GetOwner() && !GetOwner()->IsActorBeingDestroyed() && GetOwner()->HasAuthority() && GetASC();
}

void UProject_JProgressionComponent::Publish()
{
	State.Revision = State.Revision == MAX_int32 ? 1 : State.Revision + 1;
	GetOwner()->ForceNetUpdate();
	OnChanged.Broadcast();
}
void UProject_JProgressionComponent::OnRep_State() { OnChanged.Broadcast(); }

void UProject_JProgressionComponent::GrantClass()
{
	if (!State.ClassDefinition) return;
	GrantSets(GetASC(), GetOwner(), State.ClassDefinition->AbilitySets,
		TEXT("Class.") + State.ClassDefinition->ClassId.ToString(), ClassHandles);
	GrantStyle(GetASC(), GetOwner(), State.ClassDefinition->DefaultCombatStyle, ClassHandles);
}

void UProject_JProgressionComponent::InitializeDefaults(UProject_JCharacterClassDefinition* Class,
	UProject_JCharacterAdvancementDefinition* Advancement, int32 Level)
{
	if (!CanMutate() || State.Revision != 0 || (Class && !IsStyleReady(Class->DefaultCombatStyle))
		|| (Advancement && (!IsStyleReady(Advancement->CombatStyleOverride)
			|| (Advancement->BaseClass && !IsStyleReady(Advancement->BaseClass->DefaultCombatStyle))))) return;
	// Avatar defaults are applied once. A respawn must never roll persistent state back.
	{
		TGuardValue<bool> Guard(bChanging, true);
		State.ClassDefinition = Class ? Class : (Advancement ? Advancement->BaseClass.Get() : nullptr);
		State.Level = FMath::Max(FMath::Max(1, Level), State.ClassDefinition ? State.ClassDefinition->StartingLevel : 1);
		GrantClass();
		Publish();
	}
	if (Advancement) ApplyAdvancement(Advancement);
}

bool UProject_JProgressionComponent::InitializeClass(UProject_JCharacterClassDefinition* Class)
{
	if (!CanMutate() || !Class || Class->ClassId.IsNone() || !IsStyleReady(Class->DefaultCombatStyle)) return false;
	if (State.ClassDefinition) return State.ClassDefinition == Class;
	TGuardValue<bool> Guard(bChanging, true);
	State.ClassDefinition = Class;
	State.Level = FMath::Max(State.Level, Class->StartingLevel);
	GrantClass();
	Publish();
	return true;
}

bool UProject_JProgressionComponent::CanApplyAdvancement(const UProject_JCharacterAdvancementDefinition* Advancement) const
{
	if (!CanMutate() || !Advancement || Advancement->AdvancementId.IsNone()
		|| AcquiredAdvancements.Num() >= 256 || AcquiredAdvancements.Contains(Advancement->AdvancementId)
		|| !IsStyleReady(Advancement->CombatStyleOverride)
		|| (Advancement->BaseClass && !IsStyleReady(Advancement->BaseClass->DefaultCombatStyle))
		|| State.Level < Advancement->RequiredLevel
		|| (State.ClassDefinition && Advancement->BaseClass && State.ClassDefinition != Advancement->BaseClass)) return false;
	for (FName Prerequisite : Advancement->RequiredAdvancementIds)
		if (!AcquiredAdvancements.Contains(Prerequisite)) return false;
	if (!Advancement->ExclusiveBranch.IsNone())
	{
		for (const UProject_JCharacterAdvancementDefinition* Active : ActiveAdvancements)
			if (Active && Active->ExclusiveBranch == Advancement->ExclusiveBranch
				&& Advancement->AbilityGrantPolicy != EProject_JAdvancementAbilityGrantPolicy::ReplacePreviousAdvancement) return false;
	}
	FGameplayTagContainer Tags;
	GetASC()->GetOwnedGameplayTags(Tags);
	return Tags.HasAll(Advancement->RequiredTags) && !Tags.HasAny(Advancement->BlockedTags);
}

bool UProject_JProgressionComponent::ApplyAdvancement(UProject_JCharacterAdvancementDefinition* Advancement)
{
	if (!CanApplyAdvancement(Advancement)) return false;
	TGuardValue<bool> Guard(bChanging, true);
	if (Advancement->AbilityGrantPolicy == EProject_JAdvancementAbilityGrantPolicy::ReplacePreviousAdvancement)
	{
		GetDefault<UProject_JAbilitySet>()->TakeFromAbilitySystem(GetASC(), &AdvancementHandles);
		ActiveAdvancements.Reset();
	}
	if (!State.ClassDefinition && Advancement->BaseClass)
	{
		State.ClassDefinition = Advancement->BaseClass;
		GrantClass();
	}
	State.Advancement = Advancement;
	AcquiredAdvancements.Add(Advancement->AdvancementId);
	ActiveAdvancements.Add(Advancement);
	GrantSets(GetASC(), GetOwner(), Advancement->AdditionalAbilitySets,
		TEXT("Advancement.") + Advancement->AdvancementId.ToString(), AdvancementHandles);
	GrantStyle(GetASC(), GetOwner(), Advancement->CombatStyleOverride, AdvancementHandles);
	Publish();
	return true;
}

bool UProject_JProgressionComponent::SetLevel(int32 Level)
{
	if (!CanMutate() || State.Revision == 0) return false;
	Level = FMath::Max(FMath::Max(1, Level), State.ClassDefinition ? State.ClassDefinition->StartingLevel : 1);
	if (State.Level == Level) return true;
	TGuardValue<bool> Guard(bChanging, true);
	State.Level = Level;
	Publish();
	return true;
}

FProject_JProgressionSnapshot UProject_JProgressionComponent::CaptureSnapshot() const
{
	check(IsInGameThread());
	FProject_JProgressionSnapshot Snapshot;
	Snapshot.ClassId = State.ClassDefinition ? State.ClassDefinition->ClassId : NAME_None;
	Snapshot.Level = State.Level;
	Snapshot.AdvancementHistory = AcquiredAdvancements;
	return Snapshot;
}

bool UProject_JProgressionComponent::RestoreSnapshot(const FProject_JProgressionSnapshot& Snapshot,
	UProject_JCharacterClassDefinition* Class, const TArray<UProject_JCharacterAdvancementDefinition*>& OrderedHistory)
{
	if (!CanMutate() || State.Revision != 0 || Snapshot.SchemaVersion != 1 || Snapshot.Level < 1
		|| Snapshot.AdvancementHistory.Num() > 256 || Snapshot.AdvancementHistory.Num() != OrderedHistory.Num()
		|| Snapshot.ClassId != (Class ? Class->ClassId : NAME_None)
		|| (Class && (Snapshot.Level < Class->StartingLevel || !IsStyleReady(Class->DefaultCombatStyle)))) return false;
	TArray<TObjectPtr<UProject_JCharacterAdvancementDefinition>> Active;
	TSet<FName> Seen;
	for (int32 Index = 0; Index < OrderedHistory.Num(); ++Index)
	{
		auto* Advancement = OrderedHistory[Index];
		if (!Advancement || Advancement->AdvancementId.IsNone() || Seen.Contains(Advancement->AdvancementId)
			|| !IsStyleReady(Advancement->CombatStyleOverride)
			|| Advancement->AdvancementId != Snapshot.AdvancementHistory[Index]
			|| (Advancement->BaseClass && Advancement->BaseClass != Class)) return false;
		for (FName Prerequisite : Advancement->RequiredAdvancementIds) if (!Seen.Contains(Prerequisite)) return false;
		if (Advancement->AbilityGrantPolicy == EProject_JAdvancementAbilityGrantPolicy::ReplacePreviousAdvancement) Active.Reset();
		else if (!Advancement->ExclusiveBranch.IsNone())
			for (const UProject_JCharacterAdvancementDefinition* Other : Active)
				if (Other->ExclusiveBranch == Advancement->ExclusiveBranch) return false;
		Seen.Add(Advancement->AdvancementId);
		Active.Add(Advancement);
	}
	// Validation is complete before any state or GAS mutation. Historical tag/level gates
	// are not replayed; the trusted repository owns the fact that acquisition happened.
	TGuardValue<bool> Guard(bChanging, true);
	State.ClassDefinition = Class;
	State.Level = Snapshot.Level;
	State.Advancement = OrderedHistory.IsEmpty() ? nullptr : OrderedHistory.Last();
	AcquiredAdvancements = Snapshot.AdvancementHistory;
	ActiveAdvancements = MoveTemp(Active);
	GrantClass();
	for (const UProject_JCharacterAdvancementDefinition* Advancement : ActiveAdvancements)
	{
		GrantSets(GetASC(), GetOwner(), Advancement->AdditionalAbilitySets,
			TEXT("Advancement.") + Advancement->AdvancementId.ToString(), AdvancementHandles);
		GrantStyle(GetASC(), GetOwner(), Advancement->CombatStyleOverride, AdvancementHandles);
	}
	Publish();
	return true;
}
