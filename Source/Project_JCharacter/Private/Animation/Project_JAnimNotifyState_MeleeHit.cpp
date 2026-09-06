#include "Animation/Project_JAnimNotifyState_MeleeHit.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Combat/Project_JAttackDefinition.h"

UProject_JAnimNotifyState_MeleeHit::UProject_JAnimNotifyState_MeleeHit()
{
}

void UProject_JAnimNotifyState_MeleeHit::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	if (AActor* OwnerActor = MeshComp ? MeshComp->GetOwner() : nullptr)
	{
		PreviousSocketLocations.Add(MeshComp, ResolveTraceLocation(MeshComp));
		if (UProject_JCombatHitValidationComponent* HitValidation = OwnerActor->FindComponentByClass<UProject_JCombatHitValidationComponent>())
		{
			HitValidation->SetHitWindowOpen(true);
		}
	}
}

void UProject_JAnimNotifyState_MeleeHit::NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);

	AActor* OwnerActor = MeshComp->GetOwner();
	if (!OwnerActor || !HitEventTag.IsValid())
	{
		return;
	}

	UWorld* World = MeshComp->GetWorld();
	if (!World)
	{
		return;
	}

	const FVector TraceLocation = ResolveTraceLocation(MeshComp);
	const FVector TraceStart = PreviousSocketLocations.FindRef(MeshComp);
	PreviousSocketLocations.Add(MeshComp, TraceLocation);
	float EffectiveTraceRadius = TraceRadius;
	if (const UProject_JCombatHitValidationComponent* HitValidation = OwnerActor->FindComponentByClass<UProject_JCombatHitValidationComponent>())
	{
		if (const UProject_JAttackDefinition* AttackDefinition = HitValidation->GetActiveAttackDefinition())
		{
			EffectiveTraceRadius = AttackDefinition->HitSpec.TraceRadius;
		}
	}

	if (OwnerActor->HasAuthority())
	{
		if (UProject_JCombatHitValidationComponent* HitValidation = OwnerActor->FindComponentByClass<UProject_JCombatHitValidationComponent>())
		{
			HitValidation->RecordAuthoritativeTrace(TraceStart, TraceLocation);
		}
	}

	TArray<AActor*> ActorsToIgnore;
	ActorsToIgnore.Add(OwnerActor);

	TArray<FHitResult> OutHits;

	// Sweep across the weapon's actual frame-to-frame path. A point overlap misses
	// fast swings at low frame rates and gives clients inconsistent hit candidates.
	UKismetSystemLibrary::SphereTraceMultiForObjects(
		World,
		TraceStart,
		TraceLocation,
		EffectiveTraceRadius,
		{ UEngineTypes::ConvertToObjectType(ECC_Pawn) },
		false,
		ActorsToIgnore,
		EDrawDebugTrace::None,
		OutHits,
		true
	);

	for (const FHitResult& Hit : OutHits)
	{
		if (AActor* HitActor = Hit.GetActor())
		{
			FGameplayEventData Payload;
			Payload.EventTag = HitEventTag;
			Payload.Instigator = OwnerActor;
			Payload.Target = HitActor;
			Payload.TargetData = UAbilitySystemBlueprintLibrary::AbilityTargetDataFromHitResult(Hit);

			// Send the hit event to the attacker's ability system
			// The MeleeCombo Ability will catch this, filter duplicates, and apply GameplayEffects
			UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(OwnerActor, HitEventTag, Payload);
		}
	}
}

FVector UProject_JAnimNotifyState_MeleeHit::ResolveTraceLocation(USkeletalMeshComponent* MeshComp) const
{
	if (bUseWeaponPresentationSocket)
	{
		if (AActor* OwnerActor = MeshComp ? MeshComp->GetOwner() : nullptr)
		{
			if (const UProject_JWeaponPresentationComponent* Presentation = OwnerActor->FindComponentByClass<UProject_JWeaponPresentationComponent>())
			{
				FTransform WeaponSocketTransform;
				if (Presentation->GetWeaponSocketTransform(WeaponSocketName, WeaponSocketTransform))
				{
					return WeaponSocketTransform.GetLocation();
				}
			}
		}
	}

	return MeshComp ? MeshComp->GetSocketLocation(SocketName) : FVector::ZeroVector;
}

void UProject_JAnimNotifyState_MeleeHit::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	if (AActor* OwnerActor = MeshComp ? MeshComp->GetOwner() : nullptr)
	{
		if (UProject_JCombatHitValidationComponent* HitValidation = OwnerActor->FindComponentByClass<UProject_JCombatHitValidationComponent>())
		{
			HitValidation->SetHitWindowOpen(false);
		}
	}
	PreviousSocketLocations.Remove(MeshComp);
	Super::NotifyEnd(MeshComp, Animation, EventReference);
}
