// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MoverCVDEditor : ModuleRules
{
    public MoverCVDEditor(ReadOnlyTargetRules Target) : base(Target)
    {
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core"
			});

		PrivateDependencyModuleNames.AddRange(
		    new string[]
		    {
				"ApplicationCore",
				"ChaosVD",
				"ChaosVDData",
				"ChaosVDRuntime",
				"CoreUObject",
				"EditorWidgets",
				"Engine",
				"Mover",
				"MoverCVDData",
				"Projects",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"TraceServices",
				"TypedElementFramework",
				"UnrealEd",
			}
		);
        
        SetupModulePhysicsSupport(Target);

		SetupModuleChaosVisualDebuggerSupport(Target);
	}
}