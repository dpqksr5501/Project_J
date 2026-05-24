// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Project_J : ModuleRules
{
	public Project_J(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",
			"Project_JCore",
			"Project_JGAS",
			"Project_JCharacter",
			"SignificanceManager",
			"IrisCore",
			"NetCore",
			"GameFeatures",
			"ModularGameplay"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Project_J",
			"Project_J/Game",
			"Project_J/Variant_Platforming",
			"Project_J/Variant_Platforming/Animation",
			"Project_J/Variant_Combat",
			"Project_J/Variant_Combat/AI",
			"Project_J/Variant_Combat/Animation",
			"Project_J/Variant_Combat/Gameplay",
			"Project_J/Variant_Combat/Interfaces",
			"Project_J/Variant_Combat/UI",
			"Project_J/Variant_SideScrolling",
			"Project_J/Variant_SideScrolling/AI",
			"Project_J/Variant_SideScrolling/Gameplay",
			"Project_J/Variant_SideScrolling/Interfaces",
			"Project_J/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
