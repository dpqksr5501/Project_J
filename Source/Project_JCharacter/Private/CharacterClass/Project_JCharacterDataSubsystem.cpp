#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "Project_JBaseCharacter.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJCharacterData, Log, All);

UProject_JCharacterDataSubsystem::UProject_JCharacterDataSubsystem()
{
}

void UProject_JCharacterDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PopulateRegistry();
}

void UProject_JCharacterDataSubsystem::Deinitialize()
{
	ClassMap.Empty();
	AdvancementMap.Empty();
	Super::Deinitialize();
}

void UProject_JCharacterDataSubsystem::PopulateRegistry()
{
	ClassMap.Empty();
	AdvancementMap.Empty();

	if (ClassDefinitions.IsEmpty() && AdvancementDefinitions.IsEmpty())
	{
		UE_LOG(
			LogProjectJCharacterData,
			Warning,
			TEXT("Character data registry is empty. Configure ClassDefinitions and AdvancementDefinitions in DefaultGame.ini."));
	}

	// Load and register classes
	for (const TSoftObjectPtr<UProject_JCharacterClassDefinition>& ClassSoftPtr : ClassDefinitions)
	{
		if (UProject_JCharacterClassDefinition* LoadedClass = ClassSoftPtr.LoadSynchronous())
		{
			const FName ClassId = LoadedClass->ClassId;
			if (ClassId.IsNone())
			{
				UE_LOG(LogProjectJCharacterData, Warning, TEXT("Found class definition with empty ClassId: %s"), *LoadedClass->GetName());
				continue;
			}

			if (ClassMap.Contains(ClassId))
			{
				UE_LOG(LogProjectJCharacterData, Error, TEXT("Duplicate ClassId registered: %s"), *ClassId.ToString());
				continue;
			}

			ClassMap.Add(ClassId, LoadedClass);
		}
	}

	// Load and register advancements
	for (const TSoftObjectPtr<UProject_JCharacterAdvancementDefinition>& AdvSoftPtr : AdvancementDefinitions)
	{
		if (UProject_JCharacterAdvancementDefinition* LoadedAdv = AdvSoftPtr.LoadSynchronous())
		{
			const FName AdvId = LoadedAdv->AdvancementId;
			if (AdvId.IsNone())
			{
				UE_LOG(LogProjectJCharacterData, Warning, TEXT("Found advancement definition with empty AdvancementId: %s"), *LoadedAdv->GetName());
				continue;
			}

			if (AdvancementMap.Contains(AdvId))
			{
				UE_LOG(LogProjectJCharacterData, Error, TEXT("Duplicate AdvancementId registered: %s"), *AdvId.ToString());
				continue;
			}

			AdvancementMap.Add(AdvId, LoadedAdv);
		}
	}
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

	if (ClassDefinitions.IsEmpty())
	{
		OutValidationErrors.Add(NSLOCTEXT(
			"CharacterDataSubsystem",
			"EmptyClassDefinitions",
			"ClassDefinitions is empty. At least one server-authoritative class definition must be configured."));
		bIsValid = false;
	}

	// Check if soft pointers loaded correctly and detect duplicate IDs
	TSet<FName> SeenClassIds;
	for (int32 Index = 0; Index < ClassDefinitions.Num(); ++Index)
	{
		const TSoftObjectPtr<UProject_JCharacterClassDefinition>& SoftPtr = ClassDefinitions[Index];
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
	for (int32 Index = 0; Index < AdvancementDefinitions.Num(); ++Index)
	{
		const TSoftObjectPtr<UProject_JCharacterAdvancementDefinition>& SoftPtr = AdvancementDefinitions[Index];
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

	return bIsValid;
}
