#include "Components/Project_JWeaponPresentationComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Equipment/Project_JWeaponPresentationProfile.h"
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
		SetComponentTickEnabled(false);
		return;
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

	if (!Project_J::WeaponPresentation::IsDebugEnabled())
	{
		SetComponentTickEnabled(false);
	}
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
	WeaponPresentationDebugElapsedSeconds = 0.0f;
	SetComponentTickEnabled(false);
	if (SpawnedWeapon)
	{
		SpawnedWeapon->Destroy();
		SpawnedWeapon = nullptr;
	}
}

void UProject_JWeaponPresentationComponent::BeginSheathePresentation()
{
	// Do not destroy yet: an authored montage notify will move this actor from
	// the hand to the back socket at the intended animation frame.
	bCombatPresentationActive = false;
}

void UProject_JWeaponPresentationComponent::AttachWeaponToSheathedSocket()
{
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
		SpawnedWeapon->AttachToComponent(Mesh, FAttachmentTransformRules::SnapToTargetIncludingScale, PresentationProfile->DrawnSocketName);
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
