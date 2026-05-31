// Fill out your copyright notice in the Description page of Project Settings.


#include "Project_JWarriorComponent.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Engine/World.h"
#include "CombatDamageable.h"
#include "Project_JGameplayTags.h"

// Sets default values for this component's properties
UProject_JWarriorComponent::UProject_JWarriorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Bind the montage end delegate
	OnAttackMontageEndedDelegate.BindUObject(this, &UProject_JWarriorComponent::OnAttackMontageEnded);
}

// Called when the game starts
void UProject_JWarriorComponent::BeginPlay()
{
	Super::BeginPlay();
}

// Called when the game ends
void UProject_JWarriorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnequipWeapon();
	Super::EndPlay(EndPlayReason);
}

void UProject_JWarriorComponent::EquipWeapon()
{
	ACharacter* Owner = Cast<ACharacter>(GetOwner());
	if (!Owner || !WeaponClass)
	{
		return;
	}

	// Clean up existing weapon if any
	UnequipWeapon();

	// Spawn the weapon at owner's location/rotation
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Owner;
	SpawnParams.Instigator = Owner;

	SpawnedWeapon = GetWorld()->SpawnActor<AActor>(WeaponClass, Owner->GetActorLocation(), Owner->GetActorRotation(), SpawnParams);
	if (SpawnedWeapon)
	{
		// Attach to the specified weapon socket on character mesh
		SpawnedWeapon->AttachToComponent(Owner->GetMesh(), FAttachmentTransformRules::SnapToTargetIncludingScale, WeaponSocketName);
	}
}

void UProject_JWarriorComponent::UnequipWeapon()
{
	if (SpawnedWeapon)
	{
		SpawnedWeapon->Destroy();
		SpawnedWeapon = nullptr;
	}
}

void UProject_JWarriorComponent::Attack()
{
	ACharacter* Owner = Cast<ACharacter>(GetOwner());
	if (!Owner || !SwordComboMontage || ComboSectionNames.Num() == 0)
	{
		return;
	}

	const float CurrentTime = GetWorld()->GetTimeSeconds();

	// If already attacking, register input for combo queuing
	if (bIsAttacking)
	{
		CachedAttackInputTime = CurrentTime;
		return;
	}

	bIsAttacking = true;
	SetOwnedCombatStateTag(FProject_JGameplayTags::Get().State_Attacking, true);
	ComboCount = 0;
	CachedAttackInputTime = 0.0f;

	if (UAnimInstance* AnimInstance = Owner->GetMesh()->GetAnimInstance())
	{
		const float Duration = AnimInstance->Montage_Play(SwordComboMontage, 1.0f);
		if (Duration > 0.0f)
		{
			AnimInstance->Montage_JumpToSection(ComboSectionNames[0], SwordComboMontage);
			AnimInstance->Montage_SetEndDelegate(OnAttackMontageEndedDelegate, SwordComboMontage);
		}
		else
		{
			bIsAttacking = false;
			SetOwnedCombatStateTag(FProject_JGameplayTags::Get().State_Attacking, false);
		}
	}
	else
	{
		bIsAttacking = false;
		SetOwnedCombatStateTag(FProject_JGameplayTags::Get().State_Attacking, false);
	}
}

void UProject_JWarriorComponent::CheckCombo()
{
	ACharacter* Owner = Cast<ACharacter>(GetOwner());
	if (!Owner || !bIsAttacking || !SwordComboMontage)
	{
		return;
	}

	const float CurrentTime = GetWorld()->GetTimeSeconds();

	// Check if the attack button was pressed during the combo input cache window
	if (CurrentTime - CachedAttackInputTime <= AttackInputCacheTimeTolerance && CachedAttackInputTime > 0.0f)
	{
		ComboCount++;

		if (ComboCount < ComboSectionNames.Num())
		{
			if (UAnimInstance* AnimInstance = Owner->GetMesh()->GetAnimInstance())
			{
				AnimInstance->Montage_JumpToSection(ComboSectionNames[ComboCount], SwordComboMontage);
				// Reset cached input time to wait for the next combo window
				CachedAttackInputTime = 0.0f;
			}
		}
		else
		{
			ResetCombo();
		}
	}
	else
	{
		ResetCombo();
	}
}

void UProject_JWarriorComponent::ResetCombo()
{
	ComboCount = 0;
	bIsAttacking = false;
	SetOwnedCombatStateTag(FProject_JGameplayTags::Get().State_Attacking, false);
	CachedAttackInputTime = 0.0f;

	ACharacter* Owner = Cast<ACharacter>(GetOwner());
	if (Owner)
	{
		if (UAnimInstance* AnimInstance = Owner->GetMesh()->GetAnimInstance())
		{
			AnimInstance->Montage_Stop(0.2f, SwordComboMontage);
		}
	}
}

void UProject_JWarriorComponent::DoAttackTrace(FName DamageSourceBone)
{
	ACharacter* Owner = Cast<ACharacter>(GetOwner());
	if (!Owner)
	{
		return;
	}

	TArray<FHitResult> OutHits;

	// Use specified bone/socket on mesh or weapon as trace start location
	FVector TraceStart;
	if (SpawnedWeapon && SpawnedWeapon->GetRootComponent())
	{
		TraceStart = SpawnedWeapon->GetActorLocation();
	}
	else
	{
		TraceStart = Owner->GetMesh()->GetSocketLocation(DamageSourceBone);
	}

	const FVector TraceEnd = TraceStart + (Owner->GetActorForwardVector() * MeleeTraceDistance);

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);

	FCollisionShape CollisionShape;
	CollisionShape.SetSphere(MeleeTraceRadius);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(Owner);
	if (SpawnedWeapon)
	{
		QueryParams.AddIgnoredActor(SpawnedWeapon);
	}

	if (GetWorld()->SweepMultiByObjectType(OutHits, TraceStart, TraceEnd, FQuat::Identity, ObjectParams, CollisionShape, QueryParams))
	{
		for (const FHitResult& CurrentHit : OutHits)
		{
			AActor* HitActor = CurrentHit.GetActor();
			if (HitActor && HitActor != Owner)
			{
				ICombatDamageable* Damageable = Cast<ICombatDamageable>(HitActor);
				if (Damageable)
				{
					// Apply knockback normal impulse and launch impulse
					const FVector ImpulseDirection = (TraceEnd - TraceStart).GetSafeNormal();
					const FVector Impulse = (ImpulseDirection * MeleeKnockbackImpulse) + (FVector::UpVector * MeleeLaunchImpulse);

					Damageable->ApplyDamage(MeleeDamage, Owner, CurrentHit.ImpactPoint, Impulse);
				}
			}
		}
	}
}

void UProject_JWarriorComponent::OnAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage == SwordComboMontage)
	{
		bIsAttacking = false;
		SetOwnedCombatStateTag(FProject_JGameplayTags::Get().State_Attacking, false);
		ComboCount = 0;
		CachedAttackInputTime = 0.0f;
	}
}
