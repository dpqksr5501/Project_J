using UnrealBuildTool;

public class Project_JCharacter : ModuleRules
{
	public Project_JCharacter(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",
			"Project_JCore",
			"Project_JGAS"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}
