// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PlayAmerica250 : ModuleRules
{
	public PlayAmerica250(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// The game catches what the sim core throws (an unreadable save, setup or result). On the Mac
		// exceptions are always on; on Windows a module must ask, or a catch runs without unwinding.
		bEnableExceptions = true;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"ProceduralMeshComponent",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"SlateCore",
			"PA250Sim",
			"Niagara",
			"RenderCore",
			"RHI",
			"HTTP",
			"WebSockets",
			"Json",
			"JsonUtilities",
			"MediaAssets",
			"Projects",
			"NavigationSystem",
			"MediaUtils",
			"Media",
			"FunctionalTesting"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { "ImageWrapper", "AnimationCore", "AudioCapture", "AudioCaptureCore", "MoviePlayer" });
		// Motion-matched locomotion (RF/PA250AnimNode_Locomotion): the engine pose search and its blend stack.
		PrivateDependencyModuleNames.AddRange(new string[] { "PoseSearch", "BlendStack", "Chooser", "AnimGraphRuntime" });
		// Automatic traversal (Battle/PA250TraversalComponent): the engine warps the vault clips onto the obstacle.
		PrivateDependencyModuleNames.Add("MotionWarping");
		// The battle's camera shake (Battle/PA250Intensity): the engine's noise shake pattern.
		PrivateDependencyModuleNames.Add("EngineCameras");
		PublicIncludePaths.Add("PlayAmerica250/Campaign");
		PublicIncludePaths.Add("PlayAmerica250/Battle");
		PublicIncludePaths.Add("PlayAmerica250/Diplomacy");
		PublicIncludePaths.Add("PlayAmerica250/UI");

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
