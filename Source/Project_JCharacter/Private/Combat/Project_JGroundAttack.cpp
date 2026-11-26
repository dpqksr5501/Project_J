#include "Combat/Project_JGroundAttack.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Combat/Project_JCombatQueryTypes.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Components/Project_JGameplayWarningMarkerComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "TimerManager.h"
#include "Validation/Project_JDataValidation.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJGroundAttack, Log, All);

namespace
{
#if !UE_BUILD_SHIPPING
bool ShouldLogGroundWarningClient(const UWorld* World)
{
	return World && World->GetNetMode() == NM_Client && FParse::Param(FCommandLine::Get(), TEXT("ProjectJGroundWarningProbe"));
}
#endif

double GetGroundAttackTime(const UWorld& World)
{
	const auto* State = World.GetGameState();
	return State ? State->GetServerWorldTimeSeconds() : World.GetTimeSeconds();
}
}

AProject_JGroundAttackWarning::AProject_JGroundAttackWarning()
{
	SetReplicates(true);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("GroundOrigin"));
	SetNetCullDistanceSquared(FMath::Square(6000.0));
	SetNetUpdateFrequency(15.0f);
}

void AProject_JGroundAttackWarning::Publish(float RadiusCm, double ResolveAt)
{
	check(HasAuthority());
	State.RadiusCm = RadiusCm;
	State.ResolveAt = ResolveAt;
	UpdateMarker();
	ForceNetUpdate();
}

void AProject_JGroundAttackWarning::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, State);
}

void AProject_JGroundAttackWarning::EndPlay(const EEndPlayReason::Type Reason)
{
#if !UE_BUILD_SHIPPING
	if (IsValid(Marker) && ShouldLogGroundWarningClient(GetWorld()))
		UE_LOG(LogTemp, Display, TEXT("PROJECTJ_GROUND_WARNING retired actor=%s"), *GetName());
#endif
	GetWorldTimerManager().ClearTimer(MarkerTimer);
	Super::EndPlay(Reason);
}

void AProject_JGroundAttackWarning::UpdateMarker()
{
	const auto* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer || IsActorBeingDestroyed() || State.RadiusCm <= 0.0f) return;
	if (World->GetNetMode() == NM_Client && !World->GetGameState())
	{
		GetWorldTimerManager().SetTimer(MarkerTimer, this, &ThisClass::UpdateMarker, 0.1f, false);
		return;
	}
	const double Remaining = State.ResolveAt - GetGroundAttackTime(*World);
	if (Remaining <= 0.0)
	{
#if !UE_BUILD_SHIPPING
		if (IsValid(Marker) && ShouldLogGroundWarningClient(World))
		{
			UE_LOG(LogTemp, Display, TEXT("PROJECTJ_GROUND_WARNING expired actor=%s"), *GetName());
		}
		else if (ShouldLogGroundWarningClient(World))
		{
			UE_LOG(LogTemp, Display, TEXT("PROJECTJ_GROUND_WARNING arrived_expired radius=%.1f"), State.RadiusCm);
		}
#endif
		if (Marker) Marker->DestroyComponent();
		Marker = nullptr;
		return;
	}
	if (!IsValid(Marker))
	{
		Marker = NewObject<UProject_JGameplayWarningMarkerComponent>(this);
		Marker->SetGroundCircle(State.RadiusCm, GetActorTransform());
		Marker->SetupAttachment(RootComponent);
		Marker->RegisterComponent();
#if !UE_BUILD_SHIPPING
		if (ShouldLogGroundWarningClient(World))
			UE_LOG(LogTemp, Display, TEXT("PROJECTJ_GROUND_WARNING visible actor=%s remaining=%.3f radius=%.1f render_state=%d caster_owner=%d surface_up_z=%.3f"),
				*GetName(), Remaining, State.RadiusCm, Marker->IsRenderStateCreated(), GetOwner() != nullptr, Marker->GetUpVector().Z);
#endif
	}
	GetWorldTimerManager().SetTimer(MarkerTimer, this, &ThisClass::UpdateMarker, static_cast<float>(Remaining), false);
}

UProject_JGameplayAbility_GroundAttack::UProject_JGameplayAbility_GroundAttack()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	const auto& Tags = FProject_JGameplayTags::Get();
	ActivationOwnedTags.AddTag(Tags.State_Attacking);
	ActivationBlockedTags.AddTag(Tags.State_Attacking);
	ActivationBlockedTags.AddTag(Tags.State_Dead);
}

FString UProject_JGameplayAbility_GroundAttack::GetConfigurationFailure(const UProject_JAttackDefinition* Definition) const
{
	if (!FMath::IsFinite(RadiusCm) || RadiusCm < 50.f || RadiusCm > 1500.f) return TEXT("RadiusCm must be 50..1500");
	if (!FMath::IsFinite(ForwardOffsetCm) || ForwardOffsetCm < 0.f || ForwardOffsetCm > 2000.f) return TEXT("ForwardOffsetCm must be 0..2000");
	if (!FMath::IsFinite(WarningSeconds) || WarningSeconds < 0.5f || WarningSeconds > 5.f) return TEXT("WarningSeconds must be 0.5..5");
	if (MaxTargets < 1 || MaxTargets > 32) return TEXT("MaxTargets must be 1..32");
	if (Definition && Definition->WarningPolicy != EProject_JAttackWarningPolicy::GroundArea)
		return TEXT("The assigned Attack must use the GroundArea warning policy");
	return Definition ? Definition->GetWarningContractFailure() : FString();
}

#if WITH_EDITOR
FString UProject_JGameplayAbility_GroundAttack::GetOwnerGrantFailure() const
{
	return Attack ? GetConfigurationFailure(Attack.Get())
		: TEXT("assign Attack on the ability; this grant supplies an actor or item as its GAS source");
}

EDataValidationResult UProject_JGameplayAbility_GroundAttack::IsDataValid(FDataValidationContext& Context) const
{
	const auto Parent = Super::IsDataValid(Context);
	// GAS may supply the definition through the spec's SourceObject at grant time.
	const FString Failure = GetConfigurationFailure(Attack.Get());
	if (!Failure.IsEmpty()) Context.AddError(FText::FromString(Failure));
	return Project_J::DataValidation::MakeResult(Parent, !Failure.IsEmpty());
}
#endif

void UProject_JGameplayAbility_GroundAttack::ActivateAbility(FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* Info, FGameplayAbilityActivationInfo Activation, const FGameplayEventData*)
{
	auto* World = GetWorld();
	auto* Avatar = GetAvatarActorFromActorInfo();
	auto* ASC = GetAbilitySystemComponentFromActorInfo();
	auto* ProjectASC = Cast<UProject_JAbilitySystemComponent>(ASC);
	const auto* Definition = Cast<UProject_JAttackDefinition>(GetSourceObject(Handle, Info));
	if (!Definition) Definition = Attack.Get();
	const bool bValid = World && !World->bIsTearingDown && IsValid(Avatar) && Avatar->HasAuthority() && IsValid(ASC)
		&& !Avatar->IsActorBeingDestroyed() && (!ProjectASC || !ProjectASC->IsActorInfoChanging())
		&& Definition && GetConfigurationFailure(Definition).IsEmpty()
		&& Avatar->FindComponentByClass<UProject_JCombatHitValidationComponent>();
	if (!bValid)
	{
		EndAbility(Handle, Info, Activation, true, true);
		return;
	}
	FGameplayTagContainer Tags;
	ASC->GetOwnedGameplayTags(Tags);
	const FVector Center = Avatar->GetActorLocation() + Avatar->GetActorForwardVector().GetSafeNormal2D() * ForwardOffsetCm;
	FHitResult Ground;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ProjectJGroundAttackFloor), false, Avatar);
	if (!Tags.HasAll(Definition->RequiredOwnerTags) || Tags.HasAny(Definition->BlockedOwnerTags)
		|| !World->LineTraceSingleByObjectType(Ground, Center + FVector(0, 0, 40), Center - FVector(0, 0, 260),
			FCollisionObjectQueryParams(ECC_WorldStatic), Params)
		|| World->LineTraceTestByObjectType(Avatar->GetActorLocation(), Ground.ImpactPoint + FVector(0, 0, 24),
			FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		EndAbility(Handle, Info, Activation, true, true);
		return;
	}

	HitOrigin = Ground.ImpactPoint + FVector(0, 0, UProject_JGameplayWarningMarkerComponent::GroundClearanceCm);
	const uint64 Serial = ++ExecutionSerial;
	ActiveAttack = Definition;
	Warning = World->SpawnActor<AProject_JGroundAttackWarning>(HitOrigin, FRotationMatrix::MakeFromZ(Ground.ImpactNormal).Rotator());
	ExecutionAvatar = Avatar;
	ExecutionASC = ASC;
	ExecutionWorld = World;
	Avatar->OnEndPlay.AddUniqueDynamic(this, &ThisClass::OnAvatarEndPlay);
	if (ProjectASC)
	{
		ExecutionEpoch = ProjectASC->GetActorInfoRevision();
		ActorInfoChangingHandle = ProjectASC->OnActorInfoChanging.AddUObject(this, &ThisClass::OnActorInfoChanging);
	}
	const auto Dead = FProject_JGameplayTags::Get().State_Dead;
	DeathTagHandle = ASC->RegisterGameplayTagEvent(Dead, EGameplayTagEventType::NewOrRemoved)
		.AddUObject(this, &ThisClass::OnDeathTagChanged);
	if (!Warning || !CommitAbility(Handle, Info, Activation))
	{
		EndAbility(Handle, Info, Activation, true, true);
		return;
	}
	if (!IsActive() || Serial != ExecutionSerial || !IsValid(Warning)) return;
	Warning->Publish(RadiusCm, GetGroundAttackTime(*World) + WarningSeconds);
	World->GetTimerManager().SetTimer(ResolveTimer, this, &ThisClass::Resolve, WarningSeconds, false);
}

void UProject_JGameplayAbility_GroundAttack::OnDeathTagChanged(FGameplayTag, int32 Count)
{
	if (Count > 0) EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

void UProject_JGameplayAbility_GroundAttack::OnActorInfoChanging(AActor*, AActor*)
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

void UProject_JGameplayAbility_GroundAttack::OnAvatarEndPlay(AActor*, EEndPlayReason::Type)
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

void UProject_JGameplayAbility_GroundAttack::Resolve()
{
	auto* World = ExecutionWorld.Get();
	auto* Source = ExecutionAvatar.Get();
	auto* ASC = ExecutionASC.Get();
	const uint64 Serial = ExecutionSerial;
	const auto IsCurrent = [&]()
	{
		const auto* ProjectASC = Cast<UProject_JAbilitySystemComponent>(ASC);
		return IsActive() && Serial == ExecutionSerial && World && !World->bIsTearingDown && IsValid(Warning) && ActiveAttack && IsValid(Source)
			&& !Source->IsActorBeingDestroyed() && Source->HasAuthority() && IsValid(ASC)
			&& ASC->GetAvatarActor() == Source && GetAbilitySystemComponentFromActorInfo() == ASC
			&& (!ProjectASC || (!ProjectASC->IsActorInfoChanging() && ProjectASC->GetActorInfoRevision() == ExecutionEpoch))
			&& !ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Dead);
	};
	const auto* Hit = Source ? Source->FindComponentByClass<UProject_JCombatHitValidationComponent>() : nullptr;
	if (!IsCurrent() || !Hit)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	FProject_JHitQuerySpec Query;
	Query.Origin = HitOrigin;
	Query.OuterRadius = RadiusCm;
	Query.TargetPaddingCm = 0.0f;
	Query.MaxTargets = MaxTargets;
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, HitOrigin, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeBox(FVector(RadiusCm, RadiusCm, Project_J::Combat::GetCircleHalfHeight(RadiusCm))),
		FCollisionQueryParams(SCENE_QUERY_STAT(ProjectJGroundAttack), false, Source));
	TArray<AActor*> Candidates;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (auto* Candidate = Overlap.GetActor()) Candidates.AddUnique(Candidate);
	}
	Candidates.Sort([&](const AActor& A, const AActor& B)
	{
		const double ADistance = FVector::DistSquared(A.GetActorLocation(), HitOrigin);
		const double BDistance = FVector::DistSquared(B.GetActorLocation(), HitOrigin);
		return ADistance == BDistance ? A.GetUniqueID() < B.GetUniqueID() : ADistance < BDistance;
	});
	TArray<AActor*> Targets;
	TArray<float> Multipliers;
	Hit->ServerExecuteHitQuery(Query, Candidates, Targets, Multipliers, -1.0);
	UE_LOG(LogProjectJGroundAttack, Verbose, TEXT("pulse caster=%s origin=%s caster_location=%s facing=%s candidates=%d targets=%d"),
		*GetNameSafe(Source), *HitOrigin.ToCompactString(), *Source->GetActorLocation().ToCompactString(),
		*Source->GetActorForwardVector().ToCompactString(), Candidates.Num(), Targets.Num());
	for (auto* Target : Targets)
	{
		if (!IsCurrent()) break;
		auto* TargetASC = IsValid(Target) ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target) : nullptr;
		if (!TargetASC || TargetASC->GetAvatarActor() != Target
			|| !Project_J::Combat::IsPointInsideAoESpec(Query, Target->GetActorLocation())
			|| !Project_J::Combat::CanAffectCombatTarget(Source, Target, TargetASC)) continue;
		const auto Context = ASC->MakeEffectContext();
		const auto Spec = ASC->MakeOutgoingSpec(ActiveAttack->DamageEffect, GetAbilityLevel(), Context);
		if (Spec.IsValid()) ASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
	}
	if (IsCurrent() && ImpactCue.IsValid())
	{
		FGameplayCueParameters Cue;
		Cue.Location = HitOrigin;
		Cue.Instigator = Source;
		ASC->ExecuteGameplayCue(ImpactCue, Cue);
	}
	if (IsActive() && Serial == ExecutionSerial) EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UProject_JGameplayAbility_GroundAttack::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
	FGameplayAbilityActivationInfo Activation, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (!IsActive() || bEnding) return;
	TGuardValue<bool> EndingGuard(bEnding, true);
	++ExecutionSerial;
	if (auto* World = ExecutionWorld.Get()) World->GetTimerManager().ClearTimer(ResolveTimer);
	if (auto* Avatar = ExecutionAvatar.Get()) Avatar->OnEndPlay.RemoveDynamic(this, &ThisClass::OnAvatarEndPlay);
	if (auto* ASC = ExecutionASC.Get())
	{
		ASC->RegisterGameplayTagEvent(FProject_JGameplayTags::Get().State_Dead, EGameplayTagEventType::NewOrRemoved).Remove(DeathTagHandle);
		if (auto* ProjectASC = Cast<UProject_JAbilitySystemComponent>(ASC)) ProjectASC->OnActorInfoChanging.Remove(ActorInfoChangingHandle);
	}
	DeathTagHandle.Reset();
	ActorInfoChangingHandle.Reset();
	ExecutionAvatar.Reset();
	ExecutionASC.Reset();
	ExecutionWorld.Reset();
	ExecutionEpoch = 0;
	ActiveAttack = nullptr;
	auto* RetiredWarning = Warning.Get();
	Warning = nullptr;
	if (IsValid(RetiredWarning)) RetiredWarning->Destroy();
	Super::EndAbility(Handle, Info, Activation, bReplicateEndAbility, bWasCancelled);
}
