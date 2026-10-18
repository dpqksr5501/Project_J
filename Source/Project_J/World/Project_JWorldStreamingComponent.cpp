#include "World/Project_JWorldStreamingComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "World/Project_JWorldState.h"
#include "EngineUtils.h"

UProject_JWorldStreamingComponent::UProject_JWorldStreamingComponent()
{
    TargetState = EStreamingSourceTargetState::Activated;
    Priority = EStreamingSourcePriority::Low;
}
void UProject_JWorldStreamingComponent::BeginPlay()
{
    Super::BeginPlay();
    for (TActorIterator<AProject_JWorldState> It(GetWorld()); It; ++It)
    {
        if (const UProject_JWorldDefinition* Definition = It->Definition)
        {
            PredictionSeconds = Definition->PredictionSeconds;
            PreparationRadius = Definition->PreparationRadius;
            MaxPredictionDistance = Definition->MaxPredictionDistance;
            bHasWorldDefinition = true;
            break;
        }
    }
}
FVector UProject_JWorldStreamingComponent::PredictOffset(const FVector& Velocity, float Seconds, float Maximum)
{
    if (Velocity.ContainsNaN() || !FMath::IsFinite(Seconds) || !FMath::IsFinite(Maximum)) { return FVector::ZeroVector; }
    return (Velocity*FMath::Clamp(Seconds, 0.f, 10.f)).GetClampedToMaxSize(FMath::Clamp(Maximum, 0.f, 200000.f));
}
bool UProject_JWorldStreamingComponent::GetStreamingSource(FWorldPartitionStreamingSource& Out) const
{
    const APlayerController* PC = Cast<APlayerController>(GetOwner());
    const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
    if (!bHasWorldDefinition || !PC || !PC->IsLocalController() || !Pawn || GetNetMode() == NM_DedicatedServer || !IsStreamingSourceEnabled()) { return false; }
    if (!Super::GetStreamingSource(Out)) { return false; }
    Out.Location = Pawn->GetActorLocation();
    Out.Rotation = FRotator::ZeroRotator;
    Out.Shapes.Reset(2);
    FStreamingSourceShape Shape;
    Shape.bUseGridLoadingRange = false;
    Shape.Radius = FMath::Clamp(PreparationRadius, 1000.f, 200000.f);
    Out.Shapes.Add(Shape);
    Shape.Location = PredictOffset(Pawn->GetVelocity(), PredictionSeconds, MaxPredictionDistance);
    Out.Shapes.Add(Shape);
    return true;
}
