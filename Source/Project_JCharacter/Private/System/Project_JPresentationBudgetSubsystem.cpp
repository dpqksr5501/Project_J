#include "System/Project_JPresentationBudgetSubsystem.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Components/Project_JCombatPresentationComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/StrongObjectPtr.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

void UProject_JPresentationBudgetSubsystem::RefreshFrameBudget()
{
	if (Stats.Frame == GFrameCounter) { return; }
	Stats.Frame = GFrameCounter; Stats.FrameApplications = Stats.FrameVisits = 0; Stats.FrameQueueMilliseconds = 0;
}
bool UProject_JPresentationBudgetSubsystem::TryAcquireOneShot(UProject_JCombatPresentationComponent* Component)
{
	check(IsInGameThread()); RefreshFrameBudget();
	if (bStopped || !IsValid(Component) || !GetWorld() || GetWorld()->bIsTearingDown || Component->GetWorld() != GetWorld()
		|| !Component->CanAcceptPresentation() || !Requests.IsEmpty() || Stats.FrameApplications >= MaxApplications)
	{ ++Stats.ImmediateRejected; return false; }
	// Charge before Niagara callbacks. Cancellation/reentry cannot refund this frame.
	++Stats.FrameApplications; ++Stats.ImmediateApplications;
	Stats.PeakFrameApplications = FMath::Max(Stats.PeakFrameApplications,Stats.FrameApplications);
	return true;
}
bool UProject_JPresentationBudgetSubsystem::Request(UProject_JWeaponPresentationComponent* Component, uint64 Revision)
{ return RequestComponent(Component, Revision); }
bool UProject_JPresentationBudgetSubsystem::Request(UProject_JCombatPresentationComponent* Component, uint64 Revision)
{ return RequestComponent(Component, Revision); }
bool UProject_JPresentationBudgetSubsystem::RequestComponent(UActorComponent* Component, uint64 Revision)
{
	check(IsInGameThread());
	if (bStopped || !IsValid(Component) || Component->GetWorld() != GetWorld() || GetWorld()->bIsTearingDown) { return false; }
	const TWeakObjectPtr<UActorComponent> Key(Component);
	if (auto* Existing = Requests.Find(Key)) { Existing->Revision = Revision; ++Stats.Coalesced; return true; }
	if (Requests.Num() >= MaxPending || Order.Num() >= MaxPending * 2) { ++Stats.Rejected; return false; }
	const uint64 Admission = ++NextAdmission;
	Requests.Add(Key, {Revision, FPlatformTime::Seconds(), Admission}); Order.Add({Key, Admission}); Stats.Pending = Requests.Num(); return true;
}
void UProject_JPresentationBudgetSubsystem::Cancel(UProject_JWeaponPresentationComponent* Component)
{ CancelComponent(Component); }
void UProject_JPresentationBudgetSubsystem::Cancel(UProject_JCombatPresentationComponent* Component)
{ CancelComponent(Component); }
void UProject_JPresentationBudgetSubsystem::CancelComponent(UActorComponent* Component)
{ check(IsInGameThread()); Requests.Remove(Component); Stats.Pending = Requests.Num(); }
bool UProject_JPresentationBudgetSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{ return Type == EWorldType::Game || Type == EWorldType::PIE; }
bool UProject_JPresentationBudgetSubsystem::IsTickable() const
{ return !IsTemplate() && !bStopped && GetWorld() && !GetWorld()->bIsTearingDown && Head < Order.Num(); }
TStatId UProject_JPresentationBudgetSubsystem::GetStatId() const
{ RETURN_QUICK_DECLARE_CYCLE_STAT(ProjectJPresentationBudget, STATGROUP_Tickables); }
void UProject_JPresentationBudgetSubsystem::Tick(float)
{
	check(IsInGameThread()); if (!IsTickable() || bTicking) { return; }
	TStrongObjectPtr<ThisClass> PinnedThis(this);
	TGuardValue<bool> Guard(bTicking, true);
	RefreshFrameBudget();
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_PresentationBudget_Apply);
	const double Start = FPlatformTime::Seconds(); Stats.LastApplications = 0;
	for (; Head < Order.Num() && Stats.FrameVisits < 128 && !bStopped
		&& Stats.FrameApplications < MaxApplications && (Stats.FrameVisits == 0
			|| Stats.FrameQueueMilliseconds + (FPlatformTime::Seconds() - Start) * 1000 < 1.0);)
	{
		++Stats.FrameVisits;
		const auto Entry = Order[Head++]; const auto Key = Entry.Component; FRequest Request;
		const auto* Pending = Requests.Find(Key);
		// Cancellation followed by a new request cannot reclaim the cancelled FIFO slot.
		if (!Pending || Pending->Admission != Entry.Admission) { continue; }
		if (!Requests.RemoveAndCopyValue(Key, Request)) { continue; }
		if (auto* Component = Key.Get())
		{
			TStrongObjectPtr<UActorComponent> PinnedComponent(Component);
			TStrongObjectPtr<AActor> PinnedOwner(Component->GetOwner());
			Stats.MaxQueueMilliseconds = FMath::Max(Stats.MaxQueueMilliseconds, (FPlatformTime::Seconds() - Request.QueuedAt) * 1000);
			++Stats.LastApplications; ++Stats.Applied; ++Stats.FrameApplications;
			Stats.PeakFrameApplications = FMath::Max(Stats.PeakFrameApplications,Stats.FrameApplications);
			if (auto* Weapon = Cast<UProject_JWeaponPresentationComponent>(Component))
			{ Weapon->ApplyBudgetedPresentation(Request.Revision); }
			else if (auto* Cues = Cast<UProject_JCombatPresentationComponent>(Component))
			{ Cues->ApplyBudgetedReadyLoop(Request.Revision); }
		}
	}
	if (Head == Order.Num()) { Order.Reset(); Head = 0; }
	else if (Head >= 128 && Head * 2 >= Order.Num()) { Order.RemoveAt(0, Head, EAllowShrinking::No); Head = 0; }
	Stats.Pending = Requests.Num(); Stats.LastMilliseconds = (FPlatformTime::Seconds() - Start) * 1000;
	Stats.FrameQueueMilliseconds += Stats.LastMilliseconds;
}
void UProject_JPresentationBudgetSubsystem::OnWorldEndPlay(UWorld& World)
{ bStopped = true; Requests.Reset(); Order.Reset(); Head = 0; Stats.Pending = 0; Super::OnWorldEndPlay(World); }
void UProject_JPresentationBudgetSubsystem::Deinitialize()
{ bStopped = true; Requests.Reset(); Order.Reset(); Head = 0; Stats.Pending = 0; Super::Deinitialize(); }
