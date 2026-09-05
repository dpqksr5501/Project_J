using UnrealBuildTool;

public class Project_JCharacter : ModuleRules
{
	public Project_JCharacter(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Generates Iris replication fragments for this module's FFastArraySerializer types.
		SetupIrisSupport(Target);

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
			"Project_JMount",
			"SignificanceManager",
			"NetCore",
			"MassCore",
			"MassEntity",
			"MassSpawner",
			"ModelViewViewModel"
		});
		PrivateDependencyModuleNames.AddRange(new string[] {
			"InputCore",
			"EnhancedInput",
			"MassCommon"
		});

		if (Target.bBuildEditor)
		{
			PublicDependencyModuleNames.Add("DataValidation");
		}
	}
}
