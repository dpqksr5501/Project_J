#include "World/Project_JWorldPortal.h"
#include "World/Project_JWorldStreamingComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/Project_JInteractionTargetComponent.h"
#include "Net/UnrealNetwork.h"
#include "UObject/StrongObjectPtr.h"

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
bool AProject_JWorldPortal::IsSpatiallyAvailable(const APawn* Interactor) const
{
    if (!IsValid(Interactor) || Interactor->IsActorBeingDestroyed() || IsActorBeingDestroyed() || DestinationPlaceId.IsNone()
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
    auto* PC = Cast<APlayerController>(Interactor->GetController());
    if (auto* Preparation = PC ? PC->FindComponentByClass<UProject_JWorldStreamingComponent>() : nullptr)
        Preparation->RequestPreparedArrival(this, Interactor);
}
void AProject_JWorldPortal::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AProject_JWorldPortal, DestinationPlaceId);
}
