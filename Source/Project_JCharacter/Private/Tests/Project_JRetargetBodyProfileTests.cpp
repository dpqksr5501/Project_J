#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JRetargetBodyProfile.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/Project_JHandContact.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Retargeter/IKRetargeter.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

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
#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJRetargetFamilyValidationTest, "ProjectJ.Retarget.FamilyAuthoringValidation",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJRetargetFamilyValidationTest::RunTest(const FString&)
{
 auto* Authored = LoadObject<UProject_JRetargetFamilyProfile>(nullptr,
  TEXT("/Game/DataAssetSets/Presentation/Retarget/DA_RetargetFamily_Mannequin_Bip01.DA_RetargetFamily_Mannequin_Bip01"));
 if (!TestNotNull(TEXT("Actual production family"), Authored) || !TestNotNull(TEXT("Actual retargeter"), Authored->Retargeter.Get())) return false;
 FDataValidationContext Production;
 TestTrue(TEXT("Production family validates"), Authored->IsDataValid(Production) == EDataValidationResult::Valid);
 auto* Invalid = NewObject<UProject_JRetargetFamilyProfile>();
 Invalid->SourceSkeleton = Authored->TargetSkeleton;
 Invalid->TargetSkeleton = Authored->SourceSkeleton;
 Invalid->Retargeter = Authored->Retargeter;
 FDataValidationContext Pair;
 TestTrue(TEXT("Declared skeleton pair cannot disagree with its retargeter's source/target rigs"),
  Invalid->IsDataValid(Pair) == EDataValidationResult::Invalid);
 auto* Body = NewObject<UProject_JRetargetBodyProfile>(); Body->Family = Invalid;
 FDataValidationContext BodyPair;
 TestTrue(TEXT("Body cannot validate an incompatible family dependency"), Body->IsDataValid(BodyPair) == EDataValidationResult::Invalid);
 return true;
}
#endif
#endif
