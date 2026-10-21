// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MoverCVDData : ModuleRules
{
    public MoverCVDData(ReadOnlyTargetRules Target) : base(Target)
    {
	    PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"ChaosVDRuntime",
			}
		);
	}
}