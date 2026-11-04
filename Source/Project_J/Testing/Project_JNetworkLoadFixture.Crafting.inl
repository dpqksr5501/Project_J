// Explicit synthetic transport/projection cohort. Never an online profile or owned-asset provider.
#include "Misc/DateTime.h"
#include "Misc/Timespan.h"
namespace
{
int32 CraftingRawSummaryCount(UProject_JCraftingJobsComponent* Source)
{
 // Read reflected storage independently of GetJobs' owner-identity filtering.
 // A foreign DTO leak must fail even if the presentation getter hides it.
 static const FArrayProperty* Field = FindFProperty<FArrayProperty>(UProject_JCraftingJobsComponent::StaticClass(), TEXT("Summaries"));
 return IsValid(Source) && Field ? FScriptArrayHelper(Field, Field->ContainerPtrToValuePtr<void>(Source)).Num() : INDEX_NONE;
}
}
void AProject_JNetworkLoadController::ClientCraftingStage_Implementation(int32 Phase, FGuid JobId, int32 Batches, bool bReplaceSource)
{
 if (!CraftingJobsEnabled() || Phase < 46 || Phase > 49 || LocalSlot < 0 || LocalSlot > 1) return;
 if (bReplaceSource) CraftingRetiredSource = CraftingObservedSource;
 CurrentStage = Phase; CraftingExpectedJob = JobId; CraftingExpectedBatches = Batches;
 bCraftingReplaceSource = bReplaceSource; bCraftingActionSent = bCraftingSawPending = false;
 bCraftingLateSourceSafe = !bReplaceSource; CraftingCorrelation.Invalidate(); CraftingStableSince = 0; NextObservation = 0;
}
void AProject_JNetworkLoadController::ObserveCraftingJobs()
{
 if (!CraftingJobsEnabled() || HasAuthority() || CurrentStage < 46 || CurrentStage > 49 || FPlatformTime::Seconds() < NextObservation) return;
 NextObservation = FPlatformTime::Seconds() + .1;
 auto* State = GetPlayerState<AProject_JPlayerState>();
 auto* Source = State ? State->GetCraftingJobsComponent() : nullptr;
 if (!IsValid(State) || State->IsActorBeingDestroyed() || !State->GetCharacterId().IsValid() || !IsValid(Source)) return;
 const auto Rows = Source->GetJobs();
 const int32 ExpectedRows = CurrentStage == 48 ? 0 : 1;
 const int32 OwnWire = CraftingRawSummaryCount(Source);
 int32 Foreign = 0, ForeignWire = 0, ForeignStates = 0;
 for (TActorIterator<AProject_JPlayerState> It(GetWorld()); It; ++It)
  if (*It != State && !It->IsActorBeingDestroyed()) if (auto* Other = It->GetCraftingJobsComponent()) { Foreign += Other->GetJobs().Num(); const int32 Count = CraftingRawSummaryCount(Other); if (Count < 0) bCraftingViolation = true; else { ForeignWire += Count; ++ForeignStates; } }
 if (Foreign || ForeignWire || OwnWire < 0) bCraftingViolation = true;
 bool Ready = Rows.Num() == ExpectedRows && OwnWire == ExpectedRows && Foreign == 0 && ForeignWire == 0 && ForeignStates >= 1;
 if (ExpectedRows == 1) Ready &= Rows.Num() == 1 && Rows[0].JobId == CraftingExpectedJob && Rows[0].Batches == CraftingExpectedBatches;
 if (bCraftingReplaceSource)
 {
  Ready &= CraftingRetiredSource && CraftingRetiredSource.Get() != Source;
  if (Ready && !bCraftingLateSourceSafe)
  {
   const auto* RetiredOwner = CraftingRetiredSource->GetOwner();
   if (IsValid(RetiredOwner) && !RetiredOwner->IsActorBeingDestroyed()) return;
   const auto Before = Source->GetRequestState();
   CraftingRetiredSource->RequestCancelJob(CraftingExpectedJob);
   const auto After = Source->GetRequestState();
   bCraftingLateSourceSafe = Before.RequestId == After.RequestId && Before.JobId == After.JobId && Before.Stage == After.Stage && Before.Action == After.Action &&
    CraftingRetiredSource->GetRequestState().Stage != EProject_JCraftingRequestStage::Pending;
   if (!bCraftingLateSourceSafe) bCraftingViolation = true;
  }
 }
 if (Ready && !bCraftingActionSent)
 {
  CraftingObservedSource = Source;
  if (CurrentStage == 47) Source->RequestCollectJob(CraftingExpectedJob); else Source->RequestCancelJob(CraftingExpectedJob);
  const auto Request = Source->GetRequestState();
  CraftingCorrelation = Request.RequestId;
  bCraftingSawPending = Request.Stage == EProject_JCraftingRequestStage::Pending && Request.JobId == CraftingExpectedJob && Request.RequestId.IsValid();
  bCraftingActionSent = true;
 }
 const auto Request = Source->GetRequestState();
 Ready &= bCraftingActionSent && bCraftingSawPending && Request.RequestId == CraftingCorrelation && Request.JobId == CraftingExpectedJob &&
  Request.Stage == EProject_JCraftingRequestStage::Rejected && bCraftingLateSourceSafe;
 if (Ready && !bCraftingViolation)
 {
  if (CraftingStableSince == 0) CraftingStableSince = FPlatformTime::Seconds();
  Ready = FPlatformTime::Seconds() - CraftingStableSince >= .5;
 }
 else CraftingStableSince = 0;
 const FString Detail = FString::Printf(TEXT("phase=%d rows=%d wire=%d foreign=%d foreignwire=%d foreignstates=%d pending=%d state=%d replaced=%d late=%d"), CurrentStage, Rows.Num(), OwnWire, Foreign, ForeignWire, ForeignStates,
  bCraftingSawPending, int32(Request.Stage), !bCraftingReplaceSource || CraftingRetiredSource.Get() != Source, bCraftingLateSourceSafe);
 if (Ready && CraftingRecordedPhase != CurrentStage)
 {
  CraftingRecordedPhase = CurrentStage;
  CraftingRows += FString::Printf(TEXT("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s,%d,%d,%d,synthetic_transport_no_owned_assets\n"),
   CurrentStage, LocalSlot, Rows.Num(), OwnWire, Foreign, ForeignWire, ForeignStates, ExpectedRows ? CraftingExpectedBatches : 0, bCraftingSawPending, int32(Request.Stage),
   *CraftingCorrelation.ToString(EGuidFormats::Digits), bCraftingReplaceSource && CraftingRetiredSource.Get() != Source, bCraftingLateSourceSafe, !bCraftingViolation);
 }
 ServerCraftingObserved(CurrentStage, Ready, bCraftingViolation, CraftingCorrelation, Detail);
}
void AProject_JNetworkLoadController::ServerCraftingObserved_Implementation(int32 Phase, bool bReady, bool bViolation, FGuid Correlation, const FString& Detail)
{
 if (auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AProject_JNetworkLoadGameMode>() : nullptr)
  Mode->AcceptCraftingObservation(this, Phase, bReady, bViolation, Correlation, Detail);
}
void AProject_JNetworkLoadGameMode::StartCraftingStage(int32 Phase)
{
 if (!CraftingJobsEnabled() || !HasAuthority() || Clients.Num() != 2 || Phase < 46 || Phase > 49 || bFinished) return;
 Stage = Phase; StageStarted = FPlatformTime::Seconds();
 const TStrongObjectPtr<ThisClass> KeepSelf(this);
 if (Phase == 49)
 {
  auto* PC = Clients[0].Get();
  const TStrongObjectPtr<AProject_JNetworkLoadController> KeepPC(PC);
  const TStrongObjectPtr<APawn> KeepPawn(PC ? PC->GetPawn() : nullptr);
  auto* OldState = PC ? PC->GetPlayerState<AProject_JPlayerState>() : nullptr;
  if (!PC || !KeepPawn.IsValid() || !IsValid(OldState)) { Finish(false, TEXT("Missing source for native PS replacement")); return; }
  const TStrongObjectPtr<AProject_JPlayerState> KeepOldState(OldState);
  const int32 NativePlayerId = OldState->GetPlayerId();
  PC->UnPossess();
  if (bFinished || !IsValid(PC) || PC->GetPawn() || !IsValid(OldState) || !IsValid(KeepPawn.Get()) || KeepPawn->IsActorBeingDestroyed()) { Finish(false, TEXT("Native unpossess changed replacement owner")); return; }
  PC->CleanupPlayerState();
  if (bFinished || !IsValid(PC) || PC->GetPlayerState<AProject_JPlayerState>()) { Finish(false, TEXT("Native PS cleanup did not retire the source")); return; }
  PC->InitPlayerState();
  auto* NewState = PC->GetPlayerState<AProject_JPlayerState>();
  if (bFinished || !IsValid(NewState) || NewState == OldState || !IsValid(KeepPawn.Get()) || KeepPawn->IsActorBeingDestroyed()) { Finish(false, TEXT("Native PS initialization did not create a fresh source")); return; }
  const TStrongObjectPtr<AProject_JPlayerState> KeepNewState(NewState);
  AssignPrototypeIdentity(*NewState);
  if (bFinished || !IsValid(PC) || !IsValid(NewState) || NewState->IsActorBeingDestroyed() || PC->GetPlayerState<AProject_JPlayerState>() != NewState ||
   !IsValid(KeepPawn.Get()) || KeepPawn->IsActorBeingDestroyed()) { Finish(false, TEXT("Native identity callback changed replacement ownership")); return; }
  NewState->SetPlayerId(NativePlayerId);
  PC->Possess(KeepPawn.Get());
  if (bFinished || !IsValid(PC) || PC->GetPawn() != KeepPawn.Get() || PC->GetPlayerState<AProject_JPlayerState>() != NewState || NewState->GetPawn() != KeepPawn.Get())
  { Finish(false, TEXT("Native replacement did not settle reciprocal ownership")); return; }
  CraftingJobIds[0].Invalidate(); CraftingEscrowIds[0].Invalidate();
 }
 for (int32 Slot = 0; Slot < 2; ++Slot)
 {
  auto* PC = Clients[Slot].Get(); auto* State = PC ? PC->GetPlayerState<AProject_JPlayerState>() : nullptr;
  auto* Source = State ? State->GetCraftingJobsComponent() : nullptr;
  auto* Profiles = GetGameInstance()->GetSubsystem<UProject_JLocalProfileSubsystem>();
  if (!IsValid(PC) || !IsValid(State) || !Source || !State->GetCharacterId().IsValid() || !Profiles || Profiles->IsReady(State))
  { Finish(false, TEXT("Synthetic transport must have an owner but no ready profile provider")); return; }
  if (!CraftingJobIds[Slot].IsValid()) CraftingJobIds[Slot] = FGuid::NewGuid();
  if (!CraftingEscrowIds[Slot].IsValid()) CraftingEscrowIds[Slot] = FGuid::NewGuid();
  TArray<ProjectJ::Crafting::FJobValues> Payload;
  if (Phase != 48)
  {
   ProjectJ::Crafting::FJobValues Job; Job.Id = CraftingJobIds[Slot]; Job.RecipeId = TEXT("Fixture.Transport.Crafting"); Job.Batches = Phase == 47 ? 2 : 1;
   Job.RulesDigest = FString::ChrN(40, 'a'); const int64 Now = FDateTime::UtcNow().GetTicks();
   Job.StartedUtcTicks = Phase == 47 ? Now - 5 * ETimespan::TicksPerSecond : Now;
   Job.DueUtcTicks = Phase == 47 ? Now - 3 * ETimespan::TicksPerSecond : Now + 60 * ETimespan::TicksPerSecond;
   FProject_JPersistentItem Escrow; Escrow.Instance = CraftingEscrowIds[Slot]; Escrow.Definition = TEXT("Fixture.Transport.Input"); Escrow.Order = 0;
   Job.Escrow.Add(Escrow); ProjectJ::Crafting::FResolvedLine Output; Output.Definition = TEXT("Fixture.Transport.Output"); Output.MaxStackCount = 100; Job.Outputs.Add(Output);
   Payload.Add(MoveTemp(Job));
  }
  if (!Source->ValidateCommittedJobs(Payload)) { Finish(false, TEXT("Synthetic projection payload failed structural admission")); return; }
  Source->InstallCommittedJobs(Payload, true);
  if (Source->GetJobs().Num() != (Phase == 48 ? 0 : 1)) { Finish(false, TEXT("Synthetic summary publication failed")); return; }
  CraftingAuthorityRows += FString::Printf(TEXT("%d,%d,%d,%s,0,1,synthetic_transport_no_owned_assets\n"), Phase, Slot, Source->GetJobs().Num(), *CraftingJobIds[Slot].ToString(EGuidFormats::Digits));
  PC->ConfirmedStage = INDEX_NONE;
  PC->ClientCraftingStage(Phase, CraftingJobIds[Slot], Phase == 47 ? 2 : 1, Phase == 49 && Slot == 0);
 }
}
void AProject_JNetworkLoadGameMode::AcceptCraftingObservation(AProject_JNetworkLoadController* PC, int32 Phase, bool bReady, bool bViolation, FGuid Correlation, const FString& Detail)
{
 if (!CraftingJobsEnabled() || bFinished || Phase != Stage || Phase < 46 || Phase > 49 || !IsValid(PC) || !Clients.Contains(PC) || Detail.Len() > 512) return;
 PC->LastObservation = Detail;
 if (bViolation) { Finish(false, TEXT("Crafting transport privacy/source violation")); return; }
 if (bReady && Correlation.IsValid()) PC->ConfirmedStage = Phase;
}
