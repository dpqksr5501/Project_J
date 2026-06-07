#include "Combat/Project_JGameplayAbility_Melee.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Project_JGameplayTags.h"

UProject_JGameplayAbility_Melee::UProject_JGameplayAbility_Melee()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

void UProject_JGameplayAbility_Melee::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!AttackMontage)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ApplyCameraDirectionRotation();

	CurrentSectionName = InitialSectionName;
	bIsComboWindowOpen = false;
	bHasNextComboQueued = false;
	HitActorsThisSwing.Reset();

	MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, AttackMontage, 1.0f, CurrentSectionName);
	if (MontageTask)
	{
		MontageTask->OnCompleted.AddDynamic(this, &UProject_JGameplayAbility_Melee::OnMontageCompleted);
		MontageTask->OnInterrupted.AddDynamic(this, &UProject_JGameplayAbility_Melee::OnMontageInterrupted);
		MontageTask->OnCancelled.AddDynamic(this, &UProject_JGameplayAbility_Melee::OnMontageInterrupted);
		MontageTask->ReadyForActivation();
	}

	// Listen for ComboWindow event from AnimNotify
	UAbilityTask_WaitGameplayEvent* WaitComboWindowEvent = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, FProject_JGameplayTags::Get().Event_Combat_ComboWindow);
	if (WaitComboWindowEvent)
	{
		WaitComboWindowEvent->EventReceived.AddDynamic(this, &UProject_JGameplayAbility_Melee::OnComboWindowOpened);
		WaitComboWindowEvent->ReadyForActivation();
	}

	// Listen for generic attack input events
	UAbilityTask_WaitGameplayEvent* WaitInputEventLight = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, FProject_JGameplayTags::Get().InputTag_Weapon_LightAttack);
	if (WaitInputEventLight)
	{
		WaitInputEventLight->EventReceived.AddDynamic(this, &UProject_JGameplayAbility_Melee::OnComboInputReceived);
		WaitInputEventLight->ReadyForActivation();
	}
	
	UAbilityTask_WaitGameplayEvent* WaitInputEventHeavy = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, FProject_JGameplayTags::Get().InputTag_Weapon_HeavyAttack);
	if (WaitInputEventHeavy)
	{
		WaitInputEventHeavy->EventReceived.AddDynamic(this, &UProject_JGameplayAbility_Melee::OnComboInputReceived);
		WaitInputEventHeavy->ReadyForActivation();
	}

	if (MeleeHitEventTag.IsValid())
	{
		UAbilityTask_WaitGameplayEvent* WaitMeleeHitEvent = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, MeleeHitEventTag);
		if (WaitMeleeHitEvent)
		{
			WaitMeleeHitEvent->EventReceived.AddDynamic(this, &UProject_JGameplayAbility_Melee::OnMeleeHitReceived);
			WaitMeleeHitEvent->ReadyForActivation();
		}
	}
}

void UProject_JGameplayAbility_Melee::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UProject_JGameplayAbility_Melee::OnComboInputReceived(FGameplayEventData Payload)
{
	FName NextSection = GetNextSectionForInput(Payload.EventTag, CurrentSectionName);
	if (!NextSection.IsNone())
	{
		QueuedSectionName = NextSection;
		bHasNextComboQueued = true;

		// If the window is already open when the button is pressed, jump immediately.
		if (bIsComboWindowOpen)
		{
			MontageJumpToSection(QueuedSectionName);
			CurrentSectionName = QueuedSectionName;
			bIsComboWindowOpen = false;
			bHasNextComboQueued = false;
			HitActorsThisSwing.Reset();
		}
	}
}

void UProject_JGameplayAbility_Melee::OnComboWindowOpened(FGameplayEventData Payload)
{
	bIsComboWindowOpen = true;

	// If there is already a buffered input, jump immediately.
	if (bHasNextComboQueued)
	{
		MontageJumpToSection(QueuedSectionName);
		CurrentSectionName = QueuedSectionName;
		bIsComboWindowOpen = false;
		bHasNextComboQueued = false;
		HitActorsThisSwing.Reset();
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
	const AActor* HitActor = Payload.Target.Get();
	if (HitActor && !HitActorsThisSwing.Contains(HitActor))
	{
		HitActorsThisSwing.Add(HitActor);
		
		// Apply damage/effects to HitActor here
		// Note: Actual GameplayEffect application should typically check HasAuthority() or run on server.
	}
}

FName UProject_JGameplayAbility_Melee::GetNextSectionForInput_Implementation(FGameplayTag InputTag, FName CurrentSection) const
{
	// Base implementation can be simple naming conventions or data table lookups
	return FName();
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
