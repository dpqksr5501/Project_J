#include "Animation/Project_JAnimNotifyState_MeleeHit.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetSystemLibrary.h"

UProject_JAnimNotifyState_MeleeHit::UProject_JAnimNotifyState_MeleeHit()
{
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

	FVector TraceLocation = MeshComp->GetSocketLocation(SocketName);

	TArray<AActor*> ActorsToIgnore;
	ActorsToIgnore.Add(OwnerActor);

	TArray<FHitResult> OutHits;

	// Simple overlap at socket location. 
	// For better precision at high framerates, sweep from a cached previous frame location can be implemented.
	UKismetSystemLibrary::SphereTraceMultiForObjects(
		World,
		TraceLocation,
		TraceLocation, // End == Start for overlap
		TraceRadius,
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
