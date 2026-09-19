#include "Combat/Project_JCombatStyleDefinition.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Combat/Project_JComboDefinition.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace
{
bool CollectAttacks(const UProject_JCombatStyleDefinition& Style,
	TArray<TObjectPtr<UProject_JAttackDefinition>>& Attacks, TArray<FText>& Errors)
{
	TMap<FGameplayTag, const UProject_JAttackDefinition*> ByTag;
	auto Add = [&](UProject_JAttackDefinition* Attack)
	{
		if (!Attack || !Attack->AttackTag.IsValid())
		{
			Errors.Add(FText::FromString(TEXT("Attack references must be non-null with valid AttackTag.")));
			return;
		}
		if (const auto* Existing = ByTag.Find(Attack->AttackTag))
		{
			if (*Existing != Attack) Errors.Add(FText::FromString(TEXT("Different attack definitions use the same AttackTag: ") + Attack->AttackTag.ToString()));
			return;
		}
		ByTag.Add(Attack->AttackTag, Attack);
		Attacks.Add(Attack);
	};
	if (Style.bDeriveAttackCatalog)
	{
		if (Style.ComboDefinition) for (const auto& Node : Style.ComboDefinition->Nodes) Add(Node.AttackDefinition);
		for (UProject_JAttackDefinition* Attack : Style.AdditionalAttacks) Add(Attack);
	}
	else if (Style.AttackSet) for (UProject_JAttackDefinition* Attack : Style.AttackSet->Attacks) Add(Attack);
	if (Style.bUsesCombo && Style.ComboDefinition)
	{
		for (const auto& Node : Style.ComboDefinition->Nodes)
		{
			if (!Node.AttackDefinition || !Node.AttackDefinition->Montage)
				Errors.Add(FText::FromString(TEXT("Combo nodes require a montage-backed attack.")));
			if (Node.AttackDefinition && ByTag.FindRef(Node.AttackDefinition->AttackTag) != Node.AttackDefinition)
				Errors.Add(FText::FromString(TEXT("Combo attack is absent from, or conflicts with, the attack catalog.")));
		}
	}
	return Errors.IsEmpty();
}
}

bool UProject_JCombatStyleDefinition::ValidateDefinition(TArray<FText>& Errors) const
{
	Errors.Reset();
	if (!CombatStyleTag.IsValid()) Errors.Add(FText::FromString(TEXT("CombatStyleTag is required.")));
	if (SchemaVersion < 1) Errors.Add(FText::FromString(TEXT("SchemaVersion must be positive.")));
	if (bRequiresWeaponAnimation && !WeaponAnimationProfile) Errors.Add(FText::FromString(TEXT("This style requires WeaponAnimationProfile.")));
	if (bUsesCombo && (!ComboDefinition || ComboDefinition->Nodes.IsEmpty())) Errors.Add(FText::FromString(TEXT("Combo styles require a nonempty ComboDefinition.")));
	if (bDeriveAttackCatalog && AttackSet) Errors.Add(FText::FromString(TEXT("Choose either a derived catalog or an authored AttackSet, not both.")));
	TArray<TObjectPtr<UProject_JAttackDefinition>> Attacks;
	CollectAttacks(*this, Attacks, Errors);
	TSet<FGameplayTag> Inputs;
	auto CheckEntries = [&](const TArray<FProject_JAbilitySet_GameplayAbility>& Entries)
	{
		for (const auto& Entry : Entries)
		{
			if (!Entry.Ability || Entry.AbilityLevel < 1) Errors.Add(FText::FromString(TEXT("Ability entries require an execution class and a positive level.")));
			FGameplayTagContainer Tags = Entry.AdditionalInputTags;
			if (Entry.InputTag.IsValid()) Tags.AddTag(Entry.InputTag);
			for (auto Tag : Tags)
			{
				if (Inputs.Contains(Tag)) Errors.Add(FText::FromString(TEXT("Multiple abilities consume the same input: ") + Tag.ToString()));
				Inputs.Add(Tag);
			}
		}
	};
	auto CheckEffects = [&](const TArray<FProject_JAbilitySet_GameplayEffect>& Entries)
	{
		for (const auto& Entry : Entries)
			if (!Entry.Effect || !FMath::IsFinite(Entry.EffectLevel) || Entry.EffectLevel <= 0)
				Errors.Add(FText::FromString(TEXT("Effect entries require an execution class and a finite positive level.")));
	};
	TSet<const UProject_JAbilitySet*> SeenSets;
	for (const UProject_JAbilitySet* Set : AbilitySets)
	{
		if (!Set || SeenSets.Contains(Set)) { Errors.Add(FText::FromString(TEXT("AbilitySets contains a null or duplicate set."))); continue; }
		SeenSets.Add(Set);
		CheckEntries(Set->GrantedAbilityEntries);
		CheckEffects(Set->GrantedEffectEntries);
	}
	CheckEntries(InlineAbilities);
	CheckEffects(InlineEffects);
	return Errors.IsEmpty();
}

void UProject_JCombatStyleDefinition::BuildRuntimeData()
{
	check(IsInGameThread());
	if (bRuntimeDataBuilt) return;
	bRuntimeDataBuilt = true;
	TArray<FText> ValidationErrors;
	if (!ValidateDefinition(ValidationErrors)) return;
	bRuntimeDataValid = true;
	RuntimeAbilitySets = AbilitySets;
	if (!InlineAbilities.IsEmpty() || !InlineEffects.IsEmpty())
	{
		auto* Local = NewObject<UProject_JAbilitySet>(this, NAME_None, RF_Transient);
		Local->GrantedAbilityEntries = InlineAbilities;
		Local->GrantedEffectEntries = InlineEffects;
		RuntimeAbilitySets.Add(Local);
	}
	if (bDeriveAttackCatalog)
	{
		TArray<TObjectPtr<UProject_JAttackDefinition>> Attacks;
		TArray<FText> Errors;
		if (CollectAttacks(*this, Attacks, Errors))
		{
			RuntimeAttackSet = NewObject<UProject_JAttackSet>(this, NAME_None, RF_Transient);
			RuntimeAttackSet->Attacks = MoveTemp(Attacks);
		}
	}
	bRuntimeDataBuilt = true;
}

const TArray<TObjectPtr<UProject_JAbilitySet>>& UProject_JCombatStyleDefinition::GetRuntimeAbilitySets() const
{
	check(IsInGameThread());
	if (!bDeriveAttackCatalog && InlineAbilities.IsEmpty() && InlineEffects.IsEmpty()) return AbilitySets;
	const_cast<UProject_JCombatStyleDefinition*>(this)->BuildRuntimeData();
	return RuntimeAbilitySets;
}

const UProject_JAttackSet* UProject_JCombatStyleDefinition::GetRuntimeAttackSet() const
{
	check(IsInGameThread());
	if (!bDeriveAttackCatalog) return AttackSet;
	const_cast<UProject_JCombatStyleDefinition*>(this)->BuildRuntimeData();
	return RuntimeAttackSet;
}

bool UProject_JCombatStyleDefinition::IsRuntimeReady() const
{
	check(IsInGameThread());
	// Legacy assets keep their original runtime contract. Opt-in authoring compiles atomically.
	if (!bDeriveAttackCatalog && InlineAbilities.IsEmpty() && InlineEffects.IsEmpty()) return true;
	const_cast<UProject_JCombatStyleDefinition*>(this)->BuildRuntimeData();
	return bRuntimeDataValid;
}

void UProject_JCombatStyleDefinition::InvalidateRuntimeData()
{
	check(IsInGameThread());
	bRuntimeDataBuilt = false;
	bRuntimeDataValid = false;
	RuntimeAbilitySets.Reset();
	RuntimeAttackSet = nullptr;
}

#if WITH_EDITOR
void UProject_JCombatStyleDefinition::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	InvalidateRuntimeData();
	Super::PostEditChangeProperty(Event);
}
EDataValidationResult UProject_JCombatStyleDefinition::IsDataValid(FDataValidationContext& Context) const
{
	TArray<FText> Errors;
	const bool bValid = ValidateDefinition(Errors);
	for (const auto& Error : Errors) Context.AddError(Error);
	return Super::IsDataValid(Context) == EDataValidationResult::Invalid || !bValid ? EDataValidationResult::Invalid : EDataValidationResult::Valid;
}
#endif
