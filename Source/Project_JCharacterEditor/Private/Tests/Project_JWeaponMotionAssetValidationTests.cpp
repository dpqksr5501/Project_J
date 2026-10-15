#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/DataValidation.h"
#include "Validation/Project_JWeaponMotionAssetValidator.h"
#include "Animation/AnimMontage.h"
#include "Animation/Project_JAnimNotifyState_WeaponMotion.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJWeaponMotionAssetValidationTest,
	"ProjectJ.Continuation.WeaponMotionParentAssetValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJWeaponMotionAssetValidationTest::RunTest(const FString&)
{
	auto* Montage = NewObject<UAnimMontage>();
	auto* Motion = NewObject<UProject_JAnimNotifyState_WeaponMotion>(Montage);
	auto& Event = Montage->Notifies.AddDefaulted_GetRef();
	Event.NotifyStateClass = Motion;
	Event.SetDuration(1.f);
	Motion->MotionKeys.AddDefaulted();
	Motion->MotionKeys[0].NormalizedTime = std::numeric_limits<float>::quiet_NaN();
	auto* Validator = NewObject<UProject_JWeaponMotionAssetValidator>();
	FDataValidationContext InvalidContext;
	TestEqual(TEXT("Parent montage validation reaches the actual notify numeric data"),
		Validator->ValidateLoadedAsset(FAssetData(Montage), Montage, InvalidContext), EDataValidationResult::Invalid);
	TestTrue(TEXT("Failure includes a useful asset-level diagnostic"), InvalidContext.GetNumErrors() > 0);
	Motion->MotionKeys[0].NormalizedTime = 0.f;
	FDataValidationContext ValidContext;
	TestEqual(TEXT("Corrected parent asset passes without changing runtime defaults"),
		Validator->ValidateLoadedAsset(FAssetData(Montage), Montage, ValidContext), EDataValidationResult::Valid);
	return true;
}
#endif
