using UnrealBuildTool;
public class PortHost : ModuleRules {
 public PortHost(ReadOnlyTargetRules Target) : base(Target) {
  PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;
  if (Target.bBuildEditor) PrivateDependencyModuleNames.AddRange(new string[]{"SharedModule","UtilityModule","UtilityModuleEditor","AssetRegistry","UnrealEd","Json","JsonUtilities"});
  PrivateDependencyModuleNames.AddRange(new string[]{"Core","CoreUObject","Engine"});
 }
}
