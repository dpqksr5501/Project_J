#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "Project_JBaseCharacter.h"
#include "Engine/AssetManager.h"
#include "Inventory/Project_JItemDefinition.h"
#include "Crafting/Project_JCraftingRecipeDefinition.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeExit.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJCharacterData, Log, All);

UProject_JCharacterDataSubsystem::UProject_JCharacterDataSubsystem()
{
}

void UProject_JCharacterDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Config is an optional explicit source. Production class/item definitions
	// are also registered PrimaryAssets and must be available before consumers
	// initialize, independently of opting into local-profile persistence.
	if (!LoadPrimaryAssetDefinitions())
	{
		ClassMap.Reset(); AdvancementMap.Reset(); ItemMap.Reset(); CraftingRecipeMap.Reset(); bCraftingRegistryReady = false;
		UE_LOG(LogProjectJCharacterData, Error, TEXT("Character PrimaryAsset registry initialization failed; no partial registry is published."));
	}
	else if (ClassMap.IsEmpty() && AdvancementMap.IsEmpty())
	{
		UE_LOG(LogProjectJCharacterData, Warning, TEXT("Character data registry is empty after Config and PrimaryAsset discovery. Configure or author class definitions."));
	}
}

void UProject_JCharacterDataSubsystem::Deinitialize()
{
	ClassMap.Empty();
	AdvancementMap.Empty();
	ItemMap.Empty(); CraftingRecipeMap.Empty(); bPrimaryRegistryReady = bCraftingRegistryReady = false; CraftingRegistryError.Reset();
	Super::Deinitialize();
}

void UProject_JCharacterDataSubsystem::PostReloadConfig(FProperty* PropertyThatWasLoaded)
{
	Super::PostReloadConfig(PropertyThatWasLoaded);
	bPrimaryRegistryReady = false;
}

bool UProject_JCharacterDataSubsystem::EnsurePrimaryAssetDefinitions()
{
	check(IsInGameThread());
	return bPrimaryRegistryReady || LoadPrimaryAssetDefinitions();
}

bool UProject_JCharacterDataSubsystem::LoadPrimaryAssetDefinitions()
{
	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_DefinitionRegistryRefresh);
	// Failed explicit refresh cannot be mistaken for current admission readiness.
	bPrimaryRegistryReady = false;
	const double Started = FPlatformTime::Seconds();
	bool bPublished = false;
	++Stats.Refreshes; Stats.LastResolvedPaths = Stats.LastSynchronousLoads = 0;
	ON_SCOPE_EXIT
	{
		Stats.LastRefreshMilliseconds = (FPlatformTime::Seconds() - Started) * 1000;
		if (!bPublished) ++Stats.FailedRefreshes;
	};
	auto* Assets = UAssetManager::GetIfInitialized();
	if (!Assets) return false;
	const auto Gather = [&](const auto& Configured, FName Type, TArray<FSoftObjectPath>& Paths)
	{
		if (Configured.Num() > MaxCharacterDefinitions) return false;
		TSet<FSoftObjectPath> Seen;
		for (const auto& Soft : Configured)
		{
			const FSoftObjectPath Path = Soft.ToSoftObjectPath();
			// Duplicate explicit entries are authoring errors; overlap with the
			// PrimaryAsset scan of the same path is the normal shared-source case.
			if (!Path.IsValid() || Seen.Contains(Path)) return false;
			Seen.Add(Path); Paths.Add(Path);
		}
		TArray<FPrimaryAssetId> Ids;
		Assets->GetPrimaryAssetIdList(Type, Ids);
		if (Ids.Num() > MaxCharacterDefinitions) return false;
		for (const auto& Id : Ids)
		{
			const FSoftObjectPath Path = Assets->GetPrimaryAssetPath(Id);
			if (!Path.IsValid()) return false;
			if (!Seen.Contains(Path)) { Seen.Add(Path); Paths.Add(Path); }
		}
		return Paths.Num() <= MaxCharacterDefinitions;
	};
	TArray<FSoftObjectPath> ClassPaths, AdvancementPaths;
	if (!Gather(ClassDefinitions, TEXT("Project_JCharacterClassDefinition"), ClassPaths) ||
		!Gather(AdvancementDefinitions, TEXT("Project_JCharacterAdvancementDefinition"), AdvancementPaths) ||
		ClassPaths.Num() + AdvancementPaths.Num() > MaxCharacterDefinitions) return false;

	// Build fresh membership from current sources. A removed definition must not
	// survive merely because a previous refresh published it. Pin across loads.
	TMap<FName, TObjectPtr<UProject_JCharacterClassDefinition>> Classes;
	TMap<FName, TObjectPtr<UProject_JCharacterAdvancementDefinition>> Advancements;
	TMap<FName, TObjectPtr<UProject_JItemDefinition>> Items;
	TArray<TStrongObjectPtr<UObject>> Pins;
	const auto Resolve = [&](const FSoftObjectPath& Path)
	{
		++Stats.LastResolvedPaths;
		UObject* Object = Path.ResolveObject();
		if (!Object) { ++Stats.LastSynchronousLoads; Object = Path.TryLoad(); }
		if (Object) Pins.Emplace(Object);
		return Object;
	};
	for (const auto& Path : ClassPaths)
	{
		auto* Definition = Cast<UProject_JCharacterClassDefinition>(Resolve(Path));
		if (!Definition || Definition->ClassId.IsNone() || Definition->SchemaVersion < 1 || Classes.Contains(Definition->ClassId)) return false;
		Classes.Add(Definition->ClassId, Definition);
	}
	for (const auto& Path : AdvancementPaths)
	{
		auto* Definition = Cast<UProject_JCharacterAdvancementDefinition>(Resolve(Path));
		if (!Definition || Definition->AdvancementId.IsNone() || Definition->SchemaVersion < 1 || Advancements.Contains(Definition->AdvancementId)) return false;
		Advancements.Add(Definition->AdvancementId, Definition);
	}
	TArray<UProject_JCharacterClassDefinition*> ClassValues;
	TArray<UProject_JCharacterAdvancementDefinition*> AdvancementValues;
	for (const auto& Pair : Classes) ClassValues.Add(Pair.Value);
	for (const auto& Pair : Advancements) AdvancementValues.Add(Pair.Value);
	TArray<FText> Errors;
	if (!ProjectJ::ValidateAdvancementGraph(ClassValues, AdvancementValues, Errors)) return false;

	TSet<FSoftObjectPath> ItemPaths;
	for (const FName Type : {FName(TEXT("Project_JEquipmentItemDefinition")), FName(TEXT("Project_JConsumableDefinition")), FName(TEXT("Project_JCompanionItemDefinition"))})
	{
		TArray<FPrimaryAssetId> Ids; Assets->GetPrimaryAssetIdList(Type, Ids);
		if (ItemPaths.Num() + Ids.Num() > MaxItemDefinitions) return false;
		for (const auto& Id : Ids)
		{
			const FSoftObjectPath Path = Assets->GetPrimaryAssetPath(Id);
			if (!Path.IsValid()) return false;
			if (ItemPaths.Contains(Path)) continue;
			ItemPaths.Add(Path);
			auto* Definition = Cast<UProject_JItemDefinition>(Resolve(Path));
			if (!Definition || Definition->ItemId.IsNone() || Definition->SchemaVersion < 1 || Definition->MaxStackCount < 1 || Items.Contains(Definition->ItemId)) return false;
			Items.Add(Definition->ItemId, Definition);
		}
	}
	ClassMap = MoveTemp(Classes); AdvancementMap = MoveTemp(Advancements); ItemMap = MoveTemp(Items);
	bPrimaryRegistryReady = bPublished = true;
	// Optional recipe errors cannot turn valid core class/item publication into
	// failure or prevent a profile with existing ownership from opening.
	RefreshCraftingRecipes();
	return true;
}
bool UProject_JCharacterDataSubsystem::RefreshCraftingRecipes()
{
	check(IsInGameThread());
	bCraftingRegistryReady = false;
	const auto Fail = [this](const TCHAR* Message) { CraftingRegistryError = Message; return false; };
	auto* Assets = UAssetManager::GetIfInitialized();
	if (!Assets || CraftingRecipeDefinitions.Num() > MaxCraftingRecipes)
		return Fail(TEXT("Optional crafting source is unavailable or exceeds its definition bound."));
	TArray<FSoftObjectPath> Paths; TSet<FSoftObjectPath> Seen;
	for (const auto& Soft : CraftingRecipeDefinitions)
	{
		const auto Path = Soft.ToSoftObjectPath();
		if (!Path.IsValid() || Seen.Contains(Path)) return Fail(TEXT("Invalid or duplicate configured recipe path."));
		Seen.Add(Path); Paths.Add(Path);
	}
	TArray<FPrimaryAssetId> Ids;
	Assets->GetPrimaryAssetIdList(TEXT("Project_JCraftingRecipeDefinition"), Ids);
	if (Ids.Num() > MaxCraftingRecipes) return Fail(TEXT("Optional recipe discovery exceeded its bound."));
	for (const auto& Id : Ids)
	{
		const auto Path = Assets->GetPrimaryAssetPath(Id);
		if (!Path.IsValid()) return Fail(TEXT("Invalid recipe PrimaryAsset path."));
		if (!Seen.Contains(Path)) { Seen.Add(Path); Paths.Add(Path); }
	}
	if (Paths.Num() > MaxCraftingRecipes) return Fail(TEXT("Optional recipe union exceeded its bound."));
	TMap<FName, TObjectPtr<UProject_JCraftingRecipeDefinition>> Recipes;
	TArray<TStrongObjectPtr<UProject_JCraftingRecipeDefinition>> Pins;
	for (const auto& Path : Paths)
	{
		auto* Recipe = Cast<UProject_JCraftingRecipeDefinition>(Path.ResolveObject());
		if (!Recipe) Recipe = Cast<UProject_JCraftingRecipeDefinition>(Path.TryLoad());
		if (!Recipe) return Fail(TEXT("Optional recipe asset could not be resolved."));
		Pins.Emplace(Recipe);
		ProjectJ::Crafting::FRecipeValues Values; FString Digest;
		if (!Recipe->Capture(*this, Values, Digest) || Recipes.Contains(Recipe->RecipeId))
			return Fail(TEXT("Optional recipe rules, canonical item references or IDs are invalid."));
		Recipes.Add(Recipe->RecipeId, Recipe);
	}
	CraftingRecipeMap = MoveTemp(Recipes);
	CraftingRegistryError.Reset(); bCraftingRegistryReady = true;
	return true;
}
UProject_JCraftingRecipeDefinition* UProject_JCharacterDataSubsystem::GetCraftingRecipe(FName Id, int32 Version) const
{
	if (!bCraftingRegistryReady) return nullptr;
	const auto* Found = CraftingRecipeMap.Find(Id);
	return Found && *Found && (*Found)->SchemaVersion == Version ? Found->Get() : nullptr;
}

UProject_JItemDefinition* UProject_JCharacterDataSubsystem::GetItemDefinition(FName Id,int32 Version) const
{
	const auto* Found=ItemMap.Find(Id);return Found&&*Found&&(*Found)->SchemaVersion==Version?Found->Get():nullptr;
}

UProject_JCharacterClassDefinition* UProject_JCharacterDataSubsystem::GetClassDefinition(FName ClassId) const
{
	if (const TObjectPtr<UProject_JCharacterClassDefinition>* FoundClass = ClassMap.Find(ClassId))
	{
		return *FoundClass;
	}
	return nullptr;
}

UProject_JCharacterAdvancementDefinition* UProject_JCharacterDataSubsystem::GetAdvancementDefinition(FName AdvancementId) const
{
	if (const TObjectPtr<UProject_JCharacterAdvancementDefinition>* FoundAdv = AdvancementMap.Find(AdvancementId))
	{
		return *FoundAdv;
	}
	return nullptr;
}

bool UProject_JCharacterDataSubsystem::InitializeCharacterClassById(
	AProject_JBaseCharacter* TargetCharacter,
	FName ClassId) const
{
	if (!TargetCharacter || !TargetCharacter->HasAuthority() || ClassId.IsNone())
	{
		return false;
	}

	return TargetCharacter->InitializeCharacterClassDefinition(GetClassDefinition(ClassId));
}

bool UProject_JCharacterDataSubsystem::ApplyAdvancementById(
	AProject_JBaseCharacter* TargetCharacter,
	FName AdvancementId) const
{
	if (!TargetCharacter || !TargetCharacter->HasAuthority() || AdvancementId.IsNone())
	{
		return false;
	}

	return TargetCharacter->ApplyAdvancementDefinition(GetAdvancementDefinition(AdvancementId));
}

bool UProject_JCharacterDataSubsystem::ValidateDataRegistry(TArray<FText>& OutValidationErrors)
{
	OutValidationErrors.Reset();
	bool bIsValid = true;
	// Validate the same effective sources used at runtime while retaining null
	// and duplicate entries in explicitly authored Config lists for diagnostics.
	auto ClassesToValidate=ClassDefinitions;
	auto AdvancementsToValidate=AdvancementDefinitions;
	for (const auto& Pair:ClassMap) ClassesToValidate.AddUnique(TSoftObjectPtr<UProject_JCharacterClassDefinition>(Pair.Value.Get()));
	for (const auto& Pair:AdvancementMap) AdvancementsToValidate.AddUnique(TSoftObjectPtr<UProject_JCharacterAdvancementDefinition>(Pair.Value.Get()));

	if (ClassesToValidate.IsEmpty())
	{
		OutValidationErrors.Add(NSLOCTEXT(
			"CharacterDataSubsystem",
			"EmptyClassDefinitions",
			"ClassDefinitions is empty. At least one server-authoritative class definition must be configured."));
		bIsValid = false;
	}

	// Check if soft pointers loaded correctly and detect duplicate IDs
	TSet<FName> SeenClassIds;
	for (int32 Index = 0; Index < ClassesToValidate.Num(); ++Index)
	{
		const TSoftObjectPtr<UProject_JCharacterClassDefinition>& SoftPtr = ClassesToValidate[Index];
		if (SoftPtr.IsNull())
		{
			OutValidationErrors.Add(FText::Format(
				NSLOCTEXT("CharacterDataSubsystem", "NullClassSoftPtr", "ClassDefinitions[{0}] SoftPointer is Null."),
				FText::AsNumber(Index)));
			bIsValid = false;
			continue;
		}

		UProject_JCharacterClassDefinition* ClassDef = SoftPtr.LoadSynchronous();
		if (!ClassDef)
		{
			OutValidationErrors.Add(FText::Format(
				NSLOCTEXT("CharacterDataSubsystem", "FailedToLoadClass", "ClassDefinitions[{0}] failed to load synchronous asset."),
				FText::AsNumber(Index)));
			bIsValid = false;
			continue;
		}

		if (ClassDef->ClassId.IsNone())
		{
			OutValidationErrors.Add(FText::Format(
				NSLOCTEXT("CharacterDataSubsystem", "EmptyClassId", "Class definition '{0}' has an empty ClassId."),
				FText::FromString(ClassDef->GetName())));
			bIsValid = false;
		}
		else if (SeenClassIds.Contains(ClassDef->ClassId))
		{
			OutValidationErrors.Add(FText::Format(
				NSLOCTEXT("CharacterDataSubsystem", "DuplicateClassId", "Duplicate ClassId '{0}' detected on asset '{1}'."),
				FText::FromName(ClassDef->ClassId),
				FText::FromString(ClassDef->GetName())));
			bIsValid = false;
		}
		else
		{
			SeenClassIds.Add(ClassDef->ClassId);
		}
	}

	TSet<FName> SeenAdvIds;
	for (int32 Index = 0; Index < AdvancementsToValidate.Num(); ++Index)
	{
		const TSoftObjectPtr<UProject_JCharacterAdvancementDefinition>& SoftPtr = AdvancementsToValidate[Index];
		if (SoftPtr.IsNull())
		{
			OutValidationErrors.Add(FText::Format(
				NSLOCTEXT("CharacterDataSubsystem", "NullAdvSoftPtr", "AdvancementDefinitions[{0}] SoftPointer is Null."),
				FText::AsNumber(Index)));
			bIsValid = false;
			continue;
		}

		UProject_JCharacterAdvancementDefinition* AdvDef = SoftPtr.LoadSynchronous();
		if (!AdvDef)
		{
			OutValidationErrors.Add(FText::Format(
				NSLOCTEXT("CharacterDataSubsystem", "FailedToLoadAdv", "AdvancementDefinitions[{0}] failed to load synchronous asset."),
				FText::AsNumber(Index)));
			bIsValid = false;
			continue;
		}

		if (AdvDef->AdvancementId.IsNone())
		{
			OutValidationErrors.Add(FText::Format(
				NSLOCTEXT("CharacterDataSubsystem", "EmptyAdvId", "Advancement definition '{0}' has an empty AdvancementId."),
				FText::FromString(AdvDef->GetName())));
			bIsValid = false;
		}
		else if (SeenAdvIds.Contains(AdvDef->AdvancementId))
		{
			OutValidationErrors.Add(FText::Format(
				NSLOCTEXT("CharacterDataSubsystem", "DuplicateAdvId", "Duplicate AdvancementId '{0}' detected on asset '{1}'."),
				FText::FromName(AdvDef->AdvancementId),
				FText::FromString(AdvDef->GetName())));
			bIsValid = false;
		}
		else
		{
			SeenAdvIds.Add(AdvDef->AdvancementId);
		}

		// Verify base class compatibility if referenced
		if (AdvDef->BaseClass)
		{
			if (AdvDef->BaseClass->ClassId.IsNone())
			{
				OutValidationErrors.Add(FText::Format(
					NSLOCTEXT("CharacterDataSubsystem", "AdvBaseClassMissingId", "Advancement '{0}' references BaseClass '{1}' which has no ClassId."),
					FText::FromString(AdvDef->GetName()),
					FText::FromString(AdvDef->BaseClass->GetName())));
				bIsValid = false;
			}
		}
	}

	TArray<UProject_JCharacterClassDefinition*> Classes;
	TArray<UProject_JCharacterAdvancementDefinition*> Advancements;
	for (const auto& Ref : ClassesToValidate) Classes.Add(Ref.LoadSynchronous());
	for (const auto& Ref : AdvancementsToValidate) Advancements.Add(Ref.LoadSynchronous());
	return ProjectJ::ValidateAdvancementGraph(Classes, Advancements, OutValidationErrors) && bIsValid;
}
