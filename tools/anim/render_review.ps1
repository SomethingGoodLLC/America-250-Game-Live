# Render the review reels: one -game process per sequence (Movie Render Queue), then ffmpeg -> MP4 and a contact sheet.
# Run in the logged-in desktop session (needs the GPU). Usage: render_review.ps1 [-Only Gun,Volley,React] [-Cams Front,Side,High]
param([string[]]$Only=@('Gun','Volley','React'), [string[]]$Cams=@('Front','Side','High'))
$a='C:\Users\user\Documents\ChatGPT\Play America 250\AnimationPass'
$ue='C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$ff=(Get-ChildItem "$a\..\Tooling\ffmpeg" -Recurse -Filter ffmpeg.exe | Select-Object -First 1).FullName
$plan=Get-Content "$a\review\render_plan.json" -Raw | ConvertFrom-Json
foreach($fam in $Only){ foreach($cam in $Cams){
  $dir="$a\review\$fam\frames_$cam"; if(Test-Path $dir){ Remove-Item $dir -Recurse -Force }
  $seq=$plan.$fam.sequences.$cam; $cfg=$plan.$fam.presets.$cam; $map=$plan.$fam.level
  $log="$a\logs\render_${fam}_$cam.log"
  $p=Start-Process -FilePath $ue -ArgumentList "`"$a\UEHost\AnimHost.uproject`"",$map,'-game',"-LevelSequence=`"$seq`"","-MoviePipelineConfig=`"$cfg`"",'-windowed','-ResX=1280','-ResY=720','-d3d12','-nosplash','-unattended','-nosound',"-abslog=`"$log`"" -PassThru -WindowStyle Minimized
  if(-not $p.WaitForExit(1500000)){ $p.Kill(); "TIMEOUT $fam $cam" }
  $n=(Get-ChildItem $dir -Filter *.jpeg -ErrorAction SilentlyContinue).Count + (Get-ChildItem $dir -Filter *.jpg -ErrorAction SilentlyContinue).Count
  "$fam $cam exit $($p.ExitCode) frames $n"
  if($n -gt 0){
    $first=(Get-ChildItem $dir | Sort-Object Name | Select-Object -First 1); $ext=$first.Extension; $start=[int]$first.BaseName
    & $ff -y -loglevel error -framerate 30 -start_number $start -i "$dir\%04d$ext" -c:v libx264 -pix_fmt yuv420p -crf 20 "$a\review\$fam\${fam}_$cam.mp4"
    if($cam -eq $Cams[0]){ $step=[Math]::Max(1,[int]($n/16)); & $ff -y -loglevel error -framerate 30 -start_number $start -i "$dir\%04d$ext" -vf "select='not(mod(n\,$step))',scale=480:-1,tile=4x4" -frames:v 1 "$a\review\$fam\${fam}_contact_sheet.jpg" }
    if(Test-Path "$a\review\$fam\${fam}_$cam.mp4"){ Remove-Item $dir -Recurse -Force }
  } else { Select-String -Path $log -Pattern 'LogMovieRenderPipeline: (Error|Warning)|Fatal|Assertion' | Select-Object -First 5 | ForEach-Object { $_.Line.Substring(0,[Math]::Min(260,$_.Line.Length)) } }
}}
