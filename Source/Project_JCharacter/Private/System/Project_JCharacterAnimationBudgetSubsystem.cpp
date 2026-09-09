#include "System/Project_JCharacterAnimationBudgetSubsystem.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "Project_JBaseCharacter.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "IAnimationBudgetAllocator.h"
#include "UObject/UObjectIterator.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJCharacterABA, Log, All);

namespace
{
TAutoConsoleVariable<int32> CVarCharacterABA(TEXT("ProjectJ.AnimationBudget.Enabled"), 1,
	TEXT("Use ABA for eligible non-local, non-combat Character meshes. Requires a.Budget.Enabled=1. 0 restores normal mesh ticking."));
FAutoConsoleCommandWithWorld StatusCommand(TEXT("ProjectJ.AnimationBudget.Status"), TEXT("Report current world's Character ABA ownership."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		const auto* Service = World ? World->GetSubsystem<UProject_JCharacterAnimationBudgetSubsystem>() : nullptr;
		if (Service)
		{
			UE_LOG(LogProjectJCharacterABA, Display, TEXT("World=%s NetMode=%d Enabled=%d Tracked=%d Managed=%d"),
				*World->GetName(), int32(World->GetNetMode()), Service->IsEnabledForWorld(), Service->GetTrackedCount(), Service->GetManagedCount());
		}
	}));
}
TRACE_DECLARE_INT_COUNTER(CharacterABAManaged, TEXT("ProjectJ/CharacterABA/Managed"));
TRACE_DECLARE_INT_COUNTER(CharacterABAProtected, TEXT("ProjectJ/CharacterABA/CombatProtected"));

bool UProject_JCharacterAnimationBudgetSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{ return Type == EWorldType::Game || Type == EWorldType::PIE; }
void UProject_JCharacterAnimationBudgetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TickHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(this, &ThisClass::OnPostActorTick);
	CleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &ThisClass::OnWorldCleanup);
}
bool UProject_JCharacterAnimationBudgetSubsystem::IsEnabledForWorld() const
{
	const auto* EngineEnabled = IConsoleManager::Get().FindConsoleVariable(TEXT("a.Budget.Enabled"));
	return !bEnding && GetWorld() && !GetWorld()->bIsTearingDown && GetWorld()->GetNetMode() != NM_DedicatedServer
		&& EnabledOverride.Get(CVarCharacterABA.GetValueOnGameThread() != 0) && EngineEnabled && EngineEnabled->GetInt() == 1;
}
void UProject_JCharacterAnimationBudgetSubsystem::SetEnabledOverride(TOptional<bool> Enabled)
{ EnabledOverride = Enabled; Refresh(); }
void UProject_JCharacterAnimationBudgetSubsystem::RegisterMesh(UProject_JBudgetedSkeletalMeshComponent* Mesh)
{
	check(IsInGameThread());
	if (bEnding || !IsValid(Mesh) || Mesh->GetWorld() != GetWorld() || Meshes.Contains(Mesh)) { return; }
	Meshes.RemoveAllSwap([](const auto& Entry) { return !Entry.IsValid(); });
	if (Meshes.Num() < MaxMeshes) { Meshes.Add(Mesh); }
}
void UProject_JCharacterAnimationBudgetSubsystem::UnregisterMesh(UProject_JBudgetedSkeletalMeshComponent* Mesh)
{
	if (Mesh) { Mesh->LeaveBudget(); }
	Meshes.RemoveAllSwap([Mesh](const auto& Entry) { return !Entry.IsValid() || Entry.Get() == Mesh; });
}
int32 UProject_JCharacterAnimationBudgetSubsystem::GetManagedCount() const
{
	int32 Count = 0;
	for (const auto& Entry : Meshes) { Count += Entry.IsValid() && Entry->IsManagedByBudget(); }
	return Count;
}
void UProject_JCharacterAnimationBudgetSubsystem::OnPostActorTick(UWorld* World, ELevelTick, float)
{ if (World == GetWorld()) { Refresh(); } }
void UProject_JCharacterAnimationBudgetSubsystem::Refresh()
{
	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_CharacterABA_Policy);
	const bool bEnabled = IsEnabledForWorld();
	int32 Managed = 0, Protected = 0;
	for (int32 Index = Meshes.Num() - 1; Index >= 0; --Index)
	{
		auto* Mesh = Meshes[Index].Get();
		if (!Mesh) { Meshes.RemoveAtSwap(Index); continue; }
		Protected += Mesh->IsCombatCritical();
		if (!bEnabled || !Mesh->CanUseBudget()) { Mesh->LeaveBudget(); continue; }
		auto* Allocator = IAnimationBudgetAllocator::Get(GetWorld());
		if (!Allocator) { continue; }
		if (!Allocator->GetEnabled()) { Allocator->SetEnabled(true); bOwnAllocatorEnable = true; }
		Mesh->EnterBudget();
		if (Mesh->IsManagedByBudget())
		{
			const auto* Character = Cast<AProject_JBaseCharacter>(Mesh->GetOwner());
			const float Tier = Character ? FMath::Max(0.0f, Character->GetSignificance()) : 0.0f;
			Allocator->SetComponentSignificance(Mesh, 1.0f / (1.0f + Tier), false, Mesh->bBudgetTickWhenNotRendered, false, false);
			++Managed;
		}
	}
	// Counters describe the last processed world; PIE worlds are distinguished by CPU scope context.
	TRACE_COUNTER_SET(CharacterABAManaged, Managed);
	TRACE_COUNTER_SET(CharacterABAProtected, Protected);
	if (!bEnabled) { RestoreAllocatorIfUnused(); }
}
void UProject_JCharacterAnimationBudgetSubsystem::RestoreAllocatorIfUnused()
{
	if (!bOwnAllocatorEnable || GetManagedCount() != 0) { return; }
	// Conservative ownership check only on disable/teardown, never the per-frame path.
	// Unknown budgeted components may be owned by another feature; do not disable it.
	for (TObjectIterator<USkeletalMeshComponentBudgeted> It; It; ++It)
	{
		if (It->IsTemplate() || !It->IsRegistered() || It->GetWorld() != GetWorld()) { continue; }
		const auto* ProjectMesh = Cast<UProject_JBudgetedSkeletalMeshComponent>(*It);
		if (!ProjectMesh || ProjectMesh->IsManagedByBudget()) { bOwnAllocatorEnable = false; return; }
	}
	if (auto* Allocator = IAnimationBudgetAllocator::Get(GetWorld())) { Allocator->SetEnabled(false); }
	bOwnAllocatorEnable = false;
}
void UProject_JCharacterAnimationBudgetSubsystem::OnWorldCleanup(UWorld* World, bool, bool)
{
	if (World != GetWorld() || bEnding) { return; }
	bEnding = true;
	for (const auto& Entry : Meshes) { if (Entry.IsValid()) { Entry->LeaveBudget(); } }
	RestoreAllocatorIfUnused();
	Meshes.Reset();
}
void UProject_JCharacterAnimationBudgetSubsystem::Deinitialize()
{
	OnWorldCleanup(GetWorld(), false, false);
	FWorldDelegates::OnWorldPostActorTick.Remove(TickHandle);
	FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);
	Super::Deinitialize();
}
