#include "Animation/Project_JRetargetAnimInstanceProxy.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/Project_JRetargetBodyProfile.h"
#include "Animation/AnimClassInterface.h"
#include "AnimNodes/AnimNode_RetargetPoseFromMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/GCObject.h"
#include "UObject/UnrealType.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

void FProject_JRetargetAnimInstanceProxy::Initialize(UAnimInstance* Instance)
{
	// Reinitializing the same instance must not promote an applied body override
	// into its authored defaults. Reinstanced classes own entirely new node storage.
	if (CachedClass.Get() == Instance->GetClass())
		if (const auto* Interface = IAnimClassInterface::GetFromClass(Instance->GetClass()))
			for (const auto& Entry : Nodes) if (Entry.bApplied && Interface->GetAnimNodeProperties().IsValidIndex(Entry.Index) &&
				Interface->GetAnimNodeProperties()[Entry.Index]->Struct == FAnimNode_RetargetPoseFromMesh::StaticStruct())
			{
				auto* Node = Interface->GetAnimNodeProperties()[Entry.Index]->ContainerPtrToValuePtr<FAnimNode_RetargetPoseFromMesh>(Instance);
				Node->IKRetargeterAsset = Entry.Asset; Node->bUseCustomOverrideSets = Entry.bOverrideSets; Node->OverrideSetsToApply = Entry.Sets;
			}
	CachedClass.Reset(); Nodes.Reset();
	FAnimInstanceProxy::Initialize(Instance);
}

void FProject_JRetargetAnimInstanceProxy::AddReferencedObjects(UAnimInstance* Instance, FReferenceCollector& Collector)
{
	FAnimInstanceProxy::AddReferencedObjects(Instance, Collector);
	for (auto& Entry : Nodes) Collector.AddReferencedObject(Entry.Asset);
}

void FProject_JRetargetAnimInstanceProxy::PreUpdate(UAnimInstance* Instance, float DeltaSeconds)
{
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_RetargetProfile_PreUpdate);
		const auto* Anim = CastChecked<UProject_JRetargetAnimInstance>(Instance);
		const auto* Interface = IAnimClassInterface::GetFromClass(Instance->GetClass());
		if (CachedClass.Get() != Instance->GetClass())
		{
			Nodes.Reset(); CachedClass = Instance->GetClass();
			if (Interface)
			{
				const auto& Properties = Interface->GetAnimNodeProperties();
				for (int32 Index = 0; Index < Properties.Num(); ++Index)
				{
					if (Properties[Index]->Struct != FAnimNode_RetargetPoseFromMesh::StaticStruct()) continue;
					const auto* Node = Properties[Index]->ContainerPtrToValuePtr<FAnimNode_RetargetPoseFromMesh>(Instance);
					Nodes.Add({Index, Node->IKRetargeterAsset.Get(), Node->bUseCustomOverrideSets, Node->OverrideSetsToApply});
				}
			}
		}
		const auto* Body = Anim->GetRetargetBodyProfile();
		const auto* Family = Body ? Body->Family.Get() : nullptr;
		const auto* TargetMesh = Instance->GetSkelMeshComponent();
		if (Interface && TargetMesh && TargetMesh->GetSkeletalMeshAsset()) for (auto& Entry : Nodes)
		{
			const auto& Properties = Interface->GetAnimNodeProperties();
			if (!Properties.IsValidIndex(Entry.Index) || Properties[Entry.Index]->Struct != FAnimNode_RetargetPoseFromMesh::StaticStruct()) continue;
			auto* Node = Properties[Entry.Index]->ContainerPtrToValuePtr<FAnimNode_RetargetPoseFromMesh>(Instance);
			const USkeletalMeshComponent* Source = Node->SourceMeshComponent.Get();
			if (Node->RetargetFrom == ERetargetSourceMode::ParentSkeletalMeshComponent)
			{
				Source = nullptr;
				for (auto* Parent = TargetMesh->GetAttachParent(); Parent; Parent = Parent->GetAttachParent())
					if ((Source = Cast<USkeletalMeshComponent>(Parent))) break;
			}
			const bool bApply = Family && Node->RetargetFrom != ERetargetSourceMode::SourcePosePin && Source &&
				Source->GetSkeletalMeshAsset() && Family->Matches(Source->GetSkeletalMeshAsset()->GetSkeleton(), TargetMesh->GetSkeletalMeshAsset()->GetSkeleton());
			if (!bApply && !Entry.bApplied) continue; // Leave unowned graph/variable settings untouched.
			auto* DesiredAsset = bApply ? Family->Retargeter.Get() : Entry.Asset.Get();
			if (Node->IKRetargeterAsset.Get() != DesiredAsset) Node->IKRetargeterAsset = DesiredAsset;
			Node->bUseCustomOverrideSets = bApply && Body->bOverrideRetargetSets ? true : Entry.bOverrideSets;
			const auto& Sets = bApply && Body->bOverrideRetargetSets ? Body->RetargetOverrideSets : Entry.Sets;
			if (Node->OverrideSetsToApply != Sets) Node->OverrideSetsToApply = Sets;
			Entry.bApplied = bApply;
		}
	}
	// Engine PreUpdate invokes the node's GT source capture and bound-curve lookup.
	FAnimInstanceProxy::PreUpdate(Instance, DeltaSeconds);
}
