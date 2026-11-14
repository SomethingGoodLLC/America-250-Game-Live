[CmdletBinding()]
param(
    [string] $EngineRoot = 'C:/Program Files/Epic Games/UE_5.8',
    [string] $Project = (Join-Path $PSScriptRoot '../../../MotionBricksEvaluation/VaultTestHost/MBTestHost.uproject'),
    [switch] $Verify,
    [ValidateRange(15, 240)] [int] $FPS = 60
)

$ErrorActionPreference = 'Stop'
$editor = (Resolve-Path -LiteralPath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe')).Path
$descriptor = (Resolve-Path -LiteralPath $Project).Path
$map = '/Game/Developers/MotionBricksEvaluation/L_MotionBricksTraversal'
$argsList = @(
    ('"{0}"' -f $descriptor), $map, '-game', '-d3d12',
    '-windowed', '-ResX=1280', '-ResY=720', '-nosplash', '-unattended',
    ('-ExecCmds="t.MaxFPS {0}"' -f $FPS)
)
if ($Verify) {
    $logPath = Join-Path (Split-Path $descriptor -Parent) "MBVerify_${FPS}.log"
    $argsList += @('-MBVerify', ('-abslog="{0}"' -f $logPath))
    Write-Output "Verification log: $logPath"
    $process = Start-Process -FilePath $editor -ArgumentList $argsList -PassThru
    $process.WaitForExit()
    exit $process.ExitCode
}
Start-Process -FilePath $editor -ArgumentList $argsList
