#include "Optimization/Project_JNPCDecisionExperiment.h"
#include "Project_JNPCCharacter.h"
#include "Project_JAttributeSet.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "Components/SceneComponent.h"
#include "System/Project_JNPCDecisionSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

AProject_JNPCDecisionExperiment::AProject_JNPCDecisionExperiment()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("ExperimentRoot")));
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickInterval = 0.25f;
	bReplicates = false;
}

bool AProject_JNPCDecisionExperiment::StartExperiment(int32 NPCCount, int32 TargetCount)
{
	check(IsInGameThread());
	UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown || IsActorBeingDestroyed() || (World->WorldType != EWorldType::Game && World->WorldType != EWorldType::PIE)
		|| World->GetNetMode() == NM_Client || !HasAuthority() || !SpawnedActors.IsEmpty()
		|| NPCCount < 1 || NPCCount > 256 || TargetCount < 1 || TargetCount > 128) { return false; }
	auto* Scheduler = World->GetSubsystem<UProject_JNPCDecisionSubsystem>();
	if (!Scheduler) { return false; }
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 Index = 0; Index < TargetCount; ++Index)
	{
		AActor* Target = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParameters);
		if (!Target) { StopExperiment(); return false; }
		SpawnedActors.Add(Target);
		auto* Root = NewObject<USceneComponent>(Target);
		Target->SetRootComponent(Root);
		Root->RegisterComponent();
		Target->SetActorLocation(GetActorLocation() + FVector(1000 + Index % 16 * 60, Index / 16 * 60, 100));
		if (!Scheduler->RegisterTarget(Target, 2)) { StopExperiment(); return false; }
		RegisteredTargets.Add(Target);
	}
	for (int32 Index = 0; Index < NPCCount; ++Index)
	{
		auto* NPC = World->SpawnActor<AProject_JNPCCharacter>(AProject_JNPCCharacter::StaticClass(),
			GetActorLocation() + FVector(Index % 16 * 40, Index / 16 * 40, 100), FRotator::ZeroRotator, SpawnParameters);
		if (!NPC) { StopExperiment(); return false; }
		SpawnedActors.Add(NPC);
		// Native sandbox requires no class/attribute Data Asset. This modifies only actors spawned here.
		NPC->GetAttributeSet()->InitMaxHealth(100.0f);
		NPC->GetAttributeSet()->InitHealth(100.0f);
		auto* Component = NewObject<UProject_JTargetScoringComponent>(NPC);
		NPC->AddInstanceComponent(Component);
		Component->RegisterComponent();
		if (!Component->StartBatchedNPCDecisions(1)) { StopExperiment(); return false; }
	}
	SetActorTickEnabled(true);
	return true;
}

void AProject_JNPCDecisionExperiment::StopExperiment()
{
	check(IsInGameThread());
	SetActorTickEnabled(false);
	if (auto* Scheduler = GetWorld() ? GetWorld()->GetSubsystem<UProject_JNPCDecisionSubsystem>() : nullptr)
	{
		for (const auto& Target : RegisteredTargets) { if (Target.IsValid()) { Scheduler->UnregisterTarget(Target.Get()); } }
	}
	RegisteredTargets.Empty();
	for (const auto& Actor : SpawnedActors)
	{
		if (AActor* Instance = Actor.Get())
		{
			if (auto* Component = Instance->FindComponentByClass<UProject_JTargetScoringComponent>()) { Component->StopBatchedNPCDecisions(); }
			Instance->Destroy();
		}
	}
	SpawnedActors.Empty();
}

FString AProject_JNPCDecisionExperiment::GetSummary() const
{
	const auto* Scheduler = GetWorld() ? GetWorld()->GetSubsystem<UProject_JNPCDecisionSubsystem>() : nullptr;
	if (!Scheduler) { return TEXT("NPC decision scheduler unavailable"); }
	const auto& Stats = Scheduler->GetStats();
	return FString::Printf(TEXT("NPCDecision agents=%d targets=%d outstanding=%d batches=%llu decisions=%llu applied=%llu discarded=%llu rejected=%llu last_gt_ms=%.3f"),
		Scheduler->GetAgentCount(), Scheduler->GetTargetCount(), Scheduler->GetOutstandingCount(), Stats.SubmittedBatches,
		Stats.SubmittedDecisions, Stats.AppliedDecisions, Stats.DiscardedDecisions, Stats.RejectedBatches, Stats.LastTickGameThreadMilliseconds);
}

void AProject_JNPCDecisionExperiment::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	for (const auto& Actor : SpawnedActors)
	{
		AActor* Instance = Actor.Get();
		if (!Instance) { continue; }
		auto* Component = Instance->FindComponentByClass<UProject_JTargetScoringComponent>();
		DrawDebugSphere(GetWorld(), Instance->GetActorLocation(), 20, 8, Component ? FColor::Cyan : FColor::Orange, false, 0.3f);
		if (Component && Component->GetLastScoredTarget())
		{
			DrawDebugLine(GetWorld(), Instance->GetActorLocation(), Component->GetLastScoredTarget()->GetActorLocation(), FColor::Green, false, 0.3f);
		}
	}
}

void AProject_JNPCDecisionExperiment::EndPlay(const EEndPlayReason::Type Reason) { StopExperiment(); Super::EndPlay(Reason); }

#if !UE_BUILD_SHIPPING
namespace
{
	FAutoConsoleCommandWithWorldAndArgs StartNPCDecisionExperiment(TEXT("ProjectJ.NPCDecision.Start"),
		TEXT("Start a native server/standalone sandbox: <NPC count 1..256> <target count 1..128>. No assets are saved."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World || World->GetNetMode() == NM_Client || (World->WorldType != EWorldType::Game && World->WorldType != EWorldType::PIE)) { return; }
			for (TActorIterator<AProject_JNPCDecisionExperiment> It(World); It; ++It) { UE_LOG(LogTemp, Display, TEXT("%s"), *It->GetSummary()); return; }
			auto* Experiment = World->SpawnActor<AProject_JNPCDecisionExperiment>();
			if (!Experiment) { return; }
			if (!Experiment->StartExperiment(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 64, Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 32))
			{
				Experiment->Destroy();
				UE_LOG(LogTemp, Warning, TEXT("NPC decision experiment rejected its arguments or registry capacity."));
			}
		}));
	FAutoConsoleCommandWithWorldAndArgs ControlNPCDecisionExperiment(TEXT("ProjectJ.NPCDecision.Control"),
		TEXT("Native sandbox: status | nodraw | draw | stop. Disable debug drawing during performance capture."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World) { return; }
			for (TActorIterator<AProject_JNPCDecisionExperiment> It(World); It; ++It)
			{
				if (Args.Num() && Args[0] == TEXT("stop")) { It->StopExperiment(); It->Destroy(); }
				else if (Args.Num() && Args[0] == TEXT("nodraw")) { It->SetActorTickEnabled(false); }
				else if (Args.Num() && Args[0] == TEXT("draw")) { It->SetActorTickEnabled(true); }
				else { UE_LOG(LogTemp, Display, TEXT("%s"), *It->GetSummary()); }
			}
		}));
}
#endif
