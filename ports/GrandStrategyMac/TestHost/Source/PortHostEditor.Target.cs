using UnrealBuildTool;
public class PortHostEditorTarget : TargetRules {
 public PortHostEditorTarget(TargetInfo Target) : base(Target) {
  Type=TargetType.Editor;
  DefaultBuildSettings=BuildSettingsVersion.V7;
  IncludeOrderVersion=EngineIncludeOrderVersion.Unreal5_8;
  ExtraModuleNames.Add("PortHost");
 }
}
