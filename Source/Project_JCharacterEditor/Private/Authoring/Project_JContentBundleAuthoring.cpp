#include "Authoring/Project_JContentBundleAuthoring.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Combat/Project_JComboDefinition.h"
#include "Editor.h"
#include "Misc/DataValidation.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace ProjectJ::ContentAuthoring
{
TArray<FString> FBundlePlan::GetPackages() const
{
	TArray<FString> Result{RootPackage, StylePackage};
	if (!ComboPackage.IsEmpty()) Result.Add(ComboPackage);
	return Result;
}

bool MakePlan(const UProject_JContentBundleRequest& Request, FBundlePlan& OutPlan, TArray<FText>& Errors)
{
	Errors.Reset();
	OutPlan = {};
	const FString& Id = Request.Identifier;
	bool bValidId = !Id.IsEmpty() && Id.Len() <= 64 && Id != TEXT("None") && Id != TEXT("none");
	for (TCHAR C : Id)
	{
		bValidId &= (C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '_';
	}
	if (!bValidId || FName(*Id).IsNone())
	{
		Errors.Add(FText::FromString(TEXT("Identifier must be 1-64 letters, digits or underscores, and cannot be None.")));
		return false;
	}
	FText PathReason;
	if (!(Request.DestinationFolder == TEXT("/Game/DataAssetSets") || Request.DestinationFolder.StartsWith(TEXT("/Game/DataAssetSets/"))) ||
		!FPackageName::IsValidLongPackageName(Request.DestinationFolder, false, &PathReason) || Request.DestinationFolder.EndsWith(TEXT("/")))
	{
		Errors.Add(FText::FromString(TEXT("Choose a valid folder inside /Game/DataAssetSets, without a trailing slash.")));
	}
	const auto* Base = Request.BaseClass.Get();
	if (!Base || Base->ClassId.IsNone() || Base->StartingLevel < 1 || Base->SchemaVersion < 1)
		Errors.Add(FText::FromString(TEXT("Select a base class with a valid ClassId, level and schema version.")));
	if (Request.Kind != EProject_JContentBundleKind::CharacterClass && Request.Kind != EProject_JContentBundleKind::Advancement)
		Errors.Add(FText::FromString(TEXT("Unknown bundle kind.")));
	if (Base && Request.Kind == EProject_JContentBundleKind::CharacterClass && Base->ClassId == FName(*Id))
		Errors.Add(FText::FromString(TEXT("The new class must have a different ClassId from its template.")));
	const auto* Previous = Request.PreviousAdvancement.Get();
	if (Request.Kind == EProject_JContentBundleKind::Advancement && Previous &&
		(Previous->BaseClass != Base || Previous->AdvancementId.IsNone() || Previous->AdvancementId == FName(*Id) || Previous->RequiredLevel < 1))
		Errors.Add(FText::FromString(TEXT("Previous advancement must belong to this base class and have a different valid ID and positive level.")));
	const auto* Style = Request.StyleTemplate ? Request.StyleTemplate.Get() : (Base ? Base->DefaultCombatStyle.Get() : nullptr);
	if (!Style) Errors.Add(FText::FromString(TEXT("Select a style template or a base class with a default combat style.")));
	if (!Errors.IsEmpty()) return false;
	const bool bAdvancement = Request.Kind == EProject_JContentBundleKind::Advancement;
	OutPlan.RootPackage = Request.DestinationFolder / ((bAdvancement ? TEXT("DA_Advancement_") : TEXT("DA_Class_")) + Id);
	OutPlan.StylePackage = Request.DestinationFolder / (TEXT("DA_Style_") + Id);
	if (Style->ComboDefinition) OutPlan.ComboPackage = Request.DestinationFolder / (TEXT("DA_Combo_") + Id);
	OutPlan.RegistrationLine = FString::Printf(TEXT("+%s=%s.%s"), bAdvancement ? TEXT("AdvancementDefinitions") : TEXT("ClassDefinitions"),
		*OutPlan.RootPackage, *FPackageName::GetLongPackageAssetName(OutPlan.RootPackage));
	return true;
}

UProject_JContentBundleDraft* BuildDraft(const UProject_JContentBundleRequest& Request, TArray<FText>& Errors)
{
	check(IsInGameThread());
	FBundlePlan Plan;
	if (!MakePlan(Request, Plan, Errors)) return nullptr;
	auto* Draft = NewObject<UProject_JContentBundleDraft>();
	const auto* Template = Request.StyleTemplate ? Request.StyleTemplate.Get() : Request.BaseClass->DefaultCombatStyle.Get();
	Draft->Style = DuplicateObject<UProject_JCombatStyleDefinition>(Template, Draft, TEXT("Style"));
	Draft->Style->ClearFlags(RF_Public | RF_Standalone);
	Draft->Style->InvalidateRuntimeData();
	if (Template->ComboDefinition)
	{
		Draft->Combo = DuplicateObject<UProject_JComboDefinition>(Template->ComboDefinition, Draft, TEXT("Combo"));
		Draft->Combo->ClearFlags(RF_Public | RF_Standalone);
		Draft->Style->ComboDefinition = Draft->Combo;
	}
	// Keep non-combo attacks from legacy catalogs while eliminating the separately maintained catalog.
	if (!Template->bDeriveAttackCatalog && Template->AttackSet)
	{
		for (UProject_JAttackDefinition* Attack : Template->AttackSet->Attacks)
		{
			const bool bInCombo = Template->ComboDefinition && Template->ComboDefinition->Nodes.ContainsByPredicate(
				[Attack](const FProject_JComboNode& Node) { return Node.AttackDefinition == Attack; });
			if (!bInCombo) Draft->Style->AdditionalAttacks.AddUnique(Attack);
		}
	}
	Draft->Style->AttackSet = nullptr;
	Draft->Style->bDeriveAttackCatalog = true;
	if (Request.Kind == EProject_JContentBundleKind::CharacterClass)
	{
		auto* Class = DuplicateObject<UProject_JCharacterClassDefinition>(Request.BaseClass, Draft, TEXT("Class"));
		Class->ClassId = FName(*Request.Identifier);
		Class->DefaultCombatStyle = Draft->Style;
		Class->ClearFlags(RF_Public | RF_Standalone);
		Draft->Root = Class;
	}
	else
	{
		// A fresh advancement deliberately does not inherit exclusive branches or replacement policies.
		auto* Advancement = NewObject<UProject_JCharacterAdvancementDefinition>(Draft, TEXT("Advancement"));
		Advancement->AdvancementId = FName(*Request.Identifier);
		Advancement->BaseClass = Request.BaseClass;
		Advancement->CombatStyleOverride = Draft->Style;
		Advancement->RequiredLevel = Request.BaseClass->StartingLevel;
		if (Request.PreviousAdvancement)
		{
			Advancement->RequiredAdvancementIds.Add(Request.PreviousAdvancement->AdvancementId);
			Advancement->RequiredLevel = FMath::Max(Advancement->RequiredLevel, Request.PreviousAdvancement->RequiredLevel);
		}
		Draft->Root = Advancement;
	}
	Draft->Style->ValidateDefinition(Errors);
	if (Draft->Combo)
	{
		FDataValidationContext Context;
		if (Draft->Combo->IsDataValid(Context) == EDataValidationResult::Invalid)
		{
			TArray<FText> Warnings;
			Context.SplitIssues(Warnings, Errors);
		}
	}
	return Errors.IsEmpty() ? Draft : nullptr;
}

static bool CheckExisting(const UProject_JContentBundleRequest& Request, const FBundlePlan& Plan, TArray<FText>& Errors)
{
	const auto* Style = Request.StyleTemplate ? Request.StyleTemplate.Get() : Request.BaseClass->DefaultCombatStyle.Get();
	if (!Request.BaseClass->IsAsset() || !Style->IsAsset() ||
		(Request.Kind == EProject_JContentBundleKind::Advancement && Request.PreviousAdvancement && !Request.PreviousAdvancement->IsAsset()))
	{
		Errors.Add(FText::FromString(TEXT("Use project assets as templates, not transient runtime objects.")));
		return false;
	}
	if (GEditor && GEditor->PlayWorld)
	{
		Errors.Add(FText::FromString(TEXT("Finish Play/Simulate before creating a content bundle.")));
		return false;
	}
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	if (Registry.IsLoadingAssets())
	{
		Errors.Add(FText::FromString(TEXT("Wait for the Content Browser asset discovery to finish, then retry.")));
		return false;
	}
	for (const FString& PackageName : Plan.GetPackages())
	{
		TArray<FAssetData> Existing;
		Registry.GetAssetsByPackageName(FName(*PackageName), Existing);
		if (!Existing.IsEmpty() || FindPackage(nullptr, *PackageName) || FPackageName::DoesPackageExist(PackageName))
			Errors.Add(FText::FromString(TEXT("Destination already exists; nothing will be overwritten: ") + PackageName));
	}
	// IDs are runtime registry keys, not asset names. Check this narrow asset family, including unsaved assets.
	TArray<FAssetData> Definitions;
	UClass* DefinitionClass = Request.Kind == EProject_JContentBundleKind::CharacterClass
		? UProject_JCharacterClassDefinition::StaticClass() : UProject_JCharacterAdvancementDefinition::StaticClass();
	Registry.GetAssetsByClass(DefinitionClass->GetClassPathName(), Definitions, true);
	for (const FAssetData& Definition : Definitions)
	{
		UObject* Asset = Definition.GetAsset();
		if (!Asset)
		{
			Errors.Add(FText::FromString(TEXT("Cannot check the ID of: ") + Definition.GetObjectPathString()));
			continue;
		}
		const auto* Class = Cast<UProject_JCharacterClassDefinition>(Asset);
		const auto* Advancement = Cast<UProject_JCharacterAdvancementDefinition>(Asset);
		const FName ExistingId = Class ? Class->ClassId : (Advancement ? Advancement->AdvancementId : NAME_None);
		if (ExistingId == FName(*Request.Identifier))
			Errors.Add(FText::FromString(TEXT("Runtime ID is already in use: ") + Definition.GetObjectPathString()));
	}
	return Errors.IsEmpty();
}

bool ValidateBundle(const UProject_JContentBundleRequest& Request, FBundlePlan& OutPlan, TArray<FText>& Errors)
{
	if (!MakePlan(Request, OutPlan, Errors) || !CheckExisting(Request, OutPlan, Errors)) return false;
	TStrongObjectPtr<UProject_JContentBundleDraft> Draft(BuildDraft(Request, Errors));
	return Draft.IsValid();
}

bool CreateUnsavedBundle(const UProject_JContentBundleRequest& Request, TArray<UObject*>& CreatedAssets, FString& RegistrationLine, TArray<FText>& Errors)
{
	check(IsInGameThread());
	CreatedAssets.Reset();
	RegistrationLine.Reset();
	FBundlePlan Plan;
	if (!MakePlan(Request, Plan, Errors) || !CheckExisting(Request, Plan, Errors)) return false;
	TStrongObjectPtr<UProject_JContentBundleDraft> Draft(BuildDraft(Request, Errors));
	if (!Draft.IsValid()) return false;
	TArray<UObject*> Assets{Draft->Root, Draft->Style};
	if (Draft->Combo) Assets.Add(Draft->Combo);
	const TArray<FString> PackageNames = Plan.GetPackages();
	TArray<UPackage*> Packages;
	for (const FString& PackageName : PackageNames) Packages.Add(CreatePackage(*PackageName));
	for (int32 Index = 0; Index < Assets.Num(); ++Index)
	{
		const FString Name = FPackageName::GetLongPackageAssetName(PackageNames[Index]);
		if (!Assets[Index]->Rename(*Name, Packages[Index], REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty))
		{
			// No asset was announced or saved yet. Return every moved draft to its transient owner.
			for (int32 Moved = 0; Moved < Index; ++Moved)
				Assets[Moved]->Rename(nullptr, Draft.Get(), REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
			for (UPackage* Package : Packages)
			{
				Package->SetDirtyFlag(false);
				Package->MarkAsGarbage();
			}
			Errors.Add(FText::FromString(TEXT("Could not create the bundle; no files were saved.")));
			return false;
		}
	}
	for (UObject* Asset : Assets)
	{
		Asset->ClearFlags(RF_Transient);
		Asset->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Asset);
		Asset->MarkPackageDirty();
	}
	CreatedAssets = MoveTemp(Assets);
	RegistrationLine = Plan.RegistrationLine;
	return true;
}
}
