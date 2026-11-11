#include "Authoring/Project_JAssetMigrationLibrary.h"

#include "Animation/Project_JWingerAnimInstance.h"
#include "AnimGraphNode_BlendListByInt.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "AnimGraphNode_RetargetPoseFromMesh.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/Skeleton.h"
#include "AssetCompilingManager.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Editor.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "LODUtilities.h"
#include "Rendering/SkeletalMeshRenderData.h"

bool UProject_JAssetMigrationLibrary::ConfigureRetargetFollower(
    UAnimBlueprint* Blueprint, USkeletalMesh* Target, UIKRetargeter* Retargeter,
    UAnimSequence* Overlay, FName OverlayRoot)
{
    if (!Blueprint || !Target || !Target->GetSkeleton() || !Retargeter ||
        Blueprint->TargetSkeleton != Target->GetSkeleton() ||
        (Overlay && (Overlay->GetSkeleton() != Target->GetSkeleton() ||
            Target->GetRefSkeleton().FindBoneIndex(OverlayRoot) <= 0)))
    {
        return false;
    }

    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    UEdGraph* const* FoundGraph = Graphs.FindByPredicate([](const UEdGraph* Candidate)
    {
        return Candidate && Candidate->GetFName() == TEXT("AnimGraph");
    });
    UEdGraph* Graph = FoundGraph ? *FoundGraph : nullptr;
    if (!Graph || Graph->Nodes.Num() != 1)
    {
        return false;
    }
    auto* Root = Cast<UAnimGraphNode_Root>(Graph->Nodes[0]);
    if (!Root || Root->Pins.Num() != 1 || !Root->Pins[0]->LinkedTo.IsEmpty())
    {
        return false;
    }
    const auto* RetargetProperty = FindFProperty<FStructProperty>(
        UAnimGraphNode_RetargetPoseFromMesh::StaticClass(), TEXT("Node"));
    if (!RetargetProperty || RetargetProperty->Struct != FAnimNode_RetargetPoseFromMesh::StaticStruct())
    {
        return false;
    }

    Blueprint->Modify();
    Graph->Modify();
    FGraphNodeCreator<UAnimGraphNode_RetargetPoseFromMesh> RetargetCreator(*Graph);
    auto* Retarget = RetargetCreator.CreateNode();
    RetargetProperty->ContainerPtrToValuePtr<FAnimNode_RetargetPoseFromMesh>(Retarget)->IKRetargeterAsset = Retargeter;
    Retarget->NodePosX = -620;
    RetargetCreator.Finalize();
    UEdGraphPin* FinalPose = Retarget->FindPin(TEXT("Pose"), EGPD_Output);

    if (Overlay)
    {
        FGraphNodeCreator<UAnimGraphNode_SequencePlayer> PlayerCreator(*Graph);
        auto* Player = PlayerCreator.CreateNode();
        Player->SetAnimationAsset(Overlay);
        Player->NodePosX = -620;
        Player->NodePosY = 200;
        PlayerCreator.Finalize();

        FGraphNodeCreator<UAnimGraphNode_LayeredBoneBlend> BlendCreator(*Graph);
        auto* Blend = BlendCreator.CreateNode();
        Blend->NodePosX = -320;
        if (Blend->Node.BlendPoses.IsEmpty())
        {
            Blend->Node.AddPose();
        }
        Blend->Node.BlendWeights[0] = 1.0f;
        FBranchFilter& Filter = Blend->Node.LayerSetup[0].BranchFilters.AddDefaulted_GetRef();
        Filter.BoneName = OverlayRoot;
        Filter.BlendDepth = 0;
        BlendCreator.Finalize();
        UEdGraphPin* BasePose = Blend->FindPin(TEXT("BasePose"), EGPD_Input);
        UEdGraphPin* OverlayPose = Blend->FindPin(TEXT("BlendPoses_0"), EGPD_Input);
        UEdGraphPin* SequencePose = Player->FindPin(TEXT("Pose"), EGPD_Output);
        if (!FinalPose || !BasePose || !OverlayPose || !SequencePose)
        {
            return false;
        }
        FinalPose->MakeLinkTo(BasePose);
        SequencePose->MakeLinkTo(OverlayPose);
        FinalPose = Blend->FindPin(TEXT("Pose"), EGPD_Output);
    }

    if (!FinalPose)
    {
        return false;
    }
    FinalPose->MakeLinkTo(Root->Pins[0]);
    Blueprint->SetPreviewMesh(Target);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    return Blueprint->Status != BS_Error;
}

bool UProject_JAssetMigrationLibrary::InstallWingerWingMotions(
    UAnimBlueprint* Blueprint, USkeletalMesh* Target, const TArray<UAnimSequence*>& Motions,
    UBlendSpace* RunBlend, UAnimSequence* Sprint)
{
    using EWingMotion = UProject_JWingerAnimInstance::EWingMotion;
    if ((GEditor && GEditor->PlayWorld) || !Blueprint || !Target ||
        Blueprint->ParentClass != UProject_JWingerAnimInstance::StaticClass() ||
        !Blueprint->TargetSkeleton || Blueprint->TargetSkeleton != Target->GetSkeleton() ||
        Target->GetRefSkeleton().FindBoneIndex(TEXT("a_01")) <= 0 ||
        Motions.Num() != static_cast<int32>(EWingMotion::Count))
    {
        UE_LOG(LogTemp, Warning, TEXT("Winger animation graph preflight failed for %s"), *GetNameSafe(Blueprint));
        return false;
    }
    for (const auto* Motion : Motions)
    {
        if (!Motion || Motion->GetSkeleton() != Blueprint->TargetSkeleton || Motion->GetPlayLength() <= 0.f)
        {
            return false;
        }
    }
    const int32 RunIndex = static_cast<int32>(EWingMotion::Run);
    if (RunBlend || Sprint)
    {
        if (!Cast<UBlendSpace1D>(RunBlend) || RunBlend->GetSkeleton() != Blueprint->TargetSkeleton ||
            !Sprint || Sprint->GetSkeleton() != Blueprint->TargetSkeleton ||
            !FMath::IsNearlyEqual(Sprint->GetPlayLength(), Motions[RunIndex]->GetPlayLength()))
        {
            return false;
        }
        const auto& Axis = RunBlend->GetBlendParameter(0);
        if (Axis.Min != 0.f || Axis.Max != 100.f || Axis.bWrapInput)
        {
            return false;
        }
        if (RunBlend->GetBlendSamples().IsEmpty())
        {
            RunBlend->Modify();
            if (RunBlend->AddSample(Motions[RunIndex], FVector::ZeroVector) == INDEX_NONE ||
                RunBlend->AddSample(Sprint, FVector(100.f, 0.f, 0.f)) == INDEX_NONE)
            {
                return false;
            }
            RunBlend->ValidateSampleData();
            RunBlend->ResampleData();
            RunBlend->MarkPackageDirty();
        }
        const auto& Samples = RunBlend->GetBlendSamples();
        if (Samples.Num() != 2 ||
            !Samples.ContainsByPredicate([&](const FBlendSample& Sample)
                { return Sample.Animation == Motions[RunIndex] && Sample.SampleValue.X == 0.f; }) ||
            !Samples.ContainsByPredicate([&](const FBlendSample& Sample)
                { return Sample.Animation == Sprint && Sample.SampleValue.X == 100.f; }))
        {
            return false;
        }
    }

    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    UEdGraph* const* FoundGraph = Graphs.FindByPredicate([](const UEdGraph* Candidate)
    {
        return Candidate && Candidate->GetFName() == TEXT("AnimGraph");
    });
    UEdGraph* Graph = FoundGraph ? *FoundGraph : nullptr;
    if (!Graph)
    {
        return false;
    }
    const float Times[] = {0.24f, 0.22f, 0.22f, 0.10f, 0.09f, 0.16f};
    static_assert(UE_ARRAY_COUNT(Times) == static_cast<int32>(EWingMotion::Count));
    auto Connect = [Graph](UEdGraphPin* From, UEdGraphPin* To)
    {
        return From && To && Graph->GetSchema()->TryCreateConnection(From, To);
    };
    auto ConnectRunBlend = [&](UEdGraphPin* Pose, int32 X, int32 Y)
    {
        FGraphNodeCreator<UAnimGraphNode_BlendSpacePlayer> PlayerCreator(*Graph);
        auto* Player = PlayerCreator.CreateNode();
        Player->SetAnimationAsset(RunBlend);
        Player->NodePosX = X;
        Player->NodePosY = Y;
        PlayerCreator.Finalize();

        FGraphNodeCreator<UK2Node_VariableGet> SpeedCreator(*Graph);
        auto* Speed = SpeedCreator.CreateNode();
        Speed->VariableReference.SetSelfMember(
            GET_MEMBER_NAME_CHECKED(UProject_JWingerAnimInstance, WingSprintBlendPercent));
        Speed->NodePosX = X - 260;
        Speed->NodePosY = Y;
        SpeedCreator.Finalize();

        if (Connect(Speed->GetValuePin(), Player->FindPin(TEXT("X"), EGPD_Input)) &&
            Connect(Player->FindPin(TEXT("Pose"), EGPD_Output), Pose))
        {
            return true;
        }
        FBlueprintEditorUtils::RemoveNode(Blueprint, Speed, true);
        FBlueprintEditorUtils::RemoveNode(Blueprint, Player, true);
        return false;
    };
    auto SetClip = [&](UAnimGraphNode_SequencePlayer* Player, int32 Index)
    {
        Player->SetAnimationAsset(Motions[Index]);
        const auto Motion = static_cast<EWingMotion>(Index);
        Player->Node.SetLoopAnimation(Motion != EWingMotion::Takeoff && Motion != EWingMotion::Land);
    };
    auto SetWingRotationSpace = [](UAnimGraphNode_LayeredBoneBlend* Layer)
    {
        // Wing clips author the sweep relative to the character's heading.
        // Keep the attachment on the spine without rotating that sweep with
        // the locomotion pose's torso lean.
        Layer->Node.bMeshSpaceRotationBlend = true;
        Layer->Node.bRootSpaceRotationBlend = true;
    };
    auto Finish = [&]()
    {
        Blueprint->SetPreviewMesh(Target);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        if (Blueprint->Status == BS_Error)
        {
            return false;
        }
        auto* CDO = CastChecked<UProject_JWingerAnimInstance>(Blueprint->GeneratedClass->GetDefaultObject());
        CDO->TakeoffDuration = Motions[static_cast<int32>(EWingMotion::Takeoff)]->GetPlayLength();
        CDO->LandingDuration = Motions[static_cast<int32>(EWingMotion::Land)]->GetPlayLength();
        Blueprint->MarkPackageDirty();
        return true;
    };
    if (Graph->Nodes.Num() > 1)
    {
        // Migrate the existing, installed graph in place, retaining its input and branch mask.
        if (Target->GetPostProcessAnimBlueprint().Get() != Blueprint->GeneratedClass.Get())
        {
            UE_LOG(LogTemp, Warning, TEXT("Winger post-process asset mismatch: %s / %s"),
                *GetPathNameSafe(Target->GetPostProcessAnimBlueprint().Get()), *GetNameSafe(Blueprint->GeneratedClass.Get()));
            return false;
        }
        UAnimGraphNode_BlendListByInt* Selector = nullptr;
        UAnimGraphNode_LayeredBoneBlend* WingLayer = nullptr;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (auto* Candidate = Cast<UAnimGraphNode_BlendListByInt>(Node))
            {
                if (Selector)
                {
                    return false;
                }
                Selector = Candidate;
            }
            if (auto* Candidate = Cast<UAnimGraphNode_LayeredBoneBlend>(Node))
            {
                const auto& Layers = Candidate->Node.LayerSetup;
                if (WingLayer || Layers.Num() != 1 || Layers[0].BranchFilters.Num() != 1 ||
                    Layers[0].BranchFilters[0].BoneName != TEXT("a_01"))
                {
                    return false;
                }
                WingLayer = Candidate;
            }
        }
        const int32 AirIndex = static_cast<int32>(EWingMotion::Air);
        if (!WingLayer || !Selector || Selector->FindPin(TEXT("BlendPose_6"), EGPD_Input))
        {
            UE_LOG(LogTemp, Warning, TEXT("Winger blend selector missing or has unexpected inputs"));
            return false;
        }
        const FName AirPinName(*FString::Printf(TEXT("BlendPose_%d"), AirIndex));
        const auto* AirPin = Selector->FindPin(AirPinName, EGPD_Input);
        const int32 Slots = AirPin && !AirPin->LinkedTo.IsEmpty() ? Motions.Num() : AirIndex;
        TArray<UAnimGraphNode_SequencePlayer*> Players;
        for (int32 Index = 0; Index < Slots; ++Index)
        {
            auto* Pose = Selector->FindPin(*FString::Printf(TEXT("BlendPose_%d"), Index), EGPD_Input);
            auto* Player = Pose && Pose->LinkedTo.Num() == 1 ?
                Cast<UAnimGraphNode_SequencePlayer>(Pose->LinkedTo[0]->GetOwningNode()) : nullptr;
            auto* BlendPlayer = Index == RunIndex && RunBlend && Pose && Pose->LinkedTo.Num() == 1 ?
                Cast<UAnimGraphNode_BlendSpacePlayer>(Pose->LinkedTo[0]->GetOwningNode()) : nullptr;
            const bool bExpectedAsset = (Player && Player->GetAnimationAsset() == Motions[Index]) ||
                (BlendPlayer && BlendPlayer->GetAnimationAsset() == RunBlend);
            if (!bExpectedAsset ||
                !Selector->FindPin(*FString::Printf(TEXT("BlendTime_%d"), Index), EGPD_Input))
            {
                UE_LOG(LogTemp, Warning, TEXT("Winger blend input %d mismatch: %s / %s"), Index,
                    *GetPathNameSafe(Player ? Player->GetAnimationAsset() : nullptr), *GetPathNameSafe(Motions[Index]));
                return false;
            }
            Players.Add(Player);
        }
        Blueprint->Modify();
        Graph->Modify();
        Selector->Modify();
        WingLayer->Modify();
        SetWingRotationSpace(WingLayer);
        if (Slots == AirIndex)
        {
            // Older graphs can contain a spare, unlinked pin from placement.
            if (!AirPin)
            {
                Selector->Node.AddPose();
                Selector->ReconstructNode();
            }
            FGraphNodeCreator<UAnimGraphNode_SequencePlayer> Creator(*Graph);
            auto* Player = Creator.CreateNode();
            SetClip(Player, AirIndex);
            Player->NodePosX = Selector->NodePosX - 320;
            Player->NodePosY = Selector->NodePosY + AirIndex * 130;
            Creator.Finalize();
            auto* Pose = Selector->FindPin(AirPinName, EGPD_Input);
            auto* Output = Player->FindPin(TEXT("Pose"), EGPD_Output);
            if (!Pose || !Output || !Graph->GetSchema()->TryCreateConnection(Output, Pose))
            {
                UE_LOG(LogTemp, Warning, TEXT("Winger air blend link failed (input %d output %d)"), Pose != nullptr, Output != nullptr);
                return false;
            }
            Players.Add(Player);
        }
        for (int32 Index = 0; Index < Players.Num(); ++Index)
        {
            if (auto* Player = Players[Index])
            {
                Player->Modify();
                if (Index == RunIndex && RunBlend)
                {
                    auto* Pose = Selector->FindPinChecked(*FString::Printf(TEXT("BlendPose_%d"), RunIndex), EGPD_Input);
                    auto* PreviousOutput = Pose->LinkedTo[0];
                    Pose->BreakLinkTo(PreviousOutput);
                    if (!ConnectRunBlend(Pose, Player->NodePosX, Player->NodePosY))
                    {
                        Connect(PreviousOutput, Pose);
                        return false;
                    }
                    FBlueprintEditorUtils::RemoveNode(Blueprint, Player, true);
                }
                else
                {
                    SetClip(Player, Index);
                }
            }
            Selector->FindPinChecked(*FString::Printf(TEXT("BlendTime_%d"), Index), EGPD_Input)
                ->DefaultValue = FString::SanitizeFloat(Times[Index]);
        }
        return Finish();
    }
    auto* Root = Graph && Graph->Nodes.Num() == 1 ? Cast<UAnimGraphNode_Root>(Graph->Nodes[0]) : nullptr;
    const auto* ChildMode = FindFProperty<FEnumProperty>(FAnimNode_BlendListBase::StaticStruct(), TEXT("ChildUpateMode"));
    if (!Root || Root->Pins.Num() != 1 || !Root->Pins[0]->LinkedTo.IsEmpty() || !ChildMode)
    {
        return false;
    }

    Blueprint->Modify();
    Graph->Modify();
    FGraphNodeCreator<UAnimGraphNode_LinkedInputPose> InputCreator(*Graph);
    auto* Input = InputCreator.CreateNode();
    Input->Node.Name = TEXT("InputPose");
    Input->Node.Graph = TEXT("AnimGraph");
    Input->NodePosX = -650;
    InputCreator.Finalize();

    FGraphNodeCreator<UAnimGraphNode_LayeredBoneBlend> LayerCreator(*Graph);
    auto* Layer = LayerCreator.CreateNode();
    Layer->NodePosX = -320;
    if (Layer->Node.BlendPoses.IsEmpty())
    {
        Layer->Node.AddPose();
    }
    Layer->Node.BlendWeights[0] = 1.f;
    Layer->Node.CurveBlendOption = ECurveBlendOption::UseBasePose;
    Layer->Node.LayerSetup[0].BranchFilters.AddDefaulted_GetRef().BoneName = TEXT("a_01");
    SetWingRotationSpace(Layer);
    LayerCreator.Finalize();

    FGraphNodeCreator<UAnimGraphNode_BlendListByInt> BlendCreator(*Graph);
    auto* Selector = BlendCreator.CreateNode();
    Selector->NodePosX = Layer->NodePosX - 280;
    Selector->NodePosY = Layer->NodePosY + 230;
    ChildMode->GetUnderlyingProperty()->SetIntPropertyValue(
        ChildMode->ContainerPtrToValuePtr<void>(&Selector->Node),
        static_cast<uint64>(EBlendListChildUpdateMode::ResetChildOnActivate));
    BlendCreator.Finalize();
    // Placement already creates two pose pins.
    for (int32 Index = 2; Index < Motions.Num(); ++Index)
    {
        Selector->Node.AddPose();
    }
    Selector->ReconstructNode();

    FGraphNodeCreator<UK2Node_VariableGet> IndexCreator(*Graph);
    auto* WingIndex = IndexCreator.CreateNode();
    WingIndex->VariableReference.SetSelfMember(GET_MEMBER_NAME_CHECKED(UProject_JWingerAnimInstance, WingMotionIndex));
    WingIndex->NodePosX = Selector->NodePosX - 320;
    WingIndex->NodePosY = Selector->NodePosY - 100;
    IndexCreator.Finalize();

    TArray<UEdGraphNode*> Added{Input, Layer, Selector, WingIndex};
    auto Fail = [&]()
    {
        for (auto* Node : Added)
        {
            FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
        }
        return false;
    };
    if (!Connect(Input->FindPin(TEXT("Pose"), EGPD_Output), Layer->FindPin(TEXT("BasePose"), EGPD_Input)) ||
        !Connect(Layer->FindPin(TEXT("Pose"), EGPD_Output), Root->Pins[0]) ||
        !Connect(WingIndex->GetValuePin(), Selector->FindPin(TEXT("ActiveChildIndex"), EGPD_Input)))
    {
        return Fail();
    }
    for (int32 Index = 0; Index < Motions.Num(); ++Index)
    {
        auto* Time = Selector->FindPin(*FString::Printf(TEXT("BlendTime_%d"), Index), EGPD_Input);
        auto* Pose = Selector->FindPin(*FString::Printf(TEXT("BlendPose_%d"), Index), EGPD_Input);
        if (!Time)
        {
            return Fail();
        }
        Time->DefaultValue = FString::SanitizeFloat(Times[Index]);
        if (Index == RunIndex && RunBlend)
        {
            const int32 FirstNewNode = Graph->Nodes.Num();
            if (!ConnectRunBlend(Pose, Selector->NodePosX - 320, Selector->NodePosY + Index * 130))
            {
                return Fail();
            }
            for (int32 NodeIndex = FirstNewNode; NodeIndex < Graph->Nodes.Num(); ++NodeIndex)
            {
                Added.Add(Graph->Nodes[NodeIndex]);
            }
            continue;
        }
        FGraphNodeCreator<UAnimGraphNode_SequencePlayer> PlayerCreator(*Graph);
        auto* Player = PlayerCreator.CreateNode();
        SetClip(Player, Index);
        Player->NodePosX = Selector->NodePosX - 320;
        Player->NodePosY = Selector->NodePosY + Index * 130;
        PlayerCreator.Finalize();
        Added.Add(Player);
        if (!Connect(Player->FindPin(TEXT("Pose"), EGPD_Output), Pose))
        {
            return Fail();
        }
    }
    if (!Connect(Selector->FindPin(TEXT("Pose"), EGPD_Output), Layer->FindPin(TEXT("BlendPoses_0"), EGPD_Input)))
    {
        return Fail();
    }
    if (!Finish())
    {
        return false;
    }
    Target->Modify();
    Target->SetPostProcessAnimBlueprint(Blueprint->GeneratedClass.Get());
    Target->SetPostProcessAnimGraphLODThreshold(1);
    return true;
}

FString UProject_JAssetMigrationLibrary::BakeWingerFarWings(USkeletalMesh* Mesh, UAnimSequence* Idle)
{
    if ((GEditor && GEditor->PlayWorld) || !Mesh || !Idle || Idle->GetSkeleton() != Mesh->GetSkeleton() ||
        Mesh->GetLODNum() != 3)
    {
        return {};
    }
    const auto& Ref = Mesh->GetRefSkeleton();
    const int32 Anchor = Ref.FindBoneIndex(TEXT("a_01"));
    if (Anchor <= 0)
    {
        return {};
    }
    constexpr int32 FarLOD = 2;
    auto* Info = Mesh->GetLODInfo(FarLOD);
    if (!Info || !Info->bHasBeenSimplified)
    {
        return {};
    }
    TBitArray<> WingBones(false, Ref.GetNum());
    WingBones[Anchor] = true;
    TArray<FBoneReference> Removed;
    Removed.Emplace(Ref.GetBoneName(Anchor));
    for (int32 Index = Anchor + 1; Index < Ref.GetNum(); ++Index)
    {
        if (WingBones[Ref.GetParentIndex(Index)])
        {
            WingBones[Index] = true;
            Removed.Emplace(Ref.GetBoneName(Index));
        }
    }
    Mesh->Modify();
    Info->BonesToRemove = MoveTemp(Removed);
    Info->BakePose = Idle;
    Info->ReductionSettings.BaseLOD = 0;
    Info->ScreenSize.Default = 0.12f;
    FSkeletalMeshUpdateContext Context;
    Context.SkeletalMesh = Mesh;
    FLODUtilities::SimplifySkeletalMeshLOD(Context, FarLOD, GetTargetPlatformManagerRef().GetRunningTargetPlatform());
    Mesh->PostEditChange();
    FAssetCompilingManager::Get().FinishCompilationForObjects({Mesh});
    const auto* Render = Mesh->GetResourceForRendering();
    if (!Render || Render->LODRenderData.Num() != 3)
    {
        return {};
    }
    const auto& Far = Render->LODRenderData[FarLOD];
    const auto IsWing = [&WingBones](FBoneIndexType Bone) { return WingBones[Bone]; };
    if (Far.RequiredBones.ContainsByPredicate(IsWing) ||
        Far.RenderSections.ContainsByPredicate([&IsWing](const FSkelMeshRenderSection& Section)
        {
            return Section.BoneMap.ContainsByPredicate(IsWing);
        }))
    {
        return {};
    }
    Mesh->MarkPackageDirty();
    return FString::Printf(TEXT("{\"triangles\":[%d,%d,%d],\"required_bones\":[%d,%d,%d],\"removed_wing_bones\":%d}"),
        Render->LODRenderData[0].GetTotalFaces(), Render->LODRenderData[1].GetTotalFaces(), Far.GetTotalFaces(),
        Render->LODRenderData[0].RequiredBones.Num(), Render->LODRenderData[1].RequiredBones.Num(),
        Far.RequiredBones.Num(), Mesh->GetLODInfo(FarLOD)->BonesToRemove.Num());
}

int32 UProject_JAssetMigrationLibrary::CountWingerBindDifferences(USkeletalMesh* Mesh)
{
    const USkeleton* Skeleton = Mesh ? Mesh->GetSkeleton() : nullptr;
    if (!Skeleton)
    {
        return INDEX_NONE;
    }
    const auto& MeshRef = Mesh->GetRefSkeleton();
    const auto& AssetRef = Skeleton->GetReferenceSkeleton();
    if (MeshRef.FindRawBoneIndex(TEXT("a_01")) <= 0 || MeshRef.GetRawBoneNum() != AssetRef.GetRawBoneNum())
    {
        return INDEX_NONE;
    }
    int32 Differences = 0;
    for (int32 Index = 0; Index < MeshRef.GetRawBoneNum(); ++Index)
    {
        const int32 Other = AssetRef.FindRawBoneIndex(MeshRef.GetBoneName(Index));
        if (Other == INDEX_NONE)
        {
            return INDEX_NONE;
        }
        const int32 MeshParent = MeshRef.GetParentIndex(Index);
        const int32 AssetParent = AssetRef.GetParentIndex(Other);
        if ((MeshParent == INDEX_NONE) != (AssetParent == INDEX_NONE) ||
            (MeshParent != INDEX_NONE && MeshRef.GetBoneName(MeshParent) != AssetRef.GetBoneName(AssetParent)))
        {
            return INDEX_NONE;
        }
        Differences += !MeshRef.GetRawRefBonePose()[Index].Equals(AssetRef.GetRawRefBonePose()[Other], 0.0001f);
    }
    return Differences;
}

bool UProject_JAssetMigrationLibrary::UpdateWingerSkeletonBind(USkeletalMesh* Mesh)
{
    if ((GEditor && GEditor->PlayWorld) || CountWingerBindDifferences(Mesh) <= 0)
    {
        return false;
    }
    auto* Skeleton = Mesh->GetSkeleton();
    Skeleton->Modify();
    Skeleton->UpdateReferencePoseFromMesh(Mesh);
    return CountWingerBindDifferences(Mesh) == 0;
}

bool UProject_JAssetMigrationLibrary::RepairOfflineLimbMarkers(UAnimSequence* Sequence, USkeletalMesh* Mesh)
{
    if ((GEditor && GEditor->PlayWorld) || !Sequence || !Mesh ||
        Sequence->GetSkeleton() != Mesh->GetSkeleton() || !Sequence->IsDataModelValid())
    {
        return false;
    }
    const auto& Ref = Mesh->GetRefSkeleton();
    const int32 FootRoot = Ref.FindBoneIndex(TEXT("ik_foot_root"));
    const int32 HandRoot = Ref.FindBoneIndex(TEXT("ik_hand_root"));
    if (FootRoot <= 0 || HandRoot <= 0 || Ref.GetParentIndex(FootRoot) != 0 || Ref.GetParentIndex(HandRoot) != 0)
    {
        return false;
    }
    const TPair<FName, FName> Names[] = {
        {TEXT("ik_foot_l"), TEXT("foot_l")}, {TEXT("ik_foot_r"), TEXT("foot_r")},
        {TEXT("ik_hand_l"), TEXT("hand_l")}, {TEXT("ik_hand_r"), TEXT("hand_r")}
    };
    TArray<TPair<int32, int32>> Markers;
    for (const auto& [Marker, Limb] : Names)
    {
        const int32 MarkerIndex = Ref.FindBoneIndex(Marker);
        const int32 LimbIndex = Ref.FindBoneIndex(Limb);
        if (MarkerIndex <= 0 || LimbIndex <= 0)
        {
            return false;
        }
        Markers.Emplace(MarkerIndex, LimbIndex);
    }
    const IAnimationDataModel* Model = Sequence->GetDataModel();
    const int32 NumKeys = Model->GetNumberOfKeys();
    if (NumKeys < 1)
    {
        return false;
    }
    TArray<FName> TrackNames;
    Model->GetBoneTrackNames(TrackNames);
    const TArray<FTransform>& Bind = Ref.GetRefBonePose();
    TArray<TArray<FTransform>> Tracks;
    Tracks.SetNum(Ref.GetNum());
    for (const FName Track : TrackNames)
    {
        const int32 Bone = Ref.FindBoneIndex(Track);
        if (Bone != INDEX_NONE && Bone != FootRoot && Bone != HandRoot)
        {
            Model->GetBoneTrackTransforms(Track, Tracks[Bone]);
            if (Tracks[Bone].Num() != NumKeys)
            {
                return false;
            }
        }
    }

    TMap<int32, TArray<FTransform>> Result;
    Result.Add(FootRoot).Init(Bind[FootRoot], NumKeys);
    Result.Add(HandRoot).Init(Bind[HandRoot], NumKeys);
    for (const auto& [Marker, Limb] : Markers)
    {
        Result.Add(Marker).Reserve(NumKeys);
    }
    TArray<FTransform> Component;
    Component.SetNum(Ref.GetNum());
    Component[0] = FTransform::Identity;
    for (int32 Key = 0; Key < NumKeys; ++Key)
    {
        for (int32 Bone = 1; Bone < Ref.GetNum(); ++Bone)
        {
            const FTransform& Local = Tracks[Bone].IsEmpty() ? Bind[Bone] : Tracks[Bone][Key];
            Component[Bone] = Local * Component[Ref.GetParentIndex(Bone)];
        }
        for (const auto& [Marker, Limb] : Markers)
        {
            FTransform Local = Component[Limb].GetRelativeTransform(Component[Ref.GetParentIndex(Marker)]);
            Local.SetScale3D(Bind[Marker].GetScale3D());
            if (Local.ContainsNaN())
            {
                return false;
            }
            Result.FindChecked(Marker).Add(Local);
        }
    }
    Sequence->Modify();
    auto& Controller = Sequence->GetController();
    IAnimationDataController::FScopedBracket Bracket(Controller,
        NSLOCTEXT("ProjectJLocomotion", "RepairOfflineLimbMarkers", "Bake IK markers from final FK limbs"), false);
    for (const auto& [Index, Keys] : Result)
    {
        const FName Bone = Ref.GetBoneName(Index);
        if (!TrackNames.Contains(Bone) && !Controller.AddBoneCurve(Bone, false))
        {
            return false;
        }
        TArray<FVector> Positions;
        TArray<FQuat> Rotations;
        TArray<FVector> Scales;
        Positions.Reserve(NumKeys);
        Rotations.Reserve(NumKeys);
        Scales.Reserve(NumKeys);
        for (const FTransform& Key : Keys)
        {
            Positions.Add(Key.GetTranslation());
            Rotations.Add(Key.GetRotation());
            Scales.Add(Key.GetScale3D());
        }
        if (!Controller.SetBoneTrackKeys(Bone, Positions, Rotations, Scales, false))
        {
            return false;
        }
    }
    Sequence->MarkPackageDirty();
    return true;
}

bool UProject_JAssetMigrationLibrary::InjectPIEInput(
    APlayerController* Controller, const UInputAction* Action, FVector Value)
{
    if (!Controller || !Action || Value.ContainsNaN() ||
        !Controller->GetWorld() || Controller->GetWorld()->WorldType != EWorldType::PIE)
    {
        return false;
    }
    auto* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Controller->GetLocalPlayer());
    if (!Input)
    {
        return false;
    }
    Input->InjectInputVectorForAction(Action, Value, {}, {});
    return true;
}
