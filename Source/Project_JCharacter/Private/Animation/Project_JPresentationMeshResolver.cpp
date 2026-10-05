#include "Animation/Project_JPresentationMeshResolver.h"

#include "Animation/Project_JRetargetAnimInstance.h"
#include "Components/Project_JModularMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"

bool Project_J::Animation::WasCharacterVisualRecentlyRendered(const ACharacter& Character, float Tolerance)
{
	const USkeletalMeshComponent* Leader = Character.GetMesh();
	TInlineComponentArray<USkeletalMeshComponent*> Meshes;
	Character.GetComponents(Meshes);
	for (const USkeletalMeshComponent* Mesh : Meshes)
	{
		if (!IsValid(Mesh) || !Mesh->IsRegistered()) { continue; }
		// A hidden leader's shadow timestamp is not presentation demand. Its
		// visible follower/equipment independently keeps the source pose awake.
		if (Mesh == Leader && (Mesh->bHiddenInGame || !Mesh->GetVisibleFlag())) { continue; }
		if (Mesh->WasRecentlyRendered(Tolerance)) { return true; }
	}
	return false;
}

USkeletalMeshComponent* Project_J::Animation::FindVisualFollower(const ACharacter& Character)
{
	const USkeletalMeshComponent* SourceMesh = Character.GetMesh();
	if (!IsValid(SourceMesh))
	{
		return nullptr;
	}

	USkeletalMeshComponent* RetargetFollower = nullptr;
	for (USceneComponent* Child : SourceMesh->GetAttachChildren())
	{
		USkeletalMeshComponent* Candidate = Cast<USkeletalMeshComponent>(Child);
		if (!IsValid(Candidate) || Candidate->IsA<UProject_JModularMeshComponent>() ||
			!Candidate->GetSkeletalMeshAsset())
		{
			continue;
		}

		if (Candidate->ComponentTags.Contains(TEXT("VisualFollower")))
		{
			return Candidate;
		}

		const UAnimInstance* AnimInstance = Candidate->GetAnimInstance();
		const UClass* AnimClass = Candidate->GetAnimClass();
		if (!RetargetFollower &&
			((AnimInstance && AnimInstance->IsA<UProject_JRetargetAnimInstance>()) ||
				(AnimClass && AnimClass->IsChildOf(UProject_JRetargetAnimInstance::StaticClass()))))
		{
			RetargetFollower = Candidate;
		}
	}

	return RetargetFollower;
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
