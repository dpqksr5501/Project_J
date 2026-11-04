#pragma once
#include "Abilities/GameplayAbility.h"
#include "Project_JAvatarPublicationFixture.generated.h"

/** Per-actor native hook at the real GAS OnAvatarSet reentry boundary. */
UCLASS(NotBlueprintable, Transient)
class UProject_JAvatarPublicationFixture : public UGameplayAbility
{
	GENERATED_BODY()
public:
	UProject_JAvatarPublicationFixture() { InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor; }
	TFunction<void()> OnPublication;
	virtual void OnAvatarSet(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override
	{
		Super::OnAvatarSet(ActorInfo, Spec);
		auto Callback = MoveTemp(OnPublication);
		if (Callback) Callback();
	}
};
