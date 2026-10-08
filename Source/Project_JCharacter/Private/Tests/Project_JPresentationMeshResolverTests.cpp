#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Animation/Project_JPresentationMeshResolver.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/Project_JHandGripProfile.h"
#include "Animation/Project_JAnimNotifyState_WeaponGroundContact.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Equipment/Project_JWeaponMotionTypes.h"
#include "Equipment/Project_JWeaponPresentationProfile.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPresentationMeshResolverTest,
	"ProjectJ.Presentation.VisualMeshOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJPresentationMeshResolverTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ACharacter* Character = World->SpawnActor<ACharacter>();

	USkeletalMesh* SourceAsset = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"));
	USkeletalMesh* FollowerAsset = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/GreatSword/GreatSword_Woman.GreatSword_Woman"));
	if (!TestNotNull(TEXT("Animation source fixture"), SourceAsset) ||
		!TestNotNull(TEXT("Visible follower fixture"), FollowerAsset))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	Character->GetMesh()->SetSkeletalMesh(SourceAsset);
	const FName SourceBone = SourceAsset->GetRefSkeleton().GetBoneName(0);
	const FName FollowerBone = FollowerAsset->GetRefSkeleton().GetBoneName(0);

	USkeletalMeshComponent* Follower = NewObject<USkeletalMeshComponent>(Character, TEXT("GreatswordVisual"));
	Follower->SetupAttachment(Character->GetMesh());
	Follower->SetSkeletalMesh(FollowerAsset);
	Character->AddInstanceComponent(Follower);
	Follower->RegisterComponent();
	Follower->AnimScriptInstance = NewObject<UProject_JRetargetAnimInstance>(Follower);

	TestEqual(TEXT("Retarget follower is the presentation mesh"),
		Project_J::Animation::FindVisualFollower(*Character), Follower);

	FName ResolvedSocket;
	TestEqual(TEXT("Visual socket overrides the source socket"),
		Project_J::Animation::ResolveWeaponAttachmentMesh(*Character, SourceBone,
			FollowerBone, true, ResolvedSocket), Follower);
	TestEqual(TEXT("Resolved visual socket"), ResolvedSocket, FollowerBone);
	TestEqual(TEXT("Missing visual socket safely uses the source"),
		Project_J::Animation::ResolveWeaponAttachmentMesh(*Character, SourceBone,
			TEXT("Missing"), true, ResolvedSocket), Character->GetMesh());
	TestEqual(TEXT("Resolved legacy socket"), ResolvedSocket, SourceBone);
	TestEqual(TEXT("Profile can retain source attachment"),
		Project_J::Animation::ResolveWeaponAttachmentMesh(*Character, SourceBone,
			FollowerBone, false, ResolvedSocket), Character->GetMesh());

	TestEqual(TEXT("Bip01 equipment follows the visible Bip01 body"),
		Project_J::Animation::ResolveEquipmentPoseSource(*Character, *FollowerAsset), Follower);
	TestEqual(TEXT("Mannequin equipment follows the animation source"),
		Project_J::Animation::ResolveEquipmentPoseSource(*Character, *SourceAsset), Character->GetMesh());

	TArray<FProject_JWeaponMotionKey> MotionKeys;
	FProject_JWeaponMotionKey MotionKey;
	MotionKey.RelativeTransform = FTransform(FVector(20.0f, 0.0f, 0.0f));
	MotionKeys.Add(MotionKey);
	const FTransform SourceSocketWorld(FVector(100.0f, 0.0f, 0.0f));
	const FTransform VisualSocketWorld(FVector(200.0f, 0.0f, 0.0f));
	const auto EvaluateMotion = [&](float Time)
	{
		return Project_J::WeaponMotion::EvaluatePresentationWorldTransform(MotionKeys, Time,
			1.0f, 0.2f, 0.2f, SourceSocketWorld, VisualSocketWorld);
	};
	TestTrue(TEXT("Independent motion begins at the rendered hand"), EvaluateMotion(0.0f).Equals(VisualSocketWorld));
	TestTrue(TEXT("Independent motion still uses source-authored keys"),
		EvaluateMotion(0.5f).Equals(FTransform(FVector(120.0f, 0.0f, 0.0f))));
	TestTrue(TEXT("Independent motion returns to the rendered hand"), EvaluateMotion(1.0f).Equals(VisualSocketWorld));
	const FTransform CapturedEntry(FVector(200.0f, 0.0f, 0.0f));
	const auto EvaluateContactMotion = [&](float Time)
	{
		return Project_J::WeaponMotion::EvaluateContactDrivenWorldTransform(MotionKeys, Time,
			1.0f, 0.2f, SourceSocketWorld, CapturedEntry);
	};
	TestTrue(TEXT("Contact motion starts at the captured pose"), EvaluateContactMotion(0.0f).Equals(CapturedEntry));
	TestTrue(TEXT("Contact motion uses the source arc after entry"),
		EvaluateContactMotion(0.5f).Equals(FTransform(FVector(120.0f, 0.0f, 0.0f))));
	TestTrue(TEXT("Contact motion does not drag toward the IK-driven hand on exit"),
		EvaluateContactMotion(1.0f).Equals(FTransform(FVector(120.0f, 0.0f, 0.0f))));

	// The animation reader must pull the latest authored weapon pose before
	// sampling grip targets, even if the late cosmetic component tick has not run.
	UProject_JWeaponPresentationComponent* Presentation = NewObject<UProject_JWeaponPresentationComponent>(Character);
	Character->AddInstanceComponent(Presentation);
	Presentation->RegisterComponent();
	AActor* Weapon = World->SpawnActor<AActor>();
	UStaticMesh* WeaponAsset = NewObject<UStaticMesh>(Weapon);
	UStaticMeshSocket* GripSocket = NewObject<UStaticMeshSocket>(WeaponAsset);
	GripSocket->SocketName = TEXT("WeaponGrip_L");
	WeaponAsset->AddSocket(GripSocket);
	UStaticMeshSocket* PrimarySocket = NewObject<UStaticMeshSocket>(WeaponAsset);
	PrimarySocket->SocketName = TEXT("WeaponGrip_R");
	PrimarySocket->RelativeLocation = FVector(3.0f, 2.0f, 1.0f);
	WeaponAsset->AddSocket(PrimarySocket);
	UStaticMeshComponent* WeaponRoot = NewObject<UStaticMeshComponent>(Weapon);
	WeaponRoot->SetStaticMesh(WeaponAsset);
	Weapon->SetRootComponent(WeaponRoot);
	WeaponRoot->RegisterComponent();
	Weapon->AttachToComponent(Character->GetMesh(), FAttachmentTransformRules::SnapToTargetIncludingScale, SourceBone);
	UProject_JWeaponPresentationProfile* Profile = NewObject<UProject_JWeaponPresentationProfile>(Character);
	Profile->DrawnSocketName = SourceBone;
	Profile->VisualDrawnSocketName = FollowerBone;
	Profile->MotionPresentation.bSupportsIndependentMotion = true;
	Profile->MotionPresentation.DefaultDrawnSecondaryIKAlpha = 1.0f;
	Presentation->AppliedProfile = Profile;
	Presentation->SpawnedWeapon = Weapon;
	Presentation->CurrentPresentationSocket = EProject_JWeaponPresentationSocket::Drawn;
	Presentation->UpdateSocketComponentCache();
	// Exercise the opt-in diagnostic against real source/follower components.
	IConsoleVariable* TraceEnabled = IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Presentation.GripTrace"));
	IConsoleVariable* TraceActor = IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Presentation.GripTraceActor"));
	if (!TestNotNull(TEXT("Grip trace switch"), TraceEnabled) || !TestNotNull(TEXT("Grip trace actor filter"), TraceActor))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	const int32 PreviousTraceEnabled = TraceEnabled->GetInt();
	const FString PreviousTraceActor = TraceActor->GetString();
	TraceActor->Set(*Character->GetName(), ECVF_SetByCode);
	TraceEnabled->Set(1, ECVF_SetByCode);
	Presentation->SampleGripTrace();
	TArray<FProject_JWeaponMotionKey> Timeline;
	Timeline.Add(FProject_JWeaponMotionKey());
	FProject_JWeaponMotionKey EndKey;
	EndKey.NormalizedTime = 1.0f;
	EndKey.RelativeTransform = FTransform(FVector(100.0f, 0.0f, 0.0f));
	Timeline.Add(EndKey);
	TestTrue(TEXT("Independent motion begins"), Presentation->BeginIndependentMotion(Timeline, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f));
	Presentation->SetIndependentMotionPosition(0.5f);
	const FProject_JWeaponGripTargets MidGrip = Presentation->GetWeaponGripTargetsForAnimation(1.0f / 60.0f);
	TestTrue(TEXT("Animation pull evaluates the current weapon key"),
		FMath::IsNearlyEqual(WeaponRoot->GetRelativeLocation().X, 50.0f, 0.1f));
	TestEqual(TEXT("Authored weapon motion owns the grip"), MidGrip.DriveMode, EProject_JWeaponGripDriveMode::AuthoredWeaponMotion);
	Presentation->SetIndependentMotionPosition(0.75f);
	Presentation->GetWeaponGripTargetsForAnimation(1.0f / 60.0f);
	TestTrue(TEXT("A later montage position invalidates the same-frame cache"),
		FMath::IsNearlyEqual(WeaponRoot->GetRelativeLocation().X, 75.0f, 0.1f));
	const FTransform WorldBeforeHandoff = WeaponRoot->GetComponentTransform();
	Presentation->EndIndependentMotion();
	TestTrue(TEXT("Contact handoff preserves weapon world pose"),
		WeaponRoot->GetComponentTransform().Equals(WorldBeforeHandoff, 0.1f));
	TestEqual(TEXT("Contact handoff returns ownership to the visible mesh"),
		WeaponRoot->GetAttachParent(), static_cast<USceneComponent*>(Follower));
	const FProject_JWeaponGripTargets RecoveryStart = Presentation->GetWeaponGripTargets();
	TestTrue(TEXT("Handoff starts a finite contact recovery"), RecoveryStart.bContactRecovery);
	TestFalse(TEXT("Recovery retains the independent primary contact"), RecoveryStart.bPrimaryIKSuppressedByAttachment);
	TestTrue(TEXT("Recovery preserves the previously evaluated primary alpha"),
		FMath::IsNearlyEqual(RecoveryStart.PrimaryIKAlpha, 1.0f));
	const double RecoveryStartTime = Presentation->ContactRecoveryStartSeconds;
	const float SocketDistanceBeforeRecovery = FVector::Distance(WeaponRoot->GetComponentLocation(),
		Follower->GetSocketLocation(FollowerBone));
	Presentation->UpdateContactRecovery(RecoveryStartTime + Profile->MotionPresentation.ContactRecoverySeconds * 0.5);
	const FProject_JWeaponGripTargets RecoveryMid = Presentation->GetWeaponGripTargets();
	TestTrue(TEXT("Moving the attached weapon does not move its independent recovery target"),
		RecoveryMid.PrimaryGripWorldTransform.Equals(RecoveryStart.PrimaryGripWorldTransform, 0.1f));
	TestTrue(TEXT("Recovery primary alpha fades at the attachment midpoint"),
		FMath::IsNearlyEqual(RecoveryMid.PrimaryIKAlpha, 0.5f));
	TestTrue(TEXT("Recovery halves distance to the socket even with a rotated bind pose"),
		FMath::IsNearlyEqual(FVector::Distance(WeaponRoot->GetComponentLocation(), Follower->GetSocketLocation(FollowerBone)),
			SocketDistanceBeforeRecovery * 0.5f, 0.1f));
	const FTransform MidRelative = WeaponRoot->GetRelativeTransform();
	Presentation->UpdateContactRecovery(RecoveryStartTime + Profile->MotionPresentation.ContactRecoverySeconds * 0.5);
	TestTrue(TEXT("Animation pull and late tick cannot double-advance recovery"),
		WeaponRoot->GetRelativeTransform().Equals(MidRelative, 0.1f));
	// Exercise the real game-thread snapshot and worker math, rather than only
	// the component targets: a second FInterpTo would lag the attachment clock.
	UProject_JRetargetAnimInstance* RecoveryAnim = Cast<UProject_JRetargetAnimInstance>(Follower->GetAnimInstance());
	Presentation->ContactRecoveryStartSeconds = World->GetTimeSeconds() - Profile->MotionPresentation.ContactRecoverySeconds * 0.5;
	RecoveryAnim->NativeUpdateAnimation(1.0f / 60.0f);
	RecoveryAnim->NativeThreadSafeUpdateAnimation(1.0f / 60.0f);
	TestTrue(TEXT("Actual follower recovery alpha uses the same clock without a second interpolation"),
		FMath::IsNearlyEqual(RecoveryAnim->RightGripAlpha, 0.5f, 0.001f));
	Presentation->ContactRecoveryStartSeconds = RecoveryStartTime;
	Follower->SetRelativeLocation(FVector(25.0f, 0.0f, 0.0f));
	TestTrue(TEXT("Captured recovery target follows component movement without following the hand"),
		Presentation->GetWeaponGripTargets().PrimaryGripWorldTransform.GetLocation().Equals(
			RecoveryStart.PrimaryGripWorldTransform.GetLocation() + FVector(25.0f, 0.0f, 0.0f), 0.1f));
	Follower->SetRelativeLocation(FVector::ZeroVector);
	Presentation->UpdateContactRecovery(RecoveryStartTime + 1.0);
	TestFalse(TEXT("A skipped/URO update still completes recovery"), Presentation->bContactRecoveryActive);
	TestTrue(TEXT("Recovery ends at the authored idle attachment with no residual offset"),
		WeaponRoot->GetRelativeTransform().Equals(FTransform::Identity, 0.1f));
	TestTrue(TEXT("Only completed hand ownership suppresses primary IK"),
		Presentation->GetWeaponGripTargets().bPrimaryIKSuppressedByAttachment);
	const TArray<FProject_JWeaponMotionKey> NoMotionKeys;
	TestTrue(TEXT("Keyless Weapon Motion follows the authored source socket"),
		Presentation->BeginIndependentMotion(NoMotionKeys, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f));
	Presentation->SetIndependentMotionPosition(0.5f);
	const FProject_JWeaponGripTargets KeylessGrip = Presentation->GetWeaponGripTargetsForAnimation(1.0f / 60.0f);
	TestTrue(TEXT("Keyless weapon offset is identity"), WeaponRoot->GetRelativeTransform().Equals(FTransform::Identity, 0.1f));
	TestEqual(TEXT("Keyless motion still owns the weapon grip"), KeylessGrip.DriveMode,
		EProject_JWeaponGripDriveMode::AuthoredWeaponMotion);
	// A notify-free attack advances its short hand-to-source entry independently
	// of montage notify time. The hand is sampled once, not followed each frame.
	Presentation->bNotifyOwnsMotion = false;
	Presentation->bAutoAttackMotionActive = true;
	Presentation->ActiveMotionEntryWorld = FTransform(FVector(100.0f, 0.0f, 0.0f));
	Presentation->ActiveMotionNormalizedTime = 0.0f;
	Presentation->ActiveMotionDurationSeconds = 0.08f;
	Presentation->ActiveEntryBlendSeconds = 0.08f;
	Presentation->UpdateIndependentMotion(0.04f);
	TestTrue(TEXT("Notify-free entry advances to the midpoint"),
		FMath::IsNearlyEqual(WeaponRoot->GetComponentLocation().X, 50.0f, 0.1f));
	Presentation->EndIndependentMotion();
	const FTransform InterruptedWorld = WeaponRoot->GetComponentTransform();
	TestTrue(TEXT("New attack interrupts recovery"),
		Presentation->BeginIndependentMotion(NoMotionKeys, 1.0f, 0.0f, 0.08f, 0.08f, 0.0f));
	TestFalse(TEXT("Interrupted recovery cannot write to the new attack"), Presentation->bContactRecoveryActive);
	TestTrue(TEXT("Interrupted recovery preserves the next attack entry pose"),
		WeaponRoot->GetComponentTransform().Equals(InterruptedWorld, 0.1f));
	Presentation->EndIndependentMotion();
	Presentation->AttachWeaponToDrawnSocket();
	TestFalse(TEXT("Explicit attachment cancels pending recovery"), Presentation->bContactRecoveryActive);
	Profile->MotionPresentation.ContactRecoverySeconds = 0.0f;
	Presentation->BeginIndependentMotion(Timeline, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f);
	Presentation->SetIndependentMotionPosition(0.5f);
	Presentation->GetWeaponGripTargetsForAnimation(0.0f);
	Presentation->EndIndependentMotion();
	TestFalse(TEXT("Zero-duration recovery is a defined snap policy"), Presentation->bContactRecoveryActive);
	TestTrue(TEXT("Zero duration cannot leave an attack offset attached"),
		WeaponRoot->GetRelativeTransform().Equals(FTransform::Identity, 0.1f));

	UProject_JRetargetAnimInstance* FollowerAnim = Cast<UProject_JRetargetAnimInstance>(Follower->GetAnimInstance());
	// A real montage instance becomes inactive as soon as its desired weight
	// is zero, while the blend-out pose can still have full contribution.
	UAnimInstance* SourceAnim = NewObject<UAnimInstance>(Character->GetMesh());
	Character->GetMesh()->AnimScriptInstance = SourceAnim;
	FAnimMontageInstance* SourceInstance = new FAnimMontageInstance(SourceAnim);
	SourceInstance->Initialize(NewObject<UAnimMontage>(SourceAnim));
	SourceAnim->MontageInstances.Add(SourceInstance);
	SourceInstance->SetWeight(1.0f);
	SourceInstance->SetDesiredWeight(0.0f);
	// SetWeight sets the internal blend alpha. With the outgoing range 1 -> 0,
	// alpha 0 retains full pose weight and alpha 1 completes the blend.
	SourceInstance->SetWeight(0.0f);
	TestTrue(TEXT("Outgoing fixture starts at full pose weight"), FMath::IsNearlyEqual(SourceInstance->GetWeight(), 1.0f));
	TestFalse(TEXT("An outgoing montage is already inactive while its pose still contributes"), SourceInstance->IsActive());
	Profile->MotionPresentation.ContactRecoverySeconds = 0.12f;
	FollowerAnim->RightGripAlpha = 1.0f;
	Presentation->BeginIndependentMotion(NoMotionKeys, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f);
	Presentation->AutoAttackSourceAnim = SourceAnim;
	Presentation->AutoAttackSourceInstanceID = SourceInstance->GetInstanceID();
	Presentation->bAutoAttackMotionActive = true;
	Presentation->bNotifyOwnsMotion = false;
	Presentation->EndIndependentMotion();
	TestEqual(TEXT("Automatic recovery tracks the outgoing instance, including blend-out"),
		Presentation->ContactRecoverySourceInstanceID, SourceInstance->GetInstanceID());
	Presentation->UpdateContactRecovery(World->GetTimeSeconds() + 10.0);
	TestTrue(TEXT("A paused montage blend cannot finish on an unrelated wall-time clock"), Presentation->bContactRecoveryActive);
	TestTrue(TEXT("Full source weight retains full contact"),
		FMath::IsNearlyEqual(Presentation->GetWeaponGripTargets().PrimaryIKAlpha, 1.0f));
	SourceInstance->SetWeight(0.5f);
	Presentation->UpdateContactRecovery(World->GetTimeSeconds() + 10.0);
	TestTrue(TEXT("Half source pose weight releases exactly half of contact"),
		FMath::IsNearlyEqual(Presentation->GetWeaponGripTargets().PrimaryIKAlpha, 0.5f));
	FollowerAnim->NativeUpdateAnimation(1.0f / 60.0f);
	FollowerAnim->NativeThreadSafeUpdateAnimation(1.0f / 60.0f);
	TestTrue(TEXT("Follower uses montage recovery without a second easing clock"),
		FMath::IsNearlyEqual(FollowerAnim->RightGripAlpha, 0.5f, 0.001f));
	const FTransform MovingSourceContact = Presentation->GetWeaponGripTargets().PrimaryGripWorldTransform;
	Follower->SetRelativeLocation(FVector(100.0f, 0.0f, 0.0f));
	Presentation->UpdateContactRecovery(World->GetTimeSeconds() + 10.0);
	TestTrue(TEXT("Solved follower and attached weapon cannot feed back into the source recovery contact"),
		Presentation->GetWeaponGripTargets().PrimaryGripWorldTransform.Equals(MovingSourceContact, 0.1f));
	Follower->SetRelativeLocation(FVector::ZeroVector);
	const FVector PreviousSourceLocation = Character->GetMesh()->GetRelativeLocation();
	Character->GetMesh()->SetRelativeLocation(PreviousSourceLocation + FVector(10.0f, 0.0f, 0.0f));
	Presentation->UpdateContactRecovery(World->GetTimeSeconds() + 10.0);
	TestTrue(TEXT("Recovery contact follows the returning source pose rather than a frozen wrist target"),
		Presentation->GetWeaponGripTargets().PrimaryGripWorldTransform.GetLocation().Equals(
			MovingSourceContact.GetLocation() + FVector(10.0f, 0.0f, 0.0f), 0.1f));
	Character->GetMesh()->SetRelativeLocation(PreviousSourceLocation);
	SourceInstance->SetWeight(1.0f);
	TestTrue(TEXT("Outgoing fixture has finished its actual pose blend"), FMath::IsNearlyZero(SourceInstance->GetWeight()));
	Presentation->UpdateContactRecovery(World->GetTimeSeconds());
	TestFalse(TEXT("Recovery completes when the outgoing source pose reaches zero contribution"), Presentation->bContactRecoveryActive);
	TestTrue(TEXT("Montage recovery finishes at identity attachment"), WeaponRoot->GetRelativeTransform().Equals(FTransform::Identity, 0.1f));
	TestEqual(TEXT("Completed recovery releases the outgoing instance"), Presentation->ContactRecoverySourceInstanceID, INDEX_NONE);
	SourceInstance->SetWeight(0.0f);
	FollowerAnim->RightGripAlpha = 1.0f;
	Presentation->BeginIndependentMotion(NoMotionKeys, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f);
	Presentation->bAutoAttackMotionActive = true;
	Presentation->bNotifyOwnsMotion = false;
	Presentation->EndIndependentMotion();
	Presentation->BeginIndependentMotion(NoMotionKeys, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f);
	TestEqual(TEXT("A new attack discards the old montage recovery clock"), Presentation->ContactRecoverySourceInstanceID, INDEX_NONE);
	Presentation->bAutoAttackMotionActive = true;
	Presentation->bNotifyOwnsMotion = false;
	Presentation->EndIndependentMotion();
	SourceAnim->MontageInstances.Remove(SourceInstance);
	delete SourceInstance;
	Presentation->UpdateContactRecovery(World->GetTimeSeconds());
	TestFalse(TEXT("An expired source instance cannot leave contact stuck"), Presentation->bContactRecoveryActive);
	TestTrue(TEXT("Expired source instance clears the attachment offset"), WeaponRoot->GetRelativeTransform().Equals(FTransform::Identity, 0.1f));
	Presentation->AutoAttackSourceAnim.Reset();
	Presentation->AutoAttackSourceInstanceID = INDEX_NONE;
	UProject_JHandGripProfile* BodyProfile = NewObject<UProject_JHandGripProfile>(Character);
	BodyProfile->Calibration.PrimaryArm.Shoulder = TEXT("CustomUpperArm");
	BodyProfile->Calibration.PrimaryHandOffset = FTransform(FVector(1.0f, 2.0f, 3.0f));
	FollowerAnim->HandGripProfile = BodyProfile;
	TestEqual(TEXT("Body calibration is reusable on a non-player visual instance"),
		FollowerAnim->GetHandGripCalibration().PrimaryArm.Shoulder, FName(TEXT("CustomUpperArm")));
	TestTrue(TEXT("Reusable body profile preserves contact offsets"),
		FollowerAnim->GetHandGripCalibration().PrimaryHandOffset.Equals(BodyProfile->Calibration.PrimaryHandOffset));
	FollowerAnim->HandGripProfile = nullptr;

	Weapon->AttachToComponent(Follower, FAttachmentTransformRules::SnapToTargetIncludingScale, FollowerBone);
	Presentation->UpdateGripTargets();
	const FProject_JWeaponGripTargets HandGrip = Presentation->GetWeaponGripTargets();
	TestEqual(TEXT("Visible hand drives an attached weapon"), HandGrip.DriveMode, EProject_JWeaponGripDriveMode::PrimaryHand);
	TestTrue(TEXT("Secondary grip is available in primary hand bone space"), HandGrip.bHasPrimaryHandSpaceGrip);
	const FTransform HandSpaceGrip = HandGrip.SecondaryGripInPrimaryHandSpace;
	Follower->SetRelativeLocation(FVector(25.0f, 0.0f, 0.0f));
	const FProject_JWeaponGripTargets MovedHandGrip = Presentation->GetWeaponGripTargets();
	TestTrue(TEXT("Hand-space grip does not drift when the whole follower moves"),
		MovedHandGrip.SecondaryGripInPrimaryHandSpace.Equals(HandSpaceGrip, 0.1f));
	Profile->MotionPresentation.DefaultDrawnSecondaryIKAlpha = 0.0f;
	TestTrue(TEXT("Attack-only secondary policy leaves Idle contact disabled"), FMath::IsNearlyZero(Presentation->GetWeaponGripTargets().SecondaryIKAlpha));
	Presentation->BeginIndependentMotion(NoMotionKeys, 0.7f, 0.0f, 1.0f, 0.0f, 0.0f);
	const FTransform BeforeGripWindow = WeaponRoot->GetComponentTransform();
	const USceneComponent* ParentBeforeGripWindow = WeaponRoot->GetAttachParent();
	Presentation->BeginTwoHandGripNotify(22001, 0.8f, 0.1f, false);
	const FProject_JWeaponGripTargets WindowGrip = Presentation->GetWeaponGripTargets();
	TestTrue(TEXT("Two-hand window controls contact during independent source motion"),
		FMath::IsNearlyEqual(WindowGrip.SecondaryIKAlpha, 0.8f) && FMath::IsNearlyEqual(WindowGrip.PrimaryIKAlpha, 0.7f));
	TestTrue(TEXT("Hand window does not change source trajectory, attachment, or motion owner"),
		WindowGrip.DriveMode == EProject_JWeaponGripDriveMode::AuthoredWeaponMotion && Presentation->bIndependentMotionActive &&
		WeaponRoot->GetAttachParent() == ParentBeforeGripWindow && WeaponRoot->GetComponentTransform().Equals(BeforeGripWindow, 0.001f));
	Presentation->BeginTwoHandGripNotify(22002, 1.0f, 0.25f, true);
	TestTrue(TEXT("An explicit primary override applies without replacing weapon motion"),
		FMath::IsNearlyEqual(Presentation->GetWeaponGripTargets().PrimaryIKAlpha, 0.25f));
	Presentation->EndTwoHandGripNotify(22002);
	TestTrue(TEXT("Ending a primary override restores the underlying source alpha"),
		FMath::IsNearlyEqual(Presentation->GetWeaponGripTargets().PrimaryIKAlpha, 0.7f));
	Presentation->EndTwoHandGripNotify(22001);
	TestTrue(TEXT("Ending support contact restores the source base and keeps motion active"),
		FMath::IsNearlyZero(Presentation->GetWeaponGripTargets().SecondaryIKAlpha) && Presentation->bIndependentMotionActive);
	Presentation->bAutoAttackMotionActive = true;
	Presentation->bNotifyOwnsMotion = false;
	Presentation->BeginTwoHandGripNotify(22003, 1.0f, 1.0f, false);
	TestTrue(TEXT("The same contact window works during automatic notify-free source attacks"),
		FMath::IsNearlyEqual(Presentation->GetWeaponGripTargets().SecondaryIKAlpha, 1.0f));
	Presentation->EndTwoHandGripNotify(22003);
	Presentation->EndIndependentMotion();
	Presentation->UpdateContactRecovery(World->GetTimeSeconds() + 1.0);
	Presentation->BeginIndependentMotion(NoMotionKeys, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f);
	TestTrue(TEXT("Two-handed attack enables support contact on the independent weapon"),
		Presentation->GetWeaponGripTargets().bHasSecondaryGrip && FMath::IsNearlyEqual(Presentation->GetWeaponGripTargets().SecondaryIKAlpha, 1.0f));
	FollowerAnim->LeftGripAlpha = 1.0f;
	Presentation->EndIndependentMotion();
	TestTrue(TEXT("Secondary contact participates in outgoing recovery"), Presentation->bContactRecoveryActive);
	Profile->MotionPresentation.bEnableSecondaryGripContact = false;
	const FProject_JWeaponGripTargets DisabledSupport = Presentation->GetWeaponGripTargets();
	TestTrue(TEXT("Disabling support mid-recovery clears goal, reference and captured alpha"),
		!DisabledSupport.bHasSecondaryGrip && !DisabledSupport.bHasPrimaryHandSpaceGrip && FMath::IsNearlyZero(DisabledSupport.SecondaryIKAlpha));
	Presentation->BeginTwoHandGrip(1.0f, 1.0f, false);
	Presentation->BeginIndependentMotion(NoMotionKeys, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f);
	TestTrue(TEXT("A one-handed profile cannot be enabled by a two-hand notify or motion override"),
		!Presentation->GetWeaponGripTargets().bHasSecondaryGrip && FMath::IsNearlyZero(Presentation->GetWeaponGripTargets().SecondaryIKAlpha));
	// Even explicit legacy component tracking cannot resurrect a missing/disabled
	// contact when the authoritative presentation component exists.
	FollowerAnim->UpdateWeaponTarget(WeaponRoot, PrimarySocket->SocketName);
	Profile->MotionPresentation.PrimaryGripSocketName = TEXT("MissingPrimaryForAvailabilityTest");
	FollowerAnim->NativeUpdateAnimation(1.0f / 60.0f);
	FollowerAnim->NativeThreadSafeUpdateAnimation(1.0f / 60.0f);
	TestTrue(TEXT("Presentation availability beats stale legacy tracking on both hands"),
		!FollowerAnim->bLeftContactTargetValid && !FollowerAnim->bRightContactTargetValid && FMath::IsNearlyZero(FollowerAnim->LeftGripAlpha));
	Profile->MotionPresentation.PrimaryGripSocketName = PrimarySocket->SocketName;
	Presentation->EndTwoHandGrip(); Presentation->EndIndependentMotion();
	Presentation->UpdateContactRecovery(World->GetTimeSeconds() + 1.0);
	Profile->MotionPresentation.bEnableSecondaryGripContact = true;
	Profile->MotionPresentation.DefaultDrawnSecondaryIKAlpha = 1.0f;
	TestTrue(TEXT("Re-equipping an enabled two-handed policy recovers secondary availability"), Presentation->GetWeaponGripTargets().bHasSecondaryGrip);
	TraceEnabled->Set(PreviousTraceEnabled, ECVF_SetByCode);
	TraceActor->Set(*PreviousTraceActor, ECVF_SetByCode);

	// Ground windows cross montage cancellation/replacement just like motion windows.
	auto* GroundNotify = NewObject<UProject_JAnimNotifyState_WeaponGroundContact>();
	FAnimNotifyEventReference GroundA, GroundB;
	GroundA.SetNotifyInstanceID(21001); GroundB.SetNotifyInstanceID(21002);
	TestTrue(TEXT("Outgoing ground-contact motion begins"), Presentation->BeginIndependentMotion(NoMotionKeys, 1, 1, 1, 0, 0));
	GroundNotify->NotifyBegin(Character->GetMesh(), nullptr, 1, GroundA);
	Presentation->EndIndependentMotion();
	TestTrue(TEXT("Replacement ground-contact motion begins"), Presentation->BeginIndependentMotion(NoMotionKeys, 1, 1, 1, 0, 0));
	GroundNotify->NotifyBegin(Character->GetMesh(), nullptr, 1, GroundB);
	GroundNotify->NotifyEnd(Character->GetMesh(), nullptr, GroundA);
	TestEqual(TEXT("Old ground NotifyEnd cannot close the replacement window"), Presentation->GroundContactNotifyTokens.Num(), 1);
	GroundNotify->NotifyBegin(Character->GetMesh(), nullptr, 1, GroundB);
	GroundNotify->NotifyEnd(Character->GetMesh(), nullptr, GroundB);
	TestEqual(TEXT("Duplicate ground Begin does not leak a contact window"), Presentation->GroundContactNotifyTokens.Num(), 0);
	Presentation->EndIndependentMotion();

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
