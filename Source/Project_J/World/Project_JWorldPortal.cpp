#include "World/Project_JWorldPortal.h"
#include "World/Project_JWorldStreamingComponent.h"
#include "World/Project_JDungeonInstanceSubsystem.h"
#include "Network/Project_JMMOClientBridgeComponent.h"
#include "Gameplay/Project_JDeathReviveSubsystem.h"
#include "Game/Project_JPlayerState.h"
#include "Project_JBaseCharacter.h"
#include "Components/BoxComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/Project_JInteractionTargetComponent.h"
#include "Net/UnrealNetwork.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

AProject_JWorldPortal::AProject_JWorldPortal()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true; SetNetUpdateFrequency(2); SetMinNetUpdateFrequency(1);
    InteractionBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionBounds")); SetRootComponent(InteractionBounds);
    InteractionBounds->InitBoxExtent(FVector(50)); InteractionBounds->SetCollisionObjectType(ECC_WorldDynamic);
    InteractionBounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly); InteractionBounds->SetCollisionResponseToAllChannels(ECR_Overlap);
    InteractionBounds->SetGenerateOverlapEvents(false);
    InteractionTarget = CreateDefaultSubobject<UProject_JInteractionTargetComponent>(TEXT("InteractionTarget"));
    InteractionTarget->InteractionRange = 250;
    InteractionTarget->PromptText = NSLOCTEXT("WorldPortal", "Travel", "Travel");
}
void AProject_JWorldPortal::BeginPlay()
{
    Super::BeginPlay();
    if (HasAuthority() && DungeonDefinition)
    {
        if (DungeonId.IsNone() && !DungeonDefinition->DungeonId.IsNone())
        {
            DungeonId = DungeonDefinition->DungeonId;
        }
        if (UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
        {
            if (auto* Dungeon = GI->GetSubsystem<UProject_JDungeonInstanceSubsystem>())
            {
                Dungeon->RegisterDungeonDefinition(DungeonDefinition);
            }
        }
    }
}
bool AProject_JWorldPortal::IsSpatiallyAvailable(const APawn* Interactor) const
{
    const FName EffectiveDungeonId = !DungeonId.IsNone() ? DungeonId : (DungeonDefinition ? DungeonDefinition->DungeonId : NAME_None);
    if (!IsValid(Interactor) || Interactor->IsActorBeingDestroyed() || IsActorBeingDestroyed()
        || (DestinationPlaceId.IsNone() && EffectiveDungeonId.IsNone())
        || !GetWorld() || GetWorld()->bIsTearingDown || Interactor->GetWorld() != GetWorld() || !InteractionTarget || !InteractionTarget->IsAvailable()) return false;
    const double Range = InteractionTarget->InteractionRange;
    const FVector Origin = Interactor->GetActorLocation(), Target = GetActorLocation();
    if (!FMath::IsFinite(Range) || Range <= 0 || Range > 300 || Origin.ContainsNaN() || Target.ContainsNaN()
        || FVector::DistSquared(Origin, Target) > FMath::Square(Range)) return false;
    if (InteractionTarget->bRequireLineOfSight)
    {
        FCollisionQueryParams Query(SCENE_QUERY_STAT(ProjectJPortalVisibility), false, Interactor); Query.AddIgnoredActor(this);
        if (GetWorld()->LineTraceTestByChannel(Origin, Target, ECC_Visibility, Query)) return false;
    }
    return true;
}
bool AProject_JWorldPortal::CanInteract_Implementation(APawn* Interactor) const { return IsSpatiallyAvailable(Interactor); }
void AProject_JWorldPortal::Interact_Implementation(APawn* Interactor)
{
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    const TStrongObjectPtr<APawn> KeepPawn(Interactor);
    if (!HasAuthority() || !IsSpatiallyAvailable(Interactor)) return;
    if (const auto* BaseChar = Cast<AProject_JBaseCharacter>(Interactor))
    {
        if (!UProject_JDeathReviveSubsystem::CanCharacterPerformAliveAction(BaseChar)) return;
    }
    auto* PC = Cast<APlayerController>(Interactor->GetController());
    const FName EffectiveDungeonId = !DungeonId.IsNone() ? DungeonId : (DungeonDefinition ? DungeonDefinition->DungeonId : NAME_None);
    if (!EffectiveDungeonId.IsNone())
    {
        if (DungeonDefinition)
        {
            if (UGameInstance* GI = GetWorld()->GetGameInstance())
            {
                if (auto* Dungeon = GI->GetSubsystem<UProject_JDungeonInstanceSubsystem>())
                {
                    Dungeon->RegisterDungeonDefinition(DungeonDefinition);
                }
            }
        }
        UProject_JMMOClientBridgeComponent::DispatchDungeonEntryForPlayer(
            PC, EProject_JDungeonEntrySource::WorldPortal, EffectiveDungeonId, bAutoTravelOnTicket);
        return;
    }
    if (auto* Preparation = PC ? PC->FindComponentByClass<UProject_JWorldStreamingComponent>() : nullptr)
        Preparation->RequestPreparedArrival(this, Interactor);
}
void AProject_JWorldPortal::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AProject_JWorldPortal, DestinationPlaceId);
    DOREPLIFETIME(AProject_JWorldPortal, DungeonId);
}
#if WITH_EDITOR
EDataValidationResult AProject_JWorldPortal::IsDataValid(FDataValidationContext& Context) const
{
    const EDataValidationResult Base = Super::IsDataValid(Context);
    bool bValid = (Base != EDataValidationResult::Invalid);
    const FName EffectiveDungeonId = !DungeonId.IsNone() ? DungeonId : (DungeonDefinition ? DungeonDefinition->DungeonId : NAME_None);
    if (!IsTemplate() && DestinationPlaceId.IsNone() && EffectiveDungeonId.IsNone())
    {
        Context.AddError(NSLOCTEXT("WorldPortal", "MissingDestination", "Portal instance requires either DestinationPlaceId, DungeonId, or DungeonDefinition."));
        bValid = false;
    }
    if (DungeonDefinition && DungeonDefinition->DungeonId.IsNone())
    {
        Context.AddError(NSLOCTEXT("WorldPortal", "InvalidDungeonDef", "Assigned DungeonDefinition has an empty DungeonId."));
        bValid = false;
    }
    return bValid ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif
