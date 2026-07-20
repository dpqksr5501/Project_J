#include "Components/Project_JSkillInputExecutionComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Components/Project_JCombatStateComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JPlayerCharacter.h"

UProject_JSkillInputExecutionComponent::UProject_JSkillInputExecutionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UProject_JSkillInputExecutionComponent::Initialize(AProject_JPlayerCharacter* InPlayerCharacter, UProject_JCombatStateComponent* InCombatStateComponent)
{
	BoundPlayerCharacter = InPlayerCharacter;
	BoundCombatStateComponent = InCombatStateComponent;
}

void UProject_JSkillInputExecutionComponent::HandleInputTagPressed(FGameplayTag InputTag)
{
	if (!InputTag.IsValid() || !BoundPlayerCharacter)
	{
		return;
	}

	BoundPlayerCharacter->RefreshActiveCombatComponent();

	if (UAbilitySystemComponent* ASC = BoundPlayerCharacter->GetAbilitySystemComponent())
	{
		bool bHandledByInputTagAbility = false;
		if (UProject_JAbilitySystemComponent* ProjectJASC = Cast<UProject_JAbilitySystemComponent>(ASC))
		{
			bHandledByInputTagAbility = ProjectJASC->AbilityInputTagPressed(InputTag);
		}

		FGameplayEventData Payload;
		Payload.EventTag = InputTag;
		Payload.Instigator = BoundPlayerCharacter;
		Payload.Target = BoundPlayerCharacter;

		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(BoundPlayerCharacter, Payload.EventTag, Payload);

		if (!BoundPlayerCharacter->HasAuthority())
		{
			ServerSendCombatInputEvent(InputTag);
		}

		if (!bHandledByInputTagAbility)
		{
			TryLegacyFallback(InputTag);
		}
	}
}

void UProject_JSkillInputExecutionComponent::ServerSendCombatInputEvent_Implementation(const FGameplayTag InputTag)
{
	if (!InputTag.IsValid() || !BoundPlayerCharacter)
	{
		return;
	}

	FGameplayEventData Payload;
	Payload.EventTag = InputTag;
	Payload.Instigator = BoundPlayerCharacter;
	Payload.Target = BoundPlayerCharacter;
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(BoundPlayerCharacter, InputTag, Payload);
}

void UProject_JSkillInputExecutionComponent::HandleInputTagReleased(FGameplayTag InputTag)
{
	if (!InputTag.IsValid() || !BoundPlayerCharacter)
	{
		return;
	}

	if (UProject_JAbilitySystemComponent* ProjectJASC = Cast<UProject_JAbilitySystemComponent>(BoundPlayerCharacter->GetAbilitySystemComponent()))
	{
		ProjectJASC->AbilityInputTagReleased(InputTag);
	}
}

bool UProject_JSkillInputExecutionComponent::TryLegacyFallback(FGameplayTag InputTag)
{
	if (!bAllowLegacySkillInputFallback || !InputTag.IsValid() || !BoundCombatStateComponent)
	{
		return false;
	}

	if (bWarnOnLegacySkillInputFallback && !WarnedLegacySkillInputFallbackTags.Contains(InputTag))
	{
		WarnedLegacySkillInputFallbackTags.Add(InputTag);
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("%s used legacy skill input fallback for %s. Prefer granting an AbilitySet entry with this InputTag."),
			*GetNameSafe(BoundPlayerCharacter),
			*InputTag.ToString());
	}

	return BoundCombatStateComponent->TryActivateAbilityByTag(InputTag);
}
