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
			"Project_JCore", "Mover", "IKRig", "IKRigEditor", "ControlRig", "ControlRigDeveloper",
			"DataValidation",
			"Landscape",
			"Foliage", // LandscapeEdit's public editing interface includes foliage snapping types.
			"Project_JAnimationNodes",
			"Json",
			"ClothingSystemRuntimeInterface",
			"ClothingSystemRuntimeCommon", "ClothingSystemEditor", "SkeletalMeshEditor",
			"AssetRegistry",
			"AssetTools",
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
			"AnimGraphRuntime",
			"BlueprintGraph",
			"PoseSearch",
			"PoseSearchEditor",
			"BlendStack",
			"BlendStackEditor",
			"AnimationWarpingRuntime", "AnimationBlueprintLibrary",
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
