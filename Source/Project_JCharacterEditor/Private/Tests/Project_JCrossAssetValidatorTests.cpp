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
		ValidateFoliageAssetPolicy(TEXT("SM_PineTree"), TEXT("HLODLayer_Foliage_Impostor"), 8000.f).bPassed);
	TestFalse(TEXT("Foliage without Impostor HLOD blocked"),
		ValidateFoliageAssetPolicy(TEXT("SM_OakTree"), TEXT("HLODLayer_MeshMerge"), 8000.f).bPassed);
	TestFalse(TEXT("Foliage without WPO distance cut blocked"),
		ValidateFoliageAssetPolicy(TEXT("SM_BirchTree"), TEXT("HLODLayer_Foliage_Impostor"), 0.f).bPassed);

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
