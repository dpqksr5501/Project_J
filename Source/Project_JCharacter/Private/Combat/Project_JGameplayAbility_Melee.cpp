#include "Combat/Project_JGameplayAbility_Melee.h"
#include "Animation/Project_JWeaponAnimProfile.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystemComponent.h"
#include "Combat/Project_JComboDefinition.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Project_JGameplayTags.h"
#include "Project_JPlayerCharacter.h"
#include "Combat/Project_JServerSideRewindComponent.h"
#include "Project_JCombatComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"

UProject_JGameplayAbility_Melee::UProject_JGameplayAbility_Melee()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(FProject_JGameplayTags::Get().State_Attacking);
	SetAssetTags(AssetTags);

	ActivationOwnedTags.AddTag(FProject_JGameplayTags::Get().State_Attacking);
}

void UProject_JGameplayAbility_Melee::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ApplyCameraDirectionRotation();

	ActiveComboDefinition = nullptr;
	if (const AProject_JPlayerCharacter* PlayerCharacter = Cast<AProject_JPlayerCharacter>(GetAvatarActorFromActorInfo()))
	{
		if (const UProject_JWeaponAnimProfile* WeaponProfile = PlayerCharacter->GetWeaponAnimProfile())
		{
			ActiveComboDefinition = WeaponProfile->ComboDefinition;
		}
	}

	if (!ActiveComboDefinition || ActiveComboDefinition->Nodes.IsEmpty())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	CurrentComboNodeTag = FGameplayTag();
	QueuedInputTag = FGameplayTag();
	bIsComboWindowOpen = false;
	bHasNextComboQueued = false;
	HitActorsThisSwing.Reset();
	ComboEventTasks.Reset();

	BindComboInputEvents();
	if (MeleeHitEventTag.IsValid())
	{
		UAbilityTask_WaitGameplayEvent* HitTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, MeleeHitEventTag);
		if (HitTask)
		{
			HitTask->EventReceived.AddDynamic(this, &UProject_JGameplayAbility_Melee::OnMeleeHitReceived);
			HitTask->ReadyForActivation();
			ComboEventTasks.Add(HitTask);
		}
	}
	if (TriggerEventData && TriggerEventData->EventTag.IsValid())
	{
		OnComboInputReceived(*TriggerEventData);
	}
}

void UProject_JGameplayAbility_Melee::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	ComboEventTasks.Reset();
	ActiveComboDefinition = nullptr;
	ActiveComboMontage = nullptr;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UProject_JGameplayAbility_Melee::OnComboInputReceived(FGameplayEventData Payload)
{
	if (!CurrentComboNodeTag.IsValid())
	{
		if (const FProject_JComboNode* StartNode = ActiveComboDefinition->FindStartNode(Payload.EventTag, GetOwnerGameplayTags()))
		{
			StartComboNode(*StartNode);
		}
		return;
	}

	TryQueueOrConsumeComboInput(Payload.EventTag);
}

void UProject_JGameplayAbility_Melee::OnComboWindowOpened(FGameplayEventData Payload)
{
	if (Payload.EventMagnitude <= 0.0f)
	{
		bIsComboWindowOpen = false;
		return;
	}

	bIsComboWindowOpen = true;
	if (bHasNextComboQueued)
	{
		const FGameplayTag BufferedInput = QueuedInputTag;
		bHasNextComboQueued = false;
		QueuedInputTag = FGameplayTag();
		TryQueueOrConsumeComboInput(BufferedInput);
	}
}

void UProject_JGameplayAbility_Melee::BindComboInputEvents()
{
	if (!ActiveComboDefinition)
	{
		return;
	}

	UAbilityTask_WaitGameplayEvent* ComboWindowTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, FProject_JGameplayTags::Get().Event_Combat_ComboWindow);
	if (ComboWindowTask)
	{
		ComboWindowTask->EventReceived.AddDynamic(this, &UProject_JGameplayAbility_Melee::OnComboWindowOpened);
		ComboWindowTask->ReadyForActivation();
		ComboEventTasks.Add(ComboWindowTask);
	}

	FGameplayTagContainer InputTags;
	ActiveComboDefinition->GetReferencedInputTags(InputTags);
	for (const FGameplayTag& InputTag : InputTags)
	{
		if (!InputTag.IsValid())
		{
			continue;
		}

		UAbilityTask_WaitGameplayEvent* InputTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, InputTag);
		if (InputTask)
		{
			InputTask->EventReceived.AddDynamic(this, &UProject_JGameplayAbility_Melee::OnComboInputReceived);
			InputTask->ReadyForActivation();
			ComboEventTasks.Add(InputTask);
		}
	}
}

const FProject_JComboNode* UProject_JGameplayAbility_Melee::GetCurrentComboNode() const
{
	return ActiveComboDefinition ? ActiveComboDefinition->FindNode(CurrentComboNodeTag) : nullptr;
}

FGameplayTagContainer UProject_JGameplayAbility_Melee::GetOwnerGameplayTags() const
{
	FGameplayTagContainer OwnerTags;
	if (const UAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponentFromActorInfo())
	{
		AbilitySystem->GetOwnedGameplayTags(OwnerTags);
	}
	return OwnerTags;
}

bool UProject_JGameplayAbility_Melee::TryQueueOrConsumeComboInput(const FGameplayTag InputTag)
{
	const FProject_JComboNode* CurrentNode = GetCurrentComboNode();
	if (!CurrentNode || !ActiveComboDefinition)
	{
		return false;
	}

	const FProject_JComboTransition* Transition = ActiveComboDefinition->FindTransition(*CurrentNode, InputTag, GetOwnerGameplayTags());
	if (!Transition)
	{
		return false;
	}

	if (!bIsComboWindowOpen)
	{
		if (CurrentNode->bAllowInputBuffer)
		{
			QueuedInputTag = InputTag;
			bHasNextComboQueued = true;
			return true;
		}
		return false;
	}

	const FProject_JComboNode* NextNode = ActiveComboDefinition->FindNode(Transition->TargetNodeTag);
	if (!NextNode)
	{
		return false;
	}

	bIsComboWindowOpen = false;
	bHasNextComboQueued = false;
	QueuedInputTag = FGameplayTag();
	StartComboNode(*NextNode);
	return true;
}

void UProject_JGameplayAbility_Melee::StartComboNode(const FProject_JComboNode& Node)
{
	if (!Node.Montage)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	ApplyCameraDirectionRotation();
	HitActorsThisSwing.Reset();
	CurrentComboNodeTag = Node.NodeTag;
	if (MontageTask && ActiveComboMontage == Node.Montage)
	{
		if (!Node.MontageSectionName.IsNone())
		{
			MontageJumpToSection(Node.MontageSectionName);
		}
		return;
	}

	if (MontageTask)
	{
		MontageTask->EndTask();
		MontageTask = nullptr;
	}

	ActiveComboMontage = Node.Montage;
	MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, Node.Montage, Node.PlayRate, Node.MontageSectionName);
	if (MontageTask)
	{
		MontageTask->OnCompleted.AddDynamic(this, &UProject_JGameplayAbility_Melee::OnMontageCompleted);
		MontageTask->OnInterrupted.AddDynamic(this, &UProject_JGameplayAbility_Melee::OnMontageInterrupted);
		MontageTask->OnCancelled.AddDynamic(this, &UProject_JGameplayAbility_Melee::OnMontageInterrupted);
		MontageTask->ReadyForActivation();
	}
}

void UProject_JGameplayAbility_Melee::OnMontageCompleted()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UProject_JGameplayAbility_Melee::OnMontageInterrupted()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

void UProject_JGameplayAbility_Melee::OnMeleeHitReceived(FGameplayEventData Payload)
{
	AActor* HitActor = const_cast<AActor*>(Payload.Target.Get());
	if (HitActor && !HitActorsThisSwing.Contains(HitActor))
	{
		HitActorsThisSwing.Add(HitActor);
		
		AActor* AvatarActor = GetAvatarActorFromActorInfo();
		if (AvatarActor && AvatarActor->HasAuthority())
		{
			// Server hit natively (either locally controlled server or AI)
			// Apply damage/effects to HitActor here
		}
		else if (AvatarActor && AvatarActor->GetLocalRole() == ROLE_AutonomousProxy)
		{
			// Local predicting client: request SSR from the server
			if (Payload.TargetData.Num() > 0)
			{
				if (const FHitResult* HitResult = Payload.TargetData.Get(0)->GetHitResult())
				{
					const UWorld* World = GetWorld();
					const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
					const float ClientTimestamp = GameState ? GameState->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.0f);
					if (UProject_JCombatComponent* CombatComp = AvatarActor->FindComponentByClass<UProject_JCombatComponent>())
					{
						CombatComp->ServerRequestSSRHit(HitActor, ClientTimestamp, HitResult->TraceStart, HitResult->TraceEnd);
					}
				}
			}
		}
	}
}

void UProject_JGameplayAbility_Melee::ApplyCameraDirectionRotation()
{
	if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		if (APlayerController* PC = Cast<APlayerController>(Character->GetController()))
		{
			// Rotate character to face camera view or input direction (for 3rd person)
			FRotator ControlRot = PC->GetControlRotation();
			ControlRot.Pitch = 0.0f;
			ControlRot.Roll = 0.0f;
			Character->SetActorRotation(ControlRot);
		}
	}
}
