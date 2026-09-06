#include "Components/Project_JWeaponPresentationComponent.h"

#include "Animation/AnimMontage.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Equipment/Project_JWeaponPresentationProfile.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "Project_JPlayerCharacter.h"
#include "PoseSearch/PoseSearchDatabase.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJWeaponPresentation, Log, All);

namespace Project_J::WeaponPresentation
{
	static TAutoConsoleVariable<int32> CVarDebug(
		TEXT("Project_J.Combat.WeaponPresentationDebug"),
		0,
		TEXT("Logs the runtime weapon attachment, socket and visual-mesh transforms every 0.5 seconds while a weapon is drawn. 0: off, 1: on."),
		ECVF_Default);

	static bool IsDebugEnabled()
	{
		return CVarDebug.GetValueOnGameThread() != 0;
	}

	static FString ToCompactTransformString(const FTransform& Transform)
	{
		const FVector Location = Transform.GetLocation();
		const FRotator Rotation = Transform.Rotator();
		return FString::Printf(TEXT("L=(%.1f,%.1f,%.1f) R=(%.1f,%.1f,%.1f) S=(%.2f,%.2f,%.2f)"),
			Location.X, Location.Y, Location.Z,
			Rotation.Pitch, Rotation.Yaw, Rotation.Roll,
			Transform.GetScale3D().X, Transform.GetScale3D().Y, Transform.GetScale3D().Z);
	}
}

UProject_JWeaponPresentationComponent::UProject_JWeaponPresentationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UProject_JWeaponPresentationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!SpawnedWeapon)
	{
		bIndependentMotionActive = false;
		ActiveMotionKeys.Reset();
		ActiveMotionDurationSeconds = 0.0f;
		ActiveEntryBlendSeconds = 0.0f;
		ActiveExitBlendSeconds = 0.0f;
		ActivePrimaryGripIKAlpha = 0.0f;
		ActiveSecondaryGripIKAlpha = 0.0f;
		GroundContactStateCount = 0;
		GripTargets = FProject_JWeaponGripTargets();
		UpdateTickState();
		return;
	}

	if (bIndependentMotionActive)
	{
		UpdateIndependentMotion(DeltaTime);
	}

	if (Project_J::WeaponPresentation::IsDebugEnabled())
	{
		WeaponPresentationDebugElapsedSeconds += DeltaTime;
		if (WeaponPresentationDebugElapsedSeconds >= 0.5f)
		{
			WeaponPresentationDebugElapsedSeconds = 0.0f;
			LogWeaponPresentationDebug(TEXT("Tick"));
		}
	}


	UpdateTickState();
}

void UProject_JWeaponPresentationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ExitCombatPresentation();
	Super::EndPlay(EndPlayReason);
}

void UProject_JWeaponPresentationComponent::EnterCombatPresentation()
{
	bCombatPresentationActive = true;
	UE_LOG(LogTemp, Log, TEXT("[ProjectJ][WeaponPresentation] EnterCombatPresentation: Owner=%s"), *GetNameSafe(GetOwner()));
	RefreshPresentation();
}

void UProject_JWeaponPresentationComponent::ExitCombatPresentation()
{
	bCombatPresentationActive = false;
	EndIndependentMotion();
	WeaponPresentationDebugElapsedSeconds = 0.0f;
	if (SpawnedWeapon)
	{
		SpawnedWeapon->Destroy();
		SpawnedWeapon = nullptr;
	}
	UpdateTickState();
}

void UProject_JWeaponPresentationComponent::BeginSheathePresentation()
{
	// Do not destroy yet: an authored montage notify will move this actor from
	// the hand to the back socket at the intended animation frame.
	bCombatPresentationActive = false;
	EndIndependentMotion();
}

void UProject_JWeaponPresentationComponent::AttachWeaponToSheathedSocket()
{
	EndIndependentMotion();

	const UProject_JWeaponPresentationProfile* PresentationProfile = GetCurrentPresentationProfile();
	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Mesh = OwnerCharacter ? OwnerCharacter->GetMesh() : nullptr;
	if (!SpawnedWeapon || !PresentationProfile || PresentationProfile->SheathedSocketName.IsNone() || !Mesh)
	{
		return;
	}

	if (!Mesh->DoesSocketExist(PresentationProfile->SheathedSocketName))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ProjectJ][WeaponPresentation] Sheathe failed: Socket '%s' does not exist on Mesh=%s."),
			*PresentationProfile->SheathedSocketName.ToString(), *GetNameSafe(Mesh));
		return;
	}

	SpawnedWeapon->AttachToComponent(Mesh, FAttachmentTransformRules::SnapToTargetIncludingScale, PresentationProfile->SheathedSocketName);
}

void UProject_JWeaponPresentationComponent::RefreshPresentation()
{
	if (!ShouldShowWeapon())
	{
		return;
	}
	if (SpawnedWeapon)
	{
		EndIndependentMotion();
		SpawnedWeapon->Destroy();
		SpawnedWeapon = nullptr;
	}

	const UProject_JWeaponPresentationProfile* PresentationProfile = GetCurrentPresentationProfile();
	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Mesh = OwnerCharacter ? OwnerCharacter->GetMesh() : nullptr;
	if (!PresentationProfile || !PresentationProfile->WeaponActorClass || PresentationProfile->DrawnSocketName.IsNone() || !Mesh || !GetWorld())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ProjectJ][WeaponPresentation] Draw failed: Owner=%s Profile=%s ActorClass=%s Socket=%s Mesh=%s World=%s"),
			*GetNameSafe(OwnerCharacter),
			*GetNameSafe(PresentationProfile),
			PresentationProfile ? *GetNameSafe(PresentationProfile->WeaponActorClass) : TEXT("None"),
			PresentationProfile ? *PresentationProfile->DrawnSocketName.ToString() : TEXT("None"),
			*GetNameSafe(Mesh),
			*GetNameSafe(GetWorld()));
		return;
	}

	if (!Mesh->DoesSocketExist(PresentationProfile->DrawnSocketName))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ProjectJ][WeaponPresentation] Draw failed: Socket '%s' does not exist on Mesh=%s (SkeletalMesh=%s)."),
			*PresentationProfile->DrawnSocketName.ToString(),
			*GetNameSafe(Mesh),
			*GetNameSafe(Mesh->GetSkeletalMeshAsset()));
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = OwnerCharacter;
	SpawnParams.Instigator = OwnerCharacter;
	SpawnedWeapon = GetWorld()->SpawnActor<AActor>(PresentationProfile->WeaponActorClass, OwnerCharacter->GetActorLocation(), OwnerCharacter->GetActorRotation(), SpawnParams);
	if (SpawnedWeapon)
	{
		AttachWeaponToDrawnSocket();
		UE_LOG(LogTemp, Log, TEXT("[ProjectJ][WeaponPresentation] Draw success: Weapon=%s Socket=%s"),
			*GetNameSafe(SpawnedWeapon), *PresentationProfile->DrawnSocketName.ToString());

		if (Project_J::WeaponPresentation::IsDebugEnabled())
		{
			WeaponPresentationDebugElapsedSeconds = 0.0f;
			SetComponentTickEnabled(true);
			LogWeaponPresentationDebug(TEXT("DrawAttach"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[ProjectJ][WeaponPresentation] Draw failed: SpawnActor returned null for class %s."),
			*GetNameSafe(PresentationProfile->WeaponActorClass));
	}
}

bool UProject_JWeaponPresentationComponent::BeginIndependentMotion(const TArray<FProject_JWeaponMotionKey>& MotionKeys, float PrimaryGripIKAlpha, float SecondaryGripIKAlpha, float MotionDurationSeconds, float EntryBlendSeconds, float ExitBlendSeconds)
{
	if (MotionKeys.Num() < 2)
	{
		UE_LOG(LogProjectJWeaponPresentation, Warning, TEXT("[ProjectJ][WeaponPresentation] Weapon Motion requires at least two ordered transform keys."));
		return false;
	}

	for (int32 Index = 1; Index < MotionKeys.Num(); ++Index)
	{
		if (MotionKeys[Index].NormalizedTime <= MotionKeys[Index - 1].NormalizedTime)
		{
			UE_LOG(LogProjectJWeaponPresentation, Warning, TEXT("[ProjectJ][WeaponPresentation] Weapon Motion keys must have unique, ascending NormalizedTime values."));
			return false;
		}
	}

	if (bIndependentMotionActive)
	{
		return true;
	}

	const UProject_JWeaponPresentationProfile* PresentationProfile = GetCurrentPresentationProfile();
	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* CharacterMesh = OwnerCharacter ? OwnerCharacter->GetMesh() : nullptr;
	USceneComponent* WeaponRoot = SpawnedWeapon ? SpawnedWeapon->GetRootComponent() : nullptr;
	if (!PresentationProfile || !PresentationProfile->MotionPresentation.bSupportsIndependentMotion || !OwnerCharacter || !CharacterMesh || !WeaponRoot ||
		OwnerCharacter->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}

	if (PresentationProfile->DrawnSocketName.IsNone() || !CharacterMesh->DoesSocketExist(PresentationProfile->DrawnSocketName))
	{
		UE_LOG(LogProjectJWeaponPresentation, Warning, TEXT("[ProjectJ][WeaponPresentation] Weapon Motion requires drawn socket '%s' on Mesh=%s."),
			*PresentationProfile->DrawnSocketName.ToString(), *GetNameSafe(CharacterMesh->GetSkeletalMeshAsset()));
		return false;
	}

	// WeaponRoot is already the dedicated visual pivot. Keep it directly attached
	// to the same socket used by Persona's preview asset, so a Montage key is
	// represented identically in editor and at runtime: RelativeTransform == Key.
	WeaponRoot->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	WeaponRoot->AttachToComponent(CharacterMesh, FAttachmentTransformRules::SnapToTargetIncludingScale, PresentationProfile->DrawnSocketName);
	WeaponRoot->SetRelativeTransform(FTransform::Identity, false, nullptr, ETeleportType::TeleportPhysics);
	SmoothedGroundCorrectionComponentSpace = FVector::ZeroVector;
	ActiveMotionKeys = MotionKeys;
	ActiveMotionNormalizedTime = 0.0f;
	ActiveMotionDurationSeconds = FMath::Max(MotionDurationSeconds, 0.0f);
	ActiveEntryBlendSeconds = FMath::Max(EntryBlendSeconds, 0.0f);
	ActiveExitBlendSeconds = FMath::Max(ExitBlendSeconds, 0.0f);
	ActivePrimaryGripIKAlpha = FMath::Clamp(PrimaryGripIKAlpha, 0.0f, 1.0f);
	ActiveSecondaryGripIKAlpha = FMath::Clamp(SecondaryGripIKAlpha, 0.0f, 1.0f);
	bIndependentMotionActive = true;
	UpdateGripTargets();
	UpdateTickState();
	return true;
}

void UProject_JWeaponPresentationComponent::EndIndependentMotion()
{
	if (!bIndependentMotionActive)
	{
		return;
	}

	bIndependentMotionActive = false;
	ActiveMotionKeys.Reset();
	ActiveMotionNormalizedTime = 0.0f;
	ActiveMotionDurationSeconds = 0.0f;
	ActiveEntryBlendSeconds = 0.0f;
	ActiveExitBlendSeconds = 0.0f;
	ActivePrimaryGripIKAlpha = 0.0f;
	ActiveSecondaryGripIKAlpha = 0.0f;
	GroundContactStateCount = 0;
	SmoothedGroundCorrectionComponentSpace = FVector::ZeroVector;
	GripTargets = FProject_JWeaponGripTargets();

	if (USceneComponent* WeaponRoot = SpawnedWeapon ? SpawnedWeapon->GetRootComponent() : nullptr)
	{
		WeaponRoot->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		AttachWeaponToDrawnSocket();
	}

	UpdateTickState();
}

void UProject_JWeaponPresentationComponent::SetIndependentMotionPosition(float NormalizedTime)
{
	if (bIndependentMotionActive)
	{
		ActiveMotionNormalizedTime = FMath::Clamp(NormalizedTime, 0.0f, 1.0f);
	}
}

void UProject_JWeaponPresentationComponent::RefreshIndependentMotionKeys(const TArray<FProject_JWeaponMotionKey>& MotionKeys, float PrimaryGripIKAlpha, float SecondaryGripIKAlpha, float MotionDurationSeconds, float EntryBlendSeconds, float ExitBlendSeconds)
{
	if (!bIndependentMotionActive || MotionKeys.Num() < 2)
	{
		return;
	}

	bool bKeysMatch = MotionKeys.Num() == ActiveMotionKeys.Num();
	for (int32 Index = 0; bKeysMatch && Index < MotionKeys.Num(); ++Index)
	{
		bKeysMatch = FMath::IsNearlyEqual(MotionKeys[Index].NormalizedTime, ActiveMotionKeys[Index].NormalizedTime) &&
			MotionKeys[Index].RelativeTransform.Equals(ActiveMotionKeys[Index].RelativeTransform);
	}

	const float ClampedPrimaryAlpha = FMath::Clamp(PrimaryGripIKAlpha, 0.0f, 1.0f);
	const float ClampedSecondaryAlpha = FMath::Clamp(SecondaryGripIKAlpha, 0.0f, 1.0f);
	const float ClampedDuration = FMath::Max(MotionDurationSeconds, 0.0f);
	const float ClampedEntryBlend = FMath::Max(EntryBlendSeconds, 0.0f);
	const float ClampedExitBlend = FMath::Max(ExitBlendSeconds, 0.0f);
	if (!bKeysMatch || !FMath::IsNearlyEqual(ClampedPrimaryAlpha, ActivePrimaryGripIKAlpha) || !FMath::IsNearlyEqual(ClampedSecondaryAlpha, ActiveSecondaryGripIKAlpha) ||
		!FMath::IsNearlyEqual(ClampedDuration, ActiveMotionDurationSeconds) || !FMath::IsNearlyEqual(ClampedEntryBlend, ActiveEntryBlendSeconds) || !FMath::IsNearlyEqual(ClampedExitBlend, ActiveExitBlendSeconds))
	{
		ActiveMotionKeys = MotionKeys;
		ActivePrimaryGripIKAlpha = ClampedPrimaryAlpha;
		ActiveSecondaryGripIKAlpha = ClampedSecondaryAlpha;
		ActiveMotionDurationSeconds = ClampedDuration;
		ActiveEntryBlendSeconds = ClampedEntryBlend;
		ActiveExitBlendSeconds = ClampedExitBlend;
	}
}

void UProject_JWeaponPresentationComponent::BeginGroundContact()
{
	// Notify states on separate Montage tracks can begin in either order.
	// Retain the count until the matching Weapon Motion state becomes active.
	++GroundContactStateCount;
}

void UProject_JWeaponPresentationComponent::EndGroundContact()
{
	GroundContactStateCount = FMath::Max(0, GroundContactStateCount - 1);
}

void UProject_JWeaponPresentationComponent::UpdateIndependentMotion(float DeltaTime)
{
	const UProject_JWeaponPresentationProfile* PresentationProfile = GetCurrentPresentationProfile();
	USceneComponent* WeaponRoot = SpawnedWeapon ? SpawnedWeapon->GetRootComponent() : nullptr;
	if (!PresentationProfile || !PresentationProfile->MotionPresentation.bSupportsIndependentMotion || !WeaponRoot || ActiveMotionKeys.Num() < 2)
	{
		EndIndependentMotion();
		return;
	}

	// Exactly matches Persona's attached StaticMesh preview: the authored key is
	// the weapon root's transform relative to the drawn socket.
	const float StateBlendAlpha = Project_J::WeaponMotion::EvaluateStateBlendAlpha(ActiveMotionNormalizedTime, ActiveMotionDurationSeconds, ActiveEntryBlendSeconds, ActiveExitBlendSeconds);
	FTransform DesiredRootRelativeTransform = Project_J::WeaponMotion::EvaluateStateTransform(ActiveMotionKeys, ActiveMotionNormalizedTime, ActiveMotionDurationSeconds, ActiveEntryBlendSeconds, ActiveExitBlendSeconds);
	WeaponRoot->SetRelativeTransform(DesiredRootRelativeTransform, false, nullptr, ETeleportType::TeleportPhysics);

	FVector GroundCorrectionComponentSpace = FVector::ZeroVector;
	if (GroundContactStateCount > 0 && TryGetGroundCorrection(DeltaTime, GroundCorrectionComponentSpace))
	{
		// Ground correction is calculated in character-mesh component space; the
		// root's relative location is socket space, so convert before adding it.
		const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
		const USkeletalMeshComponent* CharacterMesh = OwnerCharacter ? OwnerCharacter->GetMesh() : nullptr;
		const FName DrawnSocketName = WeaponRoot->GetAttachSocketName();
		const FTransform SocketComponentTransform = CharacterMesh && !DrawnSocketName.IsNone()
			? CharacterMesh->GetSocketTransform(DrawnSocketName, RTS_Component)
			: FTransform::Identity;
		DesiredRootRelativeTransform.AddToTranslation(SocketComponentTransform.InverseTransformVectorNoScale(GroundCorrectionComponentSpace * StateBlendAlpha));
		WeaponRoot->SetRelativeTransform(DesiredRootRelativeTransform, false, nullptr, ETeleportType::TeleportPhysics);
	}

	UpdateGripTargets();
}

void UProject_JWeaponPresentationComponent::UpdateGripTargets()
{
	GripTargets = FProject_JWeaponGripTargets();

	if (!bIndependentMotionActive)
	{
		return;
	}

	const UProject_JWeaponPresentationProfile* PresentationProfile = GetCurrentPresentationProfile();
	if (!PresentationProfile)
	{
		return;
	}

	const FProject_JWeaponMotionPresentation& Motion = PresentationProfile->MotionPresentation;
	GripTargets.bHasPrimaryGrip = FindWeaponSocketTransform(Motion.PrimaryGripSocketName, GripTargets.PrimaryGripWorldTransform);
	GripTargets.bHasSecondaryGrip = FindWeaponSocketTransform(Motion.SecondaryGripSocketName, GripTargets.SecondaryGripWorldTransform);
	GripTargets.PrimaryIKAlpha = GripTargets.bHasPrimaryGrip ? ActivePrimaryGripIKAlpha : 0.0f;
	GripTargets.SecondaryIKAlpha = GripTargets.bHasSecondaryGrip ? ActiveSecondaryGripIKAlpha : 0.0f;
}

bool UProject_JWeaponPresentationComponent::FindWeaponSocketTransform(FName SocketName, FTransform& OutWorldTransform) const
{
	if (!SpawnedWeapon || SocketName.IsNone())
	{
		return false;
	}

	TInlineComponentArray<USceneComponent*> SceneComponents(SpawnedWeapon);
	SpawnedWeapon->GetComponents(SceneComponents);
	for (const USceneComponent* Component : SceneComponents)
	{
		if (Component && Component->DoesSocketExist(SocketName))
		{
			OutWorldTransform = Component->GetSocketTransform(SocketName, RTS_World);
			return true;
		}
	}

	return false;
}

bool UProject_JWeaponPresentationComponent::GetWeaponSocketTransform(FName SocketName, FTransform& OutWorldTransform) const
{
	return FindWeaponSocketTransform(SocketName, OutWorldTransform);
}

bool UProject_JWeaponPresentationComponent::TryGetGroundCorrection(float DeltaTime, FVector& OutComponentSpaceCorrection)
{
	OutComponentSpaceCorrection = FVector::ZeroVector;
	const UProject_JWeaponPresentationProfile* PresentationProfile = GetCurrentPresentationProfile();
	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* CharacterMesh = OwnerCharacter ? OwnerCharacter->GetMesh() : nullptr;
	UWorld* World = GetWorld();
	if (!PresentationProfile || !CharacterMesh || !World)
	{
		return false;
	}

	const FProject_JWeaponGroundContactSettings& Settings = PresentationProfile->MotionPresentation.GroundContact;
	if (!Settings.bEnableGroundContact ||
		(Settings.bOnlyTraceWhenRecentlyRendered && !CharacterMesh->WasRecentlyRendered(Settings.RecentlyRenderedToleranceSeconds)))
	{
		return false;
	}

	FVector WorldCorrection = FVector::ZeroVector;
	const auto TryProbeCorrection = [this, OwnerCharacter, World, &Settings, &WorldCorrection](FName ProbeSocketName)
	{
		FTransform ProbeTransform;
		if (ProbeSocketName.IsNone() || !FindWeaponSocketTransform(ProbeSocketName, ProbeTransform))
		{
			return false;
		}

		const FVector ProbeLocation = ProbeTransform.GetLocation();
		const FVector TraceStart = ProbeLocation + FVector::UpVector * Settings.TraceStartHeight;
		const FVector TraceEnd = ProbeLocation - FVector::UpVector * Settings.TraceLength;
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(WeaponGroundContact), false, OwnerCharacter);
		QueryParams.AddIgnoredActor(SpawnedWeapon);

		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, Settings.TraceChannel, QueryParams))
		{
			WorldCorrection = Hit.ImpactPoint + Hit.ImpactNormal * Settings.SurfaceClearance - ProbeLocation;
			return true;
		}

		return false;
	};

	// A sword is a rigid visual: averaging two distant contacts pulls a slanted
	// blade away from its intended tip. Primary is the authored contact point;
	// secondary is a migration/fallback socket only.
	if (!TryProbeCorrection(Settings.PrimaryProbeSocketName) && !TryProbeCorrection(Settings.SecondaryProbeSocketName))
	{
		SmoothedGroundCorrectionComponentSpace = FVector::ZeroVector;
		return false;
	}

	WorldCorrection = WorldCorrection.GetClampedToMaxSize(Settings.MaxTranslationCorrection);

	const FVector TargetComponentSpaceCorrection = CharacterMesh->GetComponentTransform().InverseTransformVectorNoScale(WorldCorrection);
	SmoothedGroundCorrectionComponentSpace = FMath::VInterpTo(
		SmoothedGroundCorrectionComponentSpace,
		TargetComponentSpaceCorrection,
		DeltaTime,
		Settings.CorrectionInterpolationSpeed);
	OutComponentSpaceCorrection = SmoothedGroundCorrectionComponentSpace;
	return true;
}

void UProject_JWeaponPresentationComponent::AttachWeaponToDrawnSocket()
{
	const UProject_JWeaponPresentationProfile* PresentationProfile = GetCurrentPresentationProfile();
	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Mesh = OwnerCharacter ? OwnerCharacter->GetMesh() : nullptr;
	if (!SpawnedWeapon || !PresentationProfile || !Mesh || PresentationProfile->DrawnSocketName.IsNone())
	{
		return;
	}

	if (Mesh->DoesSocketExist(PresentationProfile->DrawnSocketName))
	{
		SpawnedWeapon->AttachToComponent(Mesh, FAttachmentTransformRules::SnapToTargetIncludingScale, PresentationProfile->DrawnSocketName);
	}
}

void UProject_JWeaponPresentationComponent::UpdateTickState()
{
	SetComponentTickEnabled(bIndependentMotionActive || (SpawnedWeapon && Project_J::WeaponPresentation::IsDebugEnabled()));
}

const UProject_JWeaponPresentationProfile* UProject_JWeaponPresentationComponent::GetCurrentPresentationProfile() const
{
	const AProject_JPlayerCharacter* PlayerCharacter = Cast<AProject_JPlayerCharacter>(GetOwner());
	return PlayerCharacter ? PlayerCharacter->GetCurrentWeaponPresentationProfile() : nullptr;
}

bool UProject_JWeaponPresentationComponent::ShouldShowWeapon() const
{
	return bCombatPresentationActive;
}

void UProject_JWeaponPresentationComponent::LogWeaponPresentationDebug(const TCHAR* Context) const
{
	const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	const USkeletalMeshComponent* CharacterMesh = OwnerCharacter ? OwnerCharacter->GetMesh() : nullptr;
	const USceneComponent* WeaponRoot = SpawnedWeapon ? SpawnedWeapon->GetRootComponent() : nullptr;
	const FName AttachedSocketName = WeaponRoot ? WeaponRoot->GetAttachSocketName() : NAME_None;
	const USceneComponent* AttachParent = WeaponRoot ? WeaponRoot->GetAttachParent() : nullptr;
	const FTransform SocketComponentTransform = CharacterMesh && !AttachedSocketName.IsNone() && CharacterMesh->DoesSocketExist(AttachedSocketName)
		? CharacterMesh->GetSocketTransform(AttachedSocketName, RTS_Component)
		: FTransform::Identity;
	const FTransform RightHandComponentTransform = CharacterMesh
		? CharacterMesh->GetBoneTransform(TEXT("hand_r"), RTS_Component)
		: FTransform::Identity;
	const FTransform RightIKHandComponentTransform = CharacterMesh
		? CharacterMesh->GetBoneTransform(TEXT("ik_hand_r"), RTS_Component)
		: FTransform::Identity;
	const float HandToIKDistance = FVector::Distance(RightHandComponentTransform.GetLocation(), RightIKHandComponentTransform.GetLocation());
	const UAnimInstance* AnimInstance = CharacterMesh ? CharacterMesh->GetAnimInstance() : nullptr;
	const UProject_JCharacterAnimInstance* ProjectAnimInstance = Cast<UProject_JCharacterAnimInstance>(AnimInstance);
	const UAnimMontage* ActiveMontage = AnimInstance ? AnimInstance->GetCurrentActiveMontage() : nullptr;
	const float MontagePosition = (AnimInstance && ActiveMontage) ? AnimInstance->Montage_GetPosition(ActiveMontage) : -1.0f;
	const UPoseSearchDatabase* ActivePoseSearchDatabase = ProjectAnimInstance ? ProjectAnimInstance->GetCurrentActivePoseSearchDatabaseThreadSafe() : nullptr;
	const float FullBodyMontageWeight = ProjectAnimInstance ? ProjectAnimInstance->GetThreadSafeFullBodyMontageWeight() : 0.0f;
	const bool bCombatMode = ProjectAnimInstance ? ProjectAnimInstance->GetThreadSafeIsCombatMode() : false;
	const TCHAR* CombatPresentation = !ProjectAnimInstance
		? TEXT("Unknown")
		: (ProjectAnimInstance->GetThreadSafeUsesFullBodyCombatLocomotion() ? TEXT("FullBody") : TEXT("UpperBodyOverlay"));

	UE_LOG(LogProjectJWeaponPresentation, Warning,
		TEXT("[%s][Pose] HandToIK=%.1f | HandRCS=%s | IKHandRCS=%s | Montage=%s Time=%.3f Weight=%.2f Combat=%d Presentation=%s PSD=%s"),
		Context,
		HandToIKDistance,
		*Project_J::WeaponPresentation::ToCompactTransformString(RightHandComponentTransform),
		*Project_J::WeaponPresentation::ToCompactTransformString(RightIKHandComponentTransform),
		*GetNameSafe(ActiveMontage),
		MontagePosition,
		FullBodyMontageWeight,
		bCombatMode ? 1 : 0,
		CombatPresentation,
		*GetNameSafe(ActivePoseSearchDatabase));

	UE_LOG(LogProjectJWeaponPresentation, Warning,
		TEXT("[%s][Attach] Owner=%s Mesh=%s Weapon=%s Class=%s Root=%s Parent=%s ParentIsMesh=%d Socket=%s Bone=%s | SocketCS=%s | RootRel=%s | RootWorld=%s"),
		Context,
		*GetNameSafe(OwnerCharacter),
		CharacterMesh ? *GetNameSafe(CharacterMesh->GetSkeletalMeshAsset()) : TEXT("None"),
		*GetNameSafe(SpawnedWeapon),
		SpawnedWeapon ? *GetNameSafe(SpawnedWeapon->GetClass()) : TEXT("None"),
		*GetNameSafe(WeaponRoot),
		*GetNameSafe(AttachParent),
		AttachParent == CharacterMesh ? 1 : 0,
		*AttachedSocketName.ToString(),
		CharacterMesh && !AttachedSocketName.IsNone() ? *CharacterMesh->GetSocketBoneName(AttachedSocketName).ToString() : TEXT("None"),
		*Project_J::WeaponPresentation::ToCompactTransformString(SocketComponentTransform),
		WeaponRoot ? *Project_J::WeaponPresentation::ToCompactTransformString(WeaponRoot->GetRelativeTransform()) : TEXT("None"),
		WeaponRoot ? *Project_J::WeaponPresentation::ToCompactTransformString(WeaponRoot->GetComponentTransform()) : TEXT("None"));

	if (!SpawnedWeapon)
	{
		return;
	}

	TInlineComponentArray<UStaticMeshComponent*> StaticMeshComponents;
	SpawnedWeapon->GetComponents(StaticMeshComponents);
	for (const UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
	{
		UE_LOG(LogProjectJWeaponPresentation, Warning,
			TEXT("[%s] VisualMesh Component=%s Asset=%s Parent=%s | Relative=%s | World=%s"),
			Context,
			*GetNameSafe(StaticMeshComponent),
			StaticMeshComponent ? *GetNameSafe(StaticMeshComponent->GetStaticMesh()) : TEXT("None"),
			StaticMeshComponent ? *GetNameSafe(StaticMeshComponent->GetAttachParent()) : TEXT("None"),
			StaticMeshComponent ? *Project_J::WeaponPresentation::ToCompactTransformString(StaticMeshComponent->GetRelativeTransform()) : TEXT("None"),
			StaticMeshComponent ? *Project_J::WeaponPresentation::ToCompactTransformString(StaticMeshComponent->GetComponentTransform()) : TEXT("None"));
	}
}
