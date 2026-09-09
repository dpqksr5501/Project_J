#include "Components/Project_JCombatHitValidationComponent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Combat/Project_JServerSideRewindComponent.h"
#include "Combat/Project_JAttackDefinition.h"
#include "GameplayEffect.h"
#include "Engine/World.h"
#include "Project_JPlayerCharacter.h"
#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJCombatHitValidation, Log, All);

UProject_JCombatHitValidationComponent::UProject_JCombatHitValidationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UProject_JCombatHitValidationComponent::BeginAttackNode(const FGameplayTag AttackNodeTag, UProject_JAttackDefinition* AttackDefinition, const int32 PredictionKey)
{
	ProtectAttackPose();
	bRequiresWeapon = Cast<AProject_JPlayerCharacter>(GetOwner()) != nullptr;
	AttackEquipment = GetOwner() ? GetOwner()->FindComponentByClass<UProject_JEquipmentRuntimeComponent>() : nullptr;
	AttackWeaponRevision = AttackEquipment.IsValid() ? AttackEquipment->GetWeaponRevision() : 0;
	ActivePredictionKey = PredictionKey;
	ActiveAttackNodeTag = AttackNodeTag;
	ActiveAttackDefinition = AttackDefinition;
	bHitWindowOpen = false;
	bHasAuthoritativeTrace = false;
	ServerHitActors.Reset();
}

void UProject_JCombatHitValidationComponent::EndAttack()
{
	AttackEquipment.Reset();
	AttackWeaponRevision = 0;
	ActivePredictionKey = 0;
	ActiveAttackNodeTag = FGameplayTag();
	ActiveAttackDefinition = nullptr;
	bHitWindowOpen = false;
	bHasAuthoritativeTrace = false;
	ServerHitActors.Reset();
	RestoreAttackPose();
}

void UProject_JCombatHitValidationComponent::ProtectAttackPose()
{
	check(IsInGameThread());
	const auto* Character = Cast<ACharacter>(GetOwner());
	auto* Mesh = Character ? Character->GetMesh() : nullptr;
	if (!Mesh) { return; }
	if (ProtectedMesh.Get() != Mesh)
	{
		RestoreAttackPose();
		if (auto* Budgeted = Cast<UProject_JBudgetedSkeletalMeshComponent>(Mesh)) { Budgeted->SetCombatCritical(true); }
		ProtectedMesh = Mesh;
		SavedVisibility = uint8(Mesh->VisibilityBasedAnimTickOption);
		bSavedURO = Mesh->bEnableUpdateRateOptimizations;
		bSavedSuppressNotifies = Mesh->bSuppressNotifyEventDispatch;
	}
	// A hit window is authoritative gameplay. Hidden meshes/DS must still refresh
	// sockets and dispatch notifies. Do not change root-motion mode or force GT evaluation.
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Mesh->bEnableUpdateRateOptimizations = false;
	Mesh->bSuppressNotifyEventDispatch = false;
}

void UProject_JCombatHitValidationComponent::RestoreAttackPose()
{
	if (auto* Mesh = ProtectedMesh.Get())
	{
		// Preserve a newer explicit policy written by another system.
		if (Mesh->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones)
		{ Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption(SavedVisibility); }
		if (!Mesh->bEnableUpdateRateOptimizations) { Mesh->bEnableUpdateRateOptimizations = bSavedURO; }
		if (!Mesh->bSuppressNotifyEventDispatch) { Mesh->bSuppressNotifyEventDispatch = bSavedSuppressNotifies; }
		if (auto* Budgeted = Cast<UProject_JBudgetedSkeletalMeshComponent>(Mesh)) { Budgeted->SetCombatCritical(false); }
	}
	ProtectedMesh.Reset();
}

void UProject_JCombatHitValidationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	EndAttack();
	Super::EndPlay(Reason);
}

void UProject_JCombatHitValidationComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	EndAttack();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

bool UProject_JCombatHitValidationComponent::HasValidAttackWeapon() const
{
	return !bRequiresWeapon || (AttackWeaponRevision != 0 && AttackEquipment.IsValid()
		&& AttackEquipment->GetWeaponRevision() == AttackWeaponRevision);
}

void UProject_JCombatHitValidationComponent::RecordAuthoritativeTrace(const FVector& TraceStart, const FVector& TraceEnd)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !ActiveAttackNodeTag.IsValid())
	{
		return;
	}

	LastAuthoritativeTraceStart = TraceStart;
	LastAuthoritativeTraceEnd = TraceEnd;
	bHasAuthoritativeTrace = true;
}

void UProject_JCombatHitValidationComponent::SetHitWindowOpen(const bool bOpen)
{
	bHitWindowOpen = bOpen && ActiveAttackNodeTag.IsValid();
}

void UProject_JCombatHitValidationComponent::SubmitPredictedHit(AActor* HitActor, const float ClientTimestamp, const FVector& TraceStart, const FVector& TraceEnd)
{
	if (!HitActor || !ActiveAttackNodeTag.IsValid() || !bHitWindowOpen || !HasValidAttackWeapon() || ActivePredictionKey == 0)
	{
		return;
	}

	ServerRequestSSRHit(HitActor, ClientTimestamp, TraceStart, TraceEnd, ActiveAttackNodeTag, ++LocalRequestSequence, ActivePredictionKey);
}

bool UProject_JCombatHitValidationComponent::ProcessAuthorityHit(AActor* HitActor)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !HitActor || !ActiveAttackNodeTag.IsValid() || !bHitWindowOpen || !bHasAuthoritativeTrace || !HasValidAttackWeapon() || ServerHitActors.Contains(HitActor))
	{
		return false;
	}

	FProject_JCombatHitRequest Request;
	Request.Target = HitActor;
	Request.AttackNodeTag = ActiveAttackNodeTag;
	// Validate the server's recorded weapon sweep, not the request struct's zero
	// default. Otherwise legitimate authority hits depend on distance to world origin.
	Request.TraceStart = LastAuthoritativeTraceStart;
	Request.TraceEnd = LastAuthoritativeTraceEnd;
	if (HitValidationPolicy.ValidateActors(GetOwner(), Request) != EProject_JCombatHitValidationFailure::None)
	{
		return false;
	}
	if (IsTargetObstructed(HitActor))
	{
		return false;
	}

	ServerHitActors.Add(HitActor);
	return ApplyConfirmedHit(HitActor);
}

void UProject_JCombatHitValidationComponent::ServerRequestSSRHit_Implementation(AActor* HitActor, float ClientTimestamp, FVector TraceStart, FVector TraceEnd, FGameplayTag AttackNodeTag, int32 RequestSequence, int32 PredictionKey)
{
	FProject_JCombatHitRequest Request;
	Request.Target = HitActor;
	Request.ClientTimestamp = ClientTimestamp;
	Request.TraceStart = TraceStart;
	Request.TraceEnd = TraceEnd;
	Request.AttackNodeTag = AttackNodeTag;
	Request.RequestSequence = RequestSequence;
	Request.PredictionKey = PredictionKey;

	if (const EProject_JCombatHitValidationFailure ActiveFailure = ValidateActiveAttack(Request); ActiveFailure != EProject_JCombatHitValidationFailure::None)
	{
		UE_LOG(LogProjectJCombatHitValidation, Verbose, TEXT("SSR active attack rejected. Requester=%s Target=%s Node=%s Reason=%s"), *GetNameSafe(GetOwner()), *GetNameSafe(HitActor), *AttackNodeTag.ToString(), LexToString(ActiveFailure));
		return;
	}

	const FProject_JCombatHitValidationResult ValidationResult = ValidateServerHitRequest(Request);
	if (!ValidationResult.bAccepted)
	{
		UE_LOG(LogProjectJCombatHitValidation, Verbose, TEXT("SSR hit request rejected. Requester=%s Target=%s Reason=%s"), *GetNameSafe(GetOwner()), *GetNameSafe(HitActor), LexToString(ValidationResult.Failure));
		return;
	}

	bool bHitConfirmed = false;
	const float TraceRadius = ActiveAttackDefinition ? FMath::Max(0.0f, ActiveAttackDefinition->HitSpec.TraceRadius) : 0.0f;
	if (UProject_JServerSideRewindComponent* SSRComp = HitActor->FindComponentByClass<UProject_JServerSideRewindComponent>())
	{
		bHitConfirmed = SSRComp->ServerVerifyHit(ClientTimestamp, LastAuthoritativeTraceStart, LastAuthoritativeTraceEnd, TraceRadius);
	}
	else
	{
		bHitConfirmed = HitActor->GetComponentsBoundingBox().Intersect(FBox(LastAuthoritativeTraceStart.ComponentMin(LastAuthoritativeTraceEnd), LastAuthoritativeTraceStart.ComponentMax(LastAuthoritativeTraceEnd)).ExpandBy(TraceRadius));
	}

	if (!bHitConfirmed)
	{
		return;
	}

	ServerHitActors.Add(HitActor);
	ApplyConfirmedHit(HitActor);
}

EProject_JCombatHitValidationFailure UProject_JCombatHitValidationComponent::ValidateActiveAttack(const FProject_JCombatHitRequest& Request)
{
	if (!ActiveAttackNodeTag.IsValid() || !HasValidAttackWeapon())
	{
		return EProject_JCombatHitValidationFailure::NoActiveAttack;
	}
	if (Request.PredictionKey == 0 || Request.PredictionKey != ActivePredictionKey)
	{
		return EProject_JCombatHitValidationFailure::AttackActivationMismatch;
	}
	if (!Request.AttackNodeTag.MatchesTagExact(ActiveAttackNodeTag))
	{
		return EProject_JCombatHitValidationFailure::AttackNodeMismatch;
	}
	if (!bHitWindowOpen)
	{
		return EProject_JCombatHitValidationFailure::HitWindowClosed;
	}
	if (!bHasAuthoritativeTrace)
	{
		return EProject_JCombatHitValidationFailure::AuthoritativeTraceUnavailable;
	}
	if (ActiveAttackDefinition &&
		FVector::DistSquared(Request.TraceStart, Request.TraceEnd) > FMath::Square(FMath::Max(0.0f, ActiveAttackDefinition->HitSpec.TraceDistance + ActiveAttackDefinition->HitSpec.TraceRadius)))
	{
		return EProject_JCombatHitValidationFailure::TraceTooLong;
	}
	if (IsTargetObstructed(Request.Target))
	{
		return EProject_JCombatHitValidationFailure::WorldObstructed;
	}
	if (ServerHitActors.Contains(Request.Target))
	{
		return EProject_JCombatHitValidationFailure::DuplicateTarget;
	}
	if (Request.RequestSequence <= LastServerRequestSequence)
	{
		return EProject_JCombatHitValidationFailure::RequestRateLimited;
	}

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (Now - RateWindowStartSeconds >= 1.0)
	{
		RateWindowStartSeconds = Now;
		RequestsInRateWindow = 0;
	}
	if (++RequestsInRateWindow > MaxRequestsPerSecond)
	{
		return EProject_JCombatHitValidationFailure::RequestRateLimited;
	}
	LastServerRequestSequence = Request.RequestSequence;
	return EProject_JCombatHitValidationFailure::None;
}

bool UProject_JCombatHitValidationComponent::IsTargetObstructed(const AActor* HitActor) const
{
	const UWorld* World = GetWorld();
	if (!World || !GetOwner() || !HitActor)
	{
		return true;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ProjectJCombatLineOfSight), false, GetOwner());
	QueryParams.AddIgnoredActor(GetOwner());
	FHitResult BlockingHit;
	if (!World->LineTraceSingleByChannel(BlockingHit, LastAuthoritativeTraceStart, HitActor->GetActorLocation(), ECC_Visibility, QueryParams))
	{
		return false;
	}

	return BlockingHit.GetActor() != HitActor;
}

bool UProject_JCombatHitValidationComponent::ApplyConfirmedHit(AActor* HitActor)
{
	UAbilitySystemComponent* OwnerASC = ResolveOwnerAbilitySystemComponent();
	const TSubclassOf<UGameplayEffect> DamageEffect = ActiveAttackDefinition ? ActiveAttackDefinition->DamageEffect : nullptr;
	if (!HitActor || !DamageEffect || !OwnerASC)
	{
		return false;
	}

	UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(HitActor);
	if (!TargetASC)
	{
		return false;
	}

	FGameplayEffectContextHandle EffectContext = OwnerASC->MakeEffectContext();
	EffectContext.AddInstigator(GetOwner(), GetOwner());
	const FGameplayEffectSpecHandle SpecHandle = OwnerASC->MakeOutgoingSpec(DamageEffect, 1.0f, EffectContext);
	if (SpecHandle.IsValid())
	{
		OwnerASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);
		return true;
	}
	return false;
}

FProject_JCombatHitValidationResult UProject_JCombatHitValidationComponent::ValidateServerHitRequest(const FProject_JCombatHitRequest& Request) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return FProject_JCombatHitValidationResult::Rejected(EProject_JCombatHitValidationFailure::InvalidRequester);
	}
	if (const EProject_JCombatHitValidationFailure Failure = HitValidationPolicy.ValidateRequestData(Request, World->GetTimeSeconds()); Failure != EProject_JCombatHitValidationFailure::None)
	{
		return FProject_JCombatHitValidationResult::Rejected(Failure);
	}
	if (const EProject_JCombatHitValidationFailure Failure = HitValidationPolicy.ValidateActors(GetOwner(), Request); Failure != EProject_JCombatHitValidationFailure::None)
	{
		return FProject_JCombatHitValidationResult::Rejected(Failure);
	}
	return FProject_JCombatHitValidationResult::Accepted();
}

UAbilitySystemComponent* UProject_JCombatHitValidationComponent::ResolveOwnerAbilitySystemComponent() const
{
	return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
}
