#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "ShaderCore.h"
#include "Misc/Paths.h"

// Fixtures run only through explicit automation tests. No ticker, spawned actor,
// global worker or mutable gameplay service is installed at module startup.
class FProjectJExperimentsModule final : public IModuleInterface
{
    virtual void StartupModule() override
    {
        AddShaderSourceDirectoryMapping(TEXT("/ProjectJExperiments"),
            FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("ProjectJExperiments"))->GetBaseDir(), TEXT("Shaders")));
    }
};
IMPLEMENT_MODULE(FProjectJExperimentsModule, ProjectJExperiments)
