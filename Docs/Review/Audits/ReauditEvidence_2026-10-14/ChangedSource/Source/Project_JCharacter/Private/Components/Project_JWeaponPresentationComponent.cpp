#include "Components/Project_JWeaponPresentationComponent.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JCharacterAnimProfile.h"
#include "Animation/Project_JHandContact.h"
#include "Animation/Project_JPresentationMeshResolver.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/Project_JRetargetBodyProfile.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/Project_JCombatPresentationComponent.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Equipment/Project_JWeaponPresentationProfile.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JBaseCharacter.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "System/Project_JPresentationBudgetSubsystem.h"



#include "Components/Project_JWeaponPresentationDiagnostics.h"

DEFINE_LOG_CATEGORY(LogProjectJWeaponPresentation);

namespace Project_J::WeaponPresentation
{
	// Resolve rigid authoring data without consulting the previous mount's world
	// scale/pose. Even StaticMesh GetSocketTransform(RTS_Component) takes a world
	// round trip, which is unsuitable for calibrating the next attachment.
	bool ResolveRigidGripInRoot(const USceneComponent* GripComponent, FName GripName,
		const USceneComponent* Root, FTransform& OutGrip, FName& OutReason)
	{
		OutGrip = FTransform::Identity;
		if (!Root) { OutReason = TEXT("MissingWeaponRoot"); return false; }
		if (!GripComponent) { OutReason = TEXT("MissingWeaponGrip"); return false; }
		if (Cast<USkeletalMeshComponent>(GripComponent)) { OutReason = TEXT("SkeletalWeaponGrip"); return false; }
		if (GripComponent != Root && !GripComponent->IsAttachedTo(Root))
		{ OutReason = TEXT("GripOutsideWeaponRoot"); return false; }
		const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(GripComponent);
		const UStaticMeshSocket* Socket = Mesh ? Mesh->GetSocketByName(GripName) : nullptr;
		if (!Socket) { OutReason = TEXT("UnsupportedWeaponGripComponent"); return false; }
		FTransform Grip(Socket->RelativeRotation, Socket->RelativeLocation, Socket->RelativeScale);
		for (const USceneComponent* Child = GripComponent; Child != Root; Child = Child->GetAttachParent())
		{
			if (Child->GetAttachSocketName() != NAME_None) { OutReason = TEXT("SocketMountedWeaponChild"); return false; }
			if (!Project_J::Animation::IsPositiveUniformContactScale(Child->GetRelativeScale3D()))
			{ OutReason = TEXT("UnsupportedWeaponChildScale"); return false; }
			if (Child->IsUsingAbsoluteLocation() || Child->IsUsingAbsoluteRotation() || Child->IsUsingAbsoluteScale())
			{ OutReason = TEXT("AbsoluteWeaponChild"); return false; }
			Grip = Grip * Child->GetRelativeTransform();
		}
		OutGrip = Grip;
		return true;
	}

	static TAutoConsoleVariable<int32> CVarBudgetRemote(TEXT("ProjectJ.Presentation.BudgetRemote"), 1,
		TEXT("Budget non-combat remote weapon creation on GT; authority, local and draw-notify work stay immediate."));
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
	if (bRetryBudget && FPlatformTime::Seconds() >= NextBudgetRetry)
	{ bRetryBudget = false; RefreshPresentation(); }

	if (!SpawnedWeapon)
	{
		CancelContactRecovery();
		bIndependentMotionActive = false;
		bNotifyOwnsMotion = false;
		bAutoAttackMotionActive = false;
		ActiveMotionKeys.Reset();
		ActiveMotionDurationSeconds = 0.0f;
		ActiveEntryBlendSeconds = 0.0f;
		ActiveExitBlendSeconds = 0.0f;
		ActivePrimaryGripIKAlpha = 0.0f;
		ActiveSecondaryGripIKAlpha = 0.0f;
		LastMotionEvaluationFrame = MAX_uint64;
		ActiveMotionReturnMesh.Reset();
		ActiveMotionReturnSocket = NAME_None;
		GroundContactStateCount = 0;
		GroundContactNotifyTokens.Reset();
		GripTargets = FProject_JWeaponGripTargets();
		UpdateTickState();
		if (bRetryBudget) { SetComponentTickEnabled(true); }
		return;
	}
	RefreshAttackMotion();

	if (bIndependentMotionActive)
	{
		if (LastMotionEvaluationFrame != GFrameCounter)
		{
			UpdateIndependentMotion(DeltaTime);
			LastMotionEvaluationFrame = GFrameCounter;
		}
	}
	else
	{
		if (bContactRecoveryActive) { UpdateContactRecovery(GetWorld()->GetTimeSeconds()); }
		UpdateGripTargets();
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
	if (Project_J::WeaponPresentation::IsGripTraceEnabled())
	{
		SampleGripTrace();
	}
	else
	{
		bGripTraceHasPreviousSample = false;
		GripTraceNextSampleTime = 0.0;
		AttachmentTraceNextSampleTime = 0.0;
	}

	UpdateTickState();
}

void UProject_JWeaponPresentationComponent::BeginPlay()
{
	bEndingPlay = false;
	Super::BeginPlay();
}

bool UProject_JWeaponPresentationComponent::CanCreatePresentation() const
{
	return !bEndingPlay && !IsBeingDestroyed() && IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed()
		&& GetWorld() && !GetWorld()->bIsTearingDown && GetWorld()->GetNetMode() != NM_DedicatedServer;
}

void UProject_JWeaponPresentationComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	bEndingPlay = true;
	DestroyWeaponPresentation();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UProject_JWeaponPresentationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	EndIndependentMotion();
	DestroyWeaponPresentation();
	Super::EndPlay(EndPlayReason);
}

void UProject_JWeaponPresentationComponent::EnterCombatPresentation()
{
	bCombatPresentationActive = true;
	UE_LOG(LogProjectJWeaponPresentation, Verbose, TEXT("EnterCombatPresentation: Owner=%s"), *GetNameSafe(GetOwner()));
	// The draw notify owns the exact hand-transfer frame.  Entering combat only
	// guarantees that an equipped weapon exists at its stable sheathed socket.
	if (!SpawnedWeapon)
	{
		RefreshPresentation();
	}
}

void UProject_JWeaponPresentationComponent::ExitCombatPresentation()
{
	bCombatPresentationActive = false;
	EndIndependentMotion();
	TwoHandGripRequests.Reset();
	WeaponPresentationDebugElapsedSeconds = 0.0f;
	AttachWeaponToSheathedSocket();
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
	if (!SpawnedWeapon)
	{
		RefreshPresentation();
	}

	if (PresentationProfile)
	{
		AttachWeaponToSocket(PresentationProfile->SheathedSocketName,
			PresentationProfile->VisualSheathedSocketName, TEXT("Sheathe"));
	}
	CurrentPresentationSocket = EProject_JWeaponPresentationSocket::Sheathed;
	UpdateGripTargets();
}

void UProject_JWeaponPresentationComponent::AttachWeaponToDrawnSocket()
{
	TGuardValue<bool> ImmediateGuard(bApplyingBudget, true); // Authored draw frame cannot wait in a cosmetic queue.
	const UProject_JWeaponPresentationProfile* PresentationProfile = GetCurrentPresentationProfile();
	if (!SpawnedWeapon)
	{
		RefreshPresentation();
	}

	if (PresentationProfile)
	{
		AttachWeaponToSocket(PresentationProfile->DrawnSocketName,
			PresentationProfile->VisualDrawnSocketName, TEXT("Draw"), true);
	}
	CurrentPresentationSocket = EProject_JWeaponPresentationSocket::Drawn;
	UpdateGripTargets();
}

void UProject_JWeaponPresentationComponent::SetWeaponPresentationSocket(EProject_JWeaponPresentationSocket Socket)
{
	if (Socket == EProject_JWeaponPresentationSocket::Drawn)
	{
		AttachWeaponToDrawnSocket();
	}
	else
	{
		AttachWeaponToSheathedSocket();
	}
}

void UProject_JWeaponPresentationComponent::RefreshPresentation()
{
	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_WeaponPresentation_Refresh);
	if (bRefreshingPresentation) { return; }
	TGuardValue<bool> RefreshGuard(bRefreshingPresentation, true);
	if (!CanCreatePresentation()) { DestroyWeaponPresentation(); return; }

	const UProject_JWeaponPresentationProfile* PresentationProfile = GetCurrentPresentationProfile();
	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Mesh = OwnerCharacter ? OwnerCharacter->GetMesh() : nullptr;
	if (!PresentationProfile)
	{
		// An empty weapon slot is a valid state, not a failed spawn.
		DestroyWeaponPresentation();
		return;
	}
	if (IsValid(SpawnedWeapon) && !SpawnedWeapon->IsActorBeingDestroyed() && Mesh
		&& AppliedProfile.Get() == PresentationProfile && AppliedActorClass.Get() == PresentationProfile->WeaponActorClass.Get()
		&& AppliedCharacterMesh.Get() == Mesh && AppliedSkeletalMesh.Get() == Mesh->GetSkeletalMeshAsset())
	{
		// Preserve notify-owned motion and weapon-local VFX on repeated callbacks.
		// A follower can become available after the weapon actor was spawned.
		if (!bIndependentMotionActive && !bContactRecoveryActive)
		{
			const bool bDrawn = CurrentPresentationSocket == EProject_JWeaponPresentationSocket::Drawn;
			const FName SourceSocket = bDrawn ? PresentationProfile->DrawnSocketName : PresentationProfile->SheathedSocketName;
			const FName VisualSocket = bDrawn ? PresentationProfile->VisualDrawnSocketName : PresentationProfile->VisualSheathedSocketName;
			const FProject_JResolvedWeaponAttachment Desired = ResolveAttachment(bDrawn);
			bAttachmentRefreshRequested = false;
			USkeletalMeshComponent* DesiredMesh = Desired.Mesh.Get();
			if (DesiredMesh && SpawnedWeapon->GetRootComponent() &&
				(SpawnedWeapon->GetRootComponent()->GetAttachParent() != DesiredMesh ||
					SpawnedWeapon->GetRootComponent()->GetAttachSocketName() != Desired.Socket ||
					!SpawnedWeapon->GetRootComponent()->GetRelativeTransform().Equals(Desired.Relative) ||
					bPrimaryContactAttachment != Desired.bPrimaryContact))
			{
				AttachWeaponToSocket(SourceSocket, VisualSocket, TEXT("Refresh"), bDrawn);
				UpdateGripTargets();
			}
		}
		return;
	}
	if (SpawnedWeapon) { DestroyWeaponPresentation(); }
	if (!PresentationProfile->WeaponActorClass || !Mesh)
	{
		CancelBudgetedPresentation();
		UE_LOG(LogTemp, Warning, TEXT("[ProjectJ][WeaponPresentation] Refresh failed: Owner=%s Profile=%s ActorClass=%s Mesh=%s World=%s"),
			*GetNameSafe(OwnerCharacter),
			*GetNameSafe(PresentationProfile),
			PresentationProfile ? *GetNameSafe(PresentationProfile->WeaponActorClass) : TEXT("None"),
			*GetNameSafe(Mesh),
			*GetNameSafe(GetWorld()));
		return;
	}
	if (ShouldBudgetPresentation())
	{
		auto* Budget = GetWorld()->GetSubsystem<UProject_JPresentationBudgetSubsystem>();
		bRetryBudget = !Budget || !Budget->Request(this, ++PresentationRevision);
		if (bRetryBudget)
		{
			NextBudgetRetry = FPlatformTime::Seconds() + 0.1 + (GetUniqueID() % 17) * 0.003;
			SetComponentTickEnabled(true);
		}
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = OwnerCharacter;
	SpawnParams.Instigator = OwnerCharacter;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_WeaponPresentation_Spawn);
		SpawnedWeapon = GetWorld()->SpawnActor<AActor>(PresentationProfile->WeaponActorClass, OwnerCharacter->GetActorLocation(), OwnerCharacter->GetActorRotation(), SpawnParams);
	}
	if (!CanCreatePresentation()) { DestroyWeaponPresentation(); return; }
	if (SpawnedWeapon)
	{
		AppliedProfile = PresentationProfile;
		AppliedActorClass = PresentationProfile->WeaponActorClass.Get();
		AppliedCharacterMesh = Mesh;
		AppliedSkeletalMesh = Mesh->GetSkeletalMeshAsset();
		SetWeaponPresentationSocket(CurrentPresentationSocket);
		UpdateSocketComponentCache();
		UpdateGripTargets();
		NotifyWeaponTargetChanged(SpawnedWeapon->GetRootComponent());
		if (const UProject_JCombatPresentationComponent* CombatPresentation =
			OwnerCharacter->FindComponentByClass<UProject_JCombatPresentationComponent>())
		{
			SetActiveAttackPresentation(CombatPresentation->GetActiveAttackTag());
		}
		UE_LOG(LogTemp, Log, TEXT("[ProjectJ][WeaponPresentation] Spawn success: Weapon=%s SocketState=%d"),
			*GetNameSafe(SpawnedWeapon), static_cast<int32>(CurrentPresentationSocket));

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

void UProject_JWeaponPresentationComponent::RefreshAttachmentCalibration()
{
	check(IsInGameThread());
	InvalidateSocketComponentCache();
	bAttachmentRefreshRequested = true;
	if (!bIndependentMotionActive && !bContactRecoveryActive) { RefreshPresentation(); }
}

FProject_JResolvedWeaponAttachment UProject_JWeaponPresentationComponent::ResolveAttachment(bool bDrawn) const
{
	check(IsInGameThread());
	FProject_JResolvedWeaponAttachment Result;
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UProject_JWeaponPresentationProfile* Profile = GetCurrentPresentationProfile();
	if (!Character) { Result.Reason = TEXT("MissingOwner"); return Result; }
	if (!Profile) { Result.Reason = TEXT("MissingWeaponProfile"); return Result; }
	const FName SourceSocket = bDrawn ? Profile->DrawnSocketName : Profile->SheathedSocketName;
	const FName VisualSocket = bDrawn ? Profile->VisualDrawnSocketName : Profile->VisualSheathedSocketName;
	Result.Mesh = Project_J::Animation::ResolveWeaponAttachmentMesh(*Character, SourceSocket, VisualSocket,
		Profile->bPreferVisualFollowerSockets, Result.Socket);
	Result.Reason = bDrawn ? TEXT("ConfiguredSocket") : TEXT("SheathedSocket");
	if (!Result.IsValid()) { Result.Reason = TEXT("MissingAttachmentSocket"); }
	if (!bDrawn || Profile->DrawnAttachmentMode != EProject_JDrawnAttachmentMode::PrimaryGripContact) { return Result; }
	// Prefer the actual rendered body even when it has no legacy Visual socket.
	USkeletalMeshComponent* Body = Profile->bPreferVisualFollowerSockets
		? Project_J::Animation::FindVisualFollower(*Character) : Character->GetMesh();
	if (!Body) { Body = Character->GetMesh(); }
	const UProject_JRetargetAnimInstance* Anim = Body ? Cast<UProject_JRetargetAnimInstance>(Body->GetAnimInstance()) : nullptr;
	const FProject_JHandGripCalibration Calibration = Anim ? Anim->GetHandGripCalibration()
		: Project_J::Animation::ResolveHandGripCalibration(Character, nullptr);
	// Contact mounting requires a real Palm; wrist-origin compatibility is for old IK graphs only.
	const auto Contact = Body ? Project_J::Animation::ResolveHandContact(*Body,
		Calibration.PrimaryPalmSocketName, Calibration.PrimaryArm.Hand, false) : Project_J::Animation::FResolvedHandContact();
	const FName GripName = Profile->MotionPresentation.PrimaryGripSocketName;
	USceneComponent* GripComponent = FindWeaponSocketComponent(GripName);
	const USceneComponent* Root = SpawnedWeapon ? SpawnedWeapon->GetRootComponent() : nullptr;
	// Animated skeletal grip bones need explicit source-driven/custom mount ownership.
	// A fixed mount must not be calibrated from a moving weapon bone or solved hand world pose.
	if (!Contact.IsValid())
	{
		Result.Reason = Contact.Status == Project_J::Animation::EHandContactStatus::InvalidHand ? TEXT("InvalidPalmHand")
			: (Contact.Status == Project_J::Animation::EHandContactStatus::InvalidOffset ? TEXT("InvalidPalmTransform") : TEXT("MissingPalm"));
		return Result;
	}
	FTransform GripInRoot;
	if (!Project_J::WeaponPresentation::ResolveRigidGripInRoot(GripComponent, GripName, Root, GripInRoot, Result.Reason)) { return Result; }
	if (Root->IsUsingAbsoluteLocation() || Root->IsUsingAbsoluteRotation() || Root->IsUsingAbsoluteScale())
	{ Result.Reason = TEXT("AbsoluteWeaponRoot"); return Result; }
	if (!Project_J::Animation::IsPositiveUniformContactScale(Body->GetComponentScale()))
	{ Result.Reason = TEXT("UnsupportedBodyScale"); return Result; }
	// Root world scale belongs to the outgoing sheath/source/Visual mount, not
	// the authored weapon. The new root scale is normalized by the Snap policy.
	if (!Project_J::Animation::IsPositiveUniformContactScale(Body->GetBoneTransform(Contact.Hand, RTS_World).GetScale3D()))
	{ Result.Reason = TEXT("UnsupportedHandScale"); return Result; }
	FTransform RootInHand;
	if (!Project_J::Animation::MakePrimaryGripAttachment(GripInRoot, Contact.PalmInHand,
		Calibration.PrimaryHandOffset, RootInHand)) { Result.Reason = TEXT("InvalidContactTransform"); return Result; }
	Result.Mesh = Body;
	Result.Socket = Contact.Hand;
	Result.Relative = RootInHand;
	Result.bPrimaryContact = true;
	Result.Reason = TEXT("PrimaryGripContact");
	return Result;
}

bool UProject_JWeaponPresentationComponent::AttachWeaponToSocket(FName SourceSocketName, FName VisualSocketName,
	const TCHAR* Context, bool bDrawn)
{
	CancelContactRecovery();
	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	const UProject_JWeaponPresentationProfile* Profile = GetCurrentPresentationProfile();
	const FProject_JResolvedWeaponAttachment Attachment = ResolveAttachment(bDrawn);
	USkeletalMeshComponent* Mesh = Attachment.Mesh.Get();
	if (!SpawnedWeapon || !Mesh)
	{
		LogAttachmentTrace(TEXT("AttachFailed"), true, static_cast<int32>(bDrawn ? EProject_JWeaponPresentationSocket::Drawn : EProject_JWeaponPresentationSocket::Sheathed));
		UE_LOG(LogProjectJWeaponPresentation, Warning,
			TEXT("[ProjectJ][WeaponPresentation] %s failed: no presentation mesh owns source socket '%s' or visual socket '%s'. Owner=%s"),
			Context, *SourceSocketName.ToString(), *VisualSocketName.ToString(), *GetNameSafe(OwnerCharacter));
		return false;
	}

	if (bDrawn && Profile && Profile->DrawnAttachmentMode == EProject_JDrawnAttachmentMode::PrimaryGripContact && !Attachment.bPrimaryContact)
	{
		UE_LOG(LogProjectJWeaponPresentation, Warning,
			TEXT("[ProjectJ][WeaponAttachment] Palm mount unavailable; using compatibility socket. reason=%s Owner=%s Profile=%s"),
			*Attachment.Reason.ToString(), *GetNameSafe(OwnerCharacter), *GetNameSafe(Profile));
	}
	if (!SpawnedWeapon->AttachToComponent(Mesh, FAttachmentTransformRules::SnapToTargetIncludingScale, Attachment.Socket))
	{ LogAttachmentTrace(TEXT("AttachToComponentFailed"), true, static_cast<int32>(bDrawn ? EProject_JWeaponPresentationSocket::Drawn : EProject_JWeaponPresentationSocket::Sheathed)); return false; }
	SpawnedWeapon->GetRootComponent()->SetRelativeTransform(Attachment.Relative);
	bPrimaryContactAttachment = Attachment.bPrimaryContact;
	bAttachmentRefreshRequested = false;
	LogAttachmentTrace(TEXT("AttachmentResolved"), true, static_cast<int32>(bDrawn ? EProject_JWeaponPresentationSocket::Drawn : EProject_JWeaponPresentationSocket::Sheathed));
	return true;
}

void UProject_JWeaponPresentationComponent::DestroyWeaponPresentation()
{
	CancelBudgetedPresentation();
	InvalidateSocketComponentCache();
	AppliedProfile.Reset();
	AppliedActorClass.Reset();
	AppliedCharacterMesh.Reset();
	AppliedSkeletalMesh.Reset();
	EndIndependentMotion();
	CancelContactRecovery();
	bPrimaryContactAttachment = false;
	bAttachmentRefreshRequested = false;
	TwoHandGripRequests.Reset();
	if (SpawnedWeapon)
	{
		NotifyWeaponTargetChanged(nullptr);
		TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_WeaponPresentation_Destroy);
		AActor* PreviousWeapon = SpawnedWeapon;
		SpawnedWeapon = nullptr;
		PreviousWeapon->Destroy();
	}
	GripTargets = FProject_JWeaponGripTargets();
}

bool UProject_JWeaponPresentationComponent::ShouldBudgetPresentation() const
{
	if (bApplyingBudget || bCombatPresentationActive || bIndependentMotionActive) { return false; }
#if WITH_DEV_AUTOMATION_TESTS
	if (bForceBudgetForTest) { return true; }
#endif
	const auto* Pawn = Cast<APawn>(GetOwner());
	return Project_J::WeaponPresentation::CVarBudgetRemote.GetValueOnGameThread() != 0 && Pawn
		&& GetNetMode() == NM_Client && !Pawn->IsLocallyControlled();
}
void UProject_JWeaponPresentationComponent::CancelBudgetedPresentation()
{
	++PresentationRevision; bRetryBudget = false;
	if (auto* Budget = GetWorld() ? GetWorld()->GetSubsystem<UProject_JPresentationBudgetSubsystem>() : nullptr) { Budget->Cancel(this); }
}
void UProject_JWeaponPresentationComponent::ApplyBudgetedPresentation(uint64 Revision)
{
	if (Revision != PresentationRevision || !CanCreatePresentation()) { return; }
	TGuardValue<bool> Guard(bApplyingBudget, true); RefreshPresentation();
}

bool UProject_JWeaponPresentationComponent::BeginIndependentMotion(const TArray<FProject_JWeaponMotionKey>& MotionKeys, float PrimaryGripIKAlpha, float SecondaryGripIKAlpha, float MotionDurationSeconds, float EntryBlendSeconds, float ExitBlendSeconds)
{
	// No keys mean an identity offset: follow the authored source socket for
	// this whole state, which preserves the original montage weapon arc.
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
		if (bAutoAttackMotionActive && !bNotifyOwnsMotion)
		{
			// The montage state overrides the attack default without attaching the
			// weapon back to a hand between two source-driven segments.
			ActiveMotionEntryWorld = SpawnedWeapon->GetRootComponent()->GetComponentTransform();
			ActiveMotionKeys = MotionKeys;
			ActiveMotionNormalizedTime = 0.0f;
			ActiveMotionDurationSeconds = FMath::Max(MotionDurationSeconds, 0.0f);
			ActiveEntryBlendSeconds = FMath::Max(EntryBlendSeconds, 0.0f);
			ActiveExitBlendSeconds = FMath::Max(ExitBlendSeconds, 0.0f);
			ActivePrimaryGripIKAlpha = FMath::Clamp(PrimaryGripIKAlpha, 0.0f, 1.0f);
			ActiveSecondaryGripIKAlpha = FMath::Clamp(SecondaryGripIKAlpha, 0.0f, 1.0f);
			bAutoAttackMotionActive = false;
			bNotifyOwnsMotion = true;
			LastMotionEvaluationFrame = MAX_uint64;
			UpdateIndependentMotion(0.0f);
			LogGripTraceEvent(TEXT("NotifyOverridesAuto"));
		}
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

	ActiveMotionEntryWorld = WeaponRoot->GetComponentTransform();
	CancelContactRecovery(); // A new attack owns the current pose, including an interrupted recovery.
	// The authored motion remains relative to the source skeleton's drawn socket.
	// Capture the normal rendered attachment so the motion can enter and leave
	// at the visible hand even when its skeleton differs from the source.
	const FProject_JResolvedWeaponAttachment Return = ResolveAttachment(true);
	ActiveMotionReturnMesh = Return.Mesh;
	ActiveMotionReturnSocket = Return.Socket;
	ActiveMotionReturnRelative = Return.Relative;
	WeaponRoot->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	WeaponRoot->AttachToComponent(CharacterMesh, FAttachmentTransformRules::KeepWorldTransform, PresentationProfile->DrawnSocketName);
	SmoothedGroundCorrectionComponentSpace = FVector::ZeroVector;
	ActiveMotionKeys = MotionKeys;
	ActiveMotionNormalizedTime = 0.0f;
	ActiveMotionDurationSeconds = FMath::Max(MotionDurationSeconds, 0.0f);
	ActiveEntryBlendSeconds = FMath::Max(EntryBlendSeconds, 0.0f);
	ActiveExitBlendSeconds = FMath::Max(ExitBlendSeconds, 0.0f);
	ActivePrimaryGripIKAlpha = FMath::Clamp(PrimaryGripIKAlpha, 0.0f, 1.0f);
	ActiveSecondaryGripIKAlpha = FMath::Clamp(SecondaryGripIKAlpha, 0.0f, 1.0f);
	bIndependentMotionActive = true;
	bNotifyOwnsMotion = true;
	bAutoAttackMotionActive = false;
	LastMotionEvaluationFrame = MAX_uint64;
	UpdateIndependentMotion(0.0f);
	UpdateTickState();
	LogGripTraceEvent(TEXT("MotionBegin"));
	ActiveNotifyMotionToken = 0;
	return true;
}

uint64 UProject_JWeaponPresentationComponent::BeginNotifyIndependentMotion(
	const TArray<FProject_JWeaponMotionKey>& Keys, float PrimaryAlpha, float SecondaryAlpha,
	float Duration, float EntryBlend, float ExitBlend)
{
	if (!BeginIndependentMotion(Keys, PrimaryAlpha, SecondaryAlpha, Duration, EntryBlend, ExitBlend)) { return 0; }
	RefreshIndependentMotionKeys(Keys, PrimaryAlpha, SecondaryAlpha, Duration, EntryBlend, ExitBlend);
	ActiveNotifyMotionToken = ++NextNotifyMotionToken;
	return ActiveNotifyMotionToken;
}

bool UProject_JWeaponPresentationComponent::IsNotifyIndependentMotionCurrent(uint64 Token) const
{
	return Token != 0 && Token == ActiveNotifyMotionToken && bIndependentMotionActive && bNotifyOwnsMotion;
}

void UProject_JWeaponPresentationComponent::EndNotifyIndependentMotion(uint64 Token)
{
	if (IsNotifyIndependentMotionCurrent(Token)) { EndNotifyIndependentMotion(); }
}

void UProject_JWeaponPresentationComponent::EndNotifyIndependentMotion()
{
	ActiveNotifyMotionToken = 0;
	if (!bNotifyOwnsMotion)
	{
		return;
	}
	bNotifyOwnsMotion = false;
	float PrimaryAlpha = 0.0f;
	float SecondaryAlpha = 0.0f;
	if (GetAutoAttackMotionSettings(PrimaryAlpha, SecondaryAlpha))
	{
		const UProject_JWeaponPresentationProfile* Profile = GetCurrentPresentationProfile();
		const float EntrySeconds = Profile ? FMath::Max(0.0f, Profile->MotionPresentation.AttackEntryBlendSeconds) : 0.0f;
		ActiveMotionEntryWorld = SpawnedWeapon->GetRootComponent()->GetComponentTransform();
		ActiveMotionKeys.Reset();
		ActiveMotionNormalizedTime = 0.0f;
		ActiveMotionDurationSeconds = EntrySeconds;
		ActiveEntryBlendSeconds = EntrySeconds;
		ActiveExitBlendSeconds = 0.0f;
		ActivePrimaryGripIKAlpha = PrimaryAlpha;
		ActiveSecondaryGripIKAlpha = SecondaryAlpha;
		bAutoAttackMotionActive = true;
		LastMotionEvaluationFrame = MAX_uint64;
		UpdateIndependentMotion(0.0f);
		LogGripTraceEvent(TEXT("NotifyResumesAuto"));
		return;
	}
	EndIndependentMotion();
}

void UProject_JWeaponPresentationComponent::EndIndependentMotion()
{
	ActiveNotifyMotionToken = 0;
	GroundContactStateCount = 0;
	GroundContactNotifyTokens.Reset();
	if (!bIndependentMotionActive)
	{
		return;
	}
	LogGripTraceEvent(TEXT("MotionEndBeforeHandoff"));
	UpdateGripTargets();
	const FProject_JWeaponGripTargets ExitGrip = GripTargets;
	UAnimInstance* EndingSourceAnim = bAutoAttackMotionActive ? AutoAttackSourceAnim.Get() : nullptr;
	const FAnimMontageInstance* EndingMontage = EndingSourceAnim
		? EndingSourceAnim->GetMontageInstanceForID(AutoAttackSourceInstanceID) : nullptr;

	bIndependentMotionActive = false;
	bNotifyOwnsMotion = false;
	bAutoAttackMotionActive = false;
	ActiveMotionKeys.Reset();
	ActiveMotionNormalizedTime = 0.0f;
	ActiveMotionDurationSeconds = 0.0f;
	ActiveEntryBlendSeconds = 0.0f;
	ActiveExitBlendSeconds = 0.0f;
	ActivePrimaryGripIKAlpha = 0.0f;
	ActiveSecondaryGripIKAlpha = 0.0f;
	SmoothedGroundCorrectionComponentSpace = FVector::ZeroVector;
	LastMotionEvaluationFrame = MAX_uint64;
	USkeletalMeshComponent* ReturnMesh = ActiveMotionReturnMesh.Get();
	FName ReturnSocket = ActiveMotionReturnSocket;
	FTransform ReturnRelative = ActiveMotionReturnRelative;
	// Re-resolve anatomy at the ownership boundary: an equipment/body swap must
	// not finish against a stale mesh or profile captured by the previous attack.
	const FProject_JResolvedWeaponAttachment Return = ResolveAttachment(true);
	if (Return.IsValid())
	{
		ReturnMesh = Return.Mesh.Get();
		ReturnSocket = Return.Socket;
		ReturnRelative = Return.Relative;
	}
	bPrimaryContactAttachment = Return.bPrimaryContact;
	ActiveMotionReturnMesh.Reset();
	ActiveMotionReturnSocket = NAME_None;
	ActiveMotionReturnRelative = FTransform::Identity;
	GripTargets = FProject_JWeaponGripTargets();

	if (USceneComponent* WeaponRoot = SpawnedWeapon ? SpawnedWeapon->GetRootComponent() : nullptr)
	{
		WeaponRoot->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		const UProject_JWeaponPresentationProfile* Profile = GetCurrentPresentationProfile();
		const ACharacter* Character = Cast<ACharacter>(GetOwner());
		const bool bContactHandoff = !bEndingPlay && GetWorld() && Profile && Profile->MotionPresentation.bUseContactHandoff &&
			CurrentPresentationSocket == EProject_JWeaponPresentationSocket::Drawn && ReturnMesh &&
			Character && (ReturnMesh != Character->GetMesh() || bPrimaryContactAttachment) &&
			!ReturnSocket.IsNone() && ReturnMesh->DoesSocketExist(ReturnSocket) &&
			FMath::IsFinite(Profile->MotionPresentation.ContactRecoverySeconds) &&
			Profile->MotionPresentation.ContactRecoverySeconds > UE_KINDA_SMALL_NUMBER;
		if (bContactHandoff && WeaponRoot->AttachToComponent(ReturnMesh, FAttachmentTransformRules::KeepWorldTransform, ReturnSocket))
		{
			ContactRecoveryMesh = ReturnMesh;
			ContactRecoverySocket = ReturnSocket;
			ContactRecoveryAttachment = WeaponRoot->GetRelativeTransform();
			ContactRecoveryDestination = ReturnRelative;
			ContactRecoveryGripComponent = ExitGrip.PrimaryGripWorldTransform.GetRelativeTransform(ReturnMesh->GetComponentTransform());
			ContactRecoveryDurationSeconds = FMath::Clamp(Profile->MotionPresentation.ContactRecoverySeconds, 0.0f, 1.0f);
			ContactRecoveryStartSeconds = GetWorld()->GetTimeSeconds();
			ContactRecoveryAlpha = 0.0f;
			// Montage_IsActive becomes false at blend-out START, not when its pose
			// contribution reaches zero. Use that same instance and weight through
			// the return to Idle, including pauses and per-attack blend curves.
			if (Profile->MotionPresentation.bFollowMontageBlendOut && EndingMontage &&
				EndingMontage->IsStopped() && EndingMontage->GetWeight() > UE_KINDA_SMALL_NUMBER &&
				Character->GetMesh()->GetAnimInstance() == EndingSourceAnim &&
				Character->GetMesh()->DoesSocketExist(Profile->DrawnSocketName))
			{
				ContactRecoverySourceAnim = EndingSourceAnim;
				ContactRecoverySourceInstanceID = AutoAttackSourceInstanceID;
				ContactRecoverySourceStartWeight = EndingMontage->GetWeight();
				ContactRecoverySourceMesh = Character->GetMesh();
				ContactRecoverySourceSocket = Profile->DrawnSocketName;
				const FTransform SourceSocketWorld = Character->GetMesh()->GetSocketTransform(Profile->DrawnSocketName, RTS_World);
				ContactRecoveryWeaponInSourceSocket = WeaponRoot->GetComponentTransform().GetRelativeTransform(SourceSocketWorld);
				ContactRecoveryGripInSourceSocket = ExitGrip.PrimaryGripWorldTransform.GetRelativeTransform(SourceSocketWorld);
			}
			const UProject_JRetargetAnimInstance* Anim = Cast<UProject_JRetargetAnimInstance>(ReturnMesh->GetAnimInstance());
			ContactRecoveryPrimaryAlpha = ExitGrip.bHasPrimaryGrip
				? FMath::Clamp(Anim ? Anim->RightGripAlpha : ExitGrip.PrimaryIKAlpha, 0.0f, 1.0f) : 0.0f;
			ContactRecoverySecondaryAlpha = ExitGrip.bHasSecondaryGrip
				? FMath::Clamp(Anim ? Anim->LeftGripAlpha : ExitGrip.SecondaryIKAlpha, 0.0f, 1.0f) : 0.0f;
			bContactRecoveryActive = true;
			UpdateGripTargets();
			LogGripTraceEvent(TEXT("ContactRecoveryBegin"));
		}
		else
		{
			AttachWeaponToDrawnSocket();
		}
	}
	UpdateTickState();
	LogGripTraceEvent(TEXT("MotionEndAfterHandoff"));
}

void UProject_JWeaponPresentationComponent::CancelContactRecovery()
{
	bContactRecoveryActive = false;
	ContactRecoveryMesh.Reset();
	ContactRecoverySocket = NAME_None;
	ContactRecoveryAlpha = 0.0f;
	ContactRecoverySourceMesh.Reset();
	ContactRecoverySourceAnim.Reset();
	ContactRecoverySourceSocket = NAME_None;
	ContactRecoverySourceInstanceID = INDEX_NONE;
	ContactRecoverySourceStartWeight = 0.0f;
}

void UProject_JWeaponPresentationComponent::UpdateContactRecovery(double NowSeconds)
{
	USceneComponent* Root = SpawnedWeapon ? SpawnedWeapon->GetRootComponent() : nullptr;
	USkeletalMeshComponent* Mesh = ContactRecoveryMesh.Get();
	if (!bContactRecoveryActive) { return; }
	if (!Root || !Mesh || Root->GetAttachParent() != Mesh ||
		Root->GetAttachSocketName() != ContactRecoverySocket || !Mesh->DoesSocketExist(ContactRecoverySocket))
	{
		CancelContactRecovery();
		RefreshAttachmentCalibration();
		return;
	}
	// Use absolute cosmetic time: animation pull and late tick cannot advance twice;
	// skipped/URO updates still complete at the finite deadline.
	FTransform Relative;
	if (ContactRecoverySourceInstanceID != INDEX_NONE)
	{
		UAnimInstance* SourceAnim = ContactRecoverySourceAnim.Get();
		const FAnimMontageInstance* Instance = SourceAnim
			? SourceAnim->GetMontageInstanceForID(ContactRecoverySourceInstanceID) : nullptr;
		USkeletalMeshComponent* SourceMesh = ContactRecoverySourceMesh.Get();
		const bool bSourceValid = Instance && SourceMesh && SourceMesh->GetAnimInstance() == SourceAnim &&
			SourceMesh->DoesSocketExist(ContactRecoverySourceSocket);
		ContactRecoveryAlpha = FMath::Max(ContactRecoveryAlpha, bSourceValid
			? Project_J::WeaponMotion::EvaluateMontageContactRecoveryAlpha(Instance->GetWeight(), ContactRecoverySourceStartWeight)
			: 1.0f);
		if (bSourceValid)
		{
			const FTransform SourceWorld = ContactRecoveryWeaponInSourceSocket *
				SourceMesh->GetSocketTransform(ContactRecoverySourceSocket, RTS_World);
			// Recompute after the follower evaluation in the late tick as well.
			// The hand goal below only reads SourceMesh, so this introduces no
			// primary-hand -> weapon -> primary-hand feedback loop.
			Relative.Blend(SourceWorld.GetRelativeTransform(Mesh->GetSocketTransform(ContactRecoverySocket, RTS_World)),
				ContactRecoveryDestination, ContactRecoveryAlpha);
		}
		else { Relative = ContactRecoveryDestination; }
	}
	else
	{
		ContactRecoveryAlpha = Project_J::WeaponMotion::EvaluateContactRecoveryAlpha(
			NowSeconds - ContactRecoveryStartSeconds, ContactRecoveryDurationSeconds);
		Relative.Blend(ContactRecoveryAttachment, ContactRecoveryDestination, ContactRecoveryAlpha);
	}
	Root->SetRelativeTransform(Relative, false, nullptr, ETeleportType::TeleportPhysics);
	if (ContactRecoveryAlpha >= 1.0f)
	{
		CancelContactRecovery();
		if (bAttachmentRefreshRequested) { RefreshAttachmentCalibration(); }
		LogGripTraceEvent(TEXT("ContactRecoveryEnd"));
	}
}

void UProject_JWeaponPresentationComponent::SetIndependentMotionPosition(float NormalizedTime)
{
	if (bIndependentMotionActive)
	{
		ActiveMotionNormalizedTime = FMath::Clamp(NormalizedTime, 0.0f, 1.0f);
		LastMotionEvaluationFrame = MAX_uint64;
	}
}

void UProject_JWeaponPresentationComponent::RefreshIndependentMotionKeys(const TArray<FProject_JWeaponMotionKey>& MotionKeys, float PrimaryGripIKAlpha, float SecondaryGripIKAlpha, float MotionDurationSeconds, float EntryBlendSeconds, float ExitBlendSeconds)
{
	if (!bIndependentMotionActive)
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
		LastMotionEvaluationFrame = MAX_uint64;
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

uint64 UProject_JWeaponPresentationComponent::BeginGroundContactNotify()
{
	if (bEndingPlay || IsBeingDestroyed()) return 0;
	const uint64 Token = ++NextGroundContactNotifyToken;
	GroundContactNotifyTokens.Add(Token);
	return Token;
}

void UProject_JWeaponPresentationComponent::EndGroundContactNotify(uint64 Token)
{
	GroundContactNotifyTokens.Remove(Token);
}

void UProject_JWeaponPresentationComponent::BeginTwoHandGrip(float SecondaryIKAlpha, float PrimaryIKAlpha, bool bOverridePrimaryIK)
{
	BeginTwoHandGripNotify(INDEX_NONE, SecondaryIKAlpha, PrimaryIKAlpha, bOverridePrimaryIK);
}

void UProject_JWeaponPresentationComponent::EndTwoHandGrip()
{
	EndTwoHandGripNotify(INDEX_NONE);
}

void UProject_JWeaponPresentationComponent::BeginTwoHandGripNotify(int32 NotifyInstanceID, float SecondaryIKAlpha,
	float PrimaryIKAlpha, bool bOverridePrimaryIK)
{
	check(IsInGameThread());
	const int32 Existing = NotifyInstanceID == INDEX_NONE ? INDEX_NONE
		: TwoHandGripRequests.IndexOfByPredicate([NotifyInstanceID](const FTwoHandGripRequest& Request)
			{ return Request.NotifyInstanceID == NotifyInstanceID; });
	FTwoHandGripRequest& Request = Existing == INDEX_NONE ? TwoHandGripRequests.AddDefaulted_GetRef() : TwoHandGripRequests[Existing];
	Request.NotifyInstanceID = NotifyInstanceID;
	Request.SecondaryAlpha = FMath::IsFinite(SecondaryIKAlpha) ? FMath::Clamp(SecondaryIKAlpha, 0.0f, 1.0f) : 0.0f;
	Request.PrimaryAlpha = FMath::IsFinite(PrimaryIKAlpha) ? FMath::Clamp(PrimaryIKAlpha, 0.0f, 1.0f) : 0.0f;
	Request.bOverridePrimary = bOverridePrimaryIK;
	UpdateGripTargets();
	LogGripTraceEvent(TEXT("TwoHandGripBegin"));
}

void UProject_JWeaponPresentationComponent::EndTwoHandGripNotify(int32 NotifyInstanceID)
{
	check(IsInGameThread());
	const int32 Index = TwoHandGripRequests.FindLastByPredicate([NotifyInstanceID](const FTwoHandGripRequest& Request)
		{ return Request.NotifyInstanceID == NotifyInstanceID; });
	if (Index != INDEX_NONE)
	{
		TwoHandGripRequests.RemoveAt(Index);
		UpdateGripTargets();
		LogGripTraceEvent(TEXT("TwoHandGripEnd"));
	}
}

void UProject_JWeaponPresentationComponent::UpdateIndependentMotion(float DeltaTime)
{
	if (bAutoAttackMotionActive && ActiveMotionDurationSeconds > UE_KINDA_SMALL_NUMBER)
	{
		ActiveMotionNormalizedTime = FMath::Clamp(ActiveMotionNormalizedTime +
			FMath::Max(DeltaTime, 0.0f) / ActiveMotionDurationSeconds, 0.0f, 1.0f);
	}
	const UProject_JWeaponPresentationProfile* PresentationProfile = GetCurrentPresentationProfile();
	USceneComponent* WeaponRoot = SpawnedWeapon ? SpawnedWeapon->GetRootComponent() : nullptr;
	if (!PresentationProfile || !PresentationProfile->MotionPresentation.bSupportsIndependentMotion || !WeaponRoot)
	{
		EndIndependentMotion();
		return;
	}

	const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	const USkeletalMeshComponent* CharacterMesh = OwnerCharacter ? OwnerCharacter->GetMesh() : nullptr;
	if (!CharacterMesh || !CharacterMesh->DoesSocketExist(PresentationProfile->DrawnSocketName))
	{
		EndIndependentMotion();
		return;
	}
	const FTransform SourceSocketWorld = CharacterMesh->GetSocketTransform(PresentationProfile->DrawnSocketName, RTS_World);
	const bool bContactHandoff = PresentationProfile->MotionPresentation.bUseContactHandoff;
	const USkeletalMeshComponent* ReturnMesh = bContactHandoff ? nullptr : ActiveMotionReturnMesh.Get();
	const FTransform ReturnSocketWorld = ReturnMesh && !ActiveMotionReturnSocket.IsNone() && ReturnMesh->DoesSocketExist(ActiveMotionReturnSocket)
		? ActiveMotionReturnRelative * ReturnMesh->GetSocketTransform(ActiveMotionReturnSocket, RTS_World) : SourceSocketWorld;
	auto EvaluateWorld = [&](const FVector& Correction)
	{
		return bContactHandoff
			? Project_J::WeaponMotion::EvaluateContactDrivenWorldTransform(ActiveMotionKeys,
				ActiveMotionNormalizedTime, ActiveMotionDurationSeconds, ActiveEntryBlendSeconds,
				SourceSocketWorld, ActiveMotionEntryWorld, Correction)
			: Project_J::WeaponMotion::EvaluatePresentationWorldTransform(ActiveMotionKeys,
				ActiveMotionNormalizedTime, ActiveMotionDurationSeconds, ActiveEntryBlendSeconds, ActiveExitBlendSeconds,
				SourceSocketWorld, ReturnSocketWorld, Correction);
	};
	const FTransform UncorrectedWorld = EvaluateWorld(FVector::ZeroVector);
	WeaponRoot->SetRelativeTransform(UncorrectedWorld.GetRelativeTransform(SourceSocketWorld), false, nullptr, ETeleportType::TeleportPhysics);

	FVector GroundCorrectionComponentSpace = FVector::ZeroVector;
	if ((GroundContactStateCount > 0 || !GroundContactNotifyTokens.IsEmpty()) && TryGetGroundCorrection(DeltaTime, GroundCorrectionComponentSpace))
	{
		const FVector GroundCorrectionWorld = CharacterMesh->GetComponentTransform().TransformVectorNoScale(GroundCorrectionComponentSpace);
		const FTransform CorrectedWorld = EvaluateWorld(GroundCorrectionWorld);
		WeaponRoot->SetRelativeTransform(CorrectedWorld.GetRelativeTransform(SourceSocketWorld), false, nullptr, ETeleportType::TeleportPhysics);
	}

	UpdateGripTargets();
}

void UProject_JWeaponPresentationComponent::InvalidateSocketComponentCache()
{
	CachedSocketComponents.Reset();
	MissingSocketNames.Reset();
	CachedPrimaryGripSocket = NAME_None;
	CachedSecondaryGripSocket = NAME_None;
}

void UProject_JWeaponPresentationComponent::UpdateSocketComponentCache()
{
	InvalidateSocketComponentCache();

	if (!SpawnedWeapon)
	{
		return;
	}

	const UProject_JWeaponPresentationProfile* PresentationProfile = GetCurrentPresentationProfile();
	if (!PresentationProfile)
	{
		return;
	}

	const FProject_JWeaponMotionPresentation& Motion = PresentationProfile->MotionPresentation;
	CachedPrimaryGripSocket = Motion.PrimaryGripSocketName.IsNone() ? TEXT("WeaponGrip_R") : Motion.PrimaryGripSocketName;
	CachedSecondaryGripSocket = Motion.bEnableSecondaryGripContact
		? (Motion.SecondaryGripSocketName.IsNone() ? FName(TEXT("WeaponGrip_L")) : Motion.SecondaryGripSocketName) : NAME_None;

	FindWeaponSocketComponent(CachedPrimaryGripSocket);
	if (!CachedSecondaryGripSocket.IsNone()) { FindWeaponSocketComponent(CachedSecondaryGripSocket); }
}

void UProject_JWeaponPresentationComponent::UpdateGripTargets()
{
	if (!SpawnedWeapon)
	{
		return;
	}

	const UProject_JWeaponPresentationProfile* PresentationProfile = GetCurrentPresentationProfile();
	if (!PresentationProfile)
	{
		return;
	}

	const FProject_JWeaponMotionPresentation& Motion = PresentationProfile->MotionPresentation;
	const FName PrimarySocket = Motion.PrimaryGripSocketName.IsNone() ? TEXT("WeaponGrip_R") : Motion.PrimaryGripSocketName;
	const FName SecondarySocket = Motion.bEnableSecondaryGripContact
		? (Motion.SecondaryGripSocketName.IsNone() ? FName(TEXT("WeaponGrip_L")) : Motion.SecondaryGripSocketName) : NAME_None;
	if (CachedPrimaryGripSocket != PrimarySocket || CachedSecondaryGripSocket != SecondarySocket)
	{
		UpdateSocketComponentCache();
	}

	GripTargets.bHasPrimaryGrip = FindWeaponSocketTransform(PrimarySocket, GripTargets.PrimaryGripWorldTransform);
	GripTargets.SecondaryGripWorldTransform = FTransform::Identity;
	GripTargets.bHasSecondaryGrip = !SecondarySocket.IsNone() && FindWeaponSocketTransform(SecondarySocket, GripTargets.SecondaryGripWorldTransform);
	GripTargets.DriveMode = bIndependentMotionActive ? EProject_JWeaponGripDriveMode::AuthoredWeaponMotion
		: EProject_JWeaponGripDriveMode::BodySocket;
	GripTargets.bHasPrimaryHandSpaceGrip = false;
	GripTargets.bPrimaryIKSuppressedByAttachment = false;
	GripTargets.bContactRecovery = false;
	GripTargets.PrimaryHandBoneName = NAME_None;
	GripTargets.SecondaryGripInPrimaryHandSpace = FTransform::Identity;
	if (!bIndependentMotionActive && CurrentPresentationSocket == EProject_JWeaponPresentationSocket::Drawn)
	{
		const ACharacter* Character = Cast<ACharacter>(GetOwner());
		const USceneComponent* WeaponRoot = SpawnedWeapon->GetRootComponent();
		const USkeletalMeshComponent* AttachMesh = WeaponRoot
			? Cast<USkeletalMeshComponent>(WeaponRoot->GetAttachParent()) : nullptr;
		if (Character && AttachMesh && (AttachMesh != Character->GetMesh() || bPrimaryContactAttachment))
		{
			GripTargets.DriveMode = EProject_JWeaponGripDriveMode::PrimaryHand;
			const FName HandBone = AttachMesh->GetSocketBoneName(WeaponRoot->GetAttachSocketName());
			if (GripTargets.bHasSecondaryGrip && !HandBone.IsNone() && AttachMesh->GetBoneIndex(HandBone) != INDEX_NONE)
			{
				GripTargets.PrimaryHandBoneName = HandBone;
				GripTargets.SecondaryGripInPrimaryHandSpace = GripTargets.SecondaryGripWorldTransform.GetRelativeTransform(
					AttachMesh->GetBoneTransform(HandBone, RTS_World));
				GripTargets.bHasPrimaryHandSpaceGrip = true;
			}
		}
	}

	if (bIndependentMotionActive)
	{
		GripTargets.PrimaryIKAlpha = GripTargets.bHasPrimaryGrip ? ActivePrimaryGripIKAlpha : 0.0f;
		GripTargets.SecondaryIKAlpha = GripTargets.bHasSecondaryGrip ? ActiveSecondaryGripIKAlpha : 0.0f;
	}
	else
	{
		if (CurrentPresentationSocket == EProject_JWeaponPresentationSocket::Drawn)
		{
			GripTargets.PrimaryIKAlpha = GripTargets.bHasPrimaryGrip ? Motion.DefaultDrawnPrimaryIKAlpha : 0.0f;
			GripTargets.SecondaryIKAlpha = GripTargets.bHasSecondaryGrip ? Motion.DefaultDrawnSecondaryIKAlpha : 0.0f;
		}
		else
		{
			GripTargets.PrimaryIKAlpha = GripTargets.bHasPrimaryGrip ? Motion.DefaultSheathedPrimaryIKAlpha : 0.0f;
			GripTargets.SecondaryIKAlpha = GripTargets.bHasSecondaryGrip ? Motion.DefaultSheathedSecondaryIKAlpha : 0.0f;
		}
	}

	// Hand-contact windows are orthogonal to weapon trajectory ownership. Apply
	// the latest surviving window after the source/Motion/default base alphas.
	if (CurrentPresentationSocket == EProject_JWeaponPresentationSocket::Drawn && !TwoHandGripRequests.IsEmpty())
	{
		const FTwoHandGripRequest& Request = TwoHandGripRequests.Last();
		GripTargets.SecondaryIKAlpha = GripTargets.bHasSecondaryGrip ? Request.SecondaryAlpha : 0.0f;
		if (Request.bOverridePrimary) { GripTargets.PrimaryIKAlpha = GripTargets.bHasPrimaryGrip ? Request.PrimaryAlpha : 0.0f; }
	}

	// When the weapon is attached to the rendered hand, solving that same hand
	// back toward a socket on the weapon creates a feedback loop. Independent
	// weapon motion has its own authored target and remains unaffected.
	if (!bIndependentMotionActive && (bPrimaryContactAttachment || !Motion.bAllowPrimaryIKOnVisualAttachment) &&
		CurrentPresentationSocket == EProject_JWeaponPresentationSocket::Drawn)
	{
		const ACharacter* Character = Cast<ACharacter>(GetOwner());
		const USceneComponent* WeaponRoot = SpawnedWeapon->GetRootComponent();
		const USkeletalMeshComponent* AttachMesh = WeaponRoot
			? Cast<USkeletalMeshComponent>(WeaponRoot->GetAttachParent()) : nullptr;
		if (Character && AttachMesh && (AttachMesh != Character->GetMesh() || bPrimaryContactAttachment))
		{
			GripTargets.PrimaryIKAlpha = 0.0f;
			GripTargets.bPrimaryIKSuppressedByAttachment = true;
		}
	}
	if (bContactRecoveryActive && ContactRecoveryMesh.IsValid())
	{
		// Never sample the primary target from the weapon that follows this hand.
		GripTargets.PrimaryGripWorldTransform = ContactRecoveryGripComponent * ContactRecoveryMesh->GetComponentTransform();
		if (const USkeletalMeshComponent* SourceMesh = ContactRecoverySourceMesh.Get();
			ContactRecoverySourceInstanceID != INDEX_NONE && SourceMesh && SourceMesh->DoesSocketExist(ContactRecoverySourceSocket))
		{
			GripTargets.PrimaryGripWorldTransform = ContactRecoveryGripInSourceSocket *
				SourceMesh->GetSocketTransform(ContactRecoverySourceSocket, RTS_World);
		}
		GripTargets.PrimaryIKAlpha = ContactRecoveryPrimaryAlpha * (1.0f - ContactRecoveryAlpha);
		GripTargets.SecondaryIKAlpha = GripTargets.bHasSecondaryGrip
			? FMath::Lerp(ContactRecoverySecondaryAlpha, GripTargets.SecondaryIKAlpha, ContactRecoveryAlpha) : 0.0f;
		GripTargets.bPrimaryIKSuppressedByAttachment = false;
		GripTargets.bContactRecovery = true;
		GripTargets.DriveMode = EProject_JWeaponGripDriveMode::ContactRecovery;
	}
}

void UProject_JWeaponPresentationComponent::SetActiveAttackPresentation(const FGameplayTag AttackTag)
{
	if (ActiveAttackPresentationTag == AttackTag)
	{
		return;
	}
	ActiveAttackPresentationTag = AttackTag;
	LogGripTraceEvent(AttackTag.IsValid() ? TEXT("AttackSet") : TEXT("AttackCleared"));
	ResolvedAttackPresentationTag = FGameplayTag();
	ResolvedAttackStyle.Reset();
	ResolvedAttackDefinition.Reset();
	if (!AttackTag.IsValid() && bIndependentMotionActive)
	{
		EndIndependentMotion();
	}
	UpdateTickState();
}

bool UProject_JWeaponPresentationComponent::GetAutoAttackMotionSettings(float& OutPrimaryAlpha, float& OutSecondaryAlpha)
{
	if (!bCombatPresentationActive || !ActiveAttackPresentationTag.IsValid() ||
		CurrentPresentationSocket != EProject_JWeaponPresentationSocket::Drawn || !SpawnedWeapon)
	{
		return false;
	}
	const UProject_JWeaponPresentationProfile* Profile = GetCurrentPresentationProfile();
	const AProject_JPlayerCharacter* Player = Cast<AProject_JPlayerCharacter>(GetOwner());
	const AProject_JBaseCharacter* Base = Cast<AProject_JBaseCharacter>(GetOwner());
	const UProject_JCombatStyleDefinition* Style = Player ? Player->GetCombatStyleDefinition()
		: (PresentationCombatStyle ? PresentationCombatStyle.Get() : (Base ? Base->GetClassCombatStyleDefinition() : nullptr));
	if (!Profile || !Profile->MotionPresentation.bSupportsIndependentMotion || !Style)
	{
		return false;
	}
	if (ResolvedAttackPresentationTag != ActiveAttackPresentationTag || ResolvedAttackStyle.Get() != Style)
	{
		ResolvedAttackPresentationTag = ActiveAttackPresentationTag;
		ResolvedAttackStyle = Style;
		const UProject_JAttackSet* AttackSet = Style->GetRuntimeAttackSet();
		ResolvedAttackDefinition = AttackSet ? AttackSet->FindAttack(ActiveAttackPresentationTag) : nullptr;
	}
	const UProject_JAttackDefinition* Attack = ResolvedAttackDefinition.Get();
	if (!Attack || !Attack->bMontageDriven || !Attack->Montage ||
		Attack->WeaponDrive == EProject_JAttackWeaponDrive::VisualHand ||
		(Attack->WeaponDrive == EProject_JAttackWeaponDrive::WeaponDefault && !Profile->MotionPresentation.bSourceDrivenMontageAttacks))
	{
		return false;
	}
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	UAnimInstance* Anim = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (!Anim || !Anim->Montage_IsActive(Attack->Montage))
	{
		return false;
	}
	if (const FAnimMontageInstance* Instance = Anim->GetActiveInstanceForMontage(Attack->Montage))
	{
		AutoAttackSourceAnim = Anim;
		AutoAttackSourceInstanceID = Instance->GetInstanceID();
	}
	OutPrimaryAlpha = Attack->bOverrideGripIK ? FMath::Clamp(Attack->PrimaryGripIKAlpha, 0.0f, 1.0f)
		: Profile->MotionPresentation.DefaultAttackPrimaryIKAlpha;
	OutSecondaryAlpha = Attack->bOverrideGripIK ? FMath::Clamp(Attack->SecondaryGripIKAlpha, 0.0f, 1.0f)
		: Profile->MotionPresentation.DefaultAttackSecondaryIKAlpha;
	return true;
}

void UProject_JWeaponPresentationComponent::RefreshAttackMotion()
{
	if (bNotifyOwnsMotion)
	{
		return;
	}
	float PrimaryAlpha = 0.0f;
	float SecondaryAlpha = 0.0f;
	if (!GetAutoAttackMotionSettings(PrimaryAlpha, SecondaryAlpha))
	{
		if (bAutoAttackMotionActive)
		{
			EndIndependentMotion();
		}
		return;
	}
	if (!bAutoAttackMotionActive)
	{
		const UProject_JWeaponPresentationProfile* Profile = GetCurrentPresentationProfile();
		const float EntrySeconds = Profile ? FMath::Max(0.0f, Profile->MotionPresentation.AttackEntryBlendSeconds) : 0.0f;
		if (BeginIndependentMotion({}, PrimaryAlpha, SecondaryAlpha, EntrySeconds, EntrySeconds, 0.0f))
		{
			bNotifyOwnsMotion = false;
			bAutoAttackMotionActive = true;
			LogGripTraceEvent(TEXT("AutoAttackBegin"));
		}
	}
	else
	{
		ActivePrimaryGripIKAlpha = PrimaryAlpha;
		ActiveSecondaryGripIKAlpha = SecondaryAlpha;
	}
}

bool UProject_JWeaponPresentationComponent::FindWeaponSocketTransform(FName SocketName, FTransform& OutWorldTransform) const
{
	if (!SpawnedWeapon || SocketName.IsNone())
	{
		return false;
	}

	if (USceneComponent* Component = FindWeaponSocketComponent(SocketName))
	{
		OutWorldTransform = Component->GetSocketTransform(SocketName, RTS_World);
		return true;
	}
	return false;
}

USceneComponent* UProject_JWeaponPresentationComponent::FindWeaponSocketComponent(FName SocketName) const
{
	if (!SpawnedWeapon || SocketName.IsNone() || MissingSocketNames.Contains(SocketName))
	{
		return nullptr;
	}
	if (const TWeakObjectPtr<USceneComponent>* Cached = CachedSocketComponents.Find(SocketName))
	{
		if (USceneComponent* Component = Cached->Get())
		{
			return Component;
		}
		CachedSocketComponents.Remove(SocketName);
	}

	TInlineComponentArray<USceneComponent*> SceneComponents(SpawnedWeapon);
	SpawnedWeapon->GetComponents(SceneComponents);
	for (USceneComponent* Component : SceneComponents)
	{
		if (Component && Component->DoesSocketExist(SocketName))
		{
			CachedSocketComponents.Add(SocketName, Component);
			return Component;
		}
	}
	MissingSocketNames.Add(SocketName);
	return nullptr;
}

bool UProject_JWeaponPresentationComponent::GetWeaponSocketTransform(FName SocketName, FTransform& OutWorldTransform) const
{
	return FindWeaponSocketTransform(SocketName, OutWorldTransform);
}

USceneComponent* UProject_JWeaponPresentationComponent::GetWeaponVFXAttachmentComponent(const FName SocketName) const
{
	if (!SpawnedWeapon)
	{
		return nullptr;
	}

	if (USceneComponent* Component = FindWeaponSocketComponent(SocketName))
	{
		return Component;
	}

	return SpawnedWeapon->GetRootComponent();
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
	if (!Settings.bEnableGroundContact || !Settings.GetValidationFailure().IsNone() ||
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
		if (TraceStart.ContainsNaN() || TraceEnd.ContainsNaN()) { return false; }
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

void UProject_JWeaponPresentationComponent::UpdateTickState()
{
	SetComponentTickEnabled(CanCreatePresentation() && (bIndependentMotionActive || bContactRecoveryActive ||
		(SpawnedWeapon && ActiveAttackPresentationTag.IsValid()) ||
		(SpawnedWeapon && Project_J::WeaponPresentation::IsGripTraceEnabled()) ||
		(SpawnedWeapon && Project_J::WeaponPresentation::IsDebugEnabled())));
}

const UProject_JWeaponPresentationProfile* UProject_JWeaponPresentationComponent::GetCurrentPresentationProfile() const
{
	const AProject_JPlayerCharacter* PlayerCharacter = Cast<AProject_JPlayerCharacter>(GetOwner());
	// A player's replicated equipment configuration is the source of truth.
	// Other owners (including isolated presentation fixtures) may keep their
	// explicitly applied profile until a replacement is supplied.
	return PlayerCharacter ? PlayerCharacter->GetCurrentWeaponPresentationProfile()
		: (OwnerPresentationProfile ? OwnerPresentationProfile.Get() : AppliedProfile.Get());
}

FProject_JWeaponGripTargets UProject_JWeaponPresentationComponent::GetWeaponGripTargets()
{
	if (!bIndependentMotionActive && SpawnedWeapon)
	{
		UpdateGripTargets();
	}
	return GripTargets;
}

FProject_JWeaponGripTargets UProject_JWeaponPresentationComponent::GetWeaponGripTargetsForAnimation(float DeltaSeconds)
{
	check(IsInGameThread());
	if (bAttachmentRefreshRequested && !bIndependentMotionActive && !bContactRecoveryActive) { RefreshPresentation(); }
	// Turning diagnostics on in Idle must wake the existing late sample tick.
	if (Project_J::WeaponPresentation::IsGripTraceEnabled() && !IsComponentTickEnabled()) { UpdateTickState(); }
	RefreshAttackMotion();
	if (bIndependentMotionActive && LastMotionEvaluationFrame != GFrameCounter)
	{
		UpdateIndependentMotion(DeltaSeconds);
		LastMotionEvaluationFrame = GFrameCounter;
	}
	else if (!bIndependentMotionActive && SpawnedWeapon)
	{
		if (bContactRecoveryActive && GetWorld()) { UpdateContactRecovery(GetWorld()->GetTimeSeconds()); }
		UpdateGripTargets();
	}
	return GripTargets;
}

void UProject_JWeaponPresentationComponent::RegisterRetargetAnimInstance(UProject_JRetargetAnimInstance* InAnimInstance)
{
	if (InAnimInstance)
	{
		RegisteredRetargetAnimInstances.AddUnique(InAnimInstance);
		// NativeInitialize may precede assignment to Mesh->AnimScriptInstance.
		// Resolve the new body override on the next animation pull, not recursively here.
		bAttachmentRefreshRequested = true;

		if (SpawnedWeapon)
		{
			const UProject_JWeaponPresentationProfile* Profile = GetCurrentPresentationProfile();
			const FName PrimarySocket = Profile ? Profile->MotionPresentation.PrimaryGripSocketName : TEXT("WeaponGrip_R");
			InAnimInstance->UpdateWeaponTarget(SpawnedWeapon->GetRootComponent(), PrimarySocket);
		}
	}
}

void UProject_JWeaponPresentationComponent::UnregisterRetargetAnimInstance(UProject_JRetargetAnimInstance* InAnimInstance)
{
	RegisteredRetargetAnimInstances.Remove(InAnimInstance);
}

void UProject_JWeaponPresentationComponent::NotifyWeaponTargetChanged(USceneComponent* InWeaponComponent)
{
	const UProject_JWeaponPresentationProfile* Profile = GetCurrentPresentationProfile();
	const FName PrimarySocket = Profile ? Profile->MotionPresentation.PrimaryGripSocketName : TEXT("WeaponGrip_R");

	for (auto It = RegisteredRetargetAnimInstances.CreateIterator(); It; ++It)
	{
		if (UProject_JRetargetAnimInstance* AnimInst = It->Get())
		{
			AnimInst->UpdateWeaponTarget(InWeaponComponent, PrimarySocket);
		}
		else
		{
			It.RemoveCurrent();
		}
	}

	if (RegisteredRetargetAnimInstances.IsEmpty())
	{
		if (const ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
		{
			TInlineComponentArray<USkeletalMeshComponent*> SkeletalMeshes;
			OwnerChar->GetComponents(SkeletalMeshes);
			for (USkeletalMeshComponent* MeshComp : SkeletalMeshes)
			{
				if (MeshComp && MeshComp != OwnerChar->GetMesh())
				{
					if (UProject_JRetargetAnimInstance* RetargetInst = Cast<UProject_JRetargetAnimInstance>(MeshComp->GetAnimInstance()))
					{
						RegisteredRetargetAnimInstances.AddUnique(RetargetInst);
						RetargetInst->UpdateWeaponTarget(InWeaponComponent, PrimarySocket);
					}
				}
			}
		}
	}
}

