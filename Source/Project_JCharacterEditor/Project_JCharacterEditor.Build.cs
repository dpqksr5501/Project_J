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
			"Project_JCharacter",
			"StaticMeshEditor" // Native reduction options appear in the public recipe type.
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"DataValidation",
			"Project_JAnimationNodes",
			"Json",
			"AssetRegistry",
			"Chooser",
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
			"UnrealEd",
			"SkeletalMeshUtilitiesCommon", "TargetPlatform", "ContentBrowser"
		});
	}
}
