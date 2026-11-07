// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PlayAmerica250 : ModuleRules
{
	public PlayAmerica250(ReadOnlyTargetRules Target) : base(Target)
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
			"SlateCore",
			"PA250Sim",
			"HTTP",
			"WebSockets",
			"Json",
			"JsonUtilities",
			"MediaAssets",
			"Projects",
			"NavigationSystem",
			"MediaUtils",
			"Media"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { "ImageWrapper" });
		PublicIncludePaths.Add("PlayAmerica250/Campaign");
		PublicIncludePaths.Add("PlayAmerica250/Battle");
		PublicIncludePaths.Add("PlayAmerica250/Diplomacy");

		PublicIncludePaths.AddRange(new string[] {
			"PlayAmerica250",
			"PlayAmerica250/RF",
			"PlayAmerica250/Variant_Combat",
			"PlayAmerica250/Variant_Combat/AI",
			"PlayAmerica250/Variant_Combat/Animation",
			"PlayAmerica250/Variant_Combat/Gameplay",
			"PlayAmerica250/Variant_Combat/Interfaces",
			"PlayAmerica250/Variant_Combat/UI",
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
