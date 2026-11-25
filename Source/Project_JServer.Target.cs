// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class Project_JServerTarget : TargetRules
{
	public Project_JServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		bUseUnityBuild = false;
		bUseLoggingInShipping = true;
		if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			// Build SQLiteCore with upstream POSIX file locking for the persistent outbox.
			BuildEnvironment = TargetBuildEnvironment.Unique;
			bCompileCustomSQLitePlatform = false;
		}
		ExtraModuleNames.AddRange(new string[] { "Project_J", "Project_JCore", "Project_JMMO", "Project_JGAS", "Project_JMount", "Project_JCharacter" });
	}
}
