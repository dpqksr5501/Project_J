using UnrealBuildTool;

public class Project_JGAS : ModuleRules
{
	public Project_JGAS(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",
			"Project_JCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}
