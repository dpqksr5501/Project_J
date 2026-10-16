#include "Animation/Project_JPresentationMeshResolver.h"

#include "Animation/Project_JRetargetAnimInstance.h"
#include "Components/Project_JModularMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

bool Project_J::Animation::UsesIndependentClothSimulation(const USkeletalMeshComponent& Mesh)
{
	const auto* Asset = Mesh.GetSkeletalMeshAsset();
	// Metadata covers the interval before streamed cloth instances exist.
	return !Mesh.GetClothingSimulationInstances().IsEmpty() || (Asset && Asset->HasActiveClothingAssets());
}

bool Project_J::Animation::WasCharacterVisualRecentlyRendered(const ACharacter& Character, float Tolerance)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_VisualDemandResolve);
	const USkeletalMeshComponent* Leader = Character.GetMesh();
	// Pure GT visibility queries cannot mutate component ownership. Visit the
	// actor's same owned set directly and stop at demand, instead of collecting
	// every skeletal pointer into a temporary array before scanning it again.
	for (const UActorComponent* Component : Character.GetComponents())
	{
		const USkeletalMeshComponent* Mesh = Cast<USkeletalMeshComponent>(Component);
		if (!IsValid(Mesh) || !Mesh->IsRegistered()) { continue; }
		// A hidden leader's shadow timestamp is not presentation demand. Its
		// visible follower/equipment independently keeps the source pose awake.
		if (Mesh == Leader && (Mesh->bHiddenInGame || !Mesh->GetVisibleFlag())) { continue; }
		if (Mesh->WasRecentlyRendered(Tolerance)) { return true; }
	}
	return false;
}

USkeletalMeshComponent* Project_J::Animation::FindVisualFollower(const ACharacter& Character, bool bRequireMeshAsset)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_VisualFollowerResolve);
	const USkeletalMeshComponent* SourceMesh = Character.GetMesh();
	if (!IsValid(SourceMesh))
	{
		return nullptr;
	}

	USkeletalMeshComponent* RetargetFollower = nullptr;
	USkeletalMeshComponent* ExplicitFollower = nullptr;
	bool bAmbiguousExplicit = false, bAmbiguousRetarget = false;
	for (USceneComponent* Child : SourceMesh->GetAttachChildren())
	{
		USkeletalMeshComponent* Candidate = Cast<USkeletalMeshComponent>(Child);
		if (!IsValid(Candidate) || Candidate->GetOwner() != &Character || !Candidate->IsRegistered() ||
			Candidate->IsA<UProject_JModularMeshComponent>())
		{
			continue;
		}

		if (Candidate->ComponentTags.Contains(TEXT("VisualFollower")) || Candidate->ComponentTags.Contains(TEXT("RetargetFollower")))
		{
			bAmbiguousExplicit |= ExplicitFollower && ExplicitFollower != Candidate;
			ExplicitFollower = Candidate;
			continue;
		}

		if (bRequireMeshAsset && !Candidate->GetSkeletalMeshAsset()) continue;
		const UAnimInstance* AnimInstance = Candidate->GetAnimInstance();
		const UClass* AnimClass = Candidate->GetAnimClass();
		const UAnimInstance* PostProcess = Candidate->GetPostProcessInstance();
		if (
			((AnimInstance && AnimInstance->IsA<UProject_JRetargetAnimInstance>()) ||
				(AnimClass && AnimClass->IsChildOf(UProject_JRetargetAnimInstance::StaticClass())) ||
				(PostProcess && PostProcess->IsA<UProject_JRetargetAnimInstance>())))
		{
			bAmbiguousRetarget |= RetargetFollower && RetargetFollower != Candidate;
			RetargetFollower = Candidate;
		}
	}

	if (bAmbiguousExplicit) return nullptr;
	// An explicit body waiting for its asset keeps the role. Do not let an
	// already-loaded retargeted garment take its hand/equipment consumers.
	if (ExplicitFollower) return !bRequireMeshAsset || ExplicitFollower->GetSkeletalMeshAsset() ? ExplicitFollower : nullptr;
	return bAmbiguousRetarget ? nullptr : RetargetFollower;
}

USkeletalMeshComponent* Project_J::Animation::ResolveEquipmentPoseSource(const ACharacter& Character,
	const USkeletalMesh& EquipmentMesh)
{
	if (!EquipmentMesh.GetSkeleton())
	{
		return nullptr;
	}

	if (USkeletalMeshComponent* Follower = FindVisualFollower(Character))
	{
		if (const USkeletalMesh* FollowerAsset = Follower->GetSkeletalMeshAsset();
			FollowerAsset && FollowerAsset->GetSkeleton() == EquipmentMesh.GetSkeleton())
		{
			return Follower;
		}
	}

	USkeletalMeshComponent* Source = Character.GetMesh();
	return IsValid(Source) && Source->GetSkeletalMeshAsset() &&
		Source->GetSkeletalMeshAsset()->GetSkeleton() == EquipmentMesh.GetSkeleton() ? Source : nullptr;
}

USkeletalMeshComponent* Project_J::Animation::ResolveWeaponAttachmentMesh(const ACharacter& Character,
	FName SourceSocket, FName VisualSocket, bool bPreferVisualFollower, FName& OutSocket)
{
	OutSocket = NAME_None;
	const FName EffectiveVisualSocket = VisualSocket.IsNone() ? SourceSocket : VisualSocket;
	if (USkeletalMeshComponent* Follower = bPreferVisualFollower ? FindVisualFollower(Character) : nullptr)
	{
		if (!EffectiveVisualSocket.IsNone() && Follower->DoesSocketExist(EffectiveVisualSocket))
		{
			OutSocket = EffectiveVisualSocket;
			return Follower;
		}
	}

	USkeletalMeshComponent* Source = Character.GetMesh();
	if (IsValid(Source) && !SourceSocket.IsNone() && Source->DoesSocketExist(SourceSocket))
	{
		OutSocket = SourceSocket;
		return Source;
	}
	return nullptr;
}
