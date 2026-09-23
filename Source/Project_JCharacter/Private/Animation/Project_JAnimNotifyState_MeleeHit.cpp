#include "Animation/Project_JAnimNotifyState_MeleeHit.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Combat/Project_JAttackDefinition.h"

UProject_JAnimNotifyState_MeleeHit::UProject_JAnimNotifyState_MeleeHit()
{
}

void UProject_JAnimNotifyState_MeleeHit::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	if (!MeshComp || SocketName.IsNone() || !MeshComp->DoesSocketExist(SocketName))
	{
		UE_LOG(LogTemp, Warning, TEXT("Melee Hit Trace requires Leader mesh socket '%s' on %s; hit window will not open."),
			*SocketName.ToString(), *GetNameSafe(MeshComp));
		return;
	}
	if (AActor* OwnerActor = MeshComp->GetOwner())
	{
		PreviousTraceLocations.Add(MeshComp, ResolveTraceLocation(MeshComp));
		if (UProject_JCombatHitValidationComponent* HitValidation = OwnerActor->FindComponentByClass<UProject_JCombatHitValidationComponent>())
		{
			HitValidation->SetHitWindowOpen(true);
		}
	}
}

void UProject_JAnimNotifyState_MeleeHit::NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);

	if (!MeshComp)
	{
		return;
	}
	const FVector* PreviousLocation = PreviousTraceLocations.Find(MeshComp);
	if (!PreviousLocation)
	{
		return;
	}
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
	const FVector TraceStart = *PreviousLocation;
	PreviousTraceLocations.Add(MeshComp, TraceLocation);
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
	if (!MeshComp)
	{
		return FVector::ZeroVector;
	}

	// Both the predicted sweep and server rewind history use this Leader-pose
	// endpoint. The presentation weapon and follower/IK pose are cosmetic only.
	const AActor* OwnerActor = MeshComp->GetOwner();
	const UProject_JCombatHitValidationComponent* HitValidation = OwnerActor
		? OwnerActor->FindComponentByClass<UProject_JCombatHitValidationComponent>() : nullptr;
	const UProject_JAttackDefinition* AttackDefinition = HitValidation ? HitValidation->GetActiveAttackDefinition() : nullptr;
	const FVector LocalTip = AttackDefinition && AttackDefinition->HitSpec.bUseCanonicalBladeTipOffset
		? AttackDefinition->HitSpec.CanonicalBladeTipOffset : FVector::ZeroVector;
	return MeshComp->GetSocketTransform(SocketName, RTS_World).TransformPosition(LocalTip);
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
	PreviousTraceLocations.Remove(MeshComp);
	Super::NotifyEnd(MeshComp, Animation, EventReference);
}
