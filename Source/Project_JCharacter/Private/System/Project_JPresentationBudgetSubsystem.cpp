#include "System/Project_JPresentationBudgetSubsystem.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Engine/World.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

bool UProject_JPresentationBudgetSubsystem::Request(UProject_JWeaponPresentationComponent* Component, uint64 Revision)
{
	check(IsInGameThread());
	if (bStopped || !IsValid(Component) || Component->GetWorld() != GetWorld() || GetWorld()->bIsTearingDown) { return false; }
	const TWeakObjectPtr<UProject_JWeaponPresentationComponent> Key(Component);
	if (auto* Existing = Requests.Find(Key)) { Existing->Revision = Revision; ++Stats.Coalesced; return true; }
	if (Requests.Num() >= MaxPending || Order.Num() >= MaxPending * 2) { ++Stats.Rejected; return false; }
	const uint64 Admission = ++NextAdmission;
	Requests.Add(Key, {Revision, FPlatformTime::Seconds(), Admission}); Order.Add({Key, Admission}); Stats.Pending = Requests.Num(); return true;
}
void UProject_JPresentationBudgetSubsystem::Cancel(UProject_JWeaponPresentationComponent* Component)
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
	TGuardValue<bool> Guard(bTicking, true);
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_PresentationBudget_Apply);
	const double Start = FPlatformTime::Seconds(); Stats.LastApplications = 0;
	for (int32 Visits = 0; Head < Order.Num() && Visits < 128 && !bStopped
		&& Stats.LastApplications < MaxApplications && (FPlatformTime::Seconds() - Start) * 1000 < 1.0; ++Visits)
	{
		const auto Entry = Order[Head++]; const auto Key = Entry.Component; FRequest Request;
		const auto* Pending = Requests.Find(Key);
		// Cancellation followed by a new request cannot reclaim the cancelled FIFO slot.
		if (!Pending || Pending->Admission != Entry.Admission) { continue; }
		if (!Requests.RemoveAndCopyValue(Key, Request)) { continue; }
		if (auto* Component = Key.Get())
		{
			Stats.MaxQueueMilliseconds = FMath::Max(Stats.MaxQueueMilliseconds, (FPlatformTime::Seconds() - Request.QueuedAt) * 1000);
			++Stats.LastApplications; ++Stats.Applied;
			Component->ApplyBudgetedPresentation(Request.Revision);
		}
	}
	if (Head == Order.Num()) { Order.Reset(); Head = 0; }
	else if (Head >= 128 && Head * 2 >= Order.Num()) { Order.RemoveAt(0, Head, EAllowShrinking::No); Head = 0; }
	Stats.Pending = Requests.Num(); Stats.LastMilliseconds = (FPlatformTime::Seconds() - Start) * 1000;
}
void UProject_JPresentationBudgetSubsystem::OnWorldEndPlay(UWorld& World)
{ bStopped = true; Requests.Reset(); Order.Reset(); Head = 0; Stats.Pending = 0; Super::OnWorldEndPlay(World); }
void UProject_JPresentationBudgetSubsystem::Deinitialize()
{ bStopped = true; Requests.Reset(); Order.Reset(); Head = 0; Stats.Pending = 0; Super::Deinitialize(); }
