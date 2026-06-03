using UnrealBuildTool;

public class Project_JCharacterEditor : ModuleRules
{
	public Project_JCharacterEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine"
		});
	}
}
