#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Validation/Project_JCrossAssetValidator.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPhase3CrossAssetValidatorTest, "ProjectJ.Systems.Phase3.CrossAssetValidator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPhase3CrossAssetValidatorTest::RunTest(const FString&)
{
	using namespace Project_J::EditorValidation;

	TestTrue(TEXT("Non-MM NPC passes"), ValidateNPCAnimPolicy(TEXT("ABP_NPC_Goblin"), false).bPassed);
	TestFalse(TEXT("NPC with PoseSearch/Chooser blocked"), ValidateNPCAnimPolicy(TEXT("ABP_NPC_Bad"), true).bPassed);

	TestTrue(TEXT("Properly authored foliage passes"),
		ValidateFoliageAssetPolicy(TEXT("PineInstances"), true, TOptional<int32>(10000)).bPassed);
	TestFalse(TEXT("Foliage without an explicit Instancing HLOD layer is detected"),
		ValidateFoliageAssetPolicy(TEXT("OakInstances"), false, TOptional<int32>(8000)).bPassed);
	TestFalse(TEXT("Active WPO without a cutoff is detected"),
		ValidateFoliageAssetPolicy(TEXT("BirchInstances"), true, TOptional<int32>(0)).bPassed);
	TestFalse(TEXT("Active WPO beyond the cutoff ceiling is detected"),
		ValidateFoliageAssetPolicy(TEXT("BirchInstances"), true, TOptional<int32>(10001)).bPassed);
	TestTrue(TEXT("Foliage with no confirmed WPO does not require a cutoff"),
		ValidateFoliageAssetPolicy(TEXT("StaticTreeInstances"), true, {}).bPassed);

	// Verify actual repository Config/DefaultScalability.ini keeps Skeletal RT OFF on @2/@3.
	FString ScalabilityIni;
	const FString IniPath = FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("DefaultScalability.ini"));
	if (TestTrue(TEXT("Loaded DefaultScalability.ini"), FFileHelper::LoadFileToString(ScalabilityIni, *IniPath)))
	{
		const auto Res = ValidateScalabilitySkeletalRTPolicy(ScalabilityIni);
		TestTrue(TEXT("Actual DefaultScalability.ini keeps Skeletal RT OFF on High/Epic"), Res.bPassed);
	}
	TestFalse(TEXT("Skeletal RT enabled on @3 detected and blocked"),
		ValidateScalabilitySkeletalRTPolicy(TEXT("[GlobalIlluminationQuality@3]\nr.RayTracing.Geometry.SkeletalMeshes=1\n")).bPassed);
	return true;
}
#endif
