using UnrealBuildTool;
public class MotionBricksTraversalEditor : ModuleRules {
    public MotionBricksTraversalEditor(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] {"Core", "CoreUObject", "Engine", "MotionBricksTraversal"});
        PrivateDependencyModuleNames.AddRange(new[] {"UnrealEd", "Json", "AssetRegistry"});
    }
}
