#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AttributeSet.h"
#include "GameplayTagContainer.h"
#include "Blueprint/UserWidget.h"
#include "Project_JCharacterUIProfile.generated.h"

class UTexture2D;
class UAbilitySystemComponent;

USTRUCT(BlueprintType)
struct PROJECT_J_API FProject_JUISkillSlot
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FGameplayTag InputTag;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Label;
	/** Presentation hint only. Enhanced Input remains the source of key bindings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FText InputHint;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<UTexture2D> Icon;
};

USTRUCT(BlueprintType)
struct PROJECT_J_API FProject_JUIContext
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FName ClassId;
	UPROPERTY(BlueprintReadOnly) FName AdvancementId;
	UPROPERTY(BlueprintReadOnly) FGameplayTagContainer OwnedTags;
};

USTRUCT(BlueprintType)
struct PROJECT_J_API FProject_JUIResourceDefinition
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName ResourceId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText Label;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FGameplayAttribute Current;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FGameplayAttribute Maximum;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FLinearColor Color = FLinearColor::White;
};

USTRUCT(BlueprintType)
struct PROJECT_J_API FProject_JUIResourceState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FName ResourceId;
	UPROPERTY(BlueprintReadOnly) FText Label;
	UPROPERTY(BlueprintReadOnly) float Current = 0;
	UPROPERTY(BlueprintReadOnly) float Maximum = 0;
	UPROPERTY(BlueprintReadOnly) float Fraction = 0;
	UPROPERTY(BlueprintReadOnly) FLinearColor Color = FLinearColor::White;
};

/** Optional job gauge/command guide skin. Own no gameplay state or server requests here. */
UCLASS(Blueprintable)
class PROJECT_J_API UProject_JCharacterHUDModule : public UUserWidget
{
	GENERATED_BODY()
  public:
	UFUNCTION(BlueprintImplementableEvent, Category = "UI")
	void PresentCharacter(const FProject_JUIContext &Context, const TArray<FProject_JUIResourceState> &Resources);
};

/** UI module references gameplay by stable IDs/tags; gameplay modules never depend on UI. */
UCLASS(BlueprintType)
class PROJECT_J_API UProject_JCharacterUIProfile : public UDataAsset
{
	GENERATED_BODY()
  public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile") FName ProfileId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile") FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Match") int32 Priority = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Match") FName ClassId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Match") FName AdvancementId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Match") FGameplayTagContainer RequiredTags;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Match") FGameplayTagContainer BlockedTags;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skills") bool bOverrideSkills = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skills") TArray<FProject_JUISkillSlot> Skills;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources") bool bOverrideResources = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resources") TArray<FProject_JUIResourceDefinition> Resources;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skin")
	TSubclassOf<UProject_JCharacterHUDModule> CharacterModuleClass;
	bool IsUsable() const;
	bool Matches(const FProject_JUIContext &Context) const;
	int32 Specificity() const;
};

UCLASS(BlueprintType)
class PROJECT_J_API UProject_JUIProfileCatalog : public UDataAsset
{
	GENERATED_BODY()
  public:
	/** Hard references keep assigned profiles and their widget classes in the cook dependency graph. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<TObjectPtr<UProject_JCharacterUIProfile>> Profiles;
	UProject_JCharacterUIProfile *Resolve(const FProject_JUIContext &Context) const;
};

namespace ProjectJUI
{
PROJECT_J_API TArray<FProject_JUIResourceState> ReadResources(
	const UAbilitySystemComponent *ASC, const TArray<FProject_JUIResourceDefinition> &Definitions);
}
