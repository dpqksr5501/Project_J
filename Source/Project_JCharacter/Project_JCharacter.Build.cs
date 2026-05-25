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
			"PoseSearch",
			"Chooser",
			"BlendStack",
			"MotionTrajectory",
			"AnimationCore",
			"AnimGraphRuntime",
			"AnimationWarpingRuntime",
			"Project_JCore",
			"Project_JGAS",
			"SignificanceManager",
			"IrisCore",
			"NetCore",
			"GameFeatures",
			"ModularGameplay",
			"MassEntity",
			"MassCommon",
			"MassMovement",
			"MassRepresentation",
			"MassSpawner",
			"MassGameplayDebug",
			"MassActors",
			"ModelViewViewModel"
		});
		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}
