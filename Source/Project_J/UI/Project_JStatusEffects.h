#pragma once
#include "CoreMinimal.h"
#include "GameplayEffectUIData.h"
#include "ActiveGameplayEffectHandle.h"
#include "UObject/Object.h"
#include "TimerManager.h"
#include "Project_JStatusEffects.generated.h"

class UAbilitySystemComponent;
class UTexture2D;
/** Add to a GameplayEffect's Components list to expose an intentional player-facing status. */
UCLASS(DisplayName="Project J Status UI", EditInlineNew)
class PROJECT_J_API UProject_JStatusEffectUIData : public UGameplayEffectUIData
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly) FText DisplayName;
	UPROPERTY(EditDefaultsOnly, meta=(MultiLine=true)) FText Description;
	UPROPERTY(EditDefaultsOnly) TSoftObjectPtr<UTexture2D> Icon;
	UPROPERTY(EditDefaultsOnly) bool bDebuff = false;
	UPROPERTY(EditDefaultsOnly) int32 Priority = 0;
};
USTRUCT(BlueprintType)
struct FProject_JStatusEffectState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FActiveGameplayEffectHandle Handle;
	UPROPERTY(BlueprintReadOnly) FText Name;
	UPROPERTY(BlueprintReadOnly) FText Description;
	UPROPERTY(BlueprintReadOnly) TSoftObjectPtr<UTexture2D> Icon;
	UPROPERTY(BlueprintReadOnly) int32 StackCount = 1;
	UPROPERTY(BlueprintReadOnly) float Remaining = 0;
	UPROPERTY(BlueprintReadOnly) float Duration = 0;
	UPROPERTY(BlueprintReadOnly) bool bDebuff = false;
	int32 Priority = 0;
};
/** Owner-only ASC projection. Does not apply/cancel effects or reveal remote effect details. */
UCLASS()
class PROJECT_J_API UProject_JStatusEffectModel : public UObject
{
	GENERATED_BODY()
public:
	void Bind(UAbilitySystemComponent *Source);
	void Unbind();
	void Refresh();
	DECLARE_MULTICAST_DELEGATE(FChanged);
	FChanged OnChanged;
	UPROPERTY(BlueprintReadOnly) TArray<FProject_JStatusEffectState> States;
	virtual void BeginDestroy() override;
private:
	TWeakObjectPtr<UAbilitySystemComponent> ASC;
	FDelegateHandle AddedHandle;
	FDelegateHandle RemovedHandle;
	FTimerHandle Timer;
};
