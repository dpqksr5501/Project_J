#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JHandGripProfile.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Equipment/Project_JWeaponPresentationProfile.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPrimaryGripAttachmentTest,
	"ProjectJ.Presentation.PrimaryGripAttachment", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJPrimaryGripAttachmentTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ACharacter* Character = World->SpawnActor<ACharacter>();
	USkeletalMesh* Fixture = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"));
	if (!TestNotNull(TEXT("Source fixture"), Fixture))
	{
		GEngine->DestroyWorldContext(World); World->DestroyWorld(false); return false;
	}
	Character->GetMesh()->SetSkeletalMesh(Fixture);
	const FName Hand = Fixture->GetRefSkeleton().GetBoneName(0);
	// Duplicate in memory only: test-specific body sockets never modify project assets.
	USkeletalMesh* BodyAsset = DuplicateObject<USkeletalMesh>(Fixture, Character);
	USkeletalMeshSocket* Palm = NewObject<USkeletalMeshSocket>(BodyAsset);
	Palm->SocketName = TEXT("BodySpecificContact");
	Palm->BoneName = Hand;
	Palm->RelativeLocation = FVector(9, -3, 2);
	Palm->RelativeRotation = FRotator(25, -18, 67);
	BodyAsset->GetMeshOnlySocketList().Add(Palm);
	USkeletalMeshComponent* Body = NewObject<USkeletalMeshComponent>(Character);
	Body->SetupAttachment(Character->GetMesh());
	Body->SetSkeletalMesh(BodyAsset);
	Body->SetRelativeScale3D(FVector(1.25));
	Body->ComponentTags.Add(TEXT("VisualFollower"));
	Character->AddInstanceComponent(Body); Body->RegisterComponent();
	UProject_JRetargetAnimInstance* Anim = NewObject<UProject_JRetargetAnimInstance>(Body);
	Body->AnimScriptInstance = Anim;
	UProject_JHandGripProfile* Calibration = NewObject<UProject_JHandGripProfile>(Character);
	Calibration->Calibration.PrimaryPalmSocketName = Palm->SocketName;
	Calibration->Calibration.PrimaryArm.Hand = Hand;
	Calibration->Calibration.PrimaryHandOffset = FTransform(FRotator(5, 14, -9), FVector(2, 1, -3));
	Anim->HandGripProfile = Calibration;

	AActor* Weapon = World->SpawnActor<AActor>();
	USceneComponent* Root = NewObject<USceneComponent>(Weapon);
	Weapon->SetRootComponent(Root); Root->RegisterComponent();
	UStaticMesh* WeaponAsset = NewObject<UStaticMesh>(Weapon);
	UStaticMeshSocket* Grip = NewObject<UStaticMeshSocket>(WeaponAsset);
	Grip->SocketName = TEXT("CustomPrimaryContact");
	Grip->RelativeLocation = FVector(3, 5, -2);
	Grip->RelativeRotation = FRotator(-15, 31, 12);
	WeaponAsset->AddSocket(Grip);
	UStaticMeshComponent* Child = NewObject<UStaticMeshComponent>(Weapon);
	Child->SetupAttachment(Root); Child->SetStaticMesh(WeaponAsset);
	Child->SetRelativeTransform(FTransform(FRotator(16, -38, 25), FVector(13, -7, 6), FVector(2)));
	Child->RegisterComponent();
	UProject_JWeaponPresentationProfile* Profile = NewObject<UProject_JWeaponPresentationProfile>(Character);
	Profile->WeaponActorClass = AActor::StaticClass();
	Profile->DrawnSocketName = Hand;
	Profile->VisualDrawnSocketName = TEXT("MissingLegacyVisualMount");
	Profile->SheathedSocketName = Hand;
	Profile->VisualSheathedSocketName = TEXT("MissingLegacyVisualSheath");
	Profile->MotionPresentation.PrimaryGripSocketName = Grip->SocketName;
	Profile->MotionPresentation.bSupportsIndependentMotion = true;
	Profile->MotionPresentation.bAllowPrimaryIKOnVisualAttachment = true;
	UProject_JWeaponPresentationComponent* Presentation = NewObject<UProject_JWeaponPresentationComponent>(Character);
	Character->AddInstanceComponent(Presentation); Presentation->RegisterComponent();
	Presentation->AppliedProfile = Profile;
	Presentation->AppliedActorClass = AActor::StaticClass();
	Presentation->AppliedCharacterMesh = Character->GetMesh();
	Presentation->AppliedSkeletalMesh = Fixture;
	Presentation->SpawnedWeapon = Weapon;
	Presentation->UpdateSocketComponentCache();
	Presentation->AttachWeaponToDrawnSocket();
	TestEqual(TEXT("Existing assets retain the source/custom socket default"), Root->GetAttachParent(), static_cast<USceneComponent*>(Character->GetMesh()));
	TestTrue(TEXT("Legacy actor root still snaps with identity relative transform"), Root->GetRelativeTransform().Equals(FTransform::Identity));
	Profile->DrawnAttachmentMode = EProject_JDrawnAttachmentMode::PrimaryGripContact;
	Presentation->AttachWeaponToDrawnSocket();
	const auto CheckContact = [&]()
	{
		const FTransform Goal = Calibration->Calibration.PrimaryHandOffset * Child->GetSocketTransform(Grip->SocketName, RTS_World);
		const FTransform Actual = Body->GetSocketTransform(Palm->SocketName, RTS_World);
		return Actual.GetLocation().Equals(Goal.GetLocation(), 0.001) && Actual.GetRotation().Equals(Goal.GetRotation(), 0.001);
	};
	TestEqual(TEXT("A missing Visual socket does not prevent Palm mounting"), Root->GetAttachParent(), static_cast<USceneComponent*>(Body));
	TestTrue(TEXT("Contact is exact with authored child transform and uniform scale"), CheckContact());
	TestTrue(TEXT("Weapon scale is preserved on the body"), Root->GetRelativeScale3D().Equals(FVector::OneVector) && Child->GetRelativeScale3D().Equals(FVector(2)));
	TestTrue(TEXT("Palm attachment always suppresses primary self-follow, even with legacy override enabled"),
		Presentation->GetWeaponGripTargets().bPrimaryIKSuppressedByAttachment);
	const FTransform IdleMount = Root->GetRelativeTransform();
	TestEqual(TEXT("Successful resolution explains the active contact mount"), Presentation->ResolveAttachment(true).Reason, FName(TEXT("PrimaryGripContact")));
	// Simulate inherited scale from an outgoing sheath/source attachment. It
	// cannot change the authored contact or veto the destination's valid scale.
	Root->SetWorldScale3D(FVector(2, 3, 4));
	const FProject_JResolvedWeaponAttachment AfterOutgoingScale = Presentation->ResolveAttachment(true);
	TestTrue(TEXT("Previous mount anisotropy cannot block valid Palm attachment"), AfterOutgoingScale.bPrimaryContact);
	TestTrue(TEXT("Contact calibration depends only on authored local weapon transforms"), AfterOutgoingScale.Relative.Equals(IdleMount, 0.0001));
	Presentation->AttachWeaponToDrawnSocket();
	TestTrue(TEXT("Switching from a differently scaled mount restores exact contact"), CheckContact());
	Body->SetRelativeScale3D(FVector(1.25, 1.250003, 1.249998));
	TestTrue(TEXT("Harmless body/hand float scale roundoff does not select legacy mount"), Presentation->ResolveAttachment(true).bPrimaryContact);
	Body->SetRelativeScale3D(FVector(1.25, 1.3, 1.25));
	TestEqual(TEXT("Genuine body anisotropy still has an explicit fallback reason"), Presentation->ResolveAttachment(true).Reason, FName(TEXT("UnsupportedBodyScale")));
	Body->SetRelativeScale3D(FVector(1.25));
	Presentation->AttachWeaponToDrawnSocket();
	IConsoleVariable* Trace = IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Presentation.GripTrace"));
	IConsoleVariable* Filter = IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Presentation.GripTraceActor"));
	if (Trace && Filter)
	{
		const int32 OldTrace = Trace->GetInt();
		const FString OldFilter = Filter->GetString();
		Trace->Set(1, ECVF_SetByCode); Filter->Set(*Character->GetName(), ECVF_SetByCode);
		const FTransform BeforeTrace = Root->GetComponentTransform();
		Presentation->LogAttachmentTrace(TEXT("AutomationIdle"), true);
		Presentation->SetComponentTickEnabled(false);
		Presentation->GetWeaponGripTargetsForAnimation(0);
		TestTrue(TEXT("Enabling diagnostics in Idle wakes the existing sample tick"), Presentation->IsComponentTickEnabled());
		TestTrue(TEXT("Attachment diagnosis cannot alter the mount or its world pose"),
			Root->GetRelativeTransform().Equals(IdleMount) && Root->GetComponentTransform().Equals(BeforeTrace));
		Body->AnimScriptInstance = nullptr;
		Presentation->LogAttachmentTrace(TEXT("AutomationMissingAnim"), true);
		TestTrue(TEXT("Diagnosis can report a missing follower animation without moving the weapon"), Root->GetComponentTransform().Equals(BeforeTrace));
		Body->AnimScriptInstance = Anim;
		Trace->Set(OldTrace, ECVF_SetByCode); Filter->Set(*OldFilter, ECVF_SetByCode);
		Presentation->SetComponentTickEnabled(false);
	}
	const TArray<FProject_JWeaponMotionKey> NoKeys;
	TestTrue(TEXT("Palm-mounted weapon can enter source-authored attack motion"), Presentation->BeginIndependentMotion(NoKeys, 1, 0, 1, 0, 0));
	TestEqual(TEXT("Attack ownership belongs to source skeleton"), Root->GetAttachParent(), static_cast<USceneComponent*>(Character->GetMesh()));
	TestFalse(TEXT("Independent motion can solve primary hand without attachment feedback"), Presentation->GetWeaponGripTargets().bPrimaryIKSuppressedByAttachment);
	Anim->RightGripAlpha = 1;
	const FTransform BeforeEnd = Root->GetComponentTransform();
	Presentation->EndIndependentMotion();
	TestTrue(TEXT("Contact recovery preserves the outgoing weapon world pose"), Root->GetComponentTransform().Equals(BeforeEnd, 0.001));
	TestTrue(TEXT("Recovery destination is calibrated mount, not actor origin"), Presentation->ContactRecoveryDestination.Equals(IdleMount, 0.001));
	Presentation->UpdateContactRecovery(Presentation->ContactRecoveryStartSeconds + 1);
	TestTrue(TEXT("Recovery ends at the same Idle contact"), CheckContact() && Root->GetRelativeTransform().Equals(IdleMount, 0.001));
	TestTrue(TEXT("Recovery releases primary IK back to hand ownership"), Presentation->GetWeaponGripTargets().bPrimaryIKSuppressedByAttachment);
	// Recalibration during an attack is deferred; the ownership boundary resolves the current profile.
	Presentation->BeginIndependentMotion(NoKeys, 1, 0, 1, 0, 0);
	const FTransform InAttack = Root->GetComponentTransform();
	Calibration->Calibration.PrimaryHandOffset = FTransform(FRotator(-6, 18, 12), FVector(-4, 2, 1));
	Presentation->RefreshAttachmentCalibration();
	TestTrue(TEXT("Body recalibration cannot remount an active source attack"), Root->GetComponentTransform().Equals(InAttack));
	Anim->RightGripAlpha = 1;
	Presentation->EndIndependentMotion();
	Presentation->UpdateContactRecovery(Presentation->ContactRecoveryStartSeconds + 1);
	TestTrue(TEXT("Deferred body calibration is used when recovery completes"), CheckContact());
	const FTransform Recalibrated = Root->GetRelativeTransform();
	Calibration->Calibration.PrimaryHandOffset.SetLocation(FVector(1, -5, 3));
	Presentation->RefreshAttachmentCalibration();
	TestTrue(TEXT("Idle profile refresh updates the existing actor without a respawn"), Presentation->SpawnedWeapon == Weapon && CheckContact() && !Root->GetRelativeTransform().Equals(Recalibrated));
	// A replacement body using the same skeleton can author its own Mesh Socket.
	USkeletalMesh* ReplacementBody = DuplicateObject<USkeletalMesh>(Fixture, Character);
	USkeletalMeshSocket* ReplacementPalm = NewObject<USkeletalMeshSocket>(ReplacementBody);
	ReplacementPalm->SocketName = Palm->SocketName;
	ReplacementPalm->BoneName = Hand;
	ReplacementPalm->RelativeLocation = FVector(-6, 8, 5);
	ReplacementPalm->RelativeRotation = FRotator(-30, 45, 21);
	ReplacementBody->GetMeshOnlySocketList().Add(ReplacementPalm);
	Body->SetSkeletalMesh(ReplacementBody);
	Body->AnimScriptInstance = Anim;
	Presentation->RefreshAttachmentCalibration();
	TestTrue(TEXT("Body mesh replacement uses its own Palm and preserves the equipped actor"), Presentation->SpawnedWeapon == Weapon && CheckContact());
	Profile->MotionPresentation.bUseContactHandoff = false;
	Presentation->BeginIndependentMotion(NoKeys, 1, 0, 1, 0, 0.2f);
	Presentation->SetIndependentMotionPosition(1);
	Presentation->GetWeaponGripTargetsForAnimation(0);
	const auto Destination = Presentation->ResolveAttachment(true);
	TestTrue(TEXT("Legacy exit blend also returns to the calibrated endpoint"),
		Root->GetComponentTransform().Equals(Destination.Relative * Body->GetSocketTransform(Hand, RTS_World), 0.001));
	Presentation->EndIndependentMotion();
	TestTrue(TEXT("A disabled handoff returns to exact contact without a second recovery"), !Presentation->bContactRecoveryActive && CheckContact());
	// Contact mounting is independent of whether this job uses attack hand IK.
	Profile->MotionPresentation.bSupportsIndependentMotion = false;
	Profile->MotionPresentation.DefaultDrawnPrimaryIKAlpha = 0;
	Presentation->RefreshAttachmentCalibration();
	TestTrue(TEXT("A no-IK job still mounts at its body Palm"), CheckContact());
	Calibration->Calibration.PrimaryPalmSocketName = TEXT("MissingPalm");
	Calibration->Calibration.MissingPalmPolicy = EProject_JMissingPalmPolicy::LegacyWristOrigin;
	const auto Missing = Presentation->ResolveAttachment(true);
	TestFalse(TEXT("Legacy wrist IK compatibility cannot fabricate a Palm mount"), Missing.bPrimaryContact);
	TestEqual(TEXT("Diagnosis identifies the missing body Palm"), Missing.Reason, FName(TEXT("MissingPalm")));
	TestEqual(TEXT("A missing Palm has an explicit compatibility source mount"), Missing.Mesh.Get(), Character->GetMesh());
	Calibration->Calibration.PrimaryPalmSocketName = Palm->SocketName;
	Child->SetRelativeScale3D(FVector(1, 2, 1));
	TestFalse(TEXT("Nonuniform child scale uses custom/compatibility ownership"), Presentation->ResolveAttachment(true).bPrimaryContact);
	TestEqual(TEXT("Diagnosis identifies the unsupported weapon child scale"), Presentation->ResolveAttachment(true).Reason, FName(TEXT("UnsupportedWeaponChildScale")));
	Child->SetRelativeScale3D(FVector(2));
	Child->SetAbsolute(true, false, false);
	TestFalse(TEXT("World-fixed weapon components cannot be silently contact-mounted"), Presentation->ResolveAttachment(true).bPrimaryContact);
	TestEqual(TEXT("Diagnosis identifies a world-fixed weapon child"), Presentation->ResolveAttachment(true).Reason, FName(TEXT("AbsoluteWeaponChild")));
	Child->SetAbsolute(false, false, false);
	Presentation->AttachWeaponToSheathedSocket();
	TestEqual(TEXT("Sheath remains an authored body/socket mount"), Root->GetAttachParent(), static_cast<USceneComponent*>(Character->GetMesh()));
	TestTrue(TEXT("Sheath clears Palm ownership and uses identity root attachment"), !Presentation->bPrimaryContactAttachment && Root->GetRelativeTransform().Equals(FTransform::Identity));
	GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
	return true;
}
#endif
