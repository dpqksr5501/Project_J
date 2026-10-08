#include "Components/Project_JFoleyComponent.h"

#include "Audio/Project_JFoleyAudioProfile.h"
#include "Animation/Project_JCharacterAnimProfile.h"
#include "Animation/Project_JPresentationMeshResolver.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Mount/Project_JMountComponent.h"
#include "Project_JBaseCharacter.h"
#include "Project_JPlayerCharacter.h"
#include "System/Project_JFoleySubsystem.h"

UProject_JFoleyComponent::UProject_JFoleyComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
	SetIsReplicatedByDefault(false);
	for (double& Time : LastAdmission) { Time = -DBL_MAX; }
}

TSoftObjectPtr<UProject_JFoleyAudioProfile> UProject_JFoleyComponent::GetEffectiveProfile() const
{
	if (!ProfileOverride.IsNull()) { return ProfileOverride; }
	const auto* Player = Cast<AProject_JPlayerCharacter>(GetOwner());
	const auto* Profile = Player ? Player->GetCharacterAnimProfile() : nullptr;
	return Profile ? Profile->FoleyAudioProfile : TSoftObjectPtr<UProject_JFoleyAudioProfile>();
}

bool UProject_JFoleyComponent::CanPresent(EProject_JFoleyGroup Group) const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UWorld* World = GetWorld();
	if (!bEnabled || !IsActive() || !Character || !World || !World->IsGameWorld() ||
		World->bIsTearingDown || World->GetNetMode() == NM_DedicatedServer || Character->IsActorBeingDestroyed()) { return false; }
	if (const auto* Base = Cast<AProject_JBaseCharacter>(Character);
		Base && IProject_JCombatInterface::Execute_IsDead(const_cast<AProject_JBaseCharacter*>(Base))) { return false; }
	if (const auto* Mount = Character->FindComponentByClass<UProject_JMountComponent>(); Mount && Mount->IsMounted()) { return false; }
	if (Group == EProject_JFoleyGroup::Footstep || Group == EProject_JFoleyGroup::Scuff)
	{
		const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		if (!Movement || !Movement->IsMovingOnGround()) { return false; }
	}
	return true;
}

bool UProject_JFoleyComponent::PlayFoleyEvent(const FProject_JFoleyEvent& Event)
{
	if (!CanPresent(EProject_JFoleyGroup::Other)) { return false; }
	if (auto* Service = GetWorld()->GetSubsystem<UProject_JFoleySubsystem>()) { return Service->Submit(this, Event); }
	return false;
}

bool UProject_JFoleyComponent::PrepareLocalAudio()
{
	if (!CanPresent(EProject_JFoleyGroup::Other)) { return false; }
	if (auto* Service = GetWorld()->GetSubsystem<UProject_JFoleySubsystem>()) { return Service->PrepareLocalAudio(this); }
	return false;
}

void UProject_JFoleyComponent::HandleAnimNotify(USkeletalMeshComponent* Mesh, const FProject_JFoleyEvent& Event)
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	// Retarget followers and modular clothes cannot create a second contact stream.
	if (!Character || Mesh != Character->GetMesh()) { return; }
	if (bUseMovementEventsForJumpAndLand && (Event.Event == Project_J::Foley::Jump || Event.Event == Project_J::Foley::Land)) { return; }
	PlayFoleyEvent(Event);
}

void UProject_JFoleyComponent::PlayJump(float HorizontalSpeed, float AgeSeconds)
{
	if (!bUseMovementEventsForJumpAndLand || !FMath::IsFinite(HorizontalSpeed)) { return; }
	FProject_JFoleyEvent Event;
	Event.Event = Project_J::Foley::Jump;
	Event.VolumeMultiplier = FMath::GetMappedRangeValueClamped(FVector2D(0.f, 500.f), FVector2D(0.5f, 1.f), HorizontalSpeed);
	Event.AgeSeconds = AgeSeconds;
	PlayFoleyEvent(Event);
}

void UProject_JFoleyComponent::PlayLanding(float ImpactSpeed, float AgeSeconds)
{
	if (!bUseMovementEventsForJumpAndLand || !FMath::IsFinite(ImpactSpeed)) { return; }
	FProject_JFoleyEvent Event;
	Event.Event = Project_J::Foley::Land;
	Event.VolumeMultiplier = FMath::GetMappedRangeValueClamped(FVector2D(500.f, 900.f), FVector2D(0.5f, 1.5f), FMath::Abs(ImpactSpeed));
	Event.AgeSeconds = AgeSeconds;
	PlayFoleyEvent(Event);
}

FVector UProject_JFoleyComponent::ResolveContactLocation(const UProject_JFoleyAudioProfile& Profile, EProject_JFoleySide Side,
	const FProject_JFoleySoundSet* Definition) const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character) { return FVector::ZeroVector; }
	const EProject_JFoleyContact Contact = Definition ? Definition->Contact : EProject_JFoleyContact::Foot;
	const bool bHand = Contact == EProject_JFoleyContact::Hand;
	const bool bSocket = Contact == EProject_JFoleyContact::Socket;
	if (bSocket || ((Contact == EProject_JFoleyContact::Foot || bHand) && Side != EProject_JFoleySide::None))
	{
		const bool bLeft = Side == EProject_JFoleySide::Left;
		const FName SourceBone = bSocket ? Definition->ContactSocket :
			(bHand ? (bLeft ? Profile.SourceLeftHand : Profile.SourceRightHand) : (bLeft ? Profile.SourceLeftFoot : Profile.SourceRightFoot));
		const FName VisualBone = bSocket ? SourceBone :
			(bHand ? (bLeft ? Profile.VisualLeftHand : Profile.VisualRightHand) : (bLeft ? Profile.VisualLeftFoot : Profile.VisualRightFoot));
		USkeletalMeshComponent* Visual = Project_J::Animation::FindVisualFollower(*Character);
		const FName EffectiveVisualBone = VisualBone.IsNone() ? SourceBone : VisualBone;
		if (Visual && Visual->IsRegistered() && Visual->WasRecentlyRendered(0.2f) && Visual->DoesSocketExist(EffectiveVisualBone))
		{
			return Visual->GetSocketLocation(EffectiveVisualBone);
		}
		const USkeletalMeshComponent* Source = Character->GetMesh();
		if (Source && Source->IsRegistered() && Source->WasRecentlyRendered(0.2f) && Source->DoesSocketExist(SourceBone))
		{
			return Source->GetSocketLocation(SourceBone);
		}
	}
	// Offscreen/URO poses can be stale. Use a current movement-space contact without waking all bones.
	return Character->GetActorLocation() - Character->GetActorUpVector() * Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
}

void UProject_JFoleyComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (auto* Service = GetWorld() ? GetWorld()->GetSubsystem<UProject_JFoleySubsystem>() : nullptr) { Service->Cancel(this); }
	for (double& Time : LastAdmission) { Time = -DBL_MAX; }
	Super::EndPlay(EndPlayReason);
}
