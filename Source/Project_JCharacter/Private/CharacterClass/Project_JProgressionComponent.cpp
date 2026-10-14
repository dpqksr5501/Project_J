#include "CharacterClass/Project_JProgressionComponent.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "Net/UnrealNetwork.h"

namespace
{
bool IsStyleReady(const UProject_JCombatStyleDefinition* Style) { return !Style || Style->IsRuntimeReady(); }
}

void UProject_JProgressionComponent::GrantSets(const TArray<TObjectPtr<UProject_JAbilitySet>>& Sets,
	const FString& Prefix, FProject_JAbilitySet_GrantedHandles& Handles)
{
	for (const UProject_JAbilitySet* Set : Sets)
	{
		if (!CanGrant()) { return; }
		if (Set) Set->GiveToAbilitySystem(GrantedAbilitySystem.Get(), &Handles, GetOwner(), FName(*(Prefix + TEXT(".") + Set->GetPathName())));
	}
}
void UProject_JProgressionComponent::GrantStyle(const UProject_JCombatStyleDefinition* Style,
	FProject_JAbilitySet_GrantedHandles& Handles)
{
	if (Style) GrantSets(Style->GetRuntimeAbilitySets(), TEXT("CombatStyle.") + Style->GetPathName(), Handles);
}

UProject_JProgressionComponent::UProject_JProgressionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}
void UProject_JProgressionComponent::BeginPlay()
{
	const bool bResume = bEnding;
	bEnding = false;
	Super::BeginPlay();
	// A streamed actor may begin play again after a prior EndPlay. Reconstruct
	// only this component's retired leases; persistent progression values remain.
	if (bResume && CanMutate() && State.Revision > 0)
	{
		TGuardValue<bool> Guard(bChanging, true);
		GrantClass();
		if (!GrantedAbilitySystem.IsValid() && !bEnding) { GrantedAbilitySystem = GetASC(); }
		for (const UProject_JCharacterAdvancementDefinition* Advancement : ActiveAdvancements)
		{
			if (!CanGrant()) { break; }
			GrantSets(Advancement->AdditionalAbilitySets, TEXT("Advancement.") + Advancement->AdvancementId.ToString(), AdvancementHandles);
			GrantStyle(Advancement->CombatStyleOverride, AdvancementHandles);
		}
	}
}
void UProject_JProgressionComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	RetireGrants();
	Super::EndPlay(Reason);
}
void UProject_JProgressionComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	RetireGrants();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}
void UProject_JProgressionComponent::RetireGrants()
{
	bEnding = true;
	OnChanged.Clear();
	if (auto* ASC = GrantedAbilitySystem.Get())
	{
		const auto* Set = GetDefault<UProject_JAbilitySet>();
		Set->TakeFromAbilitySystem(ASC, &ClassHandles);
		Set->TakeFromAbilitySystem(ASC, &AdvancementHandles);
	}
	GrantedAbilitySystem.Reset();
}
bool UProject_JProgressionComponent::CanGrant() const
{
	return !bEnding && !IsBeingDestroyed() && IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed() &&
		GrantedAbilitySystem.IsValid() && GetASC() == GrantedAbilitySystem.Get();
}

void UProject_JProgressionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UProject_JProgressionComponent, State);
	DOREPLIFETIME_CONDITION(UProject_JProgressionComponent, Experience, COND_OwnerOnly);
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
	return !bEnding && !IsBeingDestroyed() && !bChanging && IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed() &&
		GetOwner()->HasAuthority() && GetASC() && (!GrantedAbilitySystem.IsValid() || GetASC() == GrantedAbilitySystem.Get());
}

void UProject_JProgressionComponent::Publish()
{
	if (bEnding || IsBeingDestroyed() || !IsValid(GetOwner()) || GetOwner()->IsActorBeingDestroyed()) { return; }
	State.Revision = State.Revision == MAX_int32 ? 1 : State.Revision + 1;
	GetOwner()->ForceNetUpdate();
	OnChanged.Broadcast();
}
void UProject_JProgressionComponent::OnRep_State() { if (!bEnding) OnChanged.Broadcast(); }

void UProject_JProgressionComponent::GrantClass()
{
	if (!State.ClassDefinition) return;
	if (!GrantedAbilitySystem.IsValid()) { GrantedAbilitySystem = GetASC(); }
	GrantSets(State.ClassDefinition->AbilitySets,
		TEXT("Class.") + State.ClassDefinition->ClassId.ToString(), ClassHandles);
	GrantStyle(State.ClassDefinition->DefaultCombatStyle, ClassHandles);
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
	return !bEnding;
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
	if (!GrantedAbilitySystem.IsValid()) { GrantedAbilitySystem = GetASC(); }
	if (Advancement->AbilityGrantPolicy == EProject_JAdvancementAbilityGrantPolicy::ReplacePreviousAdvancement)
	{
		GetDefault<UProject_JAbilitySet>()->TakeFromAbilitySystem(GetASC(), &AdvancementHandles);
		if (!CanGrant()) { return false; }
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
	if (!GrantedAbilitySystem.IsValid()) { GrantedAbilitySystem = GetASC(); }
	GrantSets(Advancement->AdditionalAbilitySets,
		TEXT("Advancement.") + Advancement->AdvancementId.ToString(), AdvancementHandles);
	GrantStyle(Advancement->CombatStyleOverride, AdvancementHandles);
	Publish();
	return !bEnding;
}

bool UProject_JProgressionComponent::SetLevel(int32 Level)
{
	if (!CanMutate() || State.Revision == 0 || Level>FMath::Clamp(MaximumLevel,1,10000)) return false;
	Level = FMath::Max(FMath::Max(1, Level), State.ClassDefinition ? State.ClassDefinition->StartingLevel : 1);
	if (State.Level == Level) return true;
	TGuardValue<bool> Guard(bChanging, true);
	State.Level = Level;
	Experience=0;
	Publish();
	return true;
}

FProject_JProgressionSnapshot UProject_JProgressionComponent::CaptureSnapshot() const
{
	check(IsInGameThread());
	FProject_JProgressionSnapshot Snapshot;
	Snapshot.ClassId = State.ClassDefinition ? State.ClassDefinition->ClassId : NAME_None;
	Snapshot.Level = State.Level;
	Snapshot.Experience = Experience;
	Snapshot.AdvancementHistory = AcquiredAdvancements;
	return Snapshot;
}

bool UProject_JProgressionComponent::RestoreSnapshot(const FProject_JProgressionSnapshot& Snapshot,
	UProject_JCharacterClassDefinition* Class, const TArray<UProject_JCharacterAdvancementDefinition*>& OrderedHistory)
{
	if (!CanMutate() || State.Revision != 0 || (Snapshot.SchemaVersion != 1 && Snapshot.SchemaVersion != 2) || Snapshot.Level < 1
		|| Snapshot.Level > FMath::Clamp(MaximumLevel, 1, 10000) || Snapshot.Experience < 0
		|| Snapshot.AdvancementHistory.Num() > 256 || Snapshot.AdvancementHistory.Num() != OrderedHistory.Num()
		|| Snapshot.ClassId != (Class ? Class->ClassId : NAME_None)
		|| (Class && (Snapshot.Level < Class->StartingLevel || !IsStyleReady(Class->DefaultCombatStyle)))) return false;
	const int64 RequiredXP = GetRequiredExperienceForLevel(Snapshot.Level);
 if (Snapshot.SchemaVersion>=2 && (RequiredXP==0 ? Snapshot.Experience!=0 : Snapshot.Experience>=RequiredXP)) return false;
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
	Experience = Snapshot.SchemaVersion >= 2 ? Snapshot.Experience : 0;
	State.Advancement = OrderedHistory.IsEmpty() ? nullptr : OrderedHistory.Last();
	AcquiredAdvancements = Snapshot.AdvancementHistory;
	ActiveAdvancements = MoveTemp(Active);
	GrantClass();
	for (const UProject_JCharacterAdvancementDefinition* Advancement : ActiveAdvancements)
	{
		if (!GrantedAbilitySystem.IsValid() && !bEnding) { GrantedAbilitySystem = GetASC(); }
		GrantSets(Advancement->AdditionalAbilitySets,
			TEXT("Advancement.") + Advancement->AdvancementId.ToString(), AdvancementHandles);
		GrantStyle(Advancement->CombatStyleOverride, AdvancementHandles);
	}
	Publish();
	return true;
}

void UProject_JProgressionComponent::OnRep_Experience() { if (!bEnding) OnChanged.Broadcast(); }
int64 UProject_JProgressionComponent::GetNextLevelExperience() const
{
 return GetRequiredExperienceForLevel(State.Level);
}
int64 UProject_JProgressionComponent::GetRequiredExperienceForLevel(int32 Level) const
{
	if (Level >= FMath::Clamp(MaximumLevel, 1, 10000)) return 0;
	const int32 Index = Level - 1;
	return ExperienceRequirements.IsValidIndex(Index)
		? FMath::Clamp<int64>(ExperienceRequirements[Index], 1, MAX_int64 / 10000)
		: 100LL * FMath::Max(1, Level);
}
bool UProject_JProgressionComponent::GrantExperience(int64 Amount)
{
 if (!CanMutate() || State.Revision==0 || Amount <= 0 || Amount > MAX_int64 - Experience || GetNextLevelExperience() <= 0) return false;
 TGuardValue<bool> Guard(bChanging, true);
 Experience += Amount;
 while (GetNextLevelExperience() > 0 && Experience >= GetNextLevelExperience())
 {
  Experience -= GetNextLevelExperience(); ++State.Level;
 }
 if (GetNextLevelExperience() == 0) Experience = 0;
 Publish(); return true;
}
