using UnrealBuildTool;
public class MotionBricksTraversal : ModuleRules {
    public MotionBricksTraversal(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] {"Core", "CoreUObject", "Engine", "InputCore"});
    }
}
