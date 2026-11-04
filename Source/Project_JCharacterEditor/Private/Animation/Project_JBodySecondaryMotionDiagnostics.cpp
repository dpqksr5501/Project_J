#include "Animation/Project_JPlayerLocomotionLibrary.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimClassInterface.h"
#include "AnimGraphNode_RigidBody.h"
#include "BoneControllers/AnimNode_RigidBody.h"
#include "EdGraph/EdGraph.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "K2Node_VariableGet.h"
#include "UObject/UnrealType.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

FString UProject_JPlayerLocomotionLibrary::InspectSecondaryMotionAsset(
    UAnimBlueprint* Blueprint, USkeletalMesh* Mesh, const TArray<FName>& ProtectedBones)
{
    check(IsInGameThread());
    const auto Result = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Errors, Rows, ProtectedRows;
    const auto Error = [&Errors](const FString& Text) { Errors.Add(MakeShared<FJsonValueString>(Text)); };
    const auto Finish = [&]()
    {
        Result->SetBoolField(TEXT("accessory_only_proof"), Errors.IsEmpty() && !Rows.IsEmpty());
        Result->SetArrayField(TEXT("errors"), Errors); Result->SetArrayField(TEXT("rigid_nodes"), Rows);
        Result->SetArrayField(TEXT("protected_bones"), ProtectedRows);
        Result->SetBoolField(TEXT("asset_mutation_requested"), false);
        Result->SetBoolField(TEXT("compile_or_parallel_wait_requested"), false);
        FString Text; FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Text)); return Text;
    };
    if (!IsValid(Blueprint) || !IsValid(Mesh) || ProtectedBones.IsEmpty() || ProtectedBones.Num() > 128)
    { Error(TEXT("Expected exact Blueprint/mesh and 1..128 explicit protected core bones.")); return Finish(); }
    Result->SetStringField(TEXT("blueprint"), Blueprint->GetPathName());
    Result->SetStringField(TEXT("mesh"), Mesh->GetPathName());
    if (Blueprint->Status != BS_UpToDate || !Blueprint->GeneratedClass ||
        !Blueprint->GeneratedClass->IsChildOf(UProject_JRetargetAnimInstance::StaticClass()))
    { Error(TEXT("Existing compiled UpToDate RetargetAnimInstance class is required; no compile is performed.")); return Finish(); }
    const auto* Defaults = Cast<UProject_JRetargetAnimInstance>(Blueprint->GeneratedClass->GetDefaultObject());
    if (!Defaults) { Error(TEXT("Missing native body defaults.")); return Finish(); }
    Result->SetBoolField(TEXT("asset_opt_in"), Defaults->bAllowSecondaryMotionBudget);
    const auto& Skeleton = Mesh->GetRefSkeleton();
    if (Skeleton.GetNum() <= 0 || Skeleton.GetNum() > 2048)
    { Error(TEXT("Reference skeleton exceeds the bounded inspection scope.")); return Finish(); }
    TSet<FName> ProtectedNames;
    for (FName Bone : ProtectedBones) ProtectedNames.Add(Bone);
    const auto Calibration = Defaults->GetHandGripCalibration();
    for (FName Bone : {Calibration.PrimaryArm.Shoulder, Calibration.PrimaryArm.Elbow, Calibration.PrimaryArm.Hand,
        Calibration.SecondaryArm.Shoulder, Calibration.SecondaryArm.Elbow, Calibration.SecondaryArm.Hand})
        if (!Bone.IsNone()) ProtectedNames.Add(Bone);
    // A simulated ancestor of any authored socket could change a gameplay/VFX
    // attachment. Protect every active socket instead of guessing its usage by name.
    for (const auto* Socket : Mesh->GetActiveSocketList()) if (Socket && !Socket->BoneName.IsNone()) ProtectedNames.Add(Socket->BoneName);
    if (ProtectedNames.Num() > 256) { Error(TEXT("Protected bone/socket cap exceeded.")); return Finish(); }
    TArray<int32> ProtectedIndices;
    for (FName Bone : ProtectedNames)
    {
        const int32 Index = Skeleton.FindBoneIndex(Bone);
        const auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("bone"), Bone.ToString()); Row->SetNumberField(TEXT("index"), Index);
        ProtectedRows.Add(MakeShared<FJsonValueObject>(Row));
        if (Index == INDEX_NONE) Error(FString::Printf(TEXT("Protected bone/socket parent absent: %s"), *Bone.ToString()));
        else ProtectedIndices.AddUnique(Index);
    }
    TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
    if (Graphs.Num() > 32) { Error(TEXT("Graph cap exceeded.")); return Finish(); }
    TArray<UAnimGraphNode_RigidBody*> Bodies;
    int32 NodeCount = 0;
    for (auto* Graph : Graphs) if (Graph) for (const auto& NodePtr : Graph->Nodes)
    {
        UEdGraphNode* Node = NodePtr.Get();
        if (++NodeCount > 1024) { Error(TEXT("Graph node cap exceeded.")); return Finish(); }
        if (auto* Body = Cast<UAnimGraphNode_RigidBody>(Node)) Bodies.Add(Body);
    }
    if (Bodies.IsEmpty() || Bodies.Num() > 8) { Error(TEXT("Expected 1..8 existing accessory RigidBody nodes.")); return Finish(); }
    TSet<FName> DynamicOwners;
    TMap<const UPhysicsAsset*, int32> GraphAssets;
    const FName Enable = GET_MEMBER_NAME_CHECKED(UProject_JRetargetAnimInstance, bEnableSecondaryMotion);
    for (auto* Body : Bodies)
    {
        const auto Row = MakeShared<FJsonObject>(); Rows.Add(MakeShared<FJsonValueObject>(Row));
        Row->SetStringField(TEXT("node"), Body->GetPathName());
        const auto* Pin = Body->FindPin(TEXT("bAlphaBoolEnabled"));
        const auto* Variable = Pin && Pin->LinkedTo.Num() == 1 ? Cast<UK2Node_VariableGet>(Pin->LinkedTo[0]->GetOwningNode()) : nullptr;
        bool bGate = Body->Node.AlphaInputType == EAnimAlphaInputType::Bool && Pin && !Body->HasBinding(TEXT("bAlphaBoolEnabled")) &&
            Variable && Variable->VariableReference.IsSelfContext() && Variable->VariableReference.GetMemberName() == Enable;
        for (const TCHAR* Name : {TEXT("Alpha"), TEXT("AlphaCurveName")})
            if (const auto* Other = Body->FindPin(Name); (Other && !Other->LinkedTo.IsEmpty()) || Body->HasBinding(Name)) bGate = false;
        Row->SetBoolField(TEXT("shared_bool_alpha_binding"), bGate);
        Row->SetNumberField(TEXT("evaluation_reset_time"), Body->Node.EvaluationResetTime);
        Row->SetNumberField(TEXT("alpha_blend_in_seconds"), Body->Node.AlphaBoolBlend.BlendInTime);
        Row->SetNumberField(TEXT("alpha_blend_out_seconds"), Body->Node.AlphaBoolBlend.BlendOutTime);
        Row->SetBoolField(TEXT("freezes_input_pose"), Body->Node.bFreezeIncomingPoseOnStart);
        Row->SetBoolField(TEXT("world_geometry"), Body->Node.bEnableWorldGeometry);
        Row->SetNumberField(TEXT("lod_threshold"), Body->Node.LODThreshold);
        if (!bGate) Error(TEXT("RigidBody does not exclusively use the existing secondary-motion BoolAlpha."));
        // The engine owns easing and the actual evaluation gap. No project
        // reset is triggered by a mere requested-bool edge during an easing tail.
        if (!FMath::IsFinite(Body->Node.EvaluationResetTime) || Body->Node.EvaluationResetTime <= 0.f)
            Error(TEXT("Accessory opt-in requires an authored finite positive engine evaluation reset time."));
        if (Body->Node.bFreezeIncomingPoseOnStart) Error(TEXT("A frozen-input RigidBody can affect the whole core input pose; opt-in refused."));
        const UPhysicsAsset* Physics = Body->Node.OverridePhysicsAsset ? Body->Node.OverridePhysicsAsset.Get() : Mesh->GetPhysicsAsset();
        Row->SetStringField(TEXT("physics_asset"), GetPathNameSafe(Physics));
        if (!Physics || Physics->SkeletalBodySetups.Num() > 256)
        { Error(TEXT("Missing or oversized physics asset.")); continue; }
        ++GraphAssets.FindOrAdd(Physics);
        TArray<TSharedPtr<FJsonValue>> Dynamic, Kinematic;
        for (const auto& SetupPtr : Physics->SkeletalBodySetups)
        {
            const USkeletalBodySetup* Setup = SetupPtr.Get();
            if (!Setup) continue;
            const bool bDynamic = Setup->PhysicsType == PhysType_Simulated ||
                (Body->Node.bUseDefaultAsSimulated && Setup->PhysicsType == PhysType_Default);
            if (!bDynamic) { Kinematic.Add(MakeShared<FJsonValueString>(Setup->BoneName.ToString())); continue; }
            const auto Bone = MakeShared<FJsonObject>(); Dynamic.Add(MakeShared<FJsonValueObject>(Bone));
            Bone->SetStringField(TEXT("bone"), Setup->BoneName.ToString());
            const int32 Index = Skeleton.FindBoneIndex(Setup->BoneName);
            if (Index == INDEX_NONE) { Error(TEXT("Simulated bone is absent from target reference skeleton.")); continue; }
            if (DynamicOwners.Contains(Setup->BoneName)) Error(TEXT("Multiple RigidBody nodes write the same dynamic bone."));
            DynamicOwners.Add(Setup->BoneName);
            TArray<TSharedPtr<FJsonValue>> Ancestors, Conflicts;
            for (int32 Parent = Index; Parent != INDEX_NONE; Parent = Skeleton.GetParentIndex(Parent))
                Ancestors.Add(MakeShared<FJsonValueString>(Skeleton.GetBoneName(Parent).ToString()));
            for (int32 Protected : ProtectedIndices)
                for (int32 Parent = Protected; Parent != INDEX_NONE; Parent = Skeleton.GetParentIndex(Parent))
                    if (Parent == Index) { Conflicts.Add(MakeShared<FJsonValueString>(Skeleton.GetBoneName(Protected).ToString())); break; }
            Bone->SetArrayField(TEXT("ancestors"), Ancestors); Bone->SetArrayField(TEXT("protected_descendant_conflicts"), Conflicts);
            if (!Conflicts.IsEmpty()) Error(TEXT("A simulated bone is a protected core/socket bone or its ancestor."));
        }
        Row->SetArrayField(TEXT("dynamic_bones"), Dynamic); Row->SetArrayField(TEXT("kinematic_bones"), Kinematic);
        if (Dynamic.IsEmpty()) Error(TEXT("No actual simulated body in an accessory node."));
    }
    // Account for inherited/compiled node storage, instead of trusting editor
    // graph names or the presence of a single convenient variable accessor.
    const auto* Interface = IAnimClassInterface::GetFromClass(Blueprint->GeneratedClass);
    TMap<const UPhysicsAsset*, int32> CompiledAssets;
    int32 CompiledCount = 0;
    if (Interface) for (auto* Property : Interface->GetAnimNodeProperties()) if (Property->Struct == FAnimNode_RigidBody::StaticStruct())
    {
        const auto* Node = Property->ContainerPtrToValuePtr<FAnimNode_RigidBody>(Defaults);
        const UPhysicsAsset* Physics = Node->OverridePhysicsAsset ? Node->OverridePhysicsAsset.Get() : Mesh->GetPhysicsAsset();
        ++CompiledAssets.FindOrAdd(Physics); ++CompiledCount;
        if (Node->AlphaInputType != EAnimAlphaInputType::Bool || Node->bFreezeIncomingPoseOnStart ||
            !FMath::IsFinite(Node->EvaluationResetTime) || Node->EvaluationResetTime <= 0.f)
            Error(TEXT("Compiled RigidBody defaults violate the inspected gate/input-pose contract."));
    }
    Result->SetNumberField(TEXT("compiled_rigid_nodes"), CompiledCount);
    if (!Interface || CompiledCount != Bodies.Num() || CompiledAssets.Num() != GraphAssets.Num())
        Error(TEXT("Compiled and authored RigidBody counts/assets differ; inherited/custom writers require separate inspection."));
    for (const auto& Pair : GraphAssets) if (CompiledAssets.FindRef(Pair.Key) != Pair.Value)
        Error(TEXT("Compiled physics asset differs from the authored node."));
    Result->SetStringField(TEXT("engine_output_contract"), TEXT("UE5.8 AnimNode_RigidBody.cpp893-977 emits only simulated-body output transforms; protected descendants are checked transitively."));
    Result->SetStringField(TEXT("limitation"), TEXT("Static output isolation is required but does not prove visible quality, physics re-entry continuity, runtime linked-layer substitution or 200-character performance."));
    return Finish();
}
