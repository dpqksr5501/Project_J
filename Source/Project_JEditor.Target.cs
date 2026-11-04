// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class Project_JEditorTarget : TargetRules
{
	public Project_JEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		// Keep non-unity explicit here: installed UE 5.8 forces Default
		// intermediates, while -DisableUnity queries a mismatched NU cache.
		bUseUnityBuild = false;
		ExtraModuleNames.AddRange(new string[] { "Project_J", "Project_JCore", "Project_JGAS", "Project_JCharacter" });
	}
}
