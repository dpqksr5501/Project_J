// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Project_J : ModuleRules
{
	public Project_J(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// The socket audience fixture reads public Iris NetRefHandles directly.
		// Declare its direct dependency, as the character FastArray module does.
		SetupIrisSupport(Target);

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"UMG",
			"Slate",
			"SlateCore",
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",
			"NetCore",
			"Project_JCore",
			"Project_JMMO",
			"Project_JGAS",
			"Project_JCharacter",
			"SignificanceManager",
			"AIModule"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Project_JMount",
			"NavigationSystem", // Native navigation readiness used directly by the socket fixture.
			"OnlineSubsystemUtils",
			"SQLiteCore",
			"Mover",
			"NetworkPrediction",
			"HTTP",
			"Json",
			"JsonUtilities",
			"RHI",
			"RenderCore",
			"ImageWriteQueue",
			"AssetRegistry",
			"Niagara",
			"ClothingSystemRuntimeInterface"
		});
		// UE-404796 is fixed upstream after 5.8.4. Only the affected cooked Windows targets need this hook.
		if (Target.Platform == UnrealTargetPlatform.Win64 && (Target.Type == TargetType.Game || Target.Type == TargetType.Client)
			&& Target.Version.MajorVersion == 5 && Target.Version.MinorVersion == 8 && Target.Version.PatchVersion == 4)
		{
			PrivateDependencyModuleNames.Add("Landscape");
			PrivateDefinitions.Add("PROJECT_J_LANDSCAPE_PSO_REACTIVATION_WORKAROUND=1");
		}

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("AnimationBudgetAllocator");
			PrivateDependencyModuleNames.Add("Project_JAnimationNodes");
			PrivateDependencyModuleNames.Add("AnimationBlueprintLibrary"); // Public read-only notify copy API for editor diagnostics.
			PrivateDependencyModuleNames.AddRange(new string[] { "PoseSearch", "Chooser", "BlendStack", "IKRig", "AnimGraphRuntime" });
		}
		if (Target.Type == TargetType.Editor || Target.Type == TargetType.Server)
		{
			PrivateDependencyModuleNames.Add("Sockets"); // Local HTTP integration fixture and Prometheus /metrics listener.
			DynamicallyLoadedModuleNames.Add("PerfCounters");
		}
		if (Target.Type != TargetType.Server)
		{
			DynamicallyLoadedModuleNames.Add("Voice");
		}

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
