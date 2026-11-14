[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Destination,
    [switch] $RefreshPlugin
)

$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../../unreal/PlayAmerica250')).Path
$hostDir = [IO.Path]::GetFullPath($Destination)
$pluginName = 'MotionBricksTraversal'
$pluginSource = Join-Path $source "Plugins/$pluginName"
$pluginTarget = Join-Path $hostDir "Plugins/$pluginName"
$descriptor = Join-Path $hostDir 'MBTestHost.uproject'
$marker = Join-Path $hostDir 'PA250_MOTIONBRICKS_EVALUATION.txt'

if ($RefreshPlugin) {
    if (!(Test-Path -LiteralPath $marker) -or !(Test-Path -LiteralPath $descriptor)) {
        throw "Not a MotionBricks host made by this script: $hostDir"
    }
} else {
    if ((Test-Path -LiteralPath $hostDir) -and @(Get-ChildItem -Force -LiteralPath $hostDir).Count) {
        throw "Use an empty directory for the evaluation host: $hostDir"
    }
    $mesh = Join-Path $source 'Content/Characters/Mannequins/Meshes/SKM_Quinn_Simple.uasset'
    if (!(Test-Path -LiteralPath $mesh) -or (Get-Item -LiteralPath $mesh).Length -lt 1024) {
        throw "Restore the Quinn mannequin LFS asset first: $mesh"
    }
    New-Item -ItemType Directory -Force -Path "$hostDir/Content/Characters","$hostDir/Source/MBTestHost" | Out-Null
    Copy-Item -LiteralPath (Join-Path $source 'Content/Characters/Mannequins') -Destination "$hostDir/Content/Characters" -Recurse
    Set-Content -LiteralPath $marker -Value 'Local MotionBricks evaluation host. Do not redistribute sample-derived animation.'
    Set-Content -LiteralPath $descriptor -Value @'
{
    "FileVersion": 3,
    "EngineAssociation": "5.8",
    "Modules": [{"Name":"MBTestHost","Type":"Runtime","LoadingPhase":"Default"}],
    "Plugins": [
        {"Name":"PythonScriptPlugin","Enabled":true},
        {"Name":"EditorScriptingUtilities","Enabled":true},
        {"Name":"MotionBricksTraversal","Enabled":true}
    ]
}
'@
    Set-Content -LiteralPath "$hostDir/Source/MBTestHostEditor.Target.cs" -Value @'
using UnrealBuildTool;

public class MBTestHostEditorTarget : TargetRules
{
    public MBTestHostEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("MBTestHost");
    }
}
'@
    Set-Content -LiteralPath "$hostDir/Source/MBTestHost/MBTestHost.Build.cs" -Value @'
using UnrealBuildTool;

public class MBTestHost : ModuleRules
{
    public MBTestHost(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicDependencyModuleNames.AddRange(new[] {"Core", "CoreUObject", "Engine"});
    }
}
'@
    Set-Content -LiteralPath "$hostDir/Source/MBTestHost/MBTestHost.cpp" -Value @'
#include "Modules/ModuleManager.h"
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, MBTestHost, "MBTestHost");
'@
}

New-Item -ItemType Directory -Force -Path $pluginTarget | Out-Null
Copy-Item -LiteralPath (Join-Path $pluginSource "$pluginName.uplugin") -Destination $pluginTarget -Force
Copy-Item -LiteralPath (Join-Path $pluginSource 'Source') -Destination $pluginTarget -Force -Recurse
Write-Output $descriptor
