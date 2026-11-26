#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Validation/Project_JCrossAssetValidator.h"
#include "Validation/Project_JCrossAssetEditorValidator.h"
#include "Components/StaticMeshComponent.h"
#include "AI/Project_JThreatComponent.h"
#include <limits>
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/DataValidation.h"
#include "World/Project_JDistantMeshPolicy.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartition/RuntimeHashSet/WorldPartitionRuntimeHashSet.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPhase3CrossAssetValidatorTest, "ProjectJ.Systems.Phase3.CrossAssetValidator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPhase3CrossAssetValidatorTest::RunTest(const FString&)
{
	using namespace Project_J::EditorValidation;
	auto* Brain = NewObject<UProject_JNPCBrainDefinition>();
	auto* Validator = NewObject<UProject_JCrossAssetEditorValidator>();
	const auto ValidateBrain = [&]()
	{
		FDataValidationContext Context;
		return Validator->ValidateLoadedAsset(FAssetData(Brain), Brain, Context);
	};
	TestEqual(TEXT("Default NPC brain passes"), ValidateBrain(), EDataValidationResult::Valid);
	Brain->LeashRadiusCm = std::numeric_limits<float>::quiet_NaN();
	TestEqual(TEXT("NaN leash is rejected"), ValidateBrain(), EDataValidationResult::Invalid);
	Brain->LeashRadiusCm = 3500.f;
	Brain->UnreachableEvadeSeconds = std::numeric_limits<float>::infinity();
	TestEqual(TEXT("An infinite unreachable timeout is rejected"), ValidateBrain(), EDataValidationResult::Invalid);

	TestTrue(TEXT("Non-MM NPC passes"), ValidateNPCAnimPolicy(TEXT("ABP_NPC_Goblin"), false).bPassed);
	TestFalse(TEXT("NPC with PoseSearch/Chooser blocked"), ValidateNPCAnimPolicy(TEXT("ABP_NPC_Bad"), true).bPassed);

	TestTrue(TEXT("Properly authored WPO cutoff passes"),
		ValidateCanopyWindDistance(TEXT("PineInstances"), 10000).bPassed);
	TestFalse(TEXT("Active WPO without a cutoff is detected"),
		ValidateCanopyWindDistance(TEXT("BirchInstances"), 0).bPassed);
	TestFalse(TEXT("Active WPO beyond the cutoff ceiling is detected"),
		ValidateCanopyWindDistance(TEXT("BirchInstances"), 10001).bPassed);
	TestTrue(TEXT("Foliage with no confirmed WPO does not require a cutoff"),
		ValidateCanopyWindDistance(TEXT("StaticTreeInstances"), {}).bPassed);

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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJDistantPlacementValidationTest, "ProjectJ.Systems.World.DistantPlacementValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJDistantPlacementValidationTest::RunTest(const FString&)
{
	UWorld::InitializationValues Settings;
	Settings.CreateWorldPartition(true).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	auto* World = UWorld::CreateWorld(EWorldType::Editor, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
	if (!TestNotNull(TEXT("Second WP world"), World)) return false;
	auto* Actor = World->SpawnActor<AStaticMeshActor>();
	auto* Mesh = NewObject<UStaticMesh>();
	auto* Policy = NewObject<UProject_JDistantMeshPolicy>(Mesh);
	Policy->Role = EProject_JDistantMeshRole::InstancedCanopy;
	Mesh->AddAssetUserData(Policy);
	Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
	auto* Layer = NewObject<UHLODLayer>(World);
	Layer->SetLayerType(EHLODLayerType::Instancing);
	auto* Partition = World->GetWorldPartition();
	Partition->SetDefaultHLODLayer(Layer);
	auto* RuntimeHash = NewObject<UWorldPartitionRuntimeHashSet>(Partition);
	RuntimeHash->SetDefaultValues();
	Partition->RuntimeHash = RuntimeHash;
	auto* Validator = NewObject<UProject_JCrossAssetEditorValidator>();
	const auto Validate = [&]()
	{
		FDataValidationContext Context;
		return Validator->ValidateLoadedAsset(FAssetData(Actor), Actor, Context);
	};
	TestEqual(TEXT("Untagged mesh in a different WP map inherits the world layer"), Validate(), EDataValidationResult::Valid);
	Layer->SetLayerType(EHLODLayerType::MeshMerge);
	TestEqual(TEXT("Canopy rejects a non-instancing inherited layer"), Validate(), EDataValidationResult::Invalid);
	Policy->Role = EProject_JDistantMeshRole::DistantLandmark;
	TestEqual(TEXT("A landmark permits the same merge builder"), Validate(), EDataValidationResult::Valid);
	World->GetWorldPartition()->SetDefaultHLODLayer(nullptr);
	TestEqual(TEXT("A required landmark rejects a missing layer"), Validate(), EDataValidationResult::Invalid);
	Actor->SetHLODLayer(Layer);
	TestEqual(TEXT("Local layer takes effect without a world default"), Validate(), EDataValidationResult::Valid);
	auto* UnregisteredLayer = NewObject<UHLODLayer>(World);
	UnregisteredLayer->SetLayerType(EHLODLayerType::Instancing);
	Actor->SetHLODLayer(UnregisteredLayer);
	Policy->Role = EProject_JDistantMeshRole::InstancedCanopy;
	TestEqual(TEXT("An Instancing layer outside the runtime partition is rejected"), Validate(), EDataValidationResult::Invalid);
	Actor->SetHLODLayer(Layer);
	Policy->Role = EProject_JDistantMeshRole::DistantLandmark;
	Actor->bEnableAutoLODGeneration = false;
	TestEqual(TEXT("Participation removed from required canopy is rejected"), Validate(), EDataValidationResult::Invalid);
	Actor->GetStaticMeshComponent()->ComponentTags.Add(TEXT("ProjectJ.World.NearOnly"));
	TestEqual(TEXT("Explicit placement exception is accepted"), Validate(), EDataValidationResult::Valid);
	Actor->GetStaticMeshComponent()->ComponentTags.Reset();
	World->GetWorldPartition()->SetEnableStreaming(false);
	TestEqual(TEXT("A non-streaming partition does not require outdoor HLOD"), Validate(), EDataValidationResult::NotValidated);
	World->DestroyWorld(false);
	return true;
}
#endif
