#include "Gathering/Project_JGatheringSubsystem.h"
#include "Gathering/Project_JGatheringNode.h"
#include "Backend/Project_JLocalProfileSubsystem.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "Game/Project_JPlayerState.h"
#include "Inventory/Project_JItemDefinition.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

bool UProject_JGatheringSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{ return Type == EWorldType::Game || Type == EWorldType::PIE; }
void UProject_JGatheringSubsystem::Initialize(FSubsystemCollectionBase& Collection) { Super::Initialize(Collection); bAccepting = true; }
bool UProject_JGatheringSubsystem::IsActive() const
{ return bAccepting && GetWorld() && !GetWorld()->bIsTearingDown && GetWorld()->GetNetMode() == NM_Standalone; }
void UProject_JGatheringSubsystem::OnWorldEndPlay(UWorld& World)
{ if (&World == GetWorld()) { bAccepting = false; Nodes.Reset(); ReclaimOrder.Reset(); } Super::OnWorldEndPlay(World); }
void UProject_JGatheringSubsystem::Deinitialize()
{ bAccepting = false; Nodes.Reset(); ReclaimOrder.Reset(); Super::Deinitialize(); }
void UProject_JGatheringSubsystem::RemoveSlot(FName Id)
{
    if (const auto* Slot = Nodes.Find(Id)) RemoveSlotAt(Slot->ReclaimIndex);
}
void UProject_JGatheringSubsystem::RemoveSlotAt(int32 Index)
{
    if (!ReclaimOrder.IsValidIndex(Index)) return;
    Nodes.Remove(ReclaimOrder[Index]); ReclaimOrder.RemoveAtSwap(Index);
    if (ReclaimOrder.IsValidIndex(Index)) Nodes.FindChecked(ReclaimOrder[Index]).ReclaimIndex = Index;
    if (!ReclaimOrder.IsEmpty()) ReclaimCursor %= ReclaimOrder.Num(); else ReclaimCursor = 0;
}
void UProject_JGatheringSubsystem::ReclaimExpired()
{
    Stats.LastReclaimVisits = 0;
    const double Now = GetWorld()->GetTimeSeconds();
    if (!FMath::IsFinite(Now)) return;
    const int32 Limit = FMath::Min(ReclaimOrder.Num(), MaxReclaimsPerAdmission);
    for (int32 Visit = 0; Visit < Limit && !ReclaimOrder.IsEmpty(); ++Visit)
    {
        ReclaimCursor %= ReclaimOrder.Num();
        const FName Id = ReclaimOrder[ReclaimCursor];
        auto* Slot = Nodes.Find(Id); ++Stats.LastReclaimVisits;
        const bool bExpired = Slot && !Slot->Owner.IsValid() && Slot->bTerminal
            && (Slot->Phase == EProject_JGatheringPhase::Available || (Slot->Phase == EProject_JGatheringPhase::Cooldown && Now >= Slot->NextAvailable));
        if (!Slot || bExpired) { RemoveSlotAt(ReclaimCursor); if (bExpired) ++Stats.Reclaimed; }
        else ReclaimCursor = (ReclaimCursor + 1) % ReclaimOrder.Num();
    }
}
FProject_JGatheringView UProject_JGatheringSubsystem::GetState(FName Id, const AProject_JGatheringNode* Node) const
{
    check(IsInGameThread());
    if (!IsActive() || Id.IsNone()) return {};
    const auto* Slot = Nodes.Find(Id);
    if (!Slot) return {EProject_JGatheringPhase::Available, 0}; // Advisory; capacity is decided at admission.
    if (Slot->Owner.IsValid() && Slot->Owner.Get() != Node) return {};
    const double Now = GetWorld()->GetTimeSeconds();
    const bool bAvailable = Slot->Phase == EProject_JGatheringPhase::Cooldown && FMath::IsFinite(Now) && Now >= Slot->NextAvailable;
    return {bAvailable ? EProject_JGatheringPhase::Available : Slot->Phase, Slot->NextAvailable};
}
uint64 UProject_JGatheringSubsystem::RegisterNode(AProject_JGatheringNode* Node)
{
    check(IsInGameThread());
    if (!IsActive() || !IsValid(Node) || Node->IsActorBeingDestroyed() || Node->GetWorld() != GetWorld()
        || !Node->HasAuthority() || !Node->HasValidDefinition()) return 0;
    ReclaimExpired();
    if (auto* Slot = Nodes.Find(Node->NodeId))
    {
        if (Slot->Owner.IsValid() && Slot->Owner.Get() != Node) { ++Stats.DuplicateOwners; return 0; }
        if (Slot->Owner.Get() == Node) return Slot->Registration;
        Slot->Owner = Node; Slot->Registration = ++NextRegistration;
        if (!Slot->Registration) Slot->Registration = ++NextRegistration;
        const uint64 Registered = Slot->Registration; Publish(Node->NodeId); return Registered;
    }
    if (Nodes.Num() >= MaxTrackedNodes) { ++Stats.CapacityRejected; return 0; }
    auto& Slot = Nodes.Add(Node->NodeId); Slot.Owner = Node; Slot.Registration = ++NextRegistration;
    if (!Slot.Registration) Slot.Registration = ++NextRegistration;
    Slot.ReclaimIndex = ReclaimOrder.Add(Node->NodeId);
    const uint64 Registered = Slot.Registration; Publish(Node->NodeId); return Registered;
}
void UProject_JGatheringSubsystem::UnregisterNode(FName Id, AProject_JGatheringNode* Node, uint64 Registration)
{
    check(IsInGameThread());
    auto* Slot = Nodes.Find(Id);
    if (!Slot || !Registration || Slot->Registration != Registration || Slot->Owner.Get() != Node) return;
    Slot->Owner.Reset(); Slot->Registration = 0;
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0;
    if (Slot->bTerminal && (Slot->Phase == EProject_JGatheringPhase::Available
        || (Slot->Phase == EProject_JGatheringPhase::Cooldown && FMath::IsFinite(Now) && Now >= Slot->NextAvailable))) RemoveSlot(Id);
    // Pending, unresolved and unexpired cooldown survive actor unload/Destroy/GC.
}
void UProject_JGatheringSubsystem::Publish(FName Id)
{
    const auto* Slot = Nodes.Find(Id);
    if (auto* Node = Slot ? Slot->Owner.Get() : nullptr; IsValid(Node) && !Node->IsActorBeingDestroyed()) Node->ApplyLedgerState(GetState(Id, Node));
}
uint64 UProject_JGatheringSubsystem::Reserve(AProject_JGatheringNode* Node, uint64 Registration, FGuid Request)
{
    auto* Slot = Node ? Nodes.Find(Node->NodeId) : nullptr;
    if (!IsActive() || !Slot || !Request.IsValid() || Slot->Owner.Get() != Node || Slot->Registration != Registration
        || GetState(Node->NodeId, Node).Phase != EProject_JGatheringPhase::Available || !Node->HasValidDefinition()) return 0;
    Slot->Operation = ++NextOperation; if (!Slot->Operation) Slot->Operation = ++NextOperation;
    Slot->Request = Request; Slot->CapturedCooldown = Node->CooldownSeconds; Slot->NextAvailable = 0;
    Slot->bTerminal = false; Slot->Phase = EProject_JGatheringPhase::Pending;
    const uint64 Operation = Slot->Operation;
    Publish(Node->NodeId); return Operation;
}
void UProject_JGatheringSubsystem::AbortUnsubmitted(FName Id, uint64 Operation, FGuid Request)
{
    auto* Slot = Nodes.Find(Id);
    if (!Slot || Slot->bTerminal || Slot->Operation != Operation || Slot->Request != Request) return;
    Slot->bTerminal = true; Slot->Phase = EProject_JGatheringPhase::Available; Slot->NextAvailable = 0;
    Publish(Id); // The provider did not accept a job; no stored request/retry route remains here.
}
void UProject_JGatheringSubsystem::Complete(FName Id, uint64 Operation, FGuid Request, const FProject_JProfileDurableResult& Result)
{
    check(IsInGameThread());
    auto* Slot = Nodes.Find(Id);
    if (!IsActive() || !Slot || Slot->bTerminal || Slot->Operation != Operation || Slot->Request != Request || Result.Request != Request)
    { ++Stats.StaleOutcomes; return; }
    // Fence this operation before any resource can become available. There is no API or retained Edit to re-submit it.
    Slot->bTerminal = true;
    if (Result.Outcome == EProject_JProfileDurableOutcome::Committed)
    {
        const double Next = GetWorld()->GetTimeSeconds() + Slot->CapturedCooldown;
        if (FMath::IsFinite(Next)) { Slot->Phase = EProject_JGatheringPhase::Cooldown; Slot->NextAvailable = Next; ++Stats.Committed; }
        else { Slot->Phase = EProject_JGatheringPhase::Unresolved; ++Stats.Unresolved; }
    }
    else if (Result.Outcome == EProject_JProfileDurableOutcome::ProvenAbsent)
    { Slot->Phase = EProject_JGatheringPhase::Available; Slot->NextAvailable = 0; ++Stats.ProvenAbsent; }
    else { Slot->Phase = EProject_JGatheringPhase::Unresolved; ++Stats.Unresolved; }
    Publish(Id);
}
bool UProject_JGatheringSubsystem::SubmitGather(AProject_JGatheringNode* Node, uint64 Registration, AProject_JPlayerState* Player)
{
    check(IsInGameThread());
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    const TStrongObjectPtr<AProject_JGatheringNode> KeepNode(Node);
    const TStrongObjectPtr<AProject_JPlayerState> KeepPlayer(Player);
    auto* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    if (!IsActive() || !IsValid(Node) || Node->IsActorBeingDestroyed() || !IsValid(Player) || Player->IsActorBeingDestroyed()
        || !Player->HasAuthority() || Player->GetWorld() != GetWorld() || Player->GetGameInstance() != GI || !GI) return false;
    auto* Profiles = GI->GetSubsystem<UProject_JLocalProfileSubsystem>();
    auto* Data = GI->GetSubsystem<UProject_JCharacterDataSubsystem>();
    auto* Definition = Data ? Data->GetItemDefinition(Node->YieldItemId, Node->YieldItemVersion) : nullptr;
    if (!Profiles || !Profiles->IsReady(Player) || Profiles->IsCheckpointPending(Player) || !Definition
        || Node->YieldCount < 1 || Node->YieldCount > Definition->MaxStackCount) { ++Stats.AdmissionRejected; return false; }
    FProject_JPersistentEdit Edit;
    Edit.Kind = EProject_JPersistentEdit::Gather; Edit.Definition = Definition; Edit.Amount = Node->YieldCount;
    Edit.Level = 1; Edit.Instance = FGuid::NewGuid(); Edit.Request = Profiles->NewRequestId(Player);
    const FName Id = Node->NodeId; const FGuid Request = Edit.Request;
    const uint64 Operation = Reserve(Node, Registration, Request);
    if (!Operation) { ++Stats.AdmissionRejected; return false; }
    const TWeakObjectPtr<ThisClass> WeakSelf(this);
    if (!Profiles->SubmitEdit(Player, Edit, {}, [WeakSelf, Id, Operation, Request](const FProject_JProfileDurableResult& Result)
        { if (auto* Self = WeakSelf.Get()) Self->Complete(Id, Operation, Request, Result); }))
    { AbortUnsubmitted(Id, Operation, Request); ++Stats.AdmissionRejected; return false; }
    ++Stats.Accepted;
    return true;
}
