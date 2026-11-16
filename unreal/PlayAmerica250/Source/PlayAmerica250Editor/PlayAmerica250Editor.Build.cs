using UnrealBuildTool;

// Editor-only build steps for Play America 250 that the engine's Python cannot reach
// (pose-search databases can only be filled from C++). Called from Scripts/build_locomotion.py.
// The headers sit beside the sources: the repository's .gitignore has a bare "public" rule.
public class PlayAmerica250Editor : ModuleRules
{
	public PlayAmerica250Editor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "AssetRegistry", "PoseSearch", "MotionWarping" });
		PublicIncludePaths.Add(ModuleDirectory);
	}
}
