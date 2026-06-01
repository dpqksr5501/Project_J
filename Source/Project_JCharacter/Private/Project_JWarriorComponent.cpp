// Fill out your copyright notice in the Description page of Project Settings.


#include "Project_JWarriorComponent.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Engine/World.h"
#include "CombatDamageable.h"
#include "Animation/Project_JWeaponAnimProfile.h"
#include "Combat/Project_JCombatTypes.h"
#include "Project_JGameplayTags.h"
#include "Project_JPlayerCharacter.h"

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
	const TSubclassOf<AActor> EffectiveWeaponClass = GetEffectiveWeaponClass();
	if (!Owner || !EffectiveWeaponClass)
	{
		return;
	}

	// Clean up existing weapon if any
	UnequipWeapon();

	// Spawn the weapon at owner's location/rotation
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Owner;
	SpawnParams.Instigator = Owner;

	SpawnedWeapon = GetWorld()->SpawnActor<AActor>(EffectiveWeaponClass, Owner->GetActorLocation(), Owner->GetActorRotation(), SpawnParams);
	if (SpawnedWeapon)
	{
		// Attach to the specified weapon socket on character mesh
		SpawnedWeapon->AttachToComponent(Owner->GetMesh(), FAttachmentTransformRules::SnapToTargetIncludingScale, GetEffectiveWeaponSocketName());
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
	if (TryActivatePrimaryAttackAbility())
	{
		return;
	}

	ACharacter* Owner = Cast<ACharacter>(GetOwner());
	UAnimMontage* EffectiveAttackMontage = GetEffectiveAttackMontage();
	const TArray<FName>& EffectiveComboSectionNames = GetEffectiveComboSectionNames();
	if (!CanStartPrototypeAttack(Owner, EffectiveAttackMontage, EffectiveComboSectionNames))
	{
		return;
	}

	const float CurrentTime = GetWorld()->GetTimeSeconds();

	// If already attacking, register input for combo queuing
	if (bIsAttacking)
	{
		QueuePrototypeComboInput(CurrentTime);
		return;
	}

	BeginPrototypeAttack(EffectiveAttackMontage, EffectiveComboSectionNames);

	if (UAnimInstance* AnimInstance = Owner->GetMesh()->GetAnimInstance())
	{
		const float Duration = AnimInstance->Montage_Play(EffectiveAttackMontage, 1.0f);
		if (Duration > 0.0f)
		{
			AnimInstance->Montage_JumpToSection(EffectiveComboSectionNames[0], EffectiveAttackMontage);
			AnimInstance->Montage_SetEndDelegate(OnAttackMontageEndedDelegate, EffectiveAttackMontage);
		}
		else
		{
			ClearPrototypeAttackState();
		}
	}
	else
	{
		ClearPrototypeAttackState();
	}
}

void UProject_JWarriorComponent::CheckCombo()
{
	ACharacter* Owner = Cast<ACharacter>(GetOwner());
	UAnimMontage* EffectiveAttackMontage = ActiveAttackMontage.Get();
	if (!Owner || !bIsAttacking || !EffectiveAttackMontage || ActiveComboSectionNames.IsEmpty())
	{
		return;
	}

	const float CurrentTime = GetWorld()->GetTimeSeconds();

	// Check if the attack button was pressed during the combo input cache window
	if (CurrentTime - CachedAttackInputTime <= AttackInputCacheTimeTolerance && CachedAttackInputTime > 0.0f)
	{
		ComboCount++;

		if (ComboCount < ActiveComboSectionNames.Num())
		{
			if (UAnimInstance* AnimInstance = Owner->GetMesh()->GetAnimInstance())
			{
				AnimInstance->Montage_JumpToSection(ActiveComboSectionNames[ComboCount], EffectiveAttackMontage);
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
	EndPrototypeAttack(true);
}

void UProject_JWarriorComponent::DoAttackTrace(FName DamageSourceBone)
{
	ACharacter* Owner = Cast<ACharacter>(GetOwner());
	if (!Owner)
	{
		return;
	}

	if (bRequireAuthorityForDamageTrace && !Owner->HasAuthority())
	{
		return;
	}

	TArray<FHitResult> OutHits;
	TSet<TWeakObjectPtr<AActor>> DamagedActors;
	const FProject_JWeaponAttackSpec AttackSpec = GetEffectivePrimaryAttackSpec();

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

	const FVector TraceEnd = TraceStart + (Owner->GetActorForwardVector() * AttackSpec.TraceDistance);

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);

	FCollisionShape CollisionShape;
	CollisionShape.SetSphere(AttackSpec.TraceRadius);

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
			if (HitActor && HitActor != Owner && !DamagedActors.Contains(HitActor))
			{
				ICombatDamageable* Damageable = Cast<ICombatDamageable>(HitActor);
				if (Damageable)
				{
					DamagedActors.Add(HitActor);

					// Apply knockback normal impulse and launch impulse
					const FVector ImpulseDirection = (TraceEnd - TraceStart).GetSafeNormal();
					const FVector Impulse = (ImpulseDirection * AttackSpec.KnockbackImpulse) + (FVector::UpVector * AttackSpec.LaunchImpulse);

					Damageable->ApplyDamage(AttackSpec.BaseDamage, Owner, CurrentHit.ImpactPoint, Impulse);
				}
			}
		}
	}
}

void UProject_JWarriorComponent::OnAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage == ActiveAttackMontage)
	{
		EndPrototypeAttack(false);
	}
}

bool UProject_JWarriorComponent::TryActivatePrimaryAttackAbility() const
{
	return TryActivateAbilityByTag(GetEffectivePrimaryAttackAbilityTag());
}

bool UProject_JWarriorComponent::CanStartPrototypeAttack(const ACharacter* Owner, const UAnimMontage* AttackMontage, const TArray<FName>& ComboSections) const
{
	return Owner && AttackMontage && !ComboSections.IsEmpty();
}

void UProject_JWarriorComponent::QueuePrototypeComboInput(float CurrentTime)
{
	CachedAttackInputTime = CurrentTime;
}

void UProject_JWarriorComponent::BeginPrototypeAttack(UAnimMontage* AttackMontage, const TArray<FName>& ComboSections)
{
	bIsAttacking = true;
	SetOwnedCombatStateTag(FProject_JGameplayTags::Get().State_Attacking, true);
	ComboCount = 0;
	CachedAttackInputTime = 0.0f;
	ActiveAttackMontage = AttackMontage;
	ActiveComboSectionNames = ComboSections;
}

void UProject_JWarriorComponent::ClearPrototypeAttackState()
{
	ComboCount = 0;
	bIsAttacking = false;
	SetOwnedCombatStateTag(FProject_JGameplayTags::Get().State_Attacking, false);
	CachedAttackInputTime = 0.0f;
	ActiveAttackMontage = nullptr;
	ActiveComboSectionNames.Reset();
}

void UProject_JWarriorComponent::EndPrototypeAttack(bool bStopMontage)
{
	UAnimMontage* MontageToStop = ActiveAttackMontage ? ActiveAttackMontage.Get() : SwordComboMontage;
	ClearPrototypeAttackState();

	if (!bStopMontage || !MontageToStop)
	{
		return;
	}

	ACharacter* Owner = Cast<ACharacter>(GetOwner());
	if (!Owner)
	{
		return;
	}

	if (UAnimInstance* AnimInstance = Owner->GetMesh()->GetAnimInstance())
	{
		AnimInstance->Montage_Stop(0.2f, MontageToStop);
	}
}

const UProject_JWeaponAnimProfile* UProject_JWarriorComponent::GetCurrentWeaponAnimProfile() const
{
	const AProject_JPlayerCharacter* OwnerPlayer = Cast<AProject_JPlayerCharacter>(GetOwner());
	return OwnerPlayer ? OwnerPlayer->GetWeaponAnimProfile() : nullptr;
}

TSubclassOf<AActor> UProject_JWarriorComponent::GetEffectiveWeaponClass() const
{
	if (const UProject_JWeaponAnimProfile* WeaponAnimProfile = GetCurrentWeaponAnimProfile())
	{
		if (WeaponAnimProfile->WeaponActorClass)
		{
			return WeaponAnimProfile->WeaponActorClass;
		}
	}

	return WeaponClass;
}

FName UProject_JWarriorComponent::GetEffectiveWeaponSocketName() const
{
	if (const UProject_JWeaponAnimProfile* WeaponAnimProfile = GetCurrentWeaponAnimProfile())
	{
		if (!WeaponAnimProfile->WeaponSocketName.IsNone())
		{
			return WeaponAnimProfile->WeaponSocketName;
		}
	}

	return WeaponSocketName;
}

UAnimMontage* UProject_JWarriorComponent::GetEffectiveAttackMontage() const
{
	if (const UProject_JWeaponAnimProfile* WeaponAnimProfile = GetCurrentWeaponAnimProfile())
	{
		if (WeaponAnimProfile->PrimaryAttackMontage)
		{
			return WeaponAnimProfile->PrimaryAttackMontage.Get();
		}
	}

	return SwordComboMontage;
}

const TArray<FName>& UProject_JWarriorComponent::GetEffectiveComboSectionNames() const
{
	if (const UProject_JWeaponAnimProfile* WeaponAnimProfile = GetCurrentWeaponAnimProfile())
	{
		if (!WeaponAnimProfile->PrimaryAttackSectionNames.IsEmpty())
		{
			return WeaponAnimProfile->PrimaryAttackSectionNames;
		}
	}

	return ComboSectionNames;
}

FGameplayTag UProject_JWarriorComponent::GetEffectivePrimaryAttackAbilityTag() const
{
	if (const UProject_JWeaponAnimProfile* WeaponAnimProfile = GetCurrentWeaponAnimProfile())
	{
		if (WeaponAnimProfile->PrimaryAttackSpec.AbilityTag.IsValid())
		{
			return WeaponAnimProfile->PrimaryAttackSpec.AbilityTag;
		}
	}

	return PrimaryAttackAbilityTag;
}

FProject_JWeaponAttackSpec UProject_JWarriorComponent::GetEffectivePrimaryAttackSpec() const
{
	if (const UProject_JWeaponAnimProfile* WeaponAnimProfile = GetCurrentWeaponAnimProfile())
	{
		FProject_JWeaponAttackSpec AttackSpec = WeaponAnimProfile->PrimaryAttackSpec;
		if (AttackSpec.BaseDamage > 0.0f || AttackSpec.TraceDistance > 0.0f || AttackSpec.TraceRadius > 0.0f)
		{
			return AttackSpec;
		}
	}

	FProject_JWeaponAttackSpec FallbackSpec;
	FallbackSpec.AbilityTag = PrimaryAttackAbilityTag;
	FallbackSpec.BaseDamage = MeleeDamage;
	FallbackSpec.TraceDistance = MeleeTraceDistance;
	FallbackSpec.TraceRadius = MeleeTraceRadius;
	FallbackSpec.KnockbackImpulse = MeleeKnockbackImpulse;
	FallbackSpec.LaunchImpulse = MeleeLaunchImpulse;
	return FallbackSpec;
}
