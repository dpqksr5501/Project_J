#include "Components/Project_JWeaponPresentationComponent.h"
#include "Engine/StaticMesh.h"

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
namespace Project_J::WeaponPresentation
{
	static FString ToContactScaleString(const FVector& Scale)
	{
		return FString::Printf(TEXT("(%.9f,%.9f,%.9f) uniform=%d"), Scale.X, Scale.Y, Scale.Z,
			Project_J::Animation::IsPositiveUniformContactScale(Scale));
	}

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

	bool IsDebugEnabled()
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

	bool IsGripTraceEnabled() { return CVarGripTrace.GetValueOnGameThread() != 0; }
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
	const UProject_JHandGripProfile* BodyProfile = Anim ? Anim->GetResolvedHandGripProfile()
		: (CharacterProfile ? CharacterProfile->HandGripProfile.Get() : nullptr);
	const UProject_JRetargetBodyProfile* RetargetBody = Anim ? Anim->GetRetargetBodyProfile() : nullptr;
	const TCHAR* CalibrationSource = Anim && Anim->HandGripProfile ? TEXT("VisualOverride")
		: (RetargetBody && BodyProfile && RetargetBody->HandGrip.Get() == BodyProfile ? TEXT("RetargetBodyProfile")
		: (BodyProfile ? TEXT("CharacterSharedProfile") : (CharacterProfile ? TEXT("CharacterInline") : TEXT("Defaults"))));
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

