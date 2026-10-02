using UnrealBuildTool;

public class Project_JAnimationNodes : ModuleRules
{
	public Project_JAnimationNodes(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "AnimGraph", "BlueprintGraph", "Project_JCharacter"
		});
		PrivateDependencyModuleNames.Add("UnrealEd");
	}
}
