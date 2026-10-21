using UnrealBuildTool;

public class Project_JCore : ModuleRules
{
	public Project_JCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"NetCore",
			"GameplayTags",
			"Mover"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}
