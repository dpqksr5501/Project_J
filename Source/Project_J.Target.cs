// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class Project_JTarget : TargetRules
{
	public Project_JTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		// Match the installed engine's Default dependency-cache partition
		// while retaining non-unity compilation without -DisableUnity.
		bUseUnityBuild = false;
		ExtraModuleNames.AddRange(new string[] { "Project_J", "Project_JCore", "Project_JGAS", "Project_JCharacter" });
	}
}
