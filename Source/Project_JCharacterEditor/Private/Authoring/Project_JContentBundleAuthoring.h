#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Project_JContentBundleAuthoring.generated.h"

class UProject_JCharacterClassDefinition;
class UProject_JCharacterAdvancementDefinition;
class UProject_JCombatStyleDefinition;
class UProject_JComboDefinition;

UENUM()
enum class EProject_JContentBundleKind : uint8
{
	CharacterClass,
	Advancement
};

/** Editor request only; never cooked or saved as a game asset. */
UCLASS(Transient)
class UProject_JContentBundleRequest : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category="Destination")
	EProject_JContentBundleKind Kind = EProject_JContentBundleKind::CharacterClass;
	/** Stable ClassId / AdvancementId and filename suffix; letters, digits and underscores only. */
	UPROPERTY(EditAnywhere, Category="Destination")
	FString Identifier;
	/** Kept within the project's existing Primary Asset scan root. */
	UPROPERTY(EditAnywhere, Category="Destination")
	FString DestinationFolder = TEXT("/Game/DataAssetSets/Classes");
	/** Class mode copies this class's defaults. Advancement mode links this existing base class. */
	UPROPERTY(EditAnywhere, Category="Template")
	TObjectPtr<UProject_JCharacterClassDefinition> BaseClass = nullptr;
	/** Optional; falls back to the base class's default style. Shared attacks/animations remain shared. */
	UPROPERTY(EditAnywhere, Category="Template")
	TObjectPtr<UProject_JCombatStyleDefinition> StyleTemplate = nullptr;
	/** Optional same-class predecessor. No other advancement requirements or grant policies are copied. */
	UPROPERTY(EditAnywhere, Category="Template", meta=(EditCondition="Kind == EProject_JContentBundleKind::Advancement", EditConditionHides))
	TObjectPtr<UProject_JCharacterAdvancementDefinition> PreviousAdvancement = nullptr;
};

/** Owns the complete in-memory draft until all validation succeeds. */
UCLASS(Transient)
class UProject_JContentBundleDraft : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY() TObjectPtr<UObject> Root;
	UPROPERTY() TObjectPtr<UProject_JCombatStyleDefinition> Style;
	UPROPERTY() TObjectPtr<UProject_JComboDefinition> Combo;
};

namespace ProjectJ::ContentAuthoring
{
	struct FBundlePlan
	{
		FString RootPackage;
		FString StylePackage;
		FString ComboPackage;
		FString RegistrationLine;
		TArray<FString> GetPackages() const;
	};

	/** Pure request/relationship validation, without loading packages or creating project assets. */
	bool MakePlan(const UProject_JContentBundleRequest& Request, FBundlePlan& OutPlan, TArray<FText>& Errors);
	/** Transient-only cloning; safe to discard. Does not register assets, save or change the template. */
	UProject_JContentBundleDraft* BuildDraft(const UProject_JContentBundleRequest& Request, TArray<FText>& Errors);
	bool ValidateBundle(const UProject_JContentBundleRequest& Request, FBundlePlan& OutPlan, TArray<FText>& Errors);
	/** Rechecks on every invocation. Never overwrites an existing package or saves an asset/config. */
	bool CreateUnsavedBundle(const UProject_JContentBundleRequest& Request, TArray<UObject*>& CreatedAssets, FString& RegistrationLine, TArray<FText>& Errors);
	void RegisterMenus();
}
