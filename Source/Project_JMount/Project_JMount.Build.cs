using UnrealBuildTool;

public class Project_JMount : ModuleRules
{
	public Project_JMount(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "NetCore", "EnhancedInput", "GameplayAbilities", "GameplayTags", "Project_JCore", "Project_JGAS" });
	}
}
