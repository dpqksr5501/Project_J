#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JRetargetBodyProfile.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/Project_JHandContact.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Retargeter/IKRetargeter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJRetargetBodyContracts, "ProjectJ.Retarget.BodyContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJRetargetBodyContracts::RunTest(const FString&)
{
	auto* Source = NewObject<USkeleton>(); auto* Target = NewObject<USkeleton>(); auto* Other = NewObject<USkeleton>();
	auto* Family = NewObject<UProject_JRetargetFamilyProfile>();
	Family->SourceSkeleton = Source; Family->TargetSkeleton = Target; Family->Retargeter = NewObject<UIKRetargeter>();
	TestTrue(TEXT("Family matches an explicit source/target pair"), Family->Matches(Source, Target));
	TestFalse(TEXT("Different source cannot reuse the bridge"), Family->Matches(Other, Target));
	TestFalse(TEXT("Different target cannot reuse the bridge"), Family->Matches(Source, Other));
	TestFalse(TEXT("Absent source cannot use the bridge"), Family->Matches(nullptr, Target));
	auto* Asset = NewObject<USkeletalMesh>(); Asset->SetSkeleton(Target);
	auto* Mesh = NewObject<USkeletalMeshComponent>(); Mesh->SetSkeletalMeshAsset(Asset);
	auto* Anim = NewObject<UProject_JRetargetAnimInstance>(Mesh);
	auto* Body = NewObject<UProject_JRetargetBodyProfile>(); Body->Family = Family;
	Body->HandGrip = NewObject<UProject_JHandGripProfile>();
	Body->HandGrip->Calibration.PrimaryArm.Hand = TEXT("ArbitraryTargetWrist");
	Anim->RetargetBodyProfile = Body;
	TestEqual(TEXT("Body supplies contact roles on its target family"), Anim->GetHandGripCalibration().PrimaryArm.Hand, FName(TEXT("ArbitraryTargetWrist")));
	Anim->HandGripProfile = NewObject<UProject_JHandGripProfile>();
	Anim->HandGripProfile->Calibration.PrimaryArm.Hand = TEXT("ExplicitOverride");
	TestEqual(TEXT("Legacy explicit overrides keep priority"), Anim->GetHandGripCalibration().PrimaryArm.Hand, FName(TEXT("ExplicitOverride")));
	Anim->HandGripProfile = nullptr; Asset->SetSkeleton(Other);
	TestNull(TEXT("Changing visual skeleton rejects stale body binding"), Anim->GetRetargetBodyProfile());
	TestEqual(TEXT("Rejected body does not leak its calibration"), Anim->GetHandGripCalibration().PrimaryArm.Hand, NAME_None);
	Asset->SetSkeleton(Target);
	TestEqual(TEXT("Restoring the target resolves the body again"), Anim->GetRetargetBodyProfile(), static_cast<const UProject_JRetargetBodyProfile*>(Body));
	return true;
}
#endif
