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
			"NetCore",
			"MassEntity",
			"MassSpawner",
			"ModelViewViewModel"
		});
		PrivateDependencyModuleNames.AddRange(new string[] {
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"IrisCore",
			"GameFeatures",
			"ModularGameplay",
			"MassCommon",
			"MassMovement",
			"MassRepresentation",
			"MassGameplayDebug",
			"MassActors"
		});
	}
}
