#include "PSOPrecacheMaterial.h"
#include "MaterialShared.h"
#include "HAL/IConsoleManager.h"
#include "Misc/EngineVersionComparison.h"

namespace ProjectJDistortionPrecache
{
	static int32 FindCollector(const TCHAR* Name)
	{
		// GetIndex is validation-only in UE 5.8. Runtime precaching must also
		// work when validation is disabled, including packaged production builds.
		const EShadingPath Path = EShadingPath::Deferred;
		// The array is exported, but its inline count is module-local in modular
		// Editor builds. Inspect the bounded exported table instead.
		for (int32 Index = 0; Index < FPSOCollectorCreateManager::MaxPSOCollectorCount; ++Index)
		{
			const TCHAR* RegisteredName = FPSOCollectorCreateManager::GetName(Path, Index);
			if (RegisteredName && FCString::Strcmp(RegisteredName, Name) == 0
				&& FPSOCollectorCreateManager::GetCreateFunction(Path, Index)) { return Index; }
		}
		return INDEX_NONE;
	}

	// UE 5.8's deferred rough-refraction collector omits VarianceCoverage.
	// Supplement the engine collector through its public interface. Do not replace
	// its shader/state selection, alter visual settings, or modify installed engines.
	static TAutoConsoleVariable<int32> CVarEnabled(TEXT("ProjectJ.Rendering.PrecacheRoughRefraction"), 1,
		TEXT("Supplement UE 5.8 rough-refraction PSOs with the missing render target. Set before material load; 0 is the comparison baseline."));

	static bool AddMissingRenderTarget(FGraphicsPipelineStateInitializer& PSO)
	{
		// Fail closed if an engine update already supplies the correct layout.
		if (PSO.RenderTargetsEnabled != 2 || PSO.RenderTargetFormats[1] != PF_R16F
			|| PSO.RenderTargetFormats[2] != PF_Unknown) { return false; }
		PSO.RenderTargetsEnabled = 3;
		PSO.RenderTargetFormats[2] = PSO.RenderTargetFormats[1];
		PSO.RenderTargetFlags[2] = PSO.RenderTargetFlags[1];
		PSO.RenderTargetFormats[1] = PF_G16R16F;
		return true;
	}

	class FCollector final : public IPSOCollector
	{
	public:
		explicit FCollector(ERHIFeatureLevel::Type InFeatureLevel) : IPSOCollector(INDEX_NONE), FeatureLevel(InFeatureLevel) {}
		virtual void CollectPSOInitializers(const FSceneTexturesConfig& SceneTexturesConfig, const FMaterial& Material,
			const FPSOPrecacheVertexFactoryData& VertexFactoryData, const FPSOPrecacheParams& Params, TArray<FPSOPrecacheData>& Out) override
		{
#if !UE_VERSION_OLDER_THAN(5, 8, 0) && UE_VERSION_OLDER_THAN(5, 9, 0)
			if (!CVarEnabled.GetValueOnAnyThread() || !Material.IsDistorted() || GetPSOPrecacheMode() == EPSOPrecacheMode::PreloadShader) { return; }
			const int32 Index = FindCollector(TEXT("Distortion"));
			if (Index == INDEX_NONE) { return; }
			const auto Factory = FPSOCollectorCreateManager::GetCreateFunction(EShadingPath::Deferred, Index);
			if (!Factory) { return; }
			TUniquePtr<IPSOCollector> Original(Factory(FeatureLevel));
			if (!Original) { return; }
			TArray<FPSOPrecacheData> Candidates;
			Original->CollectPSOInitializers(SceneTexturesConfig, Material, VertexFactoryData, Params, Candidates);
			for (FPSOPrecacheData& Candidate : Candidates)
			{
				if (Candidate.Type == FPSOPrecacheData::EType::Graphics && AddMissingRenderTarget(Candidate.GraphicsPSOInitializer))
				{
					// Keep the original Distortion validation identity and required/priority flags.
					Out.Add(MoveTemp(Candidate));
				}
			}
#endif
		}
	private:
		ERHIFeatureLevel::Type FeatureLevel;
	};
#if IS_MONOLITHIC
	// UE 5.8's public registration increments a non-exported inline count.
	// Register only in monolithic games; doing so from an Editor game DLL can
	// overwrite an engine collector. Editor retains the unmodified engine path.
	static IPSOCollector* Create(ERHIFeatureLevel::Type FeatureLevel) { return new FCollector(FeatureLevel); }
	static FRegisterPSOCollectorCreateFunction Registration(&Create, EShadingPath::Deferred, TEXT("ProjectJRoughRefraction"));
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJDistortionPrecacheTest, "ProjectJ.GroupE.DistortionPrecacheLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJDistortionPrecacheTest::RunTest(const FString&)
{
	FGraphicsPipelineStateInitializer PSO;
	PSO.RenderTargetsEnabled = 2;
	PSO.RenderTargetFormats[0] = PF_FloatRGBA;
	PSO.RenderTargetFormats[1] = PF_R16F;
	PSO.RenderTargetFlags[1] = TexCreate_RenderTargetable | TexCreate_ShaderResource;
	PSO.NumSamples = 1;
	TestTrue(TEXT("Known two-target layout supplemented"), ProjectJDistortionPrecache::AddMissingRenderTarget(PSO));
	TestEqual(TEXT("Variance and coverage layout matches deferred render pass"), PSO.RenderTargetFormats[1], PF_G16R16F);
	TestEqual(TEXT("Closest-depth target preserved"), PSO.RenderTargetFormats[2], PF_R16F);
	TestEqual(TEXT("Target creation flags preserved"), PSO.RenderTargetFlags[2], PSO.RenderTargetFlags[1]);
	TestFalse(TEXT("Already corrected layout is untouched"), ProjectJDistortionPrecache::AddMissingRenderTarget(PSO));
	PSO.RenderTargetsEnabled = 1;
	TestFalse(TEXT("Non-rough refraction is untouched"), ProjectJDistortionPrecache::AddMissingRenderTarget(PSO));
	TestTrue(TEXT("Engine distortion collector available without validation"), ProjectJDistortionPrecache::FindCollector(TEXT("Distortion")) != INDEX_NONE);
#if IS_MONOLITHIC
	TestTrue(TEXT("Supplement registered separately without validation"), ProjectJDistortionPrecache::FindCollector(TEXT("ProjectJRoughRefraction")) != INDEX_NONE);
#else
	TestEqual(TEXT("Modular Editor does not register into a module-local count"), ProjectJDistortionPrecache::FindCollector(TEXT("ProjectJRoughRefraction")), INDEX_NONE);
#endif
	TestEqual(TEXT("Unknown collector is unavailable"), ProjectJDistortionPrecache::FindCollector(TEXT("ProjectJMissingCollector")), INDEX_NONE);
	return true;
}
#endif
