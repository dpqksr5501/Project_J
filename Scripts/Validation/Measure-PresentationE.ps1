param(
 [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$RunName,
 [ValidateSet('Combat','Effects')][string]$Mode='Combat',
 [string]$Executable,
 [string]$UserDirectory,
 [switch]$Trace,
 [switch]$MixedDistance,
 [switch]$DistanceCull,
 [switch]$FixedBounds
)
$ErrorActionPreference='Stop'
if($Executable -and $DistanceCull){throw 'Temporary scalability authoring comparison requires Editor; omit Executable.'}
if(@(Get-Process UnrealEditor*,Project_J,UnrealBuildTool,dotnet,LiveCoding*,MSBuild,ShaderCompileWorker -ErrorAction SilentlyContinue).Count){throw 'Wait for engine/build completion.'}
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace('\','/')
$out="$root/Saved/Validation/GroupE_20260910/$RunName"
if(Test-Path -LiteralPath $out){throw 'Preserve previous run; use a unique name.'}
New-Item -ItemType Directory -Path $out | Out-Null
$cooked=![string]::IsNullOrWhiteSpace($Executable)
$exe=if($cooked){(Resolve-Path -LiteralPath $Executable).Path}else{'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'}
if(!$UserDirectory){$UserDirectory="$out/UserCache"}
$reused=Test-Path -LiteralPath $UserDirectory
New-Item -ItemType Directory -Path $UserDirectory -Force | Out-Null
$userPath=(Resolve-Path -LiteralPath $UserDirectory).Path.Replace('\','/')+'/'
$argsList=@()
if(!$cooked){$argsList+="$root/Project_J.uproject"}
$argsList+=@('/Engine/Maps/Entry?game=/Script/Engine.GameModeBase','-game','-unattended','-nop4','-nosplash','-nosound','-RenderOffscreen','-windowed','-ResX=1280','-ResY=720','-statnamedevents',"-ProjectJ${Mode}CookedFixture","-UserDir=$userPath",'-SaveToUserDir',"-ProjectJEOutput=$out","-ABSLOG=$out/Run.log",'-ExecCmds=t.MaxFPS 60','-ini:Engine:[SystemSettings]:r.PSOPrecache.Validation=2')
if($Trace){$argsList+=@('-trace=cpu,frame,bookmark,region,gpu,task,animation',"-tracefile=$out/Run.utrace")}
if($MixedDistance){$argsList+='-ProjectJEffectsMixedDistance'}
if($DistanceCull){$argsList+='-ProjectJEffectsDistanceCull'}
if($FixedBounds){$argsList+='-ProjectJEffectsFixedBounds'}
$quoted=@($argsList|ForEach-Object{if($_ -match '^([^=]+)=(.*)$'){$Matches[1]+'="'+$Matches[2]+'"'}elseif($_ -match '\s'){'"'+$_+'"'}else{$_}})-join ' '
$argsList | ConvertTo-Json | Set-Content "$out/arguments.json" -Encoding utf8
$files=@(& git -C $root -c core.fsmonitor=false ls-files --modified --others --exclude-standard -- Source Scripts Config | Sort-Object -Unique | ForEach-Object {if(Test-Path -LiteralPath "$root/$_" -PathType Leaf){[ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath "$root/$_").Hash}}})
[ordered]@{mode=$Mode;cooked=$cooked;exe=$exe;binarySHA256=(Get-FileHash -LiteralPath $exe).Hash;userDirectory=$userPath;reused=$reused;cacheScope='Application directory only; OS/driver caches preserved.';files=$files}|ConvertTo-Json -Depth 5|Set-Content "$out/manifest.json" -Encoding utf8
$process=Start-Process -FilePath $exe -ArgumentList $quoted -WorkingDirectory $root -WindowStyle Hidden -PassThru
$nativeHandle=$process.Handle
$process.WaitForExit()
[ordered]@{processId=$process.Id;exitCode=$process.ExitCode}|ConvertTo-Json|Set-Content "$out/process.json" -Encoding utf8
if($process.ExitCode -ne 0 -or !(Test-Path "$out/result.txt")){throw "Process failed: $out"}
$result=Get-Content "$out/result.txt" -Raw
if($result -notmatch 'success=1' -or ($cooked -and $result -notmatch 'cooked=1')){throw "Fixture failed: $result"}
$shots=if($Mode -eq 'Combat'){@('attack1.png','attack2.png','attack3.png')}else{@('phase2.png','phase3.png')}
foreach($shot in $shots){if(!(Test-Path "$out/$shot")){throw "Missing capture: $shot"}}
if($Mode -eq 'Effects' -and $MixedDistance){
 $settled=@(Import-Csv "$out/frames.csv"|Where-Object phase -eq 3|Select-Object -Skip 1 -First 5)
 $expected=if($DistanceCull){20}else{100}
 if($settled.Count -ne 5 -or @($settled|Where-Object {[int]$_.active -ne $expected}).Count){throw "Distance comparison did not settle at $expected active effects."}
}
[ordered]@{success=$true;cooked=$cooked;mode=$Mode;captures=$shots;distanceCull=[bool]$DistanceCull;fixedBounds=[bool]$FixedBounds}|ConvertTo-Json|Set-Content "$out/verification.json" -Encoding utf8
Write-Output "Completed $out; inspect captures and PSO statistics."
