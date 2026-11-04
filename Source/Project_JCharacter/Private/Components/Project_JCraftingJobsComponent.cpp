#include "Components/Project_JCraftingJobsComponent.h"
#include "Components/Project_JCommandBudgetComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/Timespan.h"
#include "Net/UnrealNetwork.h"
#include "UObject/StrongObjectPtr.h"

UProject_JCraftingJobsComponent::UProject_JCraftingJobsComponent()
{ PrimaryComponentTick.bCanEverTick = false; SetIsReplicatedByDefault(true); }
bool UProject_JCraftingJobsComponent::CanPublish() const
{ return !bEnding && !IsBeingDestroyed() && IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed() && GetWorld() && !GetWorld()->bIsTearingDown; }
TArray<ProjectJ::Crafting::FJobValues> UProject_JCraftingJobsComponent::CaptureJobs() const
{ return CanPublish() && GetOwner()->HasAuthority() && OwnerIdentity.IsValid() && InstalledIdentity == OwnerIdentity && InstalledGeneration == OwnerScope.Generation ? Jobs : TArray<ProjectJ::Crafting::FJobValues>(); }
bool UProject_JCraftingJobsComponent::ValidateCommittedJobs(const TArray<ProjectJ::Crafting::FJobValues>& Values) const
{
 const TArray<FProject_JPersistentItem> NoLiveItems;
 return CanPublish() && GetOwner()->HasAuthority() && ProjectJ::Crafting::ValidateJobState(NoLiveItems, Values);
}
void UProject_JCraftingJobsComponent::InstallCommittedJobs(const TArray<ProjectJ::Crafting::FJobValues>& Values, bool bBroadcast)
{
 check(IsInGameThread());
 if (!CanPublish() || !GetOwner()->HasAuthority() || !OwnerIdentity.IsValid() || !OwnerScope.Generation) return;
 if (Jobs == Values && InstalledIdentity == OwnerIdentity && InstalledGeneration == OwnerScope.Generation && SummaryWorld.Get() == GetWorld())
 { if (bBroadcast) BroadcastCommittedJobs(); return; }
 if (!ValidateCommittedJobs(Values)) return;
 TArray<FProject_JCraftingJobSummary> Rows;
 Rows.Reserve(Values.Num());
 const int64 Now = FDateTime::UtcNow().GetTicks();
 for (const auto& Job : Values)
 {
  auto& Row = Rows.AddDefaulted_GetRef();
  Row.OwnerCharacterId = OwnerIdentity; Row.OwnerGeneration = OwnerScope.Generation; Row.JobId = Job.Id; Row.RecipeId = Job.RecipeId; Row.RecipeVersion = Job.RecipeVersion; Row.Batches = Job.Batches;
  Row.RemainingSecondsAtPublish = FMath::Max(0.0, double(Job.DueUtcTicks - Now) / double(ETimespan::TicksPerSecond));
 }
 const bool SummaryChanged = Rows != Summaries;
 const bool JobsChanged = Jobs != Values || InstalledIdentity != OwnerIdentity || InstalledGeneration != OwnerScope.Generation;
 Jobs = Values; InstalledIdentity = OwnerIdentity; InstalledGeneration = OwnerScope.Generation; SummaryWorld = GetWorld(); Summaries = MoveTemp(Rows);
 bPendingNotification |= JobsChanged || SummaryChanged;
 if (SummaryChanged) GetOwner()->ForceNetUpdate();
 if (bBroadcast) BroadcastCommittedJobs();
}
void UProject_JCraftingJobsComponent::BroadcastCommittedJobs()
{
 if (!CanPublish() || !bPendingNotification) return;
 bPendingNotification = false;
 const TStrongObjectPtr<ThisClass> KeepSelf(this);
 const TStrongObjectPtr<AActor> KeepOwner(GetOwner());
 OnChanged.Broadcast();
}
void UProject_JCraftingJobsComponent::OnRep_Jobs()
{ if (CanPublish() && Summaries.Num() <= ProjectJ::Crafting::MaxJobs) { ReceivedAtSeconds = FPlatformTime::Seconds(); bPendingNotification = true; BroadcastCommittedJobs(); } }
TArray<FProject_JCraftingJobSummary> UProject_JCraftingJobsComponent::GetJobs() const
{
 TArray<FProject_JCraftingJobSummary> Rows;
 if (!CanPublish() || !GetPresentationGeneration() || Summaries.Num() > ProjectJ::Crafting::MaxJobs) return Rows;
 for (const auto& Row : Summaries) if (Row.OwnerCharacterId == OwnerIdentity && Row.OwnerGeneration == OwnerScope.Generation) Rows.Add(Row);
 return Rows;
}
double UProject_JCraftingJobsComponent::GetRemainingSeconds(FGuid JobId) const
{
 if (!GetPresentationGeneration() || !JobId.IsValid()) return -1;
 if (GetOwner()->HasAuthority())
 {
  const auto* Job = FindJob(JobId);
  return Job ? FMath::Max(0.0, double(Job->DueUtcTicks - FDateTime::UtcNow().GetTicks()) / double(ETimespan::TicksPerSecond)) : -1;
 }
 const double Now = FPlatformTime::Seconds();
 if (Summaries.Num() > ProjectJ::Crafting::MaxJobs || ReceivedAtSeconds < 0 || !FMath::IsFinite(ReceivedAtSeconds) || !FMath::IsFinite(Now) || Now < ReceivedAtSeconds) return -1;
 const auto* Row = Summaries.FindByPredicate([&](const auto& Value) { return Value.JobId == JobId && Value.OwnerCharacterId == OwnerIdentity && Value.OwnerGeneration == OwnerScope.Generation; });
 if (!Row || !FMath::IsFinite(Row->RemainingSecondsAtPublish) || Row->RemainingSecondsAtPublish < 0) return -1;
 return FMath::Max(0.0, Row->RemainingSecondsAtPublish - (Now - ReceivedAtSeconds));
}
const ProjectJ::Crafting::FJobValues* UProject_JCraftingJobsComponent::FindJob(const FGuid& JobId) const
{ return CanPublish() && GetOwner()->HasAuthority() && OwnerIdentity.IsValid() && InstalledIdentity == OwnerIdentity && InstalledGeneration == OwnerScope.Generation ? Jobs.FindByPredicate([&](const auto& Value) { return Value.Id == JobId; }) : nullptr; }
FGuid UProject_JCraftingJobsComponent::FindRecipeJob(FName RecipeId, int32 Version) const
{
 if (CanPublish() && GetOwner()->HasAuthority() && OwnerIdentity.IsValid() && InstalledIdentity == OwnerIdentity && InstalledGeneration == OwnerScope.Generation)
  for (const auto& Job : Jobs) if (Job.RecipeId == RecipeId && Job.RecipeVersion == Version) return Job.Id;
 return {};
}
bool UProject_JCraftingJobsComponent::IsJobDue(const FGuid& JobId) const
{ const auto* Job = FindJob(JobId); return Job && FDateTime::UtcNow().GetTicks() >= Job->DueUtcTicks; }
bool UProject_JCraftingJobsComponent::SubmitJob(EProject_JPersistentEdit Kind, FGuid JobId, FProject_JPersistentCompletion Complete)
{
 check(IsInGameThread());
 if (!CanPublish() || !GetOwner()->HasAuthority() || !JobId.IsValid() || !FindJob(JobId) || !SubmitPersistentEdit || !CreatePersistentRequest || !PersistentAdmission) return false;
 const TStrongObjectPtr<ThisClass> KeepSelf(this);
 const TStrongObjectPtr<AActor> KeepOwner(GetOwner());
 const auto Submit = SubmitPersistentEdit; const auto Create = CreatePersistentRequest; const auto Admit = PersistentAdmission;
 const FGuid Identity = OwnerIdentity; const uint64 Generation = OwnerScope.Generation;
 const auto IsCurrent = [&] { return CanPublish() && GetOwner() == KeepOwner.Get() && OwnerIdentity == Identity &&
  OwnerScope.Generation == Generation && InstalledIdentity == Identity && InstalledGeneration == Generation; };
 if (!Admit() || !IsCurrent()) return false;
 FProject_JPersistentEdit Edit; Edit.Kind = Kind; Edit.CraftingJob = JobId; Edit.Request = Create();
 if (!Edit.Request.IsValid() || !IsCurrent() || !Admit() || !IsCurrent() || !FindJob(JobId)) return false;
 return Submit(Edit, MoveTemp(Complete));
}
bool UProject_JCraftingJobsComponent::TryCollectJob(FGuid JobId, FProject_JPersistentCompletion Complete)
{ if (!IsJobDue(JobId)) return false; return SubmitJob(EProject_JPersistentEdit::CollectCraftJob, JobId, MoveTemp(Complete)); }
bool UProject_JCraftingJobsComponent::TryCancelJob(FGuid JobId, FProject_JPersistentCompletion Complete)
{ return SubmitJob(EProject_JPersistentEdit::CancelCraftJob, JobId, MoveTemp(Complete)); }
void UProject_JCraftingJobsComponent::NotifyOwnerIdentity(FGuid CharacterId)
{
 check(IsInGameThread());
 // PS identity and this component can arrive independently. Never derive a client scope from PS delivery order.
 if (!CanPublish() || !GetOwner()->HasAuthority() || OwnerIdentity == CharacterId) return;
 const TStrongObjectPtr<ThisClass> KeepSelf(this);
 const TStrongObjectPtr<AActor> KeepOwner(GetOwner());
 if (OwnerScope.Generation == MAX_uint64) { Stop(); return; }
 ++OwnerScope.Generation; OwnerScope.CharacterId = CharacterId; InstalledGeneration = 0; InstalledIdentity.Invalidate();
 const uint64 Generation = OwnerScope.Generation;
 // A new identity requires a freshly admitted profile binding, even after A-B-A.
 SubmitPersistentEdit = {}; CreatePersistentRequest = {}; PersistentAdmission = {};
 OwnerIdentity = CharacterId; CurrentRequest = {}; RequestIdentity.Invalidate(); RequestGeneration = 0; RequestStartedAt = -1;
 GetOwner()->ForceNetUpdate();
 // Preserve durable escrow values; retire their old presentation and input scope.
 OnChanged.Broadcast();
 if (CanPublish() && GetOwner() == KeepOwner.Get() && OwnerIdentity == CharacterId && OwnerScope.Generation == Generation) OnRequestChanged.Broadcast();
}
void UProject_JCraftingJobsComponent::OnRep_OwnerScope()
{
 if (!CanPublish() || GetOwner()->HasAuthority() || (ObservedGeneration == OwnerScope.Generation && OwnerIdentity == OwnerScope.CharacterId)) return;
 const TStrongObjectPtr<ThisClass> KeepSelf(this);
 const TStrongObjectPtr<AActor> KeepOwner(GetOwner());
 const uint64 Generation = OwnerScope.Generation; ObservedGeneration = Generation; OwnerIdentity = OwnerScope.CharacterId;
 CurrentRequest = {}; RequestIdentity.Invalidate(); RequestGeneration = 0; RequestStartedAt = -1;
 OnChanged.Broadcast();
 if (CanPublish() && GetOwner() == KeepOwner.Get() && OwnerScope.Generation == Generation) OnRequestChanged.Broadcast();
}
FProject_JCraftingRequestState UProject_JCraftingJobsComponent::GetRequestState() const
{
 if (!CanPublish() || !GetPresentationGeneration() || RequestIdentity != OwnerIdentity || RequestGeneration != OwnerScope.Generation) return {};
 auto State = CurrentRequest;
 if (State.Stage == EProject_JCraftingRequestStage::Pending)
 {
  const double Now = FPlatformTime::Seconds();
  if (RequestStartedAt < 0 || !FMath::IsFinite(Now) || Now < RequestStartedAt || Now - RequestStartedAt >= RequestTimeoutSeconds)
   State.Stage = EProject_JCraftingRequestStage::Unknown;
 }
 return State;
}
void UProject_JCraftingJobsComponent::PublishRequestState()
{
 if (!CanPublish()) return;
 const TStrongObjectPtr<ThisClass> KeepSelf(this);
 const TStrongObjectPtr<AActor> KeepOwner(GetOwner());
 OnRequestChanged.Broadcast();
}
void UProject_JCraftingJobsComponent::BeginClientRequest(FGuid JobId, EProject_JCraftingJobAction Action)
{
 check(IsInGameThread());
 if (!CanPublish() || !GetPresentationGeneration() || !JobId.IsValid() || GetRequestState().Stage == EProject_JCraftingRequestStage::Pending) return;
 const FGuid Correlation = FGuid::NewGuid();
 if (!Correlation.IsValid()) return;
 const TStrongObjectPtr<ThisClass> KeepSelf(this);
 const TStrongObjectPtr<AActor> KeepOwner(GetOwner());
 CurrentRequest.RequestId = Correlation; CurrentRequest.JobId = JobId; CurrentRequest.Action = Action;
 CurrentRequest.Stage = EProject_JCraftingRequestStage::Pending; RequestIdentity = OwnerIdentity; RequestGeneration = OwnerScope.Generation;
 RequestStartedAt = FPlatformTime::Seconds();
 PublishRequestState();
 // Observers can replace identities, retire the source or begin another request.
 if (!CanPublish() || GetOwner() != KeepOwner.Get() || CurrentRequest.RequestId != Correlation ||
  RequestIdentity != OwnerIdentity || RequestGeneration != OwnerScope.Generation || GetRequestState().Stage != EProject_JCraftingRequestStage::Pending) return;
 if (Action == EProject_JCraftingJobAction::Collect) ServerCollectJob(Correlation, JobId, RequestGeneration);
 else ServerCancelJob(Correlation, JobId, RequestGeneration);
}
void UProject_JCraftingJobsComponent::RequestCollectJob(FGuid JobId) { BeginClientRequest(JobId, EProject_JCraftingJobAction::Collect); }
void UProject_JCraftingJobsComponent::RequestCancelJob(FGuid JobId) { BeginClientRequest(JobId, EProject_JCraftingJobAction::Cancel); }
void UProject_JCraftingJobsComponent::DispatchClientRequest(FGuid RequestId, FGuid JobId, uint64 ExpectedGeneration, EProject_JCraftingJobAction Action)
{
 check(IsInGameThread());
 if (!CanPublish() || !GetOwner()->HasAuthority() || !RequestId.IsValid() || !JobId.IsValid()) return;
 // Exhausted input quota must not amplify into an unbounded reliable reply stream.
 if (!UProject_JCommandBudgetComponent::AdmitClientCommand(GetOwner())) return;
 if (!OwnerIdentity.IsValid() || !ExpectedGeneration || ExpectedGeneration != OwnerScope.Generation)
 { ClientJobRequestResult(RequestId, JobId, ExpectedGeneration, Action, EProject_JCraftingRequestStage::Rejected); return; }
 const TWeakObjectPtr<ThisClass> WeakThis(this);
 const TWeakObjectPtr<AActor> WeakOwner(GetOwner());
 const FGuid Identity = OwnerIdentity;
 FProject_JPersistentCompletion Complete = [WeakThis, WeakOwner, Identity, ExpectedGeneration, RequestId, JobId, Action](bool Confirmed, bool)
 {
  if (auto* Self = WeakThis.Get(); Self && Self->CanPublish() && Self->GetOwner() == WeakOwner.Get() && Self->OwnerIdentity == Identity && Self->OwnerScope.Generation == ExpectedGeneration)
   Self->ClientJobRequestResult(RequestId, JobId, ExpectedGeneration, Action, Confirmed ? EProject_JCraftingRequestStage::Confirmed : EProject_JCraftingRequestStage::Unknown);
 };
 const bool Admitted = Action == EProject_JCraftingJobAction::Collect ? TryCollectJob(JobId, MoveTemp(Complete)) : TryCancelJob(JobId, MoveTemp(Complete));
 if (!Admitted && CanPublish() && GetOwner() == WeakOwner.Get() && OwnerIdentity == Identity && OwnerScope.Generation == ExpectedGeneration)
  ClientJobRequestResult(RequestId, JobId, ExpectedGeneration, Action, EProject_JCraftingRequestStage::Rejected);
}
void UProject_JCraftingJobsComponent::ServerCollectJob_Implementation(FGuid RequestId, FGuid JobId, uint64 ExpectedGeneration)
{ DispatchClientRequest(RequestId, JobId, ExpectedGeneration, EProject_JCraftingJobAction::Collect); }
void UProject_JCraftingJobsComponent::ServerCancelJob_Implementation(FGuid RequestId, FGuid JobId, uint64 ExpectedGeneration)
{ DispatchClientRequest(RequestId, JobId, ExpectedGeneration, EProject_JCraftingJobAction::Cancel); }
void UProject_JCraftingJobsComponent::ClientJobRequestResult_Implementation(FGuid RequestId, FGuid JobId, uint64 ExpectedGeneration, EProject_JCraftingJobAction Action, EProject_JCraftingRequestStage Stage)
{
 check(IsInGameThread());
 if (!GetPresentationGeneration() || !RequestId.IsValid() || !JobId.IsValid() || !ExpectedGeneration ||
  ExpectedGeneration != OwnerScope.Generation || RequestGeneration != OwnerScope.Generation || RequestIdentity != OwnerIdentity || CurrentRequest.RequestId != RequestId ||
  CurrentRequest.JobId != JobId || CurrentRequest.Action != Action ||
  (CurrentRequest.Stage != EProject_JCraftingRequestStage::Pending && CurrentRequest.Stage != EProject_JCraftingRequestStage::Unknown) ||
  (Stage != EProject_JCraftingRequestStage::Rejected && Stage != EProject_JCraftingRequestStage::Confirmed && Stage != EProject_JCraftingRequestStage::Unknown)) return;
 CurrentRequest.Stage = Stage;
 PublishRequestState();
}
void UProject_JCraftingJobsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME_CONDITION(ThisClass, Summaries, COND_OwnerOnly); DOREPLIFETIME_CONDITION(ThisClass, OwnerScope, COND_OwnerOnly); }
void UProject_JCraftingJobsComponent::Stop()
{ bEnding = true; OwnerIdentity.Invalidate(); InstalledIdentity.Invalidate(); RequestIdentity.Invalidate(); CurrentRequest = {}; RequestGeneration = InstalledGeneration = 0; RequestStartedAt = -1; SubmitPersistentEdit = {}; CreatePersistentRequest = {}; PersistentAdmission = {}; Jobs.Reset(); SummaryWorld.Reset(); Summaries.Reset(); ReceivedAtSeconds = -1; bPendingNotification = false; }
void UProject_JCraftingJobsComponent::EndPlay(const EEndPlayReason::Type Reason) { Stop(); Super::EndPlay(Reason); }
void UProject_JCraftingJobsComponent::OnComponentDestroyed(bool bDestroyingHierarchy) { Stop(); Super::OnComponentDestroyed(bDestroyingHierarchy); }
