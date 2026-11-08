// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class PlayAmerica250EditorTarget : TargetRules
{
	public PlayAmerica250EditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("PlayAmerica250");
		ExtraModuleNames.Add("PA250Sim");
		ExtraModuleNames.Add("PA250Shaders");
	}
}
