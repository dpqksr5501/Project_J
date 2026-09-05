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
			"UMG",
			"Slate",
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",
			"Project_JCore",
			"Project_JGAS",
			"Project_JCharacter",
			"SignificanceManager",
			"AIModule"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"HTTP",
			"Json",
			"JsonUtilities",
			"NetCore"
		});

		PublicIncludePaths.AddRange(new string[] {
			"Project_J",
			"Project_J/Game"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
