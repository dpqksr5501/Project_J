#pragma once

#include "GameplayEffect.h"
#include "Project_JGameplayEffect_SpellbladeFocus.generated.h"

UCLASS(Abstract)
class PROJECT_JCHARACTER_API UProject_JGameplayEffect_SpellbladeBuff : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UProject_JGameplayEffect_SpellbladeBuff();

protected:
	void SetBuffTag(const FGameplayTag& Tag);
	void SetRecovery(FGameplayAttribute Attribute, float PerSecond);
};

UCLASS()
class PROJECT_JCHARACTER_API UProject_JGameplayEffect_SpellbladeFocus : public UProject_JGameplayEffect_SpellbladeBuff
{
	GENERATED_BODY()

public:
	UProject_JGameplayEffect_SpellbladeFocus();
};

UCLASS()
class PROJECT_JCHARACTER_API UProject_JGameplayEffect_SpellbladeMend : public UProject_JGameplayEffect_SpellbladeBuff
{
	GENERATED_BODY()

public:
	UProject_JGameplayEffect_SpellbladeMend();
};

UCLASS()
class PROJECT_JCHARACTER_API UProject_JGameplayEffect_SpellbladeHaste : public UProject_JGameplayEffect_SpellbladeBuff
{
	GENERATED_BODY()

public:
	UProject_JGameplayEffect_SpellbladeHaste();
};
