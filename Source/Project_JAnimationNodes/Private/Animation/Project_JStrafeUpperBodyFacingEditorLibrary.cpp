#include "Animation/Project_JStrafeUpperBodyFacingEditorLibrary.h"
#include "Animation/AnimGraphNode_ProjectJStrafeUpperBodyFacing.h"
#include "Animation/AnimGraphNode_ProjectJStrafeLowerBodyFacing.h"
#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
// Compilation can reconstruct pins. Resolve stable node/pin names again for rollback.
struct FFacingEdge
{
    TWeakObjectPtr<UEdGraphNode> From, To;
    FName FromPin, ToPin;
    FFacingEdge(UEdGraphPin* A, UEdGraphPin* B)
        : From(A->GetOwningNode()), To(B->GetOwningNode()), FromPin(A->PinName), ToPin(B->PinName) {}
    UEdGraphPin* Output() const { return From.IsValid() ? From->FindPin(FromPin, EGPD_Output) : nullptr; }
    UEdGraphPin* Input() const { return To.IsValid() ? To->FindPin(ToPin, EGPD_Input) : nullptr; }
    bool operator==(const FFacingEdge& Other) const
    { return From == Other.From && To == Other.To && FromPin == Other.FromPin && ToPin == Other.ToPin; }
    void Break() const { if (auto* A = Output()) if (auto* B = Input()) A->BreakLinkTo(B); }
    bool Restore() const
    {
        auto* A = Output(); auto* B = Input();
        if (!A || !B) return false;
        if (!A->LinkedTo.Contains(B)) A->MakeLinkTo(B);
        return A->LinkedTo.Contains(B) && B->LinkedTo.Contains(A);
    }
};

bool InstallFacingChain(UAnimBlueprint* Blueprint, bool bRequireLower)
{
    check(IsInGameThread());
    if (!IsValid(Blueprint) || Blueprint->Status == BS_Error) return false;
    TStrongObjectPtr<UAnimBlueprint> PinnedBlueprint(Blueprint);
    UEdGraph* Graph = nullptr;
    for (UEdGraph* Candidate : Blueprint->FunctionGraphs)
    {
        if (Candidate && Candidate->GetFName() == TEXT("AnimGraph"))
        { if (Graph) return false; Graph = Candidate; }
    }
    if (!Graph || !Graph->GetSchema()) return false;
    TStrongObjectPtr<UEdGraph> PinnedGraph(Graph);
    UEdGraphNode* History = nullptr;
    UAnimGraphNode_ProjectJStrafeUpperBodyFacing* Upper = nullptr;
    UAnimGraphNode_ProjectJStrafeLowerBodyFacing* Lower = nullptr;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (!Node) return false;
        if (Node->GetClass()->GetFName() == TEXT("AnimGraphNode_PoseSearchHistoryCollector"))
        { if (History) return false; History = Node; }
        if (auto* Facing = Cast<UAnimGraphNode_ProjectJStrafeUpperBodyFacing>(Node))
        { if (Upper) return false; Upper = Facing; }
        if (auto* Facing = Cast<UAnimGraphNode_ProjectJStrafeLowerBodyFacing>(Node))
        { if (Lower) return false; Lower = Facing; }
    }
    auto* HistoryPose = History ? History->FindPin(TEXT("Pose"), EGPD_Output) : nullptr;
    if (!HistoryPose || HistoryPose->LinkedTo.Num() != 1) return false;
    TArray<FFacingEdge> Original;
    UEdGraphPin* Head = HistoryPose;
    const auto FollowExisting = [&](UEdGraphNode* Node)
    {
        if (!Node) return true;
        auto* Input = Node->FindPin(TEXT("Source"), EGPD_Input);
        auto* Output = Node->FindPin(TEXT("Pose"), EGPD_Output);
        if (!Input || !Output || Head->LinkedTo.Num() != 1 || Head->LinkedTo[0] != Input ||
            Input->LinkedTo.Num() != 1 || Input->LinkedTo[0] != Head || Output->LinkedTo.Num() != 1) return false;
        Original.Emplace(Head, Input); Head = Output; return true;
    };
    if (!FollowExisting(Lower) || !FollowExisting(Upper)) return false;
    auto* Destination = Head->LinkedTo[0];
    if (!Destination || Destination->Direction != EGPD_Input || Destination->PinName != TEXT("BlendPose_0") ||
        Destination->GetOwningNode()->GetGraph() != Graph ||
        Destination->GetOwningNode()->GetClass()->GetFName() != TEXT("AnimGraphNode_BlendListByEnum") ||
        Destination->LinkedTo.Num() != 1 || Destination->LinkedTo[0] != Head) return false;
    Original.Emplace(Head, Destination);
    if (Upper && (!bRequireLower || Lower)) return Blueprint->GeneratedClass != nullptr;

    // Allocate staged pins without adding nodes or requesting blueprint extensions.
    TStrongObjectPtr<UAnimGraphNode_ProjectJStrafeUpperBodyFacing> AddedUpper;
    TStrongObjectPtr<UAnimGraphNode_ProjectJStrafeLowerBodyFacing> AddedLower;
    if (!Upper)
    {
        AddedUpper.Reset(NewObject<UAnimGraphNode_ProjectJStrafeUpperBodyFacing>(Graph, NAME_None, RF_Transactional));
        Upper = AddedUpper.Get(); Upper->CreateNewGuid(); Upper->AllocateDefaultPins();
        Upper->NodePosX = History->NodePosX + 300; Upper->NodePosY = History->NodePosY;
    }
    if (bRequireLower && !Lower)
    {
        AddedLower.Reset(NewObject<UAnimGraphNode_ProjectJStrafeLowerBodyFacing>(Graph, NAME_None, RF_Transactional));
        Lower = AddedLower.Get(); Lower->CreateNewGuid(); Lower->AllocateDefaultPins();
        Lower->NodePosX = History->NodePosX + 150; Lower->NodePosY = History->NodePosY + 150;
    }
    TArray<FFacingEdge> Planned;
    Head = HistoryPose;
    const auto Append = [&](UEdGraphNode* Node)
    {
        if (!Node) return true;
        auto* Input = Node->FindPin(TEXT("Source"), EGPD_Input);
        auto* Output = Node->FindPin(TEXT("Pose"), EGPD_Output);
        if (!Input || !Output) return false;
        Planned.Emplace(Head, Input); Head = Output; return true;
    };
    if (!Append(Lower) || !Append(Upper)) return false;
    Planned.Emplace(Head, Destination);
    const UEdGraphSchema* Schema = Graph->GetSchema();
    for (const FFacingEdge& Edge : Planned)
    {
        if (!Original.Contains(Edge) && Schema->CanCreateConnection(Edge.Output(), Edge.Input()).Response == CONNECT_RESPONSE_DISALLOW) return false;
    }
    const bool bWasDirty = Blueprint->GetOutermost()->IsDirty();
    Blueprint->Modify(); Graph->Modify();
    if (AddedUpper.IsValid()) { Graph->AddNode(Upper, false, false); Upper->PostPlacedNewNode(); }
    if (AddedLower.IsValid()) { Graph->AddNode(Lower, false, false); Lower->PostPlacedNewNode(); }
    for (const FFacingEdge& Edge : Original) if (!Planned.Contains(Edge)) Edge.Break();
    bool bConnected = true;
    for (const FFacingEdge& Edge : Planned)
    {
        if (!Original.Contains(Edge) && !Schema->TryCreateConnection(Edge.Output(), Edge.Input())) { bConnected = false; break; }
    }
    const bool bCompiled = bConnected;
    if (bConnected)
    {
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        bConnected = Blueprint->Status != BS_Error && Blueprint->GeneratedClass != nullptr;
    }
    if (bConnected) return true;

    // Revoke both newly added nodes and only the links owned by this operation.
    for (const FFacingEdge& Edge : Planned) if (!Original.Contains(Edge)) Edge.Break();
    if (AddedLower.IsValid()) { Lower->BreakAllNodeLinks(); Graph->RemoveNode(Lower); }
    if (AddedUpper.IsValid()) { Upper->BreakAllNodeLinks(); Graph->RemoveNode(Upper); }
    bool bRestored = true;
    for (const FFacingEdge& Edge : Original) bRestored = Edge.Restore() && bRestored;
    if (bCompiled)
    {
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        bRestored = bRestored && Blueprint->Status != BS_Error && Blueprint->GeneratedClass != nullptr;
    }
    if (bRestored) Blueprint->GetOutermost()->SetDirtyFlag(bWasDirty);
    else UE_LOG(LogTemp, Error, TEXT("Facing installation failed and its original graph could not be compiled/restored: %s"), *Blueprint->GetPathName());
    return false;
}
}

bool UProject_JStrafeUpperBodyFacingEditorLibrary::InstallInMasterForReview(UAnimBlueprint* Blueprint)
{
    return IsValid(Blueprint) && Blueprint->GetPathName() == TEXT("/Game/Animation_Logic/ABPs/ABP_Humanoid_Master.ABP_Humanoid_Master") && InstallUpperGraphForReview(Blueprint);
}
bool UProject_JStrafeUpperBodyFacingEditorLibrary::InstallLowerBodyInMasterForReview(UAnimBlueprint* Blueprint)
{
    return IsValid(Blueprint) && Blueprint->GetPathName() == TEXT("/Game/Animation_Logic/ABPs/ABP_Humanoid_Master.ABP_Humanoid_Master") && InstallLowerGraphForReview(Blueprint);
}
bool UProject_JStrafeUpperBodyFacingEditorLibrary::InstallUpperGraphForReview(UAnimBlueprint* Blueprint)
{ return InstallFacingChain(Blueprint, false); }
bool UProject_JStrafeUpperBodyFacingEditorLibrary::InstallLowerGraphForReview(UAnimBlueprint* Blueprint)
{ return InstallFacingChain(Blueprint, true); }
