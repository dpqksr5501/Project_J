// Copyright Project J. All Rights Reserved.

#include "Game/Project_JProfilingCrowdComponent.h"

#include "AIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Project_J.h"
#include "Project_JPlayerCharacter.h"

namespace
{
	constexpr int32 MaxProfilingCrowdCount = 100;
	constexpr int32 MaxReplicatedMovementProfileCount = 50;
	constexpr float CrowdSpacing = 260.0f;
	constexpr float CrowdForwardOffset = 700.0f;
	constexpr float CrowdTravelFrequency = 0.75f;
	constexpr float ReplicatedMovementNetUpdateFrequency = 30.0f;
}

UProject_JProfilingCrowdComponent::UProject_JProfilingCrowdComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

bool UProject_JProfilingCrowdComponent::Start(
	TSubclassOf<AProject_JPlayerCharacter> CharacterClass,
	const FVector& Center,
	const FVector& Forward,
	int32 RequestedCount)
{
	return StartInternal(CharacterClass, Center, Forward, RequestedCount, false);
}

bool UProject_JProfilingCrowdComponent::StartReplicatedMovement(
	TSubclassOf<AProject_JPlayerCharacter> CharacterClass,
	const FVector& Center,
	const FVector& Forward,
	int32 RequestedCount)
{
	return StartInternal(CharacterClass, Center, Forward, RequestedCount, true);
}

bool UProject_JProfilingCrowdComponent::StartInternal(
	TSubclassOf<AProject_JPlayerCharacter> CharacterClass,
	const FVector& Center,
	const FVector& Forward,
	int32 RequestedCount,
	bool bReplicatedMovement)
{
	Stop();

	UWorld* World = GetWorld();
	if (!World || !CharacterClass || (!bReplicatedMovement && World->GetNetMode() == NM_DedicatedServer) || (bReplicatedMovement && World->GetNetMode() == NM_Client))
	{
		return false;
	}

	const int32 MaxCount = bReplicatedMovement ? MaxReplicatedMovementProfileCount : MaxProfilingCrowdCount;
	const int32 Count = FMath::Clamp(RequestedCount, 1, MaxCount);
	const int32 Columns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Count)));
	const FVector FlatForward = Forward.GetSafeNormal2D();
	const FVector SafeForward = FlatForward.IsNearlyZero() ? FVector::ForwardVector : FlatForward;
	const FVector Right = FVector::CrossProduct(FVector::UpVector, SafeForward);

	SpawnedCharacters.Reserve(Count);
	SpawnedControllers.Reserve(Count);
	TravelAxes.Reserve(Count);
	PhaseOffsets.Reserve(Count);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const int32 Row = Index / Columns;
		const int32 Column = Index % Columns;
		const float ColumnOffset = (static_cast<float>(Column) - (static_cast<float>(Columns - 1) * 0.5f)) * CrowdSpacing;
		const FVector SpawnLocation = Center + SafeForward * (CrowdForwardOffset + Row * CrowdSpacing) + Right * ColumnOffset;

		FActorSpawnParameters SpawnParameters;
		// Network-profile movers deliberately have no owning connection, so every
		// connected client receives a simulated proxy rather than an autonomous one.
		SpawnParameters.Owner = bReplicatedMovement ? nullptr : GetOwner();
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AProject_JPlayerCharacter* Character = World->SpawnActor<AProject_JPlayerCharacter>(
			CharacterClass,
			SpawnLocation,
			SafeForward.Rotation(),
			SpawnParameters);
		if (!Character)
		{
			continue;
		}

		if (bReplicatedMovement)
		{
			// This is intentionally a stable, bounded server-to-client movement load:
			// 50 authoritative movers, replicated at 30 Hz, with no combat/RPC/input
			// traffic mixed into the capture. Always-relevant is only for this harness;
			// AOI/relevancy is measured in its own gate.
			Character->SetReplicates(true);
			Character->SetReplicateMovement(true);
			Character->bAlwaysRelevant = true;
			Character->SetNetDormancy(DORM_Awake);
			Character->SetNetUpdateFrequency(ReplicatedMovementNetUpdateFrequency);
			Character->SetMinNetUpdateFrequency(ReplicatedMovementNetUpdateFrequency);
		}
		else
		{
			// The visual harness must neither create network traffic nor claim to be a
			// simulated proxy. Client PIE does not reliably advance CMC input for an
			// unpossessed local actor, so use a non-ticking controller.
			Character->SetReplicates(false);
			Character->SetReplicateMovement(false);
		}
		// Keep world-floor/static collision, but prevent profiling clones from
		// pushing each other into corrective CMC movement.
		Character->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		if (UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement())
		{
			MovementComponent->bRunPhysicsWithNoController = true;
		}

		AAIController* Controller = World->SpawnActor<AAIController>(AAIController::StaticClass(), SpawnLocation, SafeForward.Rotation(), SpawnParameters);
		if (!Controller)
		{
			Character->Destroy();
			continue;
		}
		Controller->SetActorTickEnabled(false);
		Controller->Possess(Character);

		SpawnedCharacters.Add(Character);
		SpawnedControllers.Add(Controller);
		TravelAxes.Add((Index & 1) == 0 ? SafeForward : Right);
		PhaseOffsets.Add(static_cast<float>(Index) * 0.47f);
	}

	ElapsedSeconds = 0.0f;
	bMovementHealthReported = false;
	bReplicatedMovementProfile = bReplicatedMovement;
	SetComponentTickEnabled(IsRunning());
	UE_LOG(
		LogProject_J,
		Display,
		TEXT("ProfilingCrowd started Requested=%d Spawned=%d Replicated=%s Mode=%s"),
		RequestedCount,
		SpawnedCharacters.Num(),
		bReplicatedMovement ? TEXT("true") : TEXT("false"),
		bReplicatedMovement ? TEXT("ServerToClientMovementOnly") : TEXT("VisualCpuOnly"));
	return IsRunning();
}

void UProject_JProfilingCrowdComponent::Stop()
{
	SetComponentTickEnabled(false);
	for (AAIController* Controller : SpawnedControllers)
	{
		if (IsValid(Controller))
		{
			Controller->UnPossess();
			Controller->Destroy();
		}
	}
	for (AProject_JPlayerCharacter* Character : SpawnedCharacters)
	{
		if (IsValid(Character))
		{
			Character->Destroy();
		}
	}
	SpawnedCharacters.Reset();
	SpawnedControllers.Reset();
	TravelAxes.Reset();
	PhaseOffsets.Reset();
	ElapsedSeconds = 0.0f;
	bMovementHealthReported = false;
	bReplicatedMovementProfile = false;
}

int32 UProject_JProfilingCrowdComponent::GetMovingCharacterCount() const
{
	int32 MovingCount = 0;
	for (const AProject_JPlayerCharacter* Character : SpawnedCharacters)
	{
		if (IsValid(Character) && Character->GetVelocity().SizeSquared2D() > FMath::Square(5.0f))
		{
			++MovingCount;
		}
	}
	return MovingCount;
}

void UProject_JProfilingCrowdComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Project_J_ProfilingCrowdTick);
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ElapsedSeconds += DeltaTime;
	for (int32 Index = SpawnedCharacters.Num() - 1; Index >= 0; --Index)
	{
		AProject_JPlayerCharacter* Character = SpawnedCharacters[Index];
		if (!IsValid(Character))
		{
			SpawnedCharacters.RemoveAtSwap(Index);
			TravelAxes.RemoveAtSwap(Index);
			PhaseOffsets.RemoveAtSwap(Index);
			continue;
		}

		// Do not chase a slow, moving point: that makes a high-speed CMC overshoot
		// and reverse every frame around the point. Maintain a stable input vector
		// and reverse once per half-cycle instead, which exercises start/stop/pivot
		// transitions without artificial jitter.
		const float Phase = (ElapsedSeconds * CrowdTravelFrequency) + PhaseOffsets[Index];
		const float DirectionSign = FMath::Cos(Phase) >= 0.0f ? 1.0f : -1.0f;
		Character->AddMovementInput(TravelAxes[Index] * DirectionSign, 1.0f, true);
	}

	if (SpawnedCharacters.IsEmpty())
	{
		SetComponentTickEnabled(false);
	}
	else if (!bMovementHealthReported && ElapsedSeconds >= 2.0f)
	{
		bMovementHealthReported = true;
		UE_LOG(
			LogProject_J,
			Display,
			TEXT("ProfilingCrowd movement health Moving=%d Spawned=%d Replicated=%s"),
			GetMovingCharacterCount(),
			SpawnedCharacters.Num(),
			bReplicatedMovementProfile ? TEXT("true") : TEXT("false"));
	}
}

void UProject_JProfilingCrowdComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Stop();
	Super::EndPlay(EndPlayReason);
}
