#include "Components/Project_JWeaponPresentationComponent.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JCharacterAnimProfile.h"
#include "Animation/Project_JHandContact.h"
#include "Animation/Project_JPresentationMeshResolver.h"
#include "Animation/Project_JRetargetAnimInstance.h"
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

DEFINE_LOG_CATEGORY_STATIC(LogProjectJWeaponPresentation, Log, All);

namespace Project_J::WeaponPresentation
{
	// Resolve rigid authoring data without consulting the previous mount's world
	// scale/pose. Even StaticMesh GetSocketTransform(RTS_Component) takes a world
	// round trip, which is unsuitable for calibrating the next attachment.
	static bool ResolveRigidGripInRoot(const USceneComponent* GripComponent, FName GripName,
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

	static FString ToContactScaleString(const FVector& Scale)
	{
		return FString::Printf(TEXT("(%.9f,%.9f,%.9f) uniform=%d"), Scale.X, Scale.Y, Scale.Z,
			Project_J::Animation::IsPositiveUniformContactScale(Scale));
	}

	static TAutoConsoleVariable<int32> CVarBudgetRemote(TEXT("ProjectJ.Presentation.BudgetRemote"), 1,
		TEXT("Budget non-combat remote weapon creation on GT; authority, local and draw-notify work stay immediate."));
	static TAutoConsoleVariable<int32> CVarDebug(
		TEXT("Project_J.Combat.WeaponPresentationDebug"),
		0,
		TEXT("Logs the runtime weapon attachment, socket and visual-mesh transforms every 0.5 seconds while a weapon is drawn. 0: off, 1: on."),
		ECVF_Default);
	static TAutoConsoleVariable<int32> CVarGripTrace(TEXT("ProjectJ.Presentation.GripTrace"), 0,
		TEXT("Record the local character's post-animation sword, grip and right-elbow trajectory in Project_J.log. 0: off, 1: on."));
	static TAutoConsoleVariable<float> CVarGripTraceHz(TEXT("ProjectJ.Presentation.GripTraceHz"), 30.0f,
		TEXT("Maximum grip trace samples per second while enabled (1-120)."));
	static TAutoConsoleVariable<FString> CVarGripTraceActor(TEXT("ProjectJ.Presentation.GripTraceActor"), FString(),
		TEXT("Optional actor-name substring. Empty traces only locally controlled characters."));

	static bool ShouldTraceGrip(const ACharacter* Character)
	{
		if (!Character || CVarGripTrace.GetValueOnGameThread() == 0)
		{
			return false;
		}
		const FString Filter = CVarGripTraceActor.GetValueOnGameThread();
		return Filter.IsEmpty() ? Character->IsLocallyControlled()
			: Character->GetName().Contains(Filter, ESearchCase::IgnoreCase);
	}

	static const TCHAR* GripTraceMode(bool bIndependent, bool bNotify, bool bAuto,
		EProject_JWeaponPresentationSocket Socket, bool bRecovery)
	{
		if (bIndependent) { return bNotify ? TEXT("NotifySource") : (bAuto ? TEXT("AutoSource") : TEXT("Source")); }
		if (bRecovery) { return TEXT("ContactRecovery"); }
		return Socket == EProject_JWeaponPresentationSocket::Drawn ? TEXT("HandAttached") : TEXT("Sheathed");
	}

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
	if (Project_J::WeaponPresentation::CVarGripTrace.GetValueOnGameThread() != 0)
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
	return true;
}

void UProject_JWeaponPresentationComponent::EndNotifyIndependentMotion()
{
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
	GroundContactStateCount = 0;
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
	if (GroundContactStateCount > 0 && TryGetGroundCorrection(DeltaTime, GroundCorrectionComponentSpace))
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

void UProject_JWeaponPresentationComponent::UpdateTickState()
{
	SetComponentTickEnabled(CanCreatePresentation() && (bIndependentMotionActive || bContactRecoveryActive ||
		(SpawnedWeapon && ActiveAttackPresentationTag.IsValid()) ||
		(SpawnedWeapon && Project_J::WeaponPresentation::CVarGripTrace.GetValueOnGameThread() != 0) ||
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

bool UProject_JWeaponPresentationComponent::ShouldShowWeapon() const
{
	return GetCurrentPresentationProfile() != nullptr;
}

void UProject_JWeaponPresentationComponent::LogGripTraceEvent(const TCHAR* Event) const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Project_J::WeaponPresentation::ShouldTraceGrip(Character)) { return; }
	const USceneComponent* Root = SpawnedWeapon ? SpawnedWeapon->GetRootComponent() : nullptr;
	const USkeletalMeshComponent* Source = Character ? Character->GetMesh() : nullptr;
	const UAnimInstance* SourceAnim = Source ? Source->GetAnimInstance() : nullptr;
	const UAnimMontage* Montage = SourceAnim ? SourceAnim->GetCurrentActiveMontage() : nullptr;
	const float MontageTime = SourceAnim && Montage ? SourceAnim->Montage_GetPosition(Montage) : -1.0f;
	const FVector World = Root ? Root->GetComponentLocation() : FVector::ZeroVector;
	const FVector Relative = Root ? Root->GetRelativeLocation() : FVector::ZeroVector;
	UE_LOG(LogProjectJWeaponPresentation, Display,
		TEXT("[GripTrace][Event] t=%.3f f=%llu actor=%s event=%s mode=%s attack=%s montage=%s mt=%.3f parent=%s socket=%s weapon=(%.1f,%.1f,%.1f) rel=(%.1f,%.1f,%.1f) palmMount=%d"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0,
		static_cast<unsigned long long>(GFrameCounter), *GetNameSafe(Character), Event,
		Project_J::WeaponPresentation::GripTraceMode(bIndependentMotionActive, bNotifyOwnsMotion,
			bAutoAttackMotionActive, CurrentPresentationSocket, bContactRecoveryActive),
		*ActiveAttackPresentationTag.ToString(), *GetNameSafe(Montage), MontageTime,
		Root ? *GetNameSafe(Root->GetAttachParent()) : TEXT("None"),
		Root ? *Root->GetAttachSocketName().ToString() : TEXT("None"),
		World.X, World.Y, World.Z, Relative.X, Relative.Y, Relative.Z, bPrimaryContactAttachment ? 1 : 0);
	LogAttachmentTrace(Event, true);
}

void UProject_JWeaponPresentationComponent::LogAttachmentTrace(const TCHAR* Context, bool bForce, int32 RequestedSocket) const
{
	check(IsInGameThread());
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UWorld* World = GetWorld();
	if (!World || !Project_J::WeaponPresentation::ShouldTraceGrip(Character)) { return; }
	const double Now = World->GetTimeSeconds();
	if (!bForce && Now < AttachmentTraceNextSampleTime) { return; }
	AttachmentTraceNextSampleTime = Now + 0.5; // Configuration is slow-changing; poses retain the requested sample rate.
	const UProject_JWeaponPresentationProfile* Profile = GetCurrentPresentationProfile();
	const USceneComponent* Root = SpawnedWeapon ? SpawnedWeapon->GetRootComponent() : nullptr;
	const bool bDrawn = (RequestedSocket == INDEX_NONE ? static_cast<int32>(CurrentPresentationSocket) : RequestedSocket)
		== static_cast<int32>(EProject_JWeaponPresentationSocket::Drawn);
	const FProject_JResolvedWeaponAttachment Desired = ResolveAttachment(bDrawn);
	const USkeletalMeshComponent* ExpectedMesh = Desired.Mesh.Get();
	const USkeletalMeshComponent* Body = Profile && Profile->bPreferVisualFollowerSockets
		? Project_J::Animation::FindVisualFollower(*Character) : Character->GetMesh();
	if (!Body) { Body = Character->GetMesh(); }
	const UProject_JRetargetAnimInstance* Anim = Body ? Cast<UProject_JRetargetAnimInstance>(Body->GetAnimInstance()) : nullptr;
	const AProject_JPlayerCharacter* Player = Cast<AProject_JPlayerCharacter>(Character);
	const UProject_JCharacterAnimProfile* CharacterProfile = Player ? Player->GetCharacterAnimProfile() : nullptr;
	const UProject_JHandGripProfile* BodyProfile = Anim && Anim->HandGripProfile ? Anim->HandGripProfile.Get()
		: (CharacterProfile ? CharacterProfile->HandGripProfile.Get() : nullptr);
	const TCHAR* CalibrationSource = Anim && Anim->HandGripProfile ? TEXT("VisualOverride")
		: (BodyProfile ? TEXT("CharacterSharedProfile") : (CharacterProfile ? TEXT("CharacterInline") : TEXT("Defaults")));
	const FProject_JHandGripCalibration Calibration = Anim ? Anim->GetHandGripCalibration()
		: Project_J::Animation::ResolveHandGripCalibration(Character, nullptr);
	const auto Contact = Body ? Project_J::Animation::ResolveHandContact(*Body,
		Calibration.PrimaryPalmSocketName, Calibration.PrimaryArm.Hand, false) : Project_J::Animation::FResolvedHandContact();
	const FName GripName = Profile ? Profile->MotionPresentation.PrimaryGripSocketName : NAME_None;
	const USceneComponent* GripComponent = FindWeaponSocketComponent(GripName);
	const bool bParentMatch = Root && Root->GetAttachParent() == ExpectedMesh && Root->GetAttachSocketName() == Desired.Socket;
	const bool bMountErrorValid = Root && Desired.IsValid() && !bIndependentMotionActive && !bContactRecoveryActive;
	float WorldError = -1, RotationError = -1, RelativeError = -1;
	if (bMountErrorValid)
	{
		const FTransform ExpectedWorld = Desired.Relative * ExpectedMesh->GetSocketTransform(Desired.Socket, RTS_World);
		WorldError = FVector::Distance(Root->GetComponentLocation(), ExpectedWorld.GetLocation());
		RotationError = FMath::RadiansToDegrees(Root->GetComponentQuat().AngularDistance(ExpectedWorld.GetRotation()));
		if (bParentMatch) { RelativeError = FVector::Distance(Root->GetRelativeLocation(), Desired.Relative.GetLocation()); }
	}
	UE_LOG(LogProjectJWeaponPresentation, Display,
		TEXT("[GripTrace][Attachment] t=%.3f f=%llu actor=%s world=%s context=%s mode=%s drawn=%d configured=%s reason=%s weaponProfile=%s bodyProfile=%s calibration=%s characterProfile=%s body=%s bodyAsset=%s bodyAnim=%s preferVisual=%d palm=%s hand=%s contactStatus=%d grip=%s gripComponent=%s expectedParent=%s expectedSocket=%s actualParent=%s actualSocket=%s parentMatch=%d expectedPalmMount=%d appliedPalmMount=%d mountErrorValid=%d worldErrCm=%.4f rotationErrDeg=%.4f relativeErrCm=%.4f primarySuppressed=%d targetAlpha=%.4f animAlpha=%.4f refreshPending=%d"),
		Now, static_cast<unsigned long long>(GFrameCounter), *GetNameSafe(Character), *World->GetName(), Context,
		Project_J::WeaponPresentation::GripTraceMode(bIndependentMotionActive, bNotifyOwnsMotion, bAutoAttackMotionActive,
			bDrawn ? EProject_JWeaponPresentationSocket::Drawn : EProject_JWeaponPresentationSocket::Sheathed, bContactRecoveryActive), bDrawn,
		Profile ? (Profile->DrawnAttachmentMode == EProject_JDrawnAttachmentMode::PrimaryGripContact ? TEXT("PrimaryGripContact") : TEXT("Socket")) : TEXT("None"),
		*Desired.Reason.ToString(), Profile ? *Profile->GetPathName() : TEXT("None"), BodyProfile ? *BodyProfile->GetPathName() : TEXT("None"), CalibrationSource,
		CharacterProfile ? *CharacterProfile->GetPathName() : TEXT("None"), *GetNameSafe(Body), *GetNameSafe(Body ? Body->GetSkeletalMeshAsset() : nullptr),
		*GetNameSafe(Body ? Body->GetAnimInstance() : nullptr), Profile && Profile->bPreferVisualFollowerSockets,
		*Calibration.PrimaryPalmSocketName.ToString(), *Contact.Hand.ToString(), static_cast<int32>(Contact.Status), *GripName.ToString(), *GetNameSafe(GripComponent),
		*GetNameSafe(ExpectedMesh), *Desired.Socket.ToString(), *GetNameSafe(Root ? Root->GetAttachParent() : nullptr),
		Root ? *Root->GetAttachSocketName().ToString() : TEXT("None"), bParentMatch, Desired.bPrimaryContact, bPrimaryContactAttachment, bMountErrorValid,
		WorldError, RotationError, RelativeError, GripTargets.bPrimaryIKSuppressedByAttachment, GripTargets.PrimaryIKAlpha,
		Anim ? Anim->RightGripAlpha : -1.0f, bAttachmentRefreshRequested);
	FTransform GripInRoot;
	FName GripReason;
	const bool bGripInRootValid = Project_J::WeaponPresentation::ResolveRigidGripInRoot(GripComponent, GripName, Root, GripInRoot, GripReason);
	UE_LOG(LogProjectJWeaponPresentation, Display,
		TEXT("[GripTrace][AttachmentCalibration] t=%.3f f=%llu actor=%s context=%s palmValid=%d gripInRootValid=%d palmInHand={%s} bodyOffset={%s} gripInRoot={%s} expectedRootRel={%s} actualRootRel={%s} bodyWorld={%s} recoveryDestination={%s}"),
		Now, static_cast<unsigned long long>(GFrameCounter), *GetNameSafe(Character), Context, Contact.IsValid(), bGripInRootValid,
		*Project_J::WeaponPresentation::ToCompactTransformString(Contact.PalmInHand), *Project_J::WeaponPresentation::ToCompactTransformString(Calibration.PrimaryHandOffset),
		*Project_J::WeaponPresentation::ToCompactTransformString(GripInRoot), *Project_J::WeaponPresentation::ToCompactTransformString(Desired.Relative),
		*Project_J::WeaponPresentation::ToCompactTransformString(Root ? Root->GetRelativeTransform() : FTransform::Identity),
		*Project_J::WeaponPresentation::ToCompactTransformString(Body ? Body->GetComponentTransform() : FTransform::Identity),
		*Project_J::WeaponPresentation::ToCompactTransformString(ContactRecoveryDestination));
	const bool bHandScaleValid = Body && !Contact.Hand.IsNone() && Body->GetBoneIndex(Contact.Hand) != INDEX_NONE;
	UE_LOG(LogProjectJWeaponPresentation, Display,
		TEXT("[GripTrace][AttachmentScale] t=%.3f f=%llu actor=%s context=%s handValid=%d bodyWorld={%s} handWorld={%s} previousRootWorld={%s} gripChildRelative={%s} gripInRoot={%s}"),
		Now, static_cast<unsigned long long>(GFrameCounter), *GetNameSafe(Character), Context, bHandScaleValid,
		*Project_J::WeaponPresentation::ToContactScaleString(Body ? Body->GetComponentScale() : FVector::ZeroVector),
		*Project_J::WeaponPresentation::ToContactScaleString(bHandScaleValid ? Body->GetBoneTransform(Contact.Hand, RTS_World).GetScale3D() : FVector::ZeroVector),
		*Project_J::WeaponPresentation::ToContactScaleString(Root ? Root->GetComponentScale() : FVector::ZeroVector),
		*Project_J::WeaponPresentation::ToContactScaleString(GripComponent ? GripComponent->GetRelativeScale3D() : FVector::ZeroVector),
		*Project_J::WeaponPresentation::ToContactScaleString(bGripInRootValid ? GripInRoot.GetScale3D() : FVector::ZeroVector));
}

void UProject_JWeaponPresentationComponent::SampleGripTrace()
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Project_J::WeaponPresentation::ShouldTraceGrip(Character) || !SpawnedWeapon || !GetWorld())
	{
		bGripTraceHasPreviousSample = false;
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < GripTraceNextSampleTime) { return; }
	GripTraceNextSampleTime = Now + 1.0 / FMath::Clamp(Project_J::WeaponPresentation::CVarGripTraceHz.GetValueOnGameThread(), 1.0f, 120.0f);
	// Emit configuration even when the follower/animation instance is missing.
	LogAttachmentTrace(TEXT("Sample"));

	const USceneComponent* Root = SpawnedWeapon->GetRootComponent();
	const USkeletalMeshComponent* Source = Character->GetMesh();
	const USkeletalMeshComponent* Visible = Project_J::Animation::FindVisualFollower(*Character);
	const UProject_JRetargetAnimInstance* FollowerAnim = Visible
		? Cast<UProject_JRetargetAnimInstance>(Visible->GetAnimInstance()) : nullptr;
	if (!Root || !Source || !Visible || !FollowerAnim) { return; }
	const UProject_JWeaponPresentationProfile* Profile = GetCurrentPresentationProfile();
	const FProject_JHandGripCalibration Calibration = FollowerAnim->GetHandGripCalibration();
	const FName PalmSocket = Calibration.PrimaryPalmSocketName;
	const FName HandBone = !Calibration.PrimaryArm.Hand.IsNone() ? Calibration.PrimaryArm.Hand
		: (Visible->DoesSocketExist(PalmSocket) ? Visible->GetSocketBoneName(PalmSocket) : NAME_None);
	const FName ElbowBone = !Calibration.PrimaryArm.Elbow.IsNone() ? Calibration.PrimaryArm.Elbow
		: (!HandBone.IsNone() ? Visible->GetParentBone(HandBone) : NAME_None);
	const FName ShoulderBone = !Calibration.PrimaryArm.Shoulder.IsNone() ? Calibration.PrimaryArm.Shoulder
		: (!ElbowBone.IsNone() ? Visible->GetParentBone(ElbowBone) : NAME_None);
	const bool bHasArm = !ShoulderBone.IsNone() && Visible->GetBoneIndex(ShoulderBone) != INDEX_NONE &&
		Visible->GetBoneIndex(ElbowBone) != INDEX_NONE && Visible->GetBoneIndex(HandBone) != INDEX_NONE;
	const FVector Shoulder = bHasArm ? Visible->GetBoneTransform(ShoulderBone, RTS_World).GetLocation() : FVector::ZeroVector;
	const FVector Elbow = bHasArm ? Visible->GetBoneTransform(ElbowBone, RTS_World).GetLocation() : FVector::ZeroVector;
	const FVector Wrist = bHasArm ? Visible->GetBoneTransform(HandBone, RTS_World).GetLocation() : FVector::ZeroVector;
	const float ElbowAngle = bHasArm ? FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
		FVector::DotProduct((Shoulder - Elbow).GetSafeNormal(), (Wrist - Elbow).GetSafeNormal()), -1.0f, 1.0f))) : -1.0f;
	const FVector ArmLine = Wrist - Shoulder;
	const float AlongArm = bHasArm && ArmLine.SizeSquared() > UE_KINDA_SMALL_NUMBER
		? FMath::Clamp(FVector::DotProduct(Elbow - Shoulder, ArmLine) / ArmLine.SizeSquared(), 0.0f, 1.0f) : 0.0f;
	const float ElbowOffset = bHasArm ? FVector::Distance(Elbow, Shoulder + AlongArm * ArmLine) : -1.0f;
	// Actor rotation is not an elbow flip; straight-arm normals are ill-conditioned.
	const FVector Plane = bHasArm && ElbowOffset > 0.5f
		? Visible->GetComponentTransform().InverseTransformVectorNoScale(
			FVector::CrossProduct(Elbow - Shoulder, Wrist - Elbow)).GetSafeNormal() : FVector::ZeroVector;
	const float PlaneDot = bGripTraceHasPreviousSample && !Plane.IsNearlyZero() && !GripTracePreviousElbowPlane.IsNearlyZero()
		? FVector::DotProduct(Plane, GripTracePreviousElbowPlane) : 1.0f;
	const FVector Sword = Root->GetComponentLocation();
	const double SampleDt = bGripTraceHasPreviousSample ? Now - GripTracePreviousSampleTime : 0.0;
	const float SwordSpeed = SampleDt > UE_KINDA_SMALL_NUMBER ? FVector::Distance(Sword, GripTracePreviousWeapon) / SampleDt : 0.0f;
	const float ElbowSpeed = bHasArm && SampleDt > UE_KINDA_SMALL_NUMBER
		? FVector::Distance(Elbow, GripTracePreviousElbow) / SampleDt : 0.0f;
	const float PalmError = GripTargets.bHasPrimaryGrip && Visible->DoesSocketExist(PalmSocket)
		? FVector::Distance(Visible->GetSocketLocation(PalmSocket), GripTargets.PrimaryGripWorldTransform.GetLocation()) : -1.0f;
	const FVector WristTarget = Visible->GetComponentTransform().TransformPosition(FollowerAnim->RightGripLocation);
	const bool bHasTargetSnapshot = FollowerAnim->GetGripTargetSnapshotFrame() != MAX_uint64;
	const float WristError = bHasArm && bHasTargetSnapshot && GripTargets.bHasPrimaryGrip && FollowerAnim->RightGripAlpha > UE_KINDA_SMALL_NUMBER
		? FVector::Distance(Wrist, WristTarget) : -1.0f;
	// During recovery the independent animation target intentionally differs from
	// the displayed weapon. Measure physical contact independently of that target.
	// This is observational only: never feed the hand-attached weapon back to IK.
	const auto LogWeaponContact = [&](const TCHAR* Side, FName WeaponSocket, FName BodyPalm,
		const FTransform& ContactOffset, bool bHasGoal, const FTransform& IndependentGoal, float Alpha)
	{
		FTransform PhysicalGrip;
		const bool bValid = Visible->DoesSocketExist(BodyPalm) && FindWeaponSocketTransform(WeaponSocket, PhysicalGrip);
		float RawError = -1.0f, CalibratedError = -1.0f, RotationError = -1.0f, GoalGap = -1.0f;
		if (bValid)
		{
			const FTransform PalmWorld = Visible->GetSocketTransform(BodyPalm, RTS_World);
			const FTransform CalibratedGrip = ContactOffset * PhysicalGrip;
			RawError = FVector::Distance(PalmWorld.GetLocation(), PhysicalGrip.GetLocation());
			CalibratedError = FVector::Distance(PalmWorld.GetLocation(), CalibratedGrip.GetLocation());
			RotationError = FMath::RadiansToDegrees(PalmWorld.GetRotation().AngularDistance(CalibratedGrip.GetRotation()));
			if (bHasGoal)
			{
				GoalGap = FVector::Distance(CalibratedGrip.GetLocation(), (ContactOffset * IndependentGoal).GetLocation());
			}
		}
		UE_LOG(LogProjectJWeaponPresentation, Display,
			TEXT("[GripTrace][WeaponContact] t=%.3f f=%llu actor=%s mesh=%s world=%s side=%s mode=%s alpha=%.4f weaponSocket=%s palmSocket=%s valid=%d socketErr=%.3f calibratedErr=%.3f rotationErr=%.3f goalGap=%.3f"),
			Now, static_cast<unsigned long long>(GFrameCounter), *GetNameSafe(Character), *Visible->GetName(), *GetWorld()->GetName(),
			Side, Project_J::WeaponPresentation::GripTraceMode(bIndependentMotionActive, bNotifyOwnsMotion,
				bAutoAttackMotionActive, CurrentPresentationSocket, bContactRecoveryActive), Alpha,
			*WeaponSocket.ToString(), *BodyPalm.ToString(), bValid, RawError, CalibratedError, RotationError, GoalGap);
	};
	LogWeaponContact(TEXT("Primary"), CachedPrimaryGripSocket, Calibration.PrimaryPalmSocketName,
		Calibration.PrimaryHandOffset, GripTargets.bHasPrimaryGrip, GripTargets.PrimaryGripWorldTransform, FollowerAnim->RightGripAlpha);
	if (GripTargets.bHasSecondaryGrip || FollowerAnim->LeftGripAlpha > UE_KINDA_SMALL_NUMBER)
	{
		LogWeaponContact(TEXT("Secondary"), CachedSecondaryGripSocket, Calibration.SecondaryPalmSocketName,
			Calibration.SecondaryHandOffset, GripTargets.bHasSecondaryGrip, GripTargets.SecondaryGripWorldTransform, FollowerAnim->LeftGripAlpha);
	}
	const FName SourceSocket = Profile ? Profile->DrawnSocketName : NAME_None;
	const float SourceGap = !SourceSocket.IsNone() && Source->DoesSocketExist(SourceSocket)
		? FVector::Distance(Sword, Source->GetSocketLocation(SourceSocket)) : -1.0f;
	const FName VisualSocket = Profile ? Profile->VisualDrawnSocketName : NAME_None;
	const float VisualGap = !VisualSocket.IsNone() && Visible->DoesSocketExist(VisualSocket)
		? FVector::Distance(Sword, Visible->GetSocketLocation(VisualSocket)) : -1.0f;
	const UAnimInstance* SourceAnim = Source->GetAnimInstance();
	const UAnimMontage* Montage = SourceAnim ? SourceAnim->GetCurrentActiveMontage() : nullptr;
	const float MontageTime = SourceAnim && Montage ? SourceAnim->Montage_GetPosition(Montage) : -1.0f;
	const FVector Relative = Root->GetRelativeLocation();
	UE_LOG(LogProjectJWeaponPresentation, Display,
		TEXT("[GripTrace][Sample] t=%.3f f=%llu actor=%s mode=%s attack=%s montage=%s mt=%.3f alpha=%.2f suppressed=%d palm=%s hand=%s elbow=%s shoulder=%s angle=%.1f elbowLine=%.1f planeDot=%.2f elbowSpeed=%.1f palmErr=%.1f wristErr=%.1f swordSpeed=%.1f sourceGap=%.1f visualGap=%.1f sword=(%.1f,%.1f,%.1f) elbowPos=(%.1f,%.1f,%.1f) rel=(%.1f,%.1f,%.1f) parent=%s socket=%s"),
		Now, static_cast<unsigned long long>(GFrameCounter), *GetNameSafe(Character),
		Project_J::WeaponPresentation::GripTraceMode(bIndependentMotionActive, bNotifyOwnsMotion,
			bAutoAttackMotionActive, CurrentPresentationSocket, bContactRecoveryActive),
		*ActiveAttackPresentationTag.ToString(), *GetNameSafe(Montage), MontageTime,
		FollowerAnim->RightGripAlpha, GripTargets.bPrimaryIKSuppressedByAttachment ? 1 : 0,
		*PalmSocket.ToString(), *HandBone.ToString(), *ElbowBone.ToString(), *ShoulderBone.ToString(),
		ElbowAngle, ElbowOffset, PlaneDot, ElbowSpeed, PalmError, WristError,
		SwordSpeed, SourceGap, VisualGap, Sword.X, Sword.Y, Sword.Z,
		Elbow.X, Elbow.Y, Elbow.Z, Relative.X, Relative.Y, Relative.Z,
		*GetNameSafe(Root->GetAttachParent()), *Root->GetAttachSocketName().ToString());

	const FVector SourceSocketComponent = !SourceSocket.IsNone() && Source->DoesSocketExist(SourceSocket)
		? Source->GetSocketTransform(SourceSocket, RTS_Component).GetLocation() : FVector::ZeroVector;
	UAnimInstance* TrackedAnim = ContactRecoverySourceInstanceID != INDEX_NONE
		? ContactRecoverySourceAnim.Get() : AutoAttackSourceAnim.Get();
	const int32 TrackedInstanceID = ContactRecoverySourceInstanceID != INDEX_NONE
		? ContactRecoverySourceInstanceID : AutoAttackSourceInstanceID;
	const FAnimMontageInstance* TrackedMontage = TrackedAnim ? TrackedAnim->GetMontageInstanceForID(TrackedInstanceID) : nullptr;
	UE_LOG(LogProjectJWeaponPresentation, Display,
		TEXT("[GripTrace][Timing] f=%llu sourceBones=%u visibleBones=%u targetFrame=%llu poseFrame=%llu motionFrame=%llu recovery=%.3f actor=%s sourceMesh=%s visibleMesh=%s sourceSocketCS=%s swordRotation=%s relativeRotation=%s planeValid=%d recoveryClock=%s sourceInstance=%d sourceMontage=%s sourcePosition=%.3f sourceWeight=%.3f sourceStopped=%d"),
		static_cast<unsigned long long>(GFrameCounter), Source->GetCurrentBoneTransformFrame(), Visible->GetCurrentBoneTransformFrame(),
		static_cast<unsigned long long>(FollowerAnim->GetGripTargetSnapshotFrame()),
		static_cast<unsigned long long>(FollowerAnim->GetGripPoseEvaluationFrame()),
		static_cast<unsigned long long>(LastMotionEvaluationFrame), ContactRecoveryAlpha,
		*Character->GetActorLocation().ToCompactString(), *Source->GetComponentLocation().ToCompactString(),
		*Visible->GetComponentLocation().ToCompactString(), *SourceSocketComponent.ToCompactString(),
		*Root->GetComponentRotation().ToCompactString(), *Root->GetRelativeRotation().ToCompactString(), !Plane.IsNearlyZero(),
		bContactRecoveryActive ? (ContactRecoverySourceInstanceID != INDEX_NONE ? TEXT("MontageWeight") : TEXT("Timer")) : TEXT("None"),
		TrackedInstanceID, *GetNameSafe(TrackedMontage ? TrackedMontage->Montage : nullptr),
		TrackedMontage ? TrackedMontage->GetPosition() : -1.0f, TrackedMontage ? TrackedMontage->GetWeight() : -1.0f,
		TrackedMontage ? static_cast<int32>(TrackedMontage->IsStopped()) : -1);
	const auto LogArm = [&](const TCHAR* Side, const FProject_JGripArmBones& Bones, FName ContactSocket,
		const FVector& TargetLocation, const FRotator& TargetRotation, float Alpha)
	{
		const FName Hand = Bones.Hand.IsNone() ? Visible->GetSocketBoneName(ContactSocket) : Bones.Hand;
		const FName Forearm = Bones.Elbow.IsNone() ? Visible->GetParentBone(Hand) : Bones.Elbow;
		const FName UpperArm = Bones.Shoulder.IsNone() ? Visible->GetParentBone(Forearm) : Bones.Shoulder;
		if (Visible->GetBoneIndex(Hand) == INDEX_NONE || Visible->GetBoneIndex(Forearm) == INDEX_NONE ||
			Visible->GetBoneIndex(UpperArm) == INDEX_NONE) { return; }
		const FTransform HandWorld = Visible->GetBoneTransform(Hand, RTS_World);
		const FVector ElbowWorld = Visible->GetBoneTransform(Forearm, RTS_World).GetLocation();
		const FVector ShoulderWorld = Visible->GetBoneTransform(UpperArm, RTS_World).GetLocation();
		const float ArmLength = FVector::Distance(ShoulderWorld, ElbowWorld) + FVector::Distance(ElbowWorld, HandWorld.GetLocation());
		const FVector TargetWorld = Visible->GetComponentTransform().TransformPosition(TargetLocation);
		const bool bTargetActive = bHasTargetSnapshot && Alpha > UE_KINDA_SMALL_NUMBER;
		const float Reach = bTargetActive ? FVector::Distance(ShoulderWorld, TargetWorld) : -1.0f;
		const FQuat TargetWorldRotation = Visible->GetComponentQuat() * TargetRotation.Quaternion();
		const float RotationError = bTargetActive
			? FMath::RadiansToDegrees(HandWorld.GetRotation().AngularDistance(TargetWorldRotation)) : -1.0f;
		UE_LOG(LogProjectJWeaponPresentation, Display,
			TEXT("[GripTrace][Arm] f=%llu side=%s hand=%s elbow=%s shoulder=%s alpha=%.3f armLength=%.2f reach=%.2f reachRatio=%.3f excess=%.2f wristRotErr=%.1f shoulderPos=%s wristPos=%s targetPos=%s wristRotation=%s targetRotation=%s"),
			static_cast<unsigned long long>(GFrameCounter), Side, *Hand.ToString(), *Forearm.ToString(), *UpperArm.ToString(),
			Alpha, ArmLength, Reach, Reach >= 0.0f && ArmLength > UE_KINDA_SMALL_NUMBER ? Reach / ArmLength : -1.0f,
			Reach >= 0.0f ? FMath::Max(Reach - ArmLength, 0.0f) : -1.0f, RotationError,
			*ShoulderWorld.ToCompactString(), *HandWorld.GetLocation().ToCompactString(), *TargetWorld.ToCompactString(),
			*HandWorld.Rotator().ToCompactString(), *TargetWorldRotation.Rotator().ToCompactString());
		const FTransform FinalHand = Visible->GetBoneTransform(Hand, RTS_Component);
		const FTransform FinalForearm = Visible->GetBoneTransform(Forearm, RTS_Component);
		const FTransform FinalUpperArm = Visible->GetBoneTransform(UpperArm, RTS_Component);
		UE_LOG(LogProjectJWeaponPresentation, Display,
			TEXT("[GripTrace][FinalCS] f=%llu targetFrame=%llu poseFrame=%llu actor=%s mesh=%s world=%s side=%s hand=%s elbow=%s shoulder=%s alpha=%.4f shoulderCS=%s elbowCS=%s wristCS=%s wristRotCS=%s targetCS=%s"),
			static_cast<unsigned long long>(GFrameCounter), static_cast<unsigned long long>(FollowerAnim->GetGripTargetSnapshotFrame()),
			static_cast<unsigned long long>(FollowerAnim->GetGripPoseEvaluationFrame()), *GetNameSafe(Character),
			*Visible->GetName(), *GetWorld()->GetName(), Side, *Hand.ToString(), *Forearm.ToString(), *UpperArm.ToString(),
			Alpha, *FinalUpperArm.GetLocation().ToCompactString(), *FinalForearm.GetLocation().ToCompactString(),
			*FinalHand.GetLocation().ToCompactString(), *FinalHand.Rotator().ToCompactString(), *TargetLocation.ToCompactString());
	};
	LogArm(TEXT("Primary"), Calibration.PrimaryArm, Calibration.PrimaryPalmSocketName,
		FollowerAnim->RightGripLocation, FollowerAnim->RightGripRotation, FollowerAnim->RightGripAlpha);
	if (FollowerAnim->LeftGripAlpha > UE_KINDA_SMALL_NUMBER)
	{
		LogArm(TEXT("Secondary"), Calibration.SecondaryArm, Calibration.SecondaryPalmSocketName,
			FollowerAnim->LeftGripLocation, FollowerAnim->LeftGripRotation, FollowerAnim->LeftGripAlpha);
	}
	GripTracePreviousSampleTime = Now;
	GripTracePreviousWeapon = Sword;
	GripTracePreviousElbow = Elbow;
	GripTracePreviousElbowPlane = Plane;
	bGripTraceHasPreviousSample = true;
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
	if (Project_J::WeaponPresentation::CVarGripTrace.GetValueOnGameThread() != 0 && !IsComponentTickEnabled()) { UpdateTickState(); }
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

