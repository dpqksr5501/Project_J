#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JRetargetBodyProfile.h"
#include "Project_JPlayerCharacter.h"
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAuthoredRetargetSourceTest, "ProjectJ.Retarget.AuthoredMoverSourceFamily",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAuthoredRetargetSourceTest::RunTest(const FString&)
{
 // The canonical source belongs to the shared master. Compatible children can
 // replace that source mesh and do not consume this incompatible-body bridge.
 const UClass* AvatarClass=LoadClass<AProject_JPlayerCharacter>(nullptr,TEXT("/Game/Character_BPs/BP_Player_Master.BP_Player_Master_C"));
 const auto* Avatar=AvatarClass?AvatarClass->GetDefaultObject<AProject_JPlayerCharacter>():nullptr;
 const auto* Source=Avatar && Avatar->GetMesh()?Avatar->GetMesh()->GetSkeletalMeshAsset():nullptr;
 auto* Body=LoadObject<UProject_JRetargetBodyProfile>(nullptr,TEXT("/Game/DataAssetSets/Presentation/Retarget/DA_RetargetBody_Greatsword.DA_RetargetBody_Greatsword"));
 if(!TestNotNull(TEXT("Authored avatar pose source"),Source) || !TestNotNull(TEXT("Authored body profile"),Body) || !TestNotNull(TEXT("Authored source/target family"),Body->Family.Get())) return false;
 auto* Family=Body->Family.Get();
 TestTrue(TEXT("Body override owns the actual Mover source skeleton, not a silent graph fallback"),Family->Matches(Source->GetSkeleton(),Family->TargetSkeleton));
 if(!TestNotNull(TEXT("Retargeter retained"),Family->Retargeter.Get())) return false;
 const auto* RigSource=Family->Retargeter->GetPreviewMesh(ERetargetSourceOrTarget::Source);
 const auto* RigTarget=Family->Retargeter->GetPreviewMesh(ERetargetSourceOrTarget::Target);
 TestEqual(TEXT("Source rig uses the actual authored avatar source mesh"),RigSource,Source);
 TestTrue(TEXT("Target rig retains the body's target skeleton"),RigTarget && RigTarget->GetSkeleton()==Family->TargetSkeleton);
 TestNotNull(TEXT("Existing hand calibration is preserved"),Body->HandGrip.Get());
 FDataValidationContext Context;
 TestTrue(TEXT("The complete authored family validates"),Family->IsDataValid(Context)==EDataValidationResult::Valid);
 return true;
}
#endif
#endif
