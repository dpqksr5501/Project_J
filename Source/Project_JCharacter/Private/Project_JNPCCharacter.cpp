// Fill out your copyright notice in the Description page of Project Settings.

#include "Project_JNPCCharacter.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/Project_JNPCActionComponent.h"
#include "Components/Project_JNPCActivationComponent.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Components/Project_JCombatPresentationComponent.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#if WITH_EDITOR
#include "Validation/Project_JDataValidation.h"
#endif

AProject_JNPCCharacter::AProject_JNPCCharacter()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	CreateDefaultSubobject<UProject_JProgressionComponent>(TEXT("Progression"));

	// In Stage 5, NPC characters must construct their own local GAS/Equipment components.
	AbilitySystemComponent = CreateDefaultSubobject<UProject_JAbilitySystemComponent>(TEXT("NPCAbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);

	AttributeSet = CreateDefaultSubobject<UProject_JAttributeSet>(TEXT("NPCAttributeSet"));
	EquipmentManager = CreateDefaultSubobject<UProject_JEquipmentManagerComponent>(TEXT("NPCEquipmentManager"));
}

void AProject_JNPCCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (bApplyDefaultNPCOptimizationPolicy)
	{
		ApplyDefaultNPCOptimizationPolicy();
	}
}


bool AProject_JNPCCharacter::IsRuntimeControllerIdle(const AAIController* RuntimeAI) const
{
    return IsValid(RuntimeAI) && !RuntimeAI->IsActorBeingDestroyed() && RuntimeAI->GetWorld() == GetWorld()
        && RuntimeAI->GetPathFollowingComponent() && RuntimeAI->GetPathFollowingComponent()->GetStatus() == EPathFollowingStatus::Idle
        && (!RuntimeAI->GetBrainComponent() || !RuntimeAI->GetBrainComponent()->IsRunning());
}

bool AProject_JNPCCharacter::HasUnclaimedRuntimeActions() const
{
    TInlineComponentArray<UProject_JNPCActionComponent*> Actions(this);
    for (const auto* Action : Actions)
        if (Action->GetOwnershipToken() || Action->HasPendingResume() || Action->GetActionState() != EProjectJNPCActionState::Disabled) return false;
    TInlineComponentArray<UProject_JTargetScoringComponent*> Scoring(this);
    for (const auto* Source : Scoring) if (Source->IsBatchedNPCDecisionRegistered()) return false;
    TInlineComponentArray<UProject_JNPCActivationComponent*> Activations(this);
    for (const auto* Activation : Activations) if (Activation->HasOwnedActivation()) return false;
    return true;
}

AAIController* AProject_JNPCCharacter::PrepareRuntimeController(AController*& ExpectedController, TFunctionRef<bool()> IsCurrent)
{
    const TStrongObjectPtr<UWorld> World(GetWorld());
	TStrongObjectPtr<AAIController> AI(Cast<AAIController>(ExpectedController));
	if (ExpectedController && (!IsRuntimeControllerIdle(AI.Get()) || !IsCurrent() || AI->GetPawn() != this)) return nullptr;
	if (!ExpectedController)
	{
		// Only directly created controllers are reusable/retirable by this helper.
		AI.Reset(CreatedActionController.Get());
		if (AI.IsValid() && (AI->IsActorBeingDestroyed() || AI->GetWorld() != World.Get() || AI->GetPawn()))
		{
			if (AI->GetPawn() == this) return nullptr; // Inconsistent ownership: do not take over.
			CreatedActionController.Reset(); AI.Reset(); // It now belongs elsewhere; never destroy it.
		}
		if (!AI.IsValid())
		{
			UClass* ControllerClass = AIControllerClass.Get();
			if (!ControllerClass || !ControllerClass->IsChildOf(AAIController::StaticClass()) || ControllerClass->HasAnyClassFlags(CLASS_Abstract)) return nullptr;
			FActorSpawnParameters Params;
			// APawn::PossessedBy owns the pawn -> controller edge; a reverse Owner edge would form a cycle.
			Params.Instigator = GetInstigator(); Params.OverrideLevel = GetLevel(); Params.ObjectFlags |= RF_Transient;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AI.Reset(World->SpawnActor<AAIController>(ControllerClass, GetActorLocation(), GetActorRotation(), Params));
			if (!IsCurrent() || !IsValid(AI.Get()) || AI->IsActorBeingDestroyed() || AI->GetPawn())
			{
				if (IsValid(AI.Get()) && !AI->IsActorBeingDestroyed() && !AI->GetPawn()) AI->Destroy();
				return nullptr;
			}
			CreatedActionController = AI.Get();
		}
		if (!IsRuntimeControllerIdle(AI.Get()) || !IsCurrent() || !HasUnclaimedRuntimeActions()) return nullptr;
		ExpectedController = AI.Get();
		AI->Possess(this);
		if (!IsCurrent() || !IsRuntimeControllerIdle(AI.Get()) || !IsCurrent() || AI->GetPawn() != this || !HasUnclaimedRuntimeActions()) return nullptr;
	}
    return AI.Get();
}

bool AProject_JNPCCharacter::StartNavigationTo(FVector Destination, double AcceptanceRadius, TFunction<void(bool)> OnStopped)
{
	check(IsInGameThread());
	if (bRuntimeActionEnding || bPreparingRuntimeAction || HasCombatActions() || NavigationToken || NavigationAction.IsValid() || NavigationRequest == MAX_uint64
		|| !HasAuthority() || !HasActorBegunPlay() || IsActorBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown || GetWorld()->GetNetMode() == NM_Client
		|| Destination.ContainsNaN() || Destination.GetAbsMax() > 1.e9
		|| !FMath::IsFinite(AcceptanceRadius) || AcceptanceRadius < 1 || AcceptanceRadius > 2000) return false;
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	const TStrongObjectPtr<UWorld> World(GetWorld());
	const uint64 Request = ++NavigationRequest;
	TGuardValue<bool> Starting(bPreparingRuntimeAction, true);
	const TStrongObjectPtr<AController> InitialController(GetController());
	AController* ExpectedController = InitialController.Get();
	const TStrongObjectPtr<UAbilitySystemComponent> ASC(GetAbilitySystemComponent());
	const auto IsCurrent = [&]
	{
		return !bRuntimeActionEnding && !IsActorBeingDestroyed() && HasAuthority() && HasActorBegunPlay()
			&& NavigationRequest == Request && GetWorld() == World.Get() && !World->bIsTearingDown
			&& GetController() == ExpectedController && IsValid(ASC.Get()) && !ASC->IsBeingDestroyed()
			&& GetAbilitySystemComponent() == ASC.Get() && ASC->GetAvatarActor() == this;
	};
	if (!IsCurrent() || !HasUnclaimedRuntimeActions()) return false;
	// Life is extensible. Do not create/possess a controller for an already dead NPC.
	const bool bDead = IProject_JCombatInterface::Execute_IsDead(this);
	if (!IsCurrent() || bDead || !HasUnclaimedRuntimeActions()) return false;
	const TStrongObjectPtr<AAIController> AI(PrepareRuntimeController(ExpectedController, IsCurrent));
	if (!AI || !IsCurrent()) return false;
	UProject_JNPCActionComponent* Selected = nullptr;
	TInlineComponentArray<UProject_JNPCActionComponent*> Actions(this);
	for (auto* Action : Actions)
		if (IsValid(Action) && !Action->IsBeingDestroyed() && Action->IsRegistered() && Action->GetWorld() == World.Get()) { Selected = Action; break; }
	const bool bCreatedAction = Selected == nullptr;
	if (!Selected) Selected = NewObject<UProject_JNPCActionComponent>(this);
	const TStrongObjectPtr<UProject_JNPCActionComponent> Action(Selected);
	const auto RetireUnclaimedAction = [&]
	{
		if (bCreatedAction && IsValid(Action.Get()) && !Action->IsBeingDestroyed() && Action->GetOwner() == this
			&& !Action->GetOwnershipToken() && !Action->HasPendingResume() && Action->GetActionState() == EProjectJNPCActionState::Disabled) Action->DestroyComponent();
	};
	if (!Action->IsRegistered()) { AddInstanceComponent(Action.Get()); Action->RegisterComponent(); }
	// Registration/possession may run arbitrary native/BP callbacks. Re-enumerate, never keep container references.
	if (!IsCurrent() || !IsValid(Action.Get()) || Action->IsBeingDestroyed() || !Action->IsRegistered()
		|| Action->GetOwner() != this || Action->GetWorld() != World.Get() || !HasUnclaimedRuntimeActions()
		|| !IsRuntimeControllerIdle(AI.Get()) || !IsCurrent() || AI->GetPawn() != this) { RetireUnclaimedAction(); return false; }
	NavigationAction = Action.Get();
	const TWeakObjectPtr<ThisClass> WeakSelf(this);
	const TWeakObjectPtr<UProject_JNPCActionComponent> WeakAction(Action.Get());
	const bool bAccepted = Action->StartNavigationTo(Destination, AcceptanceRadius, [WeakSelf, WeakAction, Request](bool bArrived)
	{
		if (auto* Self = WeakSelf.Get()) Self->OnNavigationStopped(Request, WeakAction.Get(), bArrived);
	});
	const uint64 Token = bAccepted ? Action->GetOwnershipToken() : 0;
	if (!bAccepted || !Token || !IsCurrent() || NavigationAction.Get() != Action.Get())
	{
		if (NavigationRequest == Request && NavigationAction.Get() == Action.Get()) { NavigationAction.Reset(); NavigationStopped = {}; }
		if (bAccepted && Token) Action->StopActionsIfOwned(Token);
		RetireUnclaimedAction();
		return false;
	}
	NavigationToken = Token; NavigationStopped = MoveTemp(OnStopped);
	return true;
}

void AProject_JNPCCharacter::OnNavigationStopped(uint64 Request, UProject_JNPCActionComponent* Source, bool bArrived)
{
	if (NavigationRequest != Request || NavigationAction.Get() != Source || !NavigationToken) return;
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	NavigationAction.Reset(); NavigationToken = 0;
	if (NavigationRequest != MAX_uint64) ++NavigationRequest;
	auto Stopped = MoveTemp(NavigationStopped);
	// The action has already retired its scheduler/path/move/context. New starts in this callback are legal.
	if (Stopped && !IsGarbageCollecting()) Stopped(bArrived);
}

void AProject_JNPCCharacter::StopNavigation()
{
	check(IsInGameThread());
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	const TStrongObjectPtr<UProject_JNPCActionComponent> Action(NavigationAction.Get());
	const uint64 Token = NavigationToken;
	NavigationAction.Reset(); NavigationToken = 0;
	if (NavigationRequest != MAX_uint64) ++NavigationRequest;
	auto Stopped = MoveTemp(NavigationStopped);
	if (IsValid(Action.Get()) && !Action->IsBeingDestroyed()) Action->StopActionsIfOwned(Token);
	// Keep the controller on the same NPC for an explicit neutral-to-combat handoff.
	if (Stopped && !IsGarbageCollecting()) Stopped(false);
}

bool AProject_JNPCCharacter::HasCombatActions() const
{
    const auto* Activation = CombatActivation.Get();
    return CombatToken && IsValid(Activation) && !Activation->IsBeingDestroyed() && Activation->GetOwner() == this
        && Activation->GetOwnershipToken() == CombatToken;
}

bool AProject_JNPCCharacter::AreCombatCapabilitiesReady(const UProject_JCombatHitValidationComponent* Hit,
    const UProject_JCombatPresentationComponent* Presentation) const
{
    const auto IsUsable = [this](const UActorComponent* Component)
    {
        return IsValid(Component) && !Component->IsBeingDestroyed() && Component->IsRegistered() && Component->HasBegunPlay()
            && Component->GetOwner() == this && Component->GetWorld() == GetWorld() && Component->GetIsReplicated();
    };
    if (!IsUsable(Hit) || !IsUsable(Presentation)) return false;
    TInlineComponentArray<UProject_JCombatHitValidationComponent*> Hits(this);
    TInlineComponentArray<UProject_JCombatPresentationComponent*> Presentations(this);
    // All existing native consumers use FindComponentByClass. Ambiguous/unregistered authored siblings are not overridden.
    return Hits.Num() == 1 && Hits[0] == Hit && FindComponentByClass<UProject_JCombatHitValidationComponent>() == Hit
        && Presentations.Num() == 1 && Presentations[0] == Presentation
        && FindComponentByClass<UProject_JCombatPresentationComponent>() == Presentation;
}

bool AProject_JNPCCharacter::StartCombatActions(const UProject_JNPCActivationDefinition* Definition, int32 TeamId)
{
    check(IsInGameThread());
    if (bRuntimeActionEnding || bPreparingRuntimeAction || HasCombatActions() || NavigationToken || NavigationAction.IsValid()
        || CombatRequest == MAX_uint64 || !IsValid(Definition) || TeamId < 0 || !HasAuthority() || !HasActorBegunPlay()
        || IsActorBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown || GetWorld()->GetNetMode() == NM_Client) return false;
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    const TStrongObjectPtr<UWorld> World(GetWorld());
    const TStrongObjectPtr<const UProject_JNPCActivationDefinition> KeepDefinition(Definition);
    const uint64 Request = ++CombatRequest;
    TGuardValue<bool> Preparing(bPreparingRuntimeAction, true);
    // A retired external epoch cannot make an old helper binding own a new activation.
    CombatActivation.Reset(); CombatToken = 0;
    const TStrongObjectPtr<AController> InitialController(GetController());
    AController* ExpectedController = InitialController.Get();
    const TStrongObjectPtr<UAbilitySystemComponent> ASC(GetAbilitySystemComponent());
    const auto* ProjectASC = Cast<UProject_JAbilitySystemComponent>(ASC.Get());
    const uint64 ActorInfoRevision = ProjectASC ? ProjectASC->GetActorInfoRevision() : 0;
    const auto IsCurrent = [&]
    {
        return !bRuntimeActionEnding && !IsActorBeingDestroyed() && HasAuthority() && HasActorBegunPlay()
            && CombatRequest == Request && GetWorld() == World.Get() && !World->bIsTearingDown
            && GetController() == ExpectedController && IsValid(ASC.Get()) && !ASC->IsBeingDestroyed()
            && GetAbilitySystemComponent() == ASC.Get() && ASC->GetAvatarActor() == this
            && (!ProjectASC || (!ProjectASC->IsActorInfoChanging() && ProjectASC->GetActorInfoRevision() == ActorInfoRevision));
    };
    if (!IsCurrent() || !HasUnclaimedRuntimeActions()) return false;
    const bool bDead = IProject_JCombatInterface::Execute_IsDead(this);
    if (!IsCurrent() || bDead || !HasUnclaimedRuntimeActions()) return false;
    const TStrongObjectPtr<AAIController> AI(PrepareRuntimeController(ExpectedController, IsCurrent));
    if (!AI || !IsCurrent()) return false;

    TInlineComponentArray<UProject_JCombatHitValidationComponent*> ExistingHits(this);
    TInlineComponentArray<UProject_JCombatPresentationComponent*> ExistingPresentations(this);
    const auto IsUsableExisting = [&](const UActorComponent* Component)
    {
        return IsValid(Component) && !Component->IsBeingDestroyed() && Component->IsRegistered() && Component->HasBegunPlay()
            && Component->GetOwner() == this && Component->GetWorld() == World.Get() && Component->GetIsReplicated();
    };
    // Preserve authored settings and do not pick an arbitrary duplicate or register a deliberately inactive component.
    if (ExistingHits.Num() > 1 || ExistingPresentations.Num() > 1
        || (ExistingHits.Num() && (!IsUsableExisting(ExistingHits[0]) || ExistingHits[0]->GetActiveAttackDefinition()
            || ExistingHits[0]->GetActiveAttackNodeTag().IsValid()))
        || (ExistingPresentations.Num() && (!IsUsableExisting(ExistingPresentations[0])
            || ExistingPresentations[0]->GetActiveAttackTag().IsValid()))) return false;
    const bool bCreatedHit = ExistingHits.IsEmpty(), bCreatedPresentation = ExistingPresentations.IsEmpty();
    const TStrongObjectPtr<UProject_JCombatHitValidationComponent> Hit(bCreatedHit
        ? NewObject<UProject_JCombatHitValidationComponent>(this) : ExistingHits[0]);
    const TStrongObjectPtr<UProject_JCombatPresentationComponent> Presentation(bCreatedPresentation
        ? NewObject<UProject_JCombatPresentationComponent>(this) : ExistingPresentations[0]);

    UProject_JNPCActionComponent* SelectedAction = nullptr;
    TInlineComponentArray<UProject_JNPCActionComponent*> Actions(this);
    for (auto* Entry : Actions) if (IsValid(Entry) && !Entry->IsBeingDestroyed() && Entry->IsRegistered()) { SelectedAction = Entry; break; }
    const bool bCreatedAction = SelectedAction == nullptr;
    const TStrongObjectPtr<UProject_JNPCActionComponent> Action(SelectedAction ? SelectedAction : NewObject<UProject_JNPCActionComponent>(this));
    UProject_JTargetScoringComponent* SelectedScoring = nullptr;
    TInlineComponentArray<UProject_JTargetScoringComponent*> Sources(this);
    for (auto* Entry : Sources) if (IsValid(Entry) && !Entry->IsBeingDestroyed() && Entry->IsRegistered()) { SelectedScoring = Entry; break; }
    const bool bCreatedScoring = SelectedScoring == nullptr;
    const TStrongObjectPtr<UProject_JTargetScoringComponent> Scoring(SelectedScoring ? SelectedScoring : NewObject<UProject_JTargetScoringComponent>(this));
    UProject_JNPCActivationComponent* SelectedActivation = nullptr;
    TInlineComponentArray<UProject_JNPCActivationComponent*> Activations(this);
    for (auto* Entry : Activations) if (IsValid(Entry) && !Entry->IsBeingDestroyed() && Entry->IsRegistered()) { SelectedActivation = Entry; break; }
    const bool bCreatedActivation = SelectedActivation == nullptr;
    const TStrongObjectPtr<UProject_JNPCActivationComponent> Activation(SelectedActivation ? SelectedActivation : NewObject<UProject_JNPCActivationComponent>(this));
    const auto RetireUnclaimed = [&]
    {
        if (bCreatedActivation && IsValid(Activation.Get()) && !Activation->IsBeingDestroyed() && !Activation->HasOwnedActivation()) Activation->DestroyComponent();
        if (bCreatedScoring && IsValid(Scoring.Get()) && !Scoring->IsBeingDestroyed() && !Scoring->IsBatchedNPCDecisionRegistered()) Scoring->DestroyComponent();
        if (bCreatedAction && IsValid(Action.Get()) && !Action->IsBeingDestroyed() && !Action->GetOwnershipToken() && !Action->HasPendingResume()
            && Action->GetActionState() == EProjectJNPCActionState::Disabled) Action->DestroyComponent();
        // A registration/teardown callback may have installed a foreign/newer execution. Never destroy its support.
        const auto CanRetireCapability = [&]
        {
            return !HasCombatActions() && HasUnclaimedRuntimeActions()
                && (!IsValid(Hit.Get()) || (!Hit->GetActiveAttackDefinition() && !Hit->GetActiveAttackNodeTag().IsValid()))
                && (!IsValid(Presentation.Get()) || !Presentation->GetActiveAttackTag().IsValid());
        };
        if (bCreatedPresentation && CanRetireCapability() && IsValid(Presentation.Get()) && !Presentation->IsBeingDestroyed()
            && Presentation->GetOwner() == this && !Presentation->GetActiveAttackTag().IsValid()) Presentation->DestroyComponent();
        if (bCreatedHit && CanRetireCapability() && IsValid(Hit.Get()) && !Hit->IsBeingDestroyed() && Hit->GetOwner() == this
            && !Hit->GetActiveAttackDefinition() && !Hit->GetActiveAttackNodeTag().IsValid()) Hit->DestroyComponent();
    };
    if (!IsCurrent()) { RetireUnclaimed(); return false; }
    if (!Hit->IsRegistered()) { AddInstanceComponent(Hit.Get()); Hit->RegisterComponent(); }
    TInlineComponentArray<UProject_JCombatHitValidationComponent*> RegisteredHits(this);
    if (!IsCurrent() || !IsUsableExisting(Hit.Get()) || RegisteredHits.Num() != 1 || RegisteredHits[0] != Hit.Get()
        || FindComponentByClass<UProject_JCombatHitValidationComponent>() != Hit.Get())
    { RetireUnclaimed(); return false; }
    if (!Presentation->IsRegistered()) { AddInstanceComponent(Presentation.Get()); Presentation->RegisterComponent(); }
    if (!IsCurrent() || !AreCombatCapabilitiesReady(Hit.Get(), Presentation.Get())) { RetireUnclaimed(); return false; }
    if (!Action->IsRegistered()) { AddInstanceComponent(Action.Get()); Action->RegisterComponent(); }
    if (!IsCurrent() || !AreCombatCapabilitiesReady(Hit.Get(), Presentation.Get())) { RetireUnclaimed(); return false; }
    if (!Scoring->IsRegistered()) { AddInstanceComponent(Scoring.Get()); Scoring->RegisterComponent(); }
    if (!IsCurrent() || !AreCombatCapabilitiesReady(Hit.Get(), Presentation.Get())) { RetireUnclaimed(); return false; }
    if (!Activation->IsRegistered()) { AddInstanceComponent(Activation.Get()); Activation->RegisterComponent(); }
    if (!IsCurrent() || !AreCombatCapabilitiesReady(Hit.Get(), Presentation.Get()) || !IsValid(Action.Get()) || Action->IsBeingDestroyed() || !Action->IsRegistered() || Action->GetOwner() != this
        || !IsValid(Scoring.Get()) || Scoring->IsBeingDestroyed() || !Scoring->IsRegistered() || Scoring->GetOwner() != this
        || !IsValid(Activation.Get()) || Activation->IsBeingDestroyed() || !Activation->IsRegistered() || Activation->GetOwner() != this
        || !HasUnclaimedRuntimeActions() || !IsRuntimeControllerIdle(AI.Get()) || !IsCurrent()
        || !AreCombatCapabilitiesReady(Hit.Get(), Presentation.Get()) || AI->GetPawn() != this
        || Hit->GetActiveAttackDefinition() || Hit->GetActiveAttackNodeTag().IsValid() || Presentation->GetActiveAttackTag().IsValid())
    { RetireUnclaimed(); return false; }
    const bool bAccepted = Activation->ActivateNPC(Definition, Scoring.Get(), Action.Get(), TeamId);
    const uint64 Token = bAccepted ? Activation->GetOwnershipToken() : 0;
    const uint64 ActionOwner = bAccepted ? Action->GetOwnershipToken() : 0;
    const bool bDeadAfterGrant = IsCurrent() ? IProject_JCombatInterface::Execute_IsDead(this) : true;
    if (!bAccepted || !Token || !ActionOwner || !IsCurrent() || !AreCombatCapabilitiesReady(Hit.Get(), Presentation.Get()) || bDeadAfterGrant ||
        Activation->GetOwnershipToken() != Token || Action->GetOwnershipToken() != ActionOwner)
    {
        if (bAccepted && Token) Activation->DeactivateNPCIfOwned(Token);
        RetireUnclaimed(); return false;
    }
    CombatActivation = Activation.Get(); CombatToken = Token;
    return true;
}

void AProject_JNPCCharacter::StopCombatActions()
{
    check(IsInGameThread());
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    const TStrongObjectPtr<UProject_JNPCActivationComponent> Activation(CombatActivation.Get());
    const uint64 Token = CombatToken;
    CombatActivation.Reset(); CombatToken = 0;
    if (CombatRequest != MAX_uint64) ++CombatRequest;
    if (IsValid(Activation.Get()) && !Activation->IsBeingDestroyed()) Activation->DeactivateNPCIfOwned(Token);
}

void AProject_JNPCCharacter::EndPlay(EEndPlayReason::Type Reason)
{
	bRuntimeActionEnding = true;
	StopNavigation();
	StopCombatActions();
	const TStrongObjectPtr<AAIController> RetiredController(CreatedActionController.Get());
	CreatedActionController.Reset();
	if (IsValid(RetiredController.Get()) && !RetiredController->IsActorBeingDestroyed() && RetiredController->GetWorld() == GetWorld()
		&& (!RetiredController->GetPawn() || (RetiredController->GetPawn() == this && GetController() == RetiredController.Get()))) RetiredController->Destroy();
	Super::EndPlay(Reason);
}

bool AProject_JNPCCharacter::HasValidNPCNetworkPolicy() const
{
	if (!FMath::IsFinite(NPCNetCullDistance) || NPCNetCullDistance < 0.0f
		|| !FMath::IsFinite(NPCNetUpdateFrequency) || NPCNetUpdateFrequency < 1.0f
		|| !FMath::IsFinite(NPCMinNetUpdateFrequency) || NPCMinNetUpdateFrequency < 1.0f
		|| NPCMinNetUpdateFrequency > NPCNetUpdateFrequency)
	{
		return false;
	}
	// The actor setter stores float squared distance. This is a representation
	// limit, not a new authored distance cap. Validate before float multiplication.
	const double SquaredCullDistance = FMath::Square(double(NPCNetCullDistance));
	return FMath::IsFinite(SquaredCullDistance) && SquaredCullDistance <= double(MAX_flt);
}

void AProject_JNPCCharacter::ApplyDefaultNPCOptimizationPolicy()
{
	// Reject the network tuple together, preserving the actor's existing writer.
	// Mesh policy is independent and still follows the original path below.
	if (HasValidNPCNetworkPolicy())
	{
		SetNetCullDistanceSquared(FMath::Square(NPCNetCullDistance));
		SetNetUpdateFrequency(NPCNetUpdateFrequency);
		SetMinNetUpdateFrequency(NPCMinNetUpdateFrequency);
	}

	if (USkeletalMeshComponent* MeshComponent = GetMesh())
	{
		MeshComponent->bEnableUpdateRateOptimizations = true;
		MeshComponent->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	}
}

EProject_JNPCUpdateBudgetTier AProject_JNPCCharacter::GetNPCUpdateBudgetTier() const
{
	if (CurrentSignificance >= 3.0f)
	{
		return EProject_JNPCUpdateBudgetTier::Hidden;
	}
	if (CurrentSignificance >= 2.0f)
	{
		return EProject_JNPCUpdateBudgetTier::Far;
	}
	if (CurrentSignificance >= 1.0f)
	{
		return EProject_JNPCUpdateBudgetTier::Mid;
	}
	return EProject_JNPCUpdateBudgetTier::Near;
}

float AProject_JNPCCharacter::GetRecommendedAIUpdateInterval() const
{
	return GetRecommendedAIUpdateIntervalForTier(GetNPCUpdateBudgetTier());
}

EProject_JNPCUpdateBudgetTier AProject_JNPCCharacter::GetDecisionTierForDistance(double Distance, EProject_JNPCUpdateBudgetTier PreviousTier) const
{
	const double Gates[] = {SignificanceNearDistance, SignificanceMidDistance, SignificanceFarDistance};
	if (!FMath::IsFinite(Distance) || Distance < 0 || !FMath::IsFinite(Gates[0]) || !FMath::IsFinite(Gates[1])
		|| !FMath::IsFinite(Gates[2]) || Gates[0] <= 0 || Gates[1] <= Gates[0] || Gates[2] <= Gates[1])
	{
		return EProject_JNPCUpdateBudgetTier::Near;
	}
	int32 Tier = 0;
	for (int32 Boundary = 0; Boundary < 3; ++Boundary)
	{
		// Promotion is immediate; demotion requires 10% extra distance to avoid boundary oscillation.
		const double Gate = Gates[Boundary] * (Boundary >= static_cast<int32>(PreviousTier) ? 1.1 : 1.0);
		if (Distance < Gate) { break; }
		Tier = Boundary + 1;
	}
	return static_cast<EProject_JNPCUpdateBudgetTier>(Tier);
}

float AProject_JNPCCharacter::GetRecommendedAIUpdateIntervalForTier(EProject_JNPCUpdateBudgetTier Tier) const
{
	switch (Tier)
	{
	case EProject_JNPCUpdateBudgetTier::Mid:
		return NPCUpdateBudget.MidUpdateInterval;
	case EProject_JNPCUpdateBudgetTier::Far:
		return NPCUpdateBudget.FarUpdateInterval;
	case EProject_JNPCUpdateBudgetTier::Hidden:
		return NPCUpdateBudget.HiddenUpdateInterval;
	case EProject_JNPCUpdateBudgetTier::Near:
	default:
		return NPCUpdateBudget.NearUpdateInterval;
	}
}

#if WITH_EDITOR
EDataValidationResult AProject_JNPCCharacter::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult ParentResult = Super::IsDataValid(Context);
	bool bHasError = false;
	// Disabled default policy leaves these fields unused; custom writers own it.
	if (bApplyDefaultNPCOptimizationPolicy && !HasValidNPCNetworkPolicy())
	{
		Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJNPC", "InvalidNetworkPolicy",
			"Enabled NPC network policy requires finite nonnegative cull distance with a representable float square, and finite frequencies >= 1 with minimum <= update frequency."));
	}
	return Project_J::DataValidation::MakeResult(ParentResult, bHasError);
}
#endif
