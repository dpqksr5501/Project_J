using UnrealBuildTool;

// Value contracts and coordination only. No world, HTTP, UI, GAS or asset dependencies.
public class Project_JMMO : ModuleRules
{
    public Project_JMMO(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.Add("Core");
    }
}
