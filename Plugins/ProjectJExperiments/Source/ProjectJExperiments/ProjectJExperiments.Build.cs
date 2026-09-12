using UnrealBuildTool;

public class ProjectJExperiments : ModuleRules
{
    public ProjectJExperiments(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "Project_JCore", "UnrealEd",
            "AnimGraph", "BlueprintGraph", "Niagara", "Json", "JsonUtilities",
            "RenderCore", "RHI", "Projects", "PhysicsCore", "AudioMixer", "KismetCompiler", "Chaos", "PCG"
        });
    }
}
