using UnrealBuildTool;

public class Project_JCharacterEditor : ModuleRules
{
	public Project_JCharacterEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"Project_JCharacter"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Json",
			"AssetRegistry",
			"KismetCompiler",
			"ApplicationCore",
			"PropertyEditor",
			"ToolMenus",
			"GameplayTags",
			"AIModule",
			"NavigationSystem",
			"Project_JGAS",
			"GameplayAbilities",
			"Persona",
			"AnimGraph",
			"BlueprintGraph",
			"PoseSearch",
			"BlendStack",
			"BlendStackEditor",
			"AnimationWarpingRuntime",
			"AnimationWarpingEditor",
			"EditorFramework",
			"InputCore",
			"Slate",
			"SlateCore",
			"UnrealEd"
		});
	}
}
