#include "Components/Project_JSkillInputExecutionComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Combat/Project_JCombatCommandSet.h"
#include "Combat/Project_JComboDefinition.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Project_JPlayerCharacter.h"

UProject_JSkillInputExecutionComponent::UProject_JSkillInputExecutionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UProject_JSkillInputExecutionComponent::Initialize(AProject_JPlayerCharacter* InPlayerCharacter)
{
	BoundPlayerCharacter = InPlayerCharacter;
}

void UProject_JSkillInputExecutionComponent::HandleInputTagPressed(FGameplayTag InputTag)
{
	if (!InputTag.IsValid() || !BoundPlayerCharacter)
	{
		return;
	}

	const double InputTimestamp = GetSynchronizedInputTimestamp();
	bool bConsumeRawInput = false;
	const FGameplayTag DispatchTag = ResolveDispatchInputTag(InputTag, InputTimestamp, bConsumeRawInput);
	if (DispatchTag.IsValid())
	{
		DispatchInputTag(DispatchTag);
		if (!bConsumeRawInput && !DispatchTag.MatchesTagExact(InputTag))
		{
			DispatchInputTag(InputTag);
		}
	}

	if (!BoundPlayerCharacter->HasAuthority())
	{
		// Send raw input rather than the resolved result. The server replays the
		// same command table, so a client cannot claim an arbitrary command tag.
		ServerSendCombatInputEvent(InputTag, static_cast<float>(InputTimestamp), ++LocalInputSequence);
	}
}

void UProject_JSkillInputExecutionComponent::ServerSendCombatInputEvent_Implementation(const FGameplayTag InputTag, const float ClientTimestamp, const int32 InputSequence)
{
	if (!InputTag.IsValid() || !BoundPlayerCharacter || InputSequence <= LastServerInputSequence)
	{
		return;
	}

	const double ServerNow = GetSynchronizedInputTimestamp();
	if (ServerNow - ServerRateWindowStart >= 1.0)
	{
		ServerRateWindowStart = ServerNow;
		ServerInputsInRateWindow = 0;
	}
	if (++ServerInputsInRateWindow > MaxServerInputsPerSecond)
	{
		return;
	}
	LastServerInputSequence = InputSequence;

	const double Timestamp = FMath::IsFinite(ClientTimestamp)
		? FMath::Clamp(static_cast<double>(ClientTimestamp), ServerNow - MaxAcceptedInputAge, ServerNow + 0.05)
		: ServerNow;
	bool bConsumeRawInput = false;
	const FGameplayTag DispatchTag = ResolveDispatchInputTag(InputTag, Timestamp, bConsumeRawInput);
	if (DispatchTag.IsValid())
	{
		DispatchInputTag(DispatchTag, false);
		if (!bConsumeRawInput && !DispatchTag.MatchesTagExact(InputTag))
		{
			DispatchInputTag(InputTag, false);
		}
	}
}

void UProject_JSkillInputExecutionComponent::ClearCommandInputHistory()
{
	CommandInputHistory.Reset();
	LastInputTimestampSeconds = -1.0;
}

FGameplayTag UProject_JSkillInputExecutionComponent::ResolveDispatchInputTag(
	const FGameplayTag RawInputTag,
	const double TimestampSeconds,
	bool& bOutConsumeRawInput)
{
	bOutConsumeRawInput = false;
	const UProject_JCombatStyleDefinition* CombatStyle = BoundPlayerCharacter ? BoundPlayerCharacter->GetCombatStyleDefinition() : nullptr;
	const UProject_JCombatCommandSet* CommandSet = CombatStyle ? CombatStyle->CommandSet.Get() : nullptr;
	if (LastCommandSet.Get() != CommandSet)
	{
		ClearCommandInputHistory();
		LastCommandSet = CommandSet;
	}
	if (!CommandSet || CommandSet->Commands.IsEmpty())
	{
		return RawInputTag;
	}

	RecordRawInput(RawInputTag, TimestampSeconds, CommandSet);
	const FProject_JCombatCommandDefinition* MatchingCommand = CommandSet->FindBestMatch(CommandInputHistory, GetOwnerGameplayTags());
	if (!MatchingCommand)
	{
		return RawInputTag;
	}

	bOutConsumeRawInput = MatchingCommand->bConsumeMatchedInput;
	if (MatchingCommand->bClearHistoryOnMatch)
	{
		ClearCommandInputHistory();
	}

	return MatchingCommand->ResultInputTag;
}

void UProject_JSkillInputExecutionComponent::DispatchInputTag(const FGameplayTag InputTag, const bool bAllowAbilityActivation)
{
	if (!InputTag.IsValid() || !BoundPlayerCharacter)
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = BoundPlayerCharacter->GetAbilitySystemComponent())
	{
		if (bAllowAbilityActivation && !ShouldRouteInputToActiveComboOnly(InputTag))
		{
			if (UProject_JAbilitySystemComponent* ProjectJASC = Cast<UProject_JAbilitySystemComponent>(ASC))
			{
				ProjectJASC->AbilityInputTagPressed(InputTag);
			}
		}

	FGameplayEventData Payload;
	Payload.EventTag = InputTag;
	Payload.Instigator = BoundPlayerCharacter;
	Payload.Target = BoundPlayerCharacter;
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(BoundPlayerCharacter, InputTag, Payload);
	}
}

bool UProject_JSkillInputExecutionComponent::ShouldRouteInputToActiveComboOnly(const FGameplayTag InputTag) const
{
	if (!BoundPlayerCharacter || !InputTag.IsValid())
	{
		return false;
	}

	const UAbilitySystemComponent* ASC = BoundPlayerCharacter->GetAbilitySystemComponent();
	if (!ASC || !ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Attacking))
	{
		return false;
	}

	const UProject_JCombatStyleDefinition* CombatStyle = BoundPlayerCharacter->GetCombatStyleDefinition();
	const UProject_JComboDefinition* ComboDefinition = CombatStyle ? CombatStyle->ComboDefinition.Get() : nullptr;
	if (!ComboDefinition)
	{
		return false;
	}

	FGameplayTagContainer ComboInputTags;
	ComboDefinition->GetReferencedInputTags(ComboInputTags);
	return ComboInputTags.HasTagExact(InputTag);
}

double UProject_JSkillInputExecutionComponent::GetSynchronizedInputTimestamp() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}

	if (const AGameStateBase* GameState = World->GetGameState())
	{
		return GameState->GetServerWorldTimeSeconds();
	}

	return World->GetTimeSeconds();
}

void UProject_JSkillInputExecutionComponent::RecordRawInput(
	const FGameplayTag RawInputTag,
	double TimestampSeconds,
	const UProject_JCombatCommandSet* CommandSet)
{
	if (!FMath::IsFinite(TimestampSeconds))
	{
		TimestampSeconds = GetSynchronizedInputTimestamp();
	}

	// A new weapon/state or out-of-order packet must not complete an old command.
	if (LastInputTimestampSeconds >= 0.0 && TimestampSeconds + KINDA_SMALL_NUMBER < LastInputTimestampSeconds)
	{
		ClearCommandInputHistory();
	}
	LastInputTimestampSeconds = TimestampSeconds;

	FProject_JCombatCommandInputEntry& Entry = CommandInputHistory.AddDefaulted_GetRef();
	Entry.InputTag = RawInputTag;
	Entry.TimestampSeconds = TimestampSeconds;

	const int32 HistoryLimit = FMath::Clamp(FMath::Max(MaximumCommandHistoryEntries, CommandSet->GetMaximumInputCount()), 1, 16);
	if (CommandInputHistory.Num() > HistoryLimit)
	{
		CommandInputHistory.RemoveAt(0, CommandInputHistory.Num() - HistoryLimit, EAllowShrinking::No);
	}
}

FGameplayTagContainer UProject_JSkillInputExecutionComponent::GetOwnerGameplayTags() const
{
	FGameplayTagContainer OwnerTags;
	if (BoundPlayerCharacter)
	{
		if (const UAbilitySystemComponent* ASC = BoundPlayerCharacter->GetAbilitySystemComponent())
		{
			ASC->GetOwnedGameplayTags(OwnerTags);
		}
	}
	return OwnerTags;
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
