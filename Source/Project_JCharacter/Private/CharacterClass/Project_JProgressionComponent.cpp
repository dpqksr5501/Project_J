#include "CharacterClass/Project_JProgressionComponent.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "Net/UnrealNetwork.h"
#include "UObject/StrongObjectPtr.h"
#include "Project_JBaseCharacter.h"

namespace
{
bool IsStyleReady(const UProject_JCombatStyleDefinition *Style)
{
	return !Style || Style->IsRuntimeReady();
}
// GAS and publication delegates may synchronously retire participants and collect garbage.
// These pins last for one operation; persistent ASC ownership remains weak.
struct FProgressionMutationLifetime
{
	TStrongObjectPtr<UProject_JProgressionComponent> Component;
	TStrongObjectPtr<AActor> Owner;
	TStrongObjectPtr<UAbilitySystemComponent> ASC;
	FProgressionMutationLifetime(UProject_JProgressionComponent *InComponent, UAbilitySystemComponent *InASC)
		: Component(InComponent), Owner(InComponent->GetOwner()), ASC(InASC)
	{
	}
};
} // namespace

void UProject_JProgressionComponent::GrantSets(const TArray<TObjectPtr<UProject_JAbilitySet>> &Sets,
											   const FString &Prefix, FProject_JAbilitySet_GrantedHandles &Handles)
{
	TArray<TStrongObjectPtr<const UProject_JAbilitySet>> Snapshot;
	for (const UProject_JAbilitySet *Set : Sets)
	{
		if (Set)
			Snapshot.Emplace(Set);
	}
	for (const auto &PinnedSet : Snapshot)
	{
		if (!CanGrant())
		{
			return;
		}
		const auto *Set = PinnedSet.Get();
		Set->GiveToAbilitySystem(GrantedAbilitySystem.Get(), &Handles, GetOwner(),
								 FName(*(Prefix + TEXT(".") + Set->GetPathName())));
	}
}
void UProject_JProgressionComponent::GrantStyle(const UProject_JCombatStyleDefinition *Style,
												FProject_JAbilitySet_GrantedHandles &Handles)
{
	if (Style)
		GrantSets(Style->GetRuntimeAbilitySets(), TEXT("CombatStyle.") + Style->GetPathName(), Handles);
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
		if (!GrantedAbilitySystem.IsValid() && !bEnding)
		{
			GrantedAbilitySystem = GetASC();
		}
		for (const UProject_JCharacterAdvancementDefinition *Advancement : ActiveAdvancements)
		{
			if (!CanGrant())
			{
				break;
			}
			GrantSets(Advancement->AdditionalAbilitySets, TEXT("Advancement.") + Advancement->AdvancementId.ToString(),
					  AdvancementHandles);
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
	if (auto *ASC = GrantedAbilitySystem.Get())
	{
		const auto *Set = GetDefault<UProject_JAbilitySet>();
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

void UProject_JProgressionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty> &OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UProject_JProgressionComponent, State);
	DOREPLIFETIME_CONDITION(UProject_JProgressionComponent, Experience, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UProject_JProgressionComponent, AcquiredAdvancements, COND_OwnerOnly);
}

UAbilitySystemComponent *UProject_JProgressionComponent::GetASC() const
{
	const auto *Interface = Cast<IAbilitySystemInterface>(GetOwner());
	return Interface ? Interface->GetAbilitySystemComponent() : nullptr;
}

bool UProject_JProgressionComponent::CanMutate() const
{
	check(IsInGameThread());
	return !bEnding && !IsBeingDestroyed() && !bChanging && IsValid(GetOwner()) &&
		   !GetOwner()->IsActorBeingDestroyed() && GetOwner()->HasAuthority() && GetASC() &&
		   (!GrantedAbilitySystem.IsValid() || GetASC() == GrantedAbilitySystem.Get()) &&
		   (!PersistentAdmission || PersistentAdmission());
}

bool UProject_JProgressionComponent::IsMutationCurrent(const AActor *Owner, const UAbilitySystemComponent *ASC) const
{
	return !bEnding && !IsBeingDestroyed() && IsValid(Owner) && !Owner->IsActorBeingDestroyed() &&
		   GetOwner() == Owner && Owner->HasAuthority() && IsValid(ASC) && !ASC->IsBeingDestroyed() &&
		   GetASC() == ASC && (!GrantedAbilitySystem.IsValid() || GrantedAbilitySystem.Get() == ASC);
}

bool UProject_JProgressionComponent::ResolveLevel(int32 Level, const UProject_JCharacterClassDefinition *Class,
												  int32 &OutResolvedLevel) const
{
	const int32 Floor = Class ? Class->StartingLevel : 1;
	if (Floor < 1)
	{
		return false;
	}
	const int32 Resolved = FMath::Max(FMath::Max(1, Level), Floor);
	if (Resolved > FMath::Clamp(MaximumLevel, 1, 10000))
	{
		return false;
	}
	OutResolvedLevel = Resolved;
	return true;
}

bool UProject_JProgressionComponent::CanSetLevel(int32 Level, int32 &OutResolvedLevel) const
{
	return !SubmitPersistentEdit && CanMutate() && State.Revision != 0 &&
		   ResolveLevel(Level, State.ClassDefinition, OutResolvedLevel);
}

void UProject_JProgressionComponent::Publish()
{
	if (bEnding || IsBeingDestroyed() || !IsValid(GetOwner()) || GetOwner()->IsActorBeingDestroyed())
	{
		return;
	}
	State.Revision = State.Revision == MAX_int32 ? 1 : State.Revision + 1;
	GetOwner()->ForceNetUpdate();
	OnChanged.Broadcast();
}
void UProject_JProgressionComponent::OnRep_State()
{
	if (!bEnding)
		OnChanged.Broadcast();
}

void UProject_JProgressionComponent::GrantClass()
{
	if (!State.ClassDefinition)
		return;
	if (!GrantedAbilitySystem.IsValid())
	{
		GrantedAbilitySystem = GetASC();
	}
	GrantSets(State.ClassDefinition->AbilitySets, TEXT("Class.") + State.ClassDefinition->ClassId.ToString(),
			  ClassHandles);
	GrantStyle(State.ClassDefinition->DefaultCombatStyle, ClassHandles);
}

void UProject_JProgressionComponent::InitializeDefaults(UProject_JCharacterClassDefinition *Class,
														UProject_JCharacterAdvancementDefinition *Advancement,
														int32 Level)
{
	if (!CanMutate() || State.Revision != 0 || (Class && !IsStyleReady(Class->DefaultCombatStyle)) ||
		(Advancement && (!IsStyleReady(Advancement->CombatStyleOverride) ||
						 (Advancement->BaseClass && !IsStyleReady(Advancement->BaseClass->DefaultCombatStyle)))))
		return;
	auto *InitialClass = Class ? Class : (Advancement ? Advancement->BaseClass.Get() : nullptr);
	int32 ResolvedLevel;
	if (!ResolveLevel(Level, InitialClass, ResolvedLevel))
	{
		return;
	}
	const FProgressionMutationLifetime Lifetime(this, GetASC());
	// Avatar defaults are applied once. A respawn must never roll persistent state back.
	{
		TGuardValue<bool> Guard(bChanging, true);
		State.ClassDefinition = InitialClass;
		State.Level = ResolvedLevel;
		GrantClass();
		if (IsMutationCurrent(Lifetime.Owner.Get(), Lifetime.ASC.Get()))
		{
			Publish();
		}
	}
	if (Advancement)
		ApplyAdvancement(Advancement);
}

bool UProject_JProgressionComponent::InitializeClass(UProject_JCharacterClassDefinition *Class)
{
	if (SubmitPersistentEdit)
	{
		FProject_JProgressionSnapshot Planned;
		if (!PlanClass(Class, Planned))
			return false;
		FProject_JPersistentEdit Edit;
		Edit.Kind = EProject_JPersistentEdit::Class;
		Edit.Request = CreatePersistentRequest ? CreatePersistentRequest() : FGuid();
		Edit.ProgressionDefinition = Class->ClassId;
		return SubmitPersistentEdit(Edit, {});
	}
	if (!CanMutate() || !Class || Class->ClassId.IsNone() || !IsStyleReady(Class->DefaultCombatStyle))
		return false;
	int32 ResolvedLevel;
	if (!ResolveLevel(State.Level, Class, ResolvedLevel))
	{
		return false;
	}
	if (State.ClassDefinition)
		return State.ClassDefinition == Class && State.Level == ResolvedLevel;
	const FProgressionMutationLifetime Lifetime(this, GetASC());
	TGuardValue<bool> Guard(bChanging, true);
	State.ClassDefinition = Class;
	State.Level = ResolvedLevel;
	GrantClass();
	if (!IsMutationCurrent(Lifetime.Owner.Get(), Lifetime.ASC.Get()))
	{
		return false;
	}
	Publish();
	return IsMutationCurrent(Lifetime.Owner.Get(), Lifetime.ASC.Get());
}

bool UProject_JProgressionComponent::CanApplyAdvancement(
	const UProject_JCharacterAdvancementDefinition *Advancement) const
{
	if (!CanMutate() || !Advancement || Advancement->AdvancementId.IsNone() || AcquiredAdvancements.Num() >= 256 ||
		AcquiredAdvancements.Contains(Advancement->AdvancementId) || !IsStyleReady(Advancement->CombatStyleOverride) ||
		(Advancement->BaseClass && !IsStyleReady(Advancement->BaseClass->DefaultCombatStyle)) ||
		State.Level < Advancement->RequiredLevel ||
		(State.ClassDefinition && Advancement->BaseClass && State.ClassDefinition != Advancement->BaseClass))
		return false;
	int32 ResolvedLevel;
	if (!ResolveLevel(State.Level, State.ClassDefinition ? State.ClassDefinition.Get() : Advancement->BaseClass.Get(),
					  ResolvedLevel) ||
		(State.ClassDefinition && State.Level != ResolvedLevel))
	{
		return false;
	}
	for (FName Prerequisite : Advancement->RequiredAdvancementIds)
		if (!AcquiredAdvancements.Contains(Prerequisite))
			return false;
	if (!Advancement->ExclusiveBranch.IsNone())
	{
		for (const UProject_JCharacterAdvancementDefinition *Active : ActiveAdvancements)
			if (Active && Active->ExclusiveBranch == Advancement->ExclusiveBranch &&
				Advancement->AbilityGrantPolicy != EProject_JAdvancementAbilityGrantPolicy::ReplacePreviousAdvancement)
				return false;
	}
	FGameplayTagContainer Tags;
	GetASC()->GetOwnedGameplayTags(Tags);
	return Tags.HasAll(Advancement->RequiredTags) && !Tags.HasAny(Advancement->BlockedTags);
}

bool UProject_JProgressionComponent::ApplyAdvancement(UProject_JCharacterAdvancementDefinition *Advancement)
{
	if (!CanApplyAdvancement(Advancement))
		return false;
	if (SubmitPersistentEdit)
	{
		FProject_JPersistentEdit Edit;
		Edit.Kind = EProject_JPersistentEdit::Advancement;
		Edit.Request = CreatePersistentRequest ? CreatePersistentRequest() : FGuid();
		Edit.ProgressionDefinition = Advancement->AdvancementId;
		return SubmitPersistentEdit(Edit, {});
	}
	const FProgressionMutationLifetime Lifetime(this, GetASC());
	int32 ResolvedLevel;
	if (!ResolveLevel(State.Level, State.ClassDefinition ? State.ClassDefinition.Get() : Advancement->BaseClass.Get(),
					  ResolvedLevel))
	{
		return false;
	}
	TGuardValue<bool> Guard(bChanging, true);
	if (!GrantedAbilitySystem.IsValid())
	{
		GrantedAbilitySystem = GetASC();
	}
	if (Advancement->AbilityGrantPolicy == EProject_JAdvancementAbilityGrantPolicy::ReplacePreviousAdvancement)
	{
		GetDefault<UProject_JAbilitySet>()->TakeFromAbilitySystem(GetASC(), &AdvancementHandles);
		if (!CanGrant())
		{
			return false;
		}
		ActiveAdvancements.Reset();
	}
	if (!State.ClassDefinition && Advancement->BaseClass)
	{
		State.ClassDefinition = Advancement->BaseClass;
		State.Level = ResolvedLevel;
		GrantClass();
		if (!CanGrant())
		{
			return false;
		}
	}
	State.Advancement = Advancement;
	AcquiredAdvancements.Add(Advancement->AdvancementId);
	ActiveAdvancements.Add(Advancement);
	if (!GrantedAbilitySystem.IsValid())
	{
		GrantedAbilitySystem = GetASC();
	}
	GrantSets(Advancement->AdditionalAbilitySets, TEXT("Advancement.") + Advancement->AdvancementId.ToString(),
			  AdvancementHandles);
	GrantStyle(Advancement->CombatStyleOverride, AdvancementHandles);
	if (!IsMutationCurrent(Lifetime.Owner.Get(), Lifetime.ASC.Get()))
	{
		return false;
	}
	Publish();
	return IsMutationCurrent(Lifetime.Owner.Get(), Lifetime.ASC.Get());
}

bool UProject_JProgressionComponent::SetLevel(int32 Level)
{
	int32 ResolvedLevel;
	if (!CanSetLevel(Level, ResolvedLevel))
		return false;
	Level = ResolvedLevel;
	if (State.Level == Level)
		return true;
	const FProgressionMutationLifetime Lifetime(this, GetASC());
	TGuardValue<bool> Guard(bChanging, true);
	State.Level = Level;
	Experience = 0;
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

bool UProject_JProgressionComponent::ValidateRestoreSnapshot(
	const FProject_JProgressionSnapshot &Snapshot, UProject_JCharacterClassDefinition *Class,
	const TArray<UProject_JCharacterAdvancementDefinition *> &OrderedHistory,
	TArray<TObjectPtr<UProject_JCharacterAdvancementDefinition>> &Active, bool bCommitted) const
{
	if ((bCommitted ? (bChanging || !IsMutationCurrent(GetOwner(), GetASC()))
					: (!CanMutate() || State.Revision != 0)) ||
		(Snapshot.SchemaVersion != 1 && Snapshot.SchemaVersion != 2) || Snapshot.Level < 1 ||
		Snapshot.Level > FMath::Clamp(MaximumLevel, 1, 10000) || Snapshot.Experience < 0 ||
		Snapshot.AdvancementHistory.Num() > 256 || Snapshot.AdvancementHistory.Num() != OrderedHistory.Num() ||
		Snapshot.ClassId != (Class ? Class->ClassId : NAME_None) ||
		(Class && (Snapshot.Level < Class->StartingLevel || !IsStyleReady(Class->DefaultCombatStyle))))
		return false;
	const int64 RequiredXP = GetRequiredExperienceForLevel(Snapshot.Level);
	if (Snapshot.SchemaVersion >= 2 && (RequiredXP == 0 ? Snapshot.Experience != 0 : Snapshot.Experience >= RequiredXP))
		return false;
	Active.Reset();
	TSet<FName> Seen;
	for (int32 Index = 0; Index < OrderedHistory.Num(); ++Index)
	{
		auto *Advancement = OrderedHistory[Index];
		if (!Advancement || Advancement->AdvancementId.IsNone() || Seen.Contains(Advancement->AdvancementId) ||
			!IsStyleReady(Advancement->CombatStyleOverride) ||
			Advancement->AdvancementId != Snapshot.AdvancementHistory[Index] ||
			(Advancement->BaseClass && Advancement->BaseClass != Class))
			return false;
		for (FName Prerequisite : Advancement->RequiredAdvancementIds)
			if (!Seen.Contains(Prerequisite))
				return false;
		if (Advancement->AbilityGrantPolicy == EProject_JAdvancementAbilityGrantPolicy::ReplacePreviousAdvancement)
			Active.Reset();
		else if (!Advancement->ExclusiveBranch.IsNone())
			for (const UProject_JCharacterAdvancementDefinition *Other : Active)
				if (Other->ExclusiveBranch == Advancement->ExclusiveBranch)
					return false;
		Seen.Add(Advancement->AdvancementId);
		Active.Add(Advancement);
	}
	return true;
}
bool UProject_JProgressionComponent::CanRestoreSnapshot(
	const FProject_JProgressionSnapshot &Snapshot, UProject_JCharacterClassDefinition *Class,
	const TArray<UProject_JCharacterAdvancementDefinition *> &OrderedHistory) const
{
	TArray<TObjectPtr<UProject_JCharacterAdvancementDefinition>> Active;
	return ValidateRestoreSnapshot(Snapshot, Class, OrderedHistory, Active);
}
bool UProject_JProgressionComponent::RestoreSnapshot(
	const FProject_JProgressionSnapshot &Snapshot, UProject_JCharacterClassDefinition *Class,
	const TArray<UProject_JCharacterAdvancementDefinition *> &OrderedHistory)
{
	TArray<TObjectPtr<UProject_JCharacterAdvancementDefinition>> Active;
	if (!ValidateRestoreSnapshot(Snapshot, Class, OrderedHistory, Active))
		return false;
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
	if (!IsMutationCurrent(Lifetime.Owner.Get(), Lifetime.ASC.Get()))
	{
		return false;
	}
	for (const UProject_JCharacterAdvancementDefinition *Advancement : ActiveAdvancements)
	{
		if (!GrantedAbilitySystem.IsValid() && !bEnding)
		{
			GrantedAbilitySystem = GetASC();
		}
		GrantSets(Advancement->AdditionalAbilitySets, TEXT("Advancement.") + Advancement->AdvancementId.ToString(),
				  AdvancementHandles);
		GrantStyle(Advancement->CombatStyleOverride, AdvancementHandles);
		if (!IsMutationCurrent(Lifetime.Owner.Get(), Lifetime.ASC.Get()))
		{
			return false;
		}
	}
	Publish();
	return IsMutationCurrent(Lifetime.Owner.Get(), Lifetime.ASC.Get());
}

void UProject_JProgressionComponent::OnRep_Experience()
{
	if (!bEnding)
		OnChanged.Broadcast();
}
int64 UProject_JProgressionComponent::GetNextLevelExperience() const
{
	return GetRequiredExperienceForLevel(State.Level);
}
int64 UProject_JProgressionComponent::GetRequiredExperienceForLevel(int32 Level) const
{
	if (Level >= FMath::Clamp(MaximumLevel, 1, 10000))
		return 0;
	const int32 Index = Level - 1;
	return ExperienceRequirements.IsValidIndex(Index)
			   ? FMath::Clamp<int64>(ExperienceRequirements[Index], 1, MAX_int64 / 10000)
			   : 100LL * FMath::Max(1, Level);
}
bool UProject_JProgressionComponent::GrantExperience(int64 Amount)
{
	if (SubmitPersistentEdit)
	{
		if (!CanMutate())
			return false;
		FProject_JPersistentEdit Edit;
		Edit.Kind = EProject_JPersistentEdit::Experience;
		Edit.Request = CreatePersistentRequest ? CreatePersistentRequest() : FGuid();
		Edit.Amount = Amount;
		return SubmitPersistentEdit(Edit, {}); // Accepted asynchronously; publication follows durable confirmation.
	}
	if (!CanMutate() || State.Revision == 0 || Amount <= 0 || Amount > MAX_int64 - Experience ||
		GetNextLevelExperience() <= 0)
		return false;
	const FProgressionMutationLifetime Lifetime(this, GetASC());
	TGuardValue<bool> Guard(bChanging, true);
	Experience += Amount;
	while (GetNextLevelExperience() > 0 && Experience >= GetNextLevelExperience())
	{
		Experience -= GetNextLevelExperience();
		++State.Level;
	}
	if (GetNextLevelExperience() == 0)
		Experience = 0;
	// Once published, XP is paid. Returning failure after a notification retires the
	// owner would allow the quest reward caller to undo its claim and pay twice.
	Publish();
	return true;
}
bool UProject_JProgressionComponent::PlanExperience(int64 Amount, FProject_JProgressionSnapshot &Out) const
{
	if (!CanMutate() || State.Revision == 0 || Amount <= 0 || Amount > MAX_int64 - Experience ||
		GetNextLevelExperience() <= 0)
		return false;
	Out = CaptureSnapshot();
	Out.Experience += Amount;
	while (GetRequiredExperienceForLevel(Out.Level) > 0 && Out.Experience >= GetRequiredExperienceForLevel(Out.Level))
	{
		Out.Experience -= GetRequiredExperienceForLevel(Out.Level);
		++Out.Level;
	}
	if (!GetRequiredExperienceForLevel(Out.Level))
		Out.Experience = 0;
	return true;
}
bool UProject_JProgressionComponent::ValidateCommittedExperience(const FProject_JProgressionSnapshot &Snapshot,
																 bool bTrustedApply) const
{
	const auto Current = CaptureSnapshot();
	const int32 Floor = State.ClassDefinition ? State.ClassDefinition->StartingLevel : 1;
	const bool Admitted = bTrustedApply ? (!bChanging && IsMutationCurrent(GetOwner(), GetASC())) : CanMutate();
	return Admitted && State.Revision > 0 && Snapshot.SchemaVersion == Current.SchemaVersion &&
		   Snapshot.ClassId == Current.ClassId && Snapshot.AdvancementHistory == Current.AdvancementHistory &&
		   Snapshot.Level >= FMath::Max(1, Floor) && Snapshot.Level <= FMath::Clamp(MaximumLevel, 1, 10000) &&
		   Snapshot.Experience >= 0 &&
		   (GetRequiredExperienceForLevel(Snapshot.Level)
				? Snapshot.Experience < GetRequiredExperienceForLevel(Snapshot.Level)
				: Snapshot.Experience == 0);
}
void UProject_JProgressionComponent::InstallCommittedExperience(const FProject_JProgressionSnapshot &Snapshot)
{
	check(IsInGameThread());
	check(ValidateCommittedExperience(Snapshot, true));
	State.Level = Snapshot.Level;
	Experience = Snapshot.Experience;
}

bool UProject_JProgressionComponent::PlanClass(UProject_JCharacterClassDefinition *Class,
											   FProject_JProgressionSnapshot &Out) const
{
	int32 Level = 0;
	if (!CanMutate() || !Class || Class->ClassId.IsNone() || !IsStyleReady(Class->DefaultCombatStyle) ||
		!ResolveLevel(State.Level, Class, Level) || (State.ClassDefinition && State.ClassDefinition != Class))
		return false;
	Out = CaptureSnapshot();
	Out.ClassId = Class->ClassId;
	Out.Level = Level;
	return true;
}
bool UProject_JProgressionComponent::PlanAdvancement(UProject_JCharacterAdvancementDefinition *Adv,
													 FProject_JProgressionSnapshot &Out) const
{
	if (!CanApplyAdvancement(Adv))
		return false;
	Out = CaptureSnapshot();
	Out.AdvancementHistory.Add(Adv->AdvancementId);
	if (Out.ClassId.IsNone() && Adv->BaseClass)
		Out.ClassId = Adv->BaseClass->ClassId;
	return ResolveLevel(Out.Level, State.ClassDefinition ? State.ClassDefinition.Get() : Adv->BaseClass.Get(),
						Out.Level);
}
bool UProject_JProgressionComponent::ValidateCommittedProgression(
	const FProject_JProgressionSnapshot &Snapshot, UProject_JCharacterClassDefinition *Class,
	const TArray<UProject_JCharacterAdvancementDefinition *> &History, bool Trusted, bool AllowUninitialized) const
{
	TArray<TObjectPtr<UProject_JCharacterAdvancementDefinition>> Active;
	return (Trusted || CanMutate()) && (State.Revision > 0 || (AllowUninitialized && State.Revision == 0)) &&
		   ValidateRestoreSnapshot(Snapshot, Class, History, Active, true);
}
void UProject_JProgressionComponent::InstallCommittedProgression(
	const FProject_JProgressionSnapshot &Snapshot, UProject_JCharacterClassDefinition *Class,
	const TArray<UProject_JCharacterAdvancementDefinition *> &History)
{
	check(IsInGameThread());
	TArray<TObjectPtr<UProject_JCharacterAdvancementDefinition>> Active;
	const bool bValidSnapshot = ValidateRestoreSnapshot(Snapshot, Class, History, Active, true);
	checkf(bValidSnapshot, TEXT("Committed progression snapshot must have been validated"));
	if (!bValidSnapshot) { return; }
	bRebuildCommittedClass = State.ClassDefinition != Class;
	bRebuildCommittedAdvancements = false;
	CommittedGrantsToAdd.Reset();
	if (AcquiredAdvancements != Snapshot.AdvancementHistory)
	{
		bool AppendOnly = Snapshot.AdvancementHistory.Num() == AcquiredAdvancements.Num() + 1;
		for (int32 Index = 0; AppendOnly && Index < AcquiredAdvancements.Num(); ++Index)
			AppendOnly = AcquiredAdvancements[Index] == Snapshot.AdvancementHistory[Index];
		bRebuildCommittedAdvancements =
			bRebuildCommittedClass || !AppendOnly ||
			History.Last()->AbilityGrantPolicy == EProject_JAdvancementAbilityGrantPolicy::ReplacePreviousAdvancement;
		if (bRebuildCommittedAdvancements)
			CommittedGrantsToAdd = Active;
		else
			CommittedGrantsToAdd.Add(History.Last());
	}
	bInitializeCommittedAttributes = !State.ClassDefinition && Class;
	State.ClassDefinition = Class;
	State.Advancement = History.IsEmpty() ? nullptr : History.Last();
	State.Level = Snapshot.Level;
	Experience = Snapshot.SchemaVersion >= 2 ? Snapshot.Experience : 0;
	AcquiredAdvancements = Snapshot.AdvancementHistory;
	ActiveAdvancements = MoveTemp(Active);
}
void UProject_JProgressionComponent::PublishCommittedExperience()
{
	const FProgressionMutationLifetime Lifetime(this, GetASC());
	if (bRebuildCommittedClass || bRebuildCommittedAdvancements || !CommittedGrantsToAdd.IsEmpty())
	{
		const bool RebuildClass = bRebuildCommittedClass, RebuildAdv = bRebuildCommittedAdvancements;
		bRebuildCommittedClass = bRebuildCommittedAdvancements = false;
		TArray<TStrongObjectPtr<UProject_JCharacterAdvancementDefinition>> Grants;
		for (UProject_JCharacterAdvancementDefinition *Adv : CommittedGrantsToAdd)
			Grants.Emplace(Adv);
		CommittedGrantsToAdd.Reset();
		TGuardValue<bool> Guard(bChanging, true);
		auto *Set = GetDefault<UProject_JAbilitySet>();
		if (auto *ASC = GrantedAbilitySystem.Get())
		{
			if (RebuildClass)
				Set->TakeFromAbilitySystem(ASC, &ClassHandles);
			if (RebuildAdv)
				Set->TakeFromAbilitySystem(ASC, &AdvancementHandles);
		}
		if (!IsMutationCurrent(Lifetime.Owner.Get(), Lifetime.ASC.Get()))
			return;
		if (RebuildClass)
			GrantClass();
		if (!GrantedAbilitySystem.IsValid())
			GrantedAbilitySystem = GetASC();
		for (const auto &PinnedAdv : Grants)
		{
			const auto *Adv = PinnedAdv.Get();
			if (!CanGrant())
				return;
			GrantSets(Adv->AdditionalAbilitySets, TEXT("Advancement.") + Adv->AdvancementId.ToString(),
					  AdvancementHandles);
			GrantStyle(Adv->CombatStyleOverride, AdvancementHandles);
		}
	}
	if (!IsMutationCurrent(Lifetime.Owner.Get(), Lifetime.ASC.Get()))
		return;
	if (bInitializeCommittedAttributes)
	{
		bInitializeCommittedAttributes = false;
		if (auto *Avatar = Cast<AProject_JBaseCharacter>(GetASC()->GetAvatarActor()))
			Avatar->InitializeDefaultAttributes(true);
	}
	if (IsMutationCurrent(Lifetime.Owner.Get(), Lifetime.ASC.Get()))
		Publish();
}
