#include "CharacterClass/Project_JProgressionComponent.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "Net/UnrealNetwork.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
bool IsStyleReady(const UProject_JCombatStyleDefinition* Style) { return !Style || Style->IsRuntimeReady(); }
// GAS and publication delegates may synchronously retire participants and collect garbage.
// These pins last for one operation; persistent ASC ownership remains weak.
struct FProgressionMutationLifetime
{
	TStrongObjectPtr<UProject_JProgressionComponent> Component;
	TStrongObjectPtr<AActor> Owner;
	TStrongObjectPtr<UAbilitySystemComponent> ASC;
	FProgressionMutationLifetime(UProject_JProgressionComponent* InComponent, UAbilitySystemComponent* InASC)
		: Component(InComponent), Owner(InComponent->GetOwner()), ASC(InASC) {}
};
}

void UProject_JProgressionComponent::GrantSets(const TArray<TObjectPtr<UProject_JAbilitySet>>& Sets,
	const FString& Prefix, FProject_JAbilitySet_GrantedHandles& Handles)
{
	TArray<TStrongObjectPtr<const UProject_JAbilitySet>> Snapshot;
	for (const UProject_JAbilitySet* Set : Sets) { if (Set) Snapshot.Emplace(Set); }
	for (const auto& PinnedSet : Snapshot)
	{
		if (!CanGrant()) { return; }
		const auto* Set = PinnedSet.Get();
		Set->GiveToAbilitySystem(GrantedAbilitySystem.Get(), &Handles, GetOwner(), FName(*(Prefix + TEXT(".") + Set->GetPathName())));
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

bool UProject_JProgressionComponent::IsMutationCurrent(const AActor* Owner, const UAbilitySystemComponent* ASC) const
{
	return !bEnding && !IsBeingDestroyed() && IsValid(Owner) && !Owner->IsActorBeingDestroyed()
		&& GetOwner() == Owner && Owner->HasAuthority() && IsValid(ASC) && !ASC->IsBeingDestroyed()
		&& GetASC() == ASC && (!GrantedAbilitySystem.IsValid() || GrantedAbilitySystem.Get() == ASC);
}

bool UProject_JProgressionComponent::ResolveLevel(int32 Level, const UProject_JCharacterClassDefinition* Class,
	int32& OutResolvedLevel) const
{
	const int32 Floor = Class ? Class->StartingLevel : 1;
	if (Floor < 1) { return false; }
	const int32 Resolved = FMath::Max(FMath::Max(1, Level), Floor);
	if (Resolved > FMath::Clamp(MaximumLevel, 1, 10000)) { return false; }
	OutResolvedLevel = Resolved;
	return true;
}

bool UProject_JProgressionComponent::CanSetLevel(int32 Level, int32& OutResolvedLevel) const
{
	return CanMutate() && State.Revision != 0 && ResolveLevel(Level, State.ClassDefinition, OutResolvedLevel);
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
	auto* InitialClass = Class ? Class : (Advancement ? Advancement->BaseClass.Get() : nullptr);
	int32 ResolvedLevel;
	if (!ResolveLevel(Level, InitialClass, ResolvedLevel)) { return; }
	const FProgressionMutationLifetime Lifetime(this, GetASC());
	// Avatar defaults are applied once. A respawn must never roll persistent state back.
	{
		TGuardValue<bool> Guard(bChanging, true);
		State.ClassDefinition = InitialClass;
		State.Level = ResolvedLevel;
		GrantClass();
		if (IsMutationCurrent(Lifetime.Owner.Get(), Lifetime.ASC.Get())) { Publish(); }
	}
	if (Advancement) ApplyAdvancement(Advancement);
}

bool UProject_JProgressionComponent::InitializeClass(UProject_JCharacterClassDefinition* Class)
{
	if (!CanMutate() || !Class || Class->ClassId.IsNone() || !IsStyleReady(Class->DefaultCombatStyle)) return false;
	int32 ResolvedLevel;
	if (!ResolveLevel(State.Level, Class, ResolvedLevel)) { return false; }
	if (State.ClassDefinition) return State.ClassDefinition == Class && State.Level == ResolvedLevel;
	const FProgressionMutationLifetime Lifetime(this, GetASC());
	TGuardValue<bool> Guard(bChanging, true);
	State.ClassDefinition = Class;
	State.Level = ResolvedLevel;
	GrantClass();
	if (!IsMutationCurrent(Lifetime.Owner.Get(), Lifetime.ASC.Get())) { return false; }
	Publish();
	return IsMutationCurrent(Lifetime.Owner.Get(), Lifetime.ASC.Get());
}

bool UProject_JProgressionComponent::CanApplyAdvancement(const UProject_JCharacterAdvancementDefinition* Advancement) const
{
	if (!CanMutate() || !Advancement || Advancement->AdvancementId.IsNone()
		|| AcquiredAdvancements.Num() >= 256 || AcquiredAdvancements.Contains(Advancement->AdvancementId)
		|| !IsStyleReady(Advancement->CombatStyleOverride)
		|| (Advancement->BaseClass && !IsStyleReady(Advancement->BaseClass->DefaultCombatStyle))
		|| State.Level < Advancement->RequiredLevel
		|| (State.ClassDefinition && Advancement->BaseClass && State.ClassDefinition != Advancement->BaseClass)) return false;
	int32 ResolvedLevel;
	if (!ResolveLevel(State.Level, State.ClassDefinition ? State.ClassDefinition.Get() : Advancement->BaseClass.Get(), ResolvedLevel)
		|| (State.ClassDefinition && State.Level != ResolvedLevel)) { return false; }
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
	const FProgressionMutationLifetime Lifetime(this, GetASC());
	int32 ResolvedLevel;
	if (!ResolveLevel(State.Level, State.ClassDefinition ? State.ClassDefinition.Get() : Advancement->BaseClass.Get(), ResolvedLevel)) { return false; }
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
		State.Level = ResolvedLevel;
		GrantClass();
		if (!CanGrant()) { return false; }
	}
	State.Advancement = Advancement;
	AcquiredAdvancements.Add(Advancement->AdvancementId);
	ActiveAdvancements.Add(Advancement);
	if (!GrantedAbilitySystem.IsValid()) { GrantedAbilitySystem = GetASC(); }
	GrantSets(Advancement->AdditionalAbilitySets,
		TEXT("Advancement.") + Advancement->AdvancementId.ToString(), AdvancementHandles);
	GrantStyle(Advancement->CombatStyleOverride, AdvancementHandles);
	if (!IsMutationCurrent(Lifetime.Owner.Get(), Lifetime.ASC.Get())) { return false; }
	Publish();
	return IsMutationCurrent(Lifetime.Owner.Get(), Lifetime.ASC.Get());
}

bool UProject_JProgressionComponent::SetLevel(int32 Level)
{
	int32 ResolvedLevel;
	if (!CanSetLevel(Level, ResolvedLevel)) return false;
	Level = ResolvedLevel;
	if (State.Level == Level) return true;
	const FProgressionMutationLifetime Lifetime(this, GetASC());
	TGuardValue<bool> Guard(bChanging, true);
	State.Level = Level;
	Experience=0;
	Publish();
	return IsMutationCurrent(Lifetime.Owner.Get(), Lifetime.ASC.Get());
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
	const FProgressionMutationLifetime Lifetime(this, GetASC());
	TGuardValue<bool> Guard(bChanging, true);
	State.ClassDefinition = Class;
	State.Level = Snapshot.Level;
	Experience = Snapshot.SchemaVersion >= 2 ? Snapshot.Experience : 0;
	State.Advancement = OrderedHistory.IsEmpty() ? nullptr : OrderedHistory.Last();
	AcquiredAdvancements = Snapshot.AdvancementHistory;
	ActiveAdvancements = MoveTemp(Active);
	GrantClass();
	if (!IsMutationCurrent(Lifetime.Owner.Get(), Lifetime.ASC.Get())) { return false; }
	for (const UProject_JCharacterAdvancementDefinition* Advancement : ActiveAdvancements)
	{
		if (!GrantedAbilitySystem.IsValid() && !bEnding) { GrantedAbilitySystem = GetASC(); }
		GrantSets(Advancement->AdditionalAbilitySets,
			TEXT("Advancement.") + Advancement->AdvancementId.ToString(), AdvancementHandles);
		GrantStyle(Advancement->CombatStyleOverride, AdvancementHandles);
		if (!IsMutationCurrent(Lifetime.Owner.Get(), Lifetime.ASC.Get())) { return false; }
	}
	Publish();
	return IsMutationCurrent(Lifetime.Owner.Get(), Lifetime.ASC.Get());
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
 const FProgressionMutationLifetime Lifetime(this, GetASC());
 TGuardValue<bool> Guard(bChanging, true);
 Experience += Amount;
 while (GetNextLevelExperience() > 0 && Experience >= GetNextLevelExperience())
 {
  Experience -= GetNextLevelExperience(); ++State.Level;
 }
 if (GetNextLevelExperience() == 0) Experience = 0;
 // Once published, XP is paid. Returning failure after a notification retires the
 // owner would allow the quest reward caller to undo its claim and pay twice.
 Publish(); return true;
}
