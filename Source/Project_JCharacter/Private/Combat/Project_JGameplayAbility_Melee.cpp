#include "Combat/Project_JGameplayAbility_Melee.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystemComponent.h"
#include "Combat/Project_JComboDefinition.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Combat/Project_JAttackDefinition.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Project_JGameplayTags.h"
#include "Project_JPlayerCharacter.h"
#include "Combat/Project_JServerSideRewindComponent.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJCombatCombo, Log, All);

namespace
{
TAutoConsoleVariable<int32> CVarProjectJCombatComboDebug(
	TEXT("Project_J.Combat.ComboDebug"),
	0,
	TEXT("Logs combo input, selected nodes, and montage playback. 0: off, 1: on."),
	ECVF_Default);

bool IsComboDebugEnabled()
{
	return CVarProjectJCombatComboDebug.GetValueOnGameThread() != 0;
}
}

UProject_JGameplayAbility_Melee::UProject_JGameplayAbility_Melee()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(FProject_JGameplayTags::Get().State_Attacking);
	SetAssetTags(AssetTags);

	ActivationOwnedTags.AddTag(FProject_JGameplayTags::Get().State_Attacking);

	// Weapon combo inputs may be bound at all times, but a weapon attack is only
	// valid after the persistent combat-mode effect has armed the character.
	// This keeps input routing generic across jobs without allowing an equipped
	// weapon's GA to fire while the character is sheathed/non-combat.
	ActivationRequiredTags.AddTag(FProject_JGameplayTags::Get().State_CombatMode);
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
	ActiveAttackDefinition = nullptr;
	if (const AProject_JPlayerCharacter* PlayerCharacter = Cast<AProject_JPlayerCharacter>(GetAvatarActorFromActorInfo()))
	{
		if (const UProject_JCombatStyleDefinition* CombatStyle = PlayerCharacter->GetCombatStyleDefinition(); CombatStyle && CombatStyle->ComboDefinition)
		{
			ActiveComboDefinition = CombatStyle->ComboDefinition;
		}
	}

	if (!ActiveComboDefinition || ActiveComboDefinition->Nodes.IsEmpty())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (IsComboDebugEnabled())
	{
		UE_LOG(LogProjectJCombatCombo, Log, TEXT("Activate Owner=%s Combo=%s Trigger=%s"),
			*GetNameSafe(GetAvatarActorFromActorInfo()),
			*GetNameSafe(ActiveComboDefinition),
			TriggerEventData ? *TriggerEventData->EventTag.ToString() : TEXT("None"));
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
	RestoreAttackMovementMode(bWasCancelled);
	if (AActor* AvatarActor = GetAvatarActorFromActorInfo())
	{
		if (UProject_JCombatHitValidationComponent* HitValidation = AvatarActor->FindComponentByClass<UProject_JCombatHitValidationComponent>())
		{
			HitValidation->EndAttack();
		}
	}
	ComboEventTasks.Reset();
	ActiveComboDefinition = nullptr;
	ActiveAttackDefinition = nullptr;
	ActiveComboMontage = nullptr;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UProject_JGameplayAbility_Melee::ApplyAttackMovementPolicy(const UProject_JAttackDefinition& AttackDefinition)
{
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	UCharacterMovementComponent* MovementComponent = Character ? Character->GetCharacterMovement() : nullptr;
	if (!AttackDefinition.bUseFlyingMovementModeForRootMotion || AttackDefinition.MovementPolicy == EProject_JAttackMovementPolicy::InPlace)
	{
		if (IsComboDebugEnabled())
		{
			UE_LOG(LogProjectJCombatCombo, Display,
				TEXT("RootMotionPolicy Attack=%s Policy=%d UseFlying=%s Result=Skipped MovementMode=%d"),
				*AttackDefinition.AttackTag.ToString(),
				static_cast<int32>(AttackDefinition.MovementPolicy),
				AttackDefinition.bUseFlyingMovementModeForRootMotion ? TEXT("true") : TEXT("false"),
				MovementComponent ? static_cast<int32>(MovementComponent->MovementMode) : -1);
		}
		return;
	}

	RootMotionEndMovementPolicy = AttackDefinition.RootMotionEndMovementPolicy;
	if (bRootMotionForcedFlying)
	{
		return;
	}

	if (!MovementComponent || MovementComponent->IsFlying())
	{
		if (IsComboDebugEnabled())
		{
			UE_LOG(LogProjectJCombatCombo, Display,
				TEXT("RootMotionPolicy Attack=%s Policy=%d UseFlying=true Result=%s MovementMode=%d"),
				*AttackDefinition.AttackTag.ToString(),
				static_cast<int32>(AttackDefinition.MovementPolicy),
				MovementComponent ? TEXT("AlreadyFlying") : TEXT("NoMovementComponent"),
				MovementComponent ? static_cast<int32>(MovementComponent->MovementMode) : -1);
		}
		return;
	}

	RootMotionMovementComponent = MovementComponent;
	bRootMotionForcedFlying = true;
	MovementComponent->SetMovementMode(MOVE_Flying);
	if (IsComboDebugEnabled())
	{
		UE_LOG(LogProjectJCombatCombo, Display,
			TEXT("RootMotionPolicy Attack=%s Policy=%d UseFlying=true Result=SetFlying MovementMode=%d"),
			*AttackDefinition.AttackTag.ToString(),
			static_cast<int32>(AttackDefinition.MovementPolicy),
			static_cast<int32>(MovementComponent->MovementMode));
	}
}

void UProject_JGameplayAbility_Melee::RestoreAttackMovementMode(const bool bWasCancelled)
{
	if (!bRootMotionForcedFlying)
	{
		return;
	}

	if (UCharacterMovementComponent* MovementComponent = RootMotionMovementComponent.Get())
	{
		// An interrupted aerial attack always falls. On a normal completion the
		// authored AttackDefinition controls whether it lands, falls, or begins a
		// persistent flying state. Landing directly avoids a transient Falling
		// state that would make Motion Matching play an unwanted landing montage.
		if (bWasCancelled)
		{
			MovementComponent->SetMovementMode(MOVE_Falling);
		}
		else if (RootMotionEndMovementPolicy == EProject_JRootMotionEndMovementPolicy::Land)
		{
			MovementComponent->SetMovementMode(MOVE_Walking);
		}
		else if (RootMotionEndMovementPolicy == EProject_JRootMotionEndMovementPolicy::Fall)
		{
			MovementComponent->SetMovementMode(MOVE_Falling);
		}
	}

	RootMotionMovementComponent = nullptr;
	RootMotionEndMovementPolicy = EProject_JRootMotionEndMovementPolicy::Land;
	bRootMotionForcedFlying = false;
}

void UProject_JGameplayAbility_Melee::OnComboInputReceived(FGameplayEventData Payload)
{
	if (IsComboDebugEnabled())
	{
		UE_LOG(LogProjectJCombatCombo, Log, TEXT("Input Received CurrentNode=%s Input=%s WindowOpen=%d"),
			*CurrentComboNodeTag.ToString(),
			*Payload.EventTag.ToString(),
			bIsComboWindowOpen ? 1 : 0);
	}

	if (!CurrentComboNodeTag.IsValid())
	{
		if (const FProject_JComboNode* StartNode = ActiveComboDefinition->FindStartNode(Payload.EventTag, GetOwnerGameplayTags()))
		{
			if (IsComboDebugEnabled())
			{
				UE_LOG(LogProjectJCombatCombo, Log, TEXT("Start Node Selected Node=%s"), *StartNode->NodeTag.ToString());
			}
			StartComboNode(*StartNode);
		}
		else if (IsComboDebugEnabled())
		{
			UE_LOG(LogProjectJCombatCombo, Warning, TEXT("No Start Node for Input=%s"), *Payload.EventTag.ToString());
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
	if (IsComboDebugEnabled())
	{
		UE_LOG(LogProjectJCombatCombo, Log, TEXT("Combo Window Opened Node=%s Queued=%s"), *CurrentComboNodeTag.ToString(), *QueuedInputTag.ToString());
	}
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
		if (IsComboDebugEnabled())
		{
			UE_LOG(LogProjectJCombatCombo, Warning, TEXT("Cannot Consume Input=%s: no active combo node."), *InputTag.ToString());
		}
		return false;
	}

	const FProject_JComboTransition* Transition = ActiveComboDefinition->FindTransition(*CurrentNode, InputTag, GetOwnerGameplayTags());
	if (!Transition)
	{
		if (IsComboDebugEnabled())
		{
			UE_LOG(LogProjectJCombatCombo, Warning, TEXT("No Transition From=%s Input=%s"), *CurrentNode->NodeTag.ToString(), *InputTag.ToString());
		}
		return false;
	}

	if (!bIsComboWindowOpen)
	{
		if (CurrentNode->bAllowInputBuffer)
		{
			QueuedInputTag = InputTag;
			bHasNextComboQueued = true;
			if (IsComboDebugEnabled())
			{
				UE_LOG(LogProjectJCombatCombo, Log, TEXT("Input Buffered From=%s Input=%s Target=%s"), *CurrentNode->NodeTag.ToString(), *InputTag.ToString(), *Transition->TargetNodeTag.ToString());
			}
			return true;
		}
		return false;
	}

	const FProject_JComboNode* NextNode = ActiveComboDefinition->FindNode(Transition->TargetNodeTag);
	if (!NextNode)
	{
		if (IsComboDebugEnabled())
		{
			UE_LOG(LogProjectJCombatCombo, Warning, TEXT("Transition Target Missing From=%s Target=%s"), *CurrentNode->NodeTag.ToString(), *Transition->TargetNodeTag.ToString());
		}
		return false;
	}

	bIsComboWindowOpen = false;
	bHasNextComboQueued = false;
	QueuedInputTag = FGameplayTag();
	if (IsComboDebugEnabled())
	{
		UE_LOG(LogProjectJCombatCombo, Log, TEXT("Transition Consumed From=%s Input=%s Target=%s"), *CurrentNode->NodeTag.ToString(), *InputTag.ToString(), *NextNode->NodeTag.ToString());
	}
	StartComboNode(*NextNode);
	return true;
}

void UProject_JGameplayAbility_Melee::StartComboNode(const FProject_JComboNode& Node)
{
	UProject_JAttackDefinition* AttackDefinition = Node.AttackDefinition;
	if (AttackDefinition)
	{
		const FGameplayTagContainer OwnerTags = GetOwnerGameplayTags();
		if (!OwnerTags.HasAll(AttackDefinition->RequiredOwnerTags) || OwnerTags.HasAny(AttackDefinition->BlockedOwnerTags))
		{
			EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
			return;
		}
	}
	if (!AttackDefinition)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}
	ApplyAttackMovementPolicy(*AttackDefinition);
	UAnimMontage* NodeMontage = AttackDefinition->Montage.Get();
	const FName NodeSection = AttackDefinition->MontageSectionName;
	const float NodePlayRate = AttackDefinition->PlayRate;
	if (!NodeMontage)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	if (IsComboDebugEnabled())
	{
		UE_LOG(LogProjectJCombatCombo, Log, TEXT("Play Node=%s Attack=%s Montage=%s Section=%s SameMontage=%d"),
			*Node.NodeTag.ToString(),
			*AttackDefinition->AttackTag.ToString(),
			*GetNameSafe(NodeMontage),
			*NodeSection.ToString(),
			ActiveComboMontage == NodeMontage ? 1 : 0);
	}

	ApplyCameraDirectionRotation();
	HitActorsThisSwing.Reset();
	CurrentComboNodeTag = Node.NodeTag;
	ActiveAttackDefinition = AttackDefinition;
	if (AActor* AvatarActor = GetAvatarActorFromActorInfo())
	{
		if (UProject_JCombatHitValidationComponent* HitValidation = AvatarActor->FindComponentByClass<UProject_JCombatHitValidationComponent>())
		{
			HitValidation->BeginAttackNode(CurrentComboNodeTag, AttackDefinition);
		}
	}
	if (MontageTask && ActiveComboMontage == NodeMontage)
	{
		if (!NodeSection.IsNone())
		{
			if (IsComboDebugEnabled())
			{
				UE_LOG(LogProjectJCombatCombo, Log, TEXT("Jump Existing Montage Section=%s"), *NodeSection.ToString());
			}
			MontageJumpToSection(NodeSection);
		}
		return;
	}

	if (MontageTask)
	{
		MontageTask->EndTask();
		MontageTask = nullptr;
	}

	ActiveComboMontage = NodeMontage;
	MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, NodeMontage, NodePlayRate, NodeSection);
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
			if (UProject_JCombatHitValidationComponent* CombatComp = AvatarActor->FindComponentByClass<UProject_JCombatHitValidationComponent>())
			{
				CombatComp->ProcessAuthorityHit(HitActor);
			}
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
					if (UProject_JCombatHitValidationComponent* CombatComp = AvatarActor->FindComponentByClass<UProject_JCombatHitValidationComponent>())
					{
						CombatComp->SubmitPredictedHit(HitActor, ClientTimestamp, HitResult->TraceStart, HitResult->TraceEnd);
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
