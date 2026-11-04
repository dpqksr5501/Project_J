#pragma once
#include "Abilities/GameplayAbility.h"
#include "Project_JUISkillQueryFixture.generated.h"

/** Uses the real virtual query boundary without depending on a particular job. */
UCLASS(NotBlueprintable, Transient)
class UProject_JUISkillQueryFixture : public UGameplayAbility
{
	GENERATED_BODY()
public:
	mutable int32 Queries = 0, CooldownQueries = 0;
	mutable TFunction<void()> OnQuery, OnCooldown;
	bool bAvailable = true;
	float Remaining = 4, Duration = 8;
	virtual bool CanActivateAbility(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*,
		const FGameplayTagContainer* = nullptr, const FGameplayTagContainer* = nullptr, FGameplayTagContainer* = nullptr) const override
	{
		++Queries;
		auto Callback = MoveTemp(OnQuery); if (Callback) Callback();
		return bAvailable;
	}
	virtual void GetCooldownTimeRemainingAndDuration(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*,
		float& Time, float& Total) const override
	{
		++CooldownQueries; Time = Remaining; Total = Duration;
		auto Callback = MoveTemp(OnCooldown); if (Callback) Callback();
	}
};
