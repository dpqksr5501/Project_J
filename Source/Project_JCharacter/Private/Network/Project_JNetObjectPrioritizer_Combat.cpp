#include "Network/Project_JNetObjectPrioritizer_Combat.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AI/Project_JThreatComponent.h"
#include "Combat/Project_JMultiPartHitComponent.h"
#include "Components/ActorComponent.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Project_JGameplayTags.h"
#include "Project_JNPCCharacter.h"
#include "UObject/UObjectIterator.h"
#if UE_WITH_IRIS
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Iris/ReplicationSystem/Prioritization/NetObjectPrioritizer.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogProjectJIrisCombatPriority, Log, All);

namespace
{
TAutoConsoleVariable<int32> CVarProjectJCombatIrisPrioritizer(
	TEXT("ProjectJ.Net.CombatIrisPrioritizer"),
	1,
	TEXT("Enable event-driven production Iris combat priority tier switching (1 = enabled, 0 = static mover tier)."),
	ECVF_Default);

FAutoConsoleVariableSink CombatIrisPrioritizerSink(
	FConsoleCommandDelegate::CreateStatic(&UProject_JNetObjectPrioritizer_Combat::RefreshIrisPrioritizersAfterConsoleChange));

bool IsGiantBossActor(const AActor* TargetActor)
{
	if (const AProject_JNPCCharacter* NPC = Cast<AProject_JNPCCharacter>(TargetActor))
	{
		if (NPC->BrainDefinition && NPC->BrainDefinition->bIsGiantBoss)
		{
			return true;
		}
		if (const auto* MultiPart = NPC->FindComponentByClass<UProject_JMultiPartHitComponent>();
			IsValid(MultiPart) && MultiPart->IsRegistered() && !MultiPart->IsBeingDestroyed())
		{
			return true;
		}
	}
	return false;
}
}

const FName UProject_JNetObjectPrioritizer_Combat::PrioritizerName_DefaultMover(TEXT("ProjectJPlayerMover"));
const FName UProject_JNetObjectPrioritizer_Combat::PrioritizerName_CombatActive(TEXT("ProjectJCombatActive"));
const FName UProject_JNetObjectPrioritizer_Combat::PrioritizerName_CombatMode(TEXT("ProjectJCombatMode"));
const FName UProject_JNetObjectPrioritizer_Combat::PrioritizerName_CombatDead(TEXT("ProjectJCombatDead"));
const FName UProject_JNetObjectPrioritizer_Combat::PrioritizerName_CombatBoss(TEXT("ProjectJCombatBoss"));

bool UProject_JNetObjectPrioritizer_Combat::IsIrisCombatPrioritizerEnabled()
{
	return CVarProjectJCombatIrisPrioritizer.GetValueOnAnyThread() != 0;
}

FName UProject_JNetObjectPrioritizer_Combat::ResolveIrisPrioritizerName(const AActor* TargetActor)
{
	if (!IsValid(TargetActor))
	{
		return PrioritizerName_DefaultMover;
	}

	const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(const_cast<AActor*>(TargetActor));
	const FProject_JGameplayTags& GameplayTags = FProject_JGameplayTags::Get();
	if (ASC && ASC->HasMatchingGameplayTag(GameplayTags.State_Dead))
	{
		return PrioritizerName_CombatDead;
	}

	if (IsGiantBossActor(TargetActor))
	{
		return PrioritizerName_CombatBoss;
	}

	if (ASC)
	{
		if (ASC->HasMatchingGameplayTag(GameplayTags.State_Attacking) ||
			ASC->HasMatchingGameplayTag(GameplayTags.State_Dodging) ||
			ASC->HasMatchingGameplayTag(GameplayTags.State_HitReacting))
		{
			return PrioritizerName_CombatActive;
		}

		if (ASC->HasMatchingGameplayTag(GameplayTags.State_CombatMode))
		{
			return PrioritizerName_CombatMode;
		}
	}

	return PrioritizerName_DefaultMover;
}

void UProject_JNetObjectPrioritizer_Combat::RefreshIrisPrioritizersAfterConsoleChange()
{
#if UE_WITH_IRIS
	check(IsInGameThread());
	static bool bPreviousEnabled = true;
	const bool bEnabled = IsIrisCombatPrioritizerEnabled();
	if (bEnabled == bPreviousEnabled) return;
	bPreviousEnabled = bEnabled;
	for (TObjectIterator<AProject_JBaseCharacter> It; It; ++It)
	{
		AProject_JBaseCharacter* Character = *It;
		if (Character->HasAuthority() && Character->GetWorld() && Character->GetWorld()->IsGameWorld())
		{
			Character->SyncIrisCombatPrioritizer();
		}
	}
#endif
}

bool UProject_JNetObjectPrioritizer_Combat::SyncActorIrisPrioritizer(AActor* TargetActor, FName& InOutLastAppliedName, bool bForceMoverTier)
{
	check(IsInGameThread());
	if (!IsValid(TargetActor) || TargetActor->IsActorBeingDestroyed() || !TargetActor->HasAuthority())
	{
		return false;
	}

#if UE_WITH_IRIS
	UWorld* World = TargetActor->GetWorld();
	UNetDriver* NetDriver = World && !World->bIsTearingDown ? TargetActor->GetNetDriver() : nullptr;
	UReplicationSystem* ReplicationSystem = NetDriver ? NetDriver->GetReplicationSystem() : nullptr;
	if (!IsValid(ReplicationSystem) || !ReplicationSystem->IsServer())
	{
		return false;
	}

	UObjectReplicationBridge* Bridge = Cast<UObjectReplicationBridge>(ReplicationSystem->GetReplicationBridge());
	if (!IsValid(Bridge))
	{
		return false;
	}

	const UE::Net::FNetRefHandle Handle = Bridge->GetReplicatedRefHandle(TargetActor);
	if (!Handle.IsCompleteHandle())
	{
		return false;
	}

	const FName DesiredName = (bForceMoverTier || !IsIrisCombatPrioritizerEnabled())
		? PrioritizerName_DefaultMover : ResolveIrisPrioritizerName(TargetActor);
	if (DesiredName == InOutLastAppliedName)
	{
		return true;
	}

	const UE::Net::FNetObjectPrioritizerHandle PrioritizerHandle = ReplicationSystem->GetPrioritizerHandle(DesiredName);
	if (PrioritizerHandle != UE::Net::InvalidNetObjectPrioritizerHandle
		&& ReplicationSystem->SetPrioritizer(Handle, PrioritizerHandle))
	{
		InOutLastAppliedName = DesiredName;
		UE_LOG(LogProjectJIrisCombatPriority, Verbose, TEXT("Applied actor=%s tier=%s handle=%u"),
			*GetNameSafe(TargetActor), *DesiredName.ToString(), static_cast<uint32>(PrioritizerHandle));
		return true;
	}
	UE_LOG(LogProjectJIrisCombatPriority, Verbose, TEXT("Rejected actor=%s tier=%s"), *GetNameSafe(TargetActor), *DesiredName.ToString());
#endif
	return false;
}

float UProject_JNetObjectPrioritizer_Combat::CalculatePriorityMultiplier(const AActor* TargetActor) const
{
	if (!TargetActor)
	{
		return 0.0f;
	}

	UAbilitySystemComponent* AbilitySystemComponent = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(const_cast<AActor*>(TargetActor));
	if (!AbilitySystemComponent)
	{
		return 1.0f;
	}

	const FProject_JGameplayTags& GameplayTags = FProject_JGameplayTags::Get();
	if (AbilitySystemComponent->HasMatchingGameplayTag(GameplayTags.State_Dead))
	{
		return DeadPriorityMultiplier;
	}

	if (AbilitySystemComponent->HasMatchingGameplayTag(GameplayTags.State_Attacking) ||
		AbilitySystemComponent->HasMatchingGameplayTag(GameplayTags.State_Dodging) ||
		AbilitySystemComponent->HasMatchingGameplayTag(GameplayTags.State_HitReacting))
	{
		return ActiveCombatPriorityMultiplier;
	}

	if (AbilitySystemComponent->HasMatchingGameplayTag(GameplayTags.State_CombatMode))
	{
		return CombatModePriorityMultiplier;
	}

	return 1.0f;
}

bool UProject_JNetObjectPrioritizer_Combat::HasCombatPriority(const AActor* TargetActor) const
{
	return CalculatePriorityMultiplier(TargetActor) > 1.0f;
}

FProject_JReplicationPolicyDecision UProject_JNetObjectPrioritizer_Combat::ApplyCombatPriority(const AActor* TargetActor, FProject_JReplicationPolicyDecision Decision) const
{
	const float PriorityMultiplier = CalculatePriorityMultiplier(TargetActor);
	if (PriorityMultiplier < 1.0f)
	{
		Decision.PriorityMultiplier *= PriorityMultiplier;
	}
	else
	{
		Decision.PriorityMultiplier = FMath::Max(Decision.PriorityMultiplier, PriorityMultiplier);
	}
	if (PriorityMultiplier > 1.0f)
	{
		Decision.AddReason(EProject_JReplicationRelevanceReason::Combat);
	}
	return Decision;
}
