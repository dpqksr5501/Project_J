param([Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$RunName, [switch]$Trace, [string]$Executable, [switch]$DisableEditorPython, [switch]$LocalPlayer,
 [ValidateRange(1,200)][int]$Count=200,[switch]$Movement,[string]$ConsoleCommands='')
$ErrorActionPreference='Stop'
. "$PSScriptRoot/Source-Provenance.ps1"
if(@(Get-Process UnrealEditor*,Project_J,UnrealBuildTool,dotnet,LiveCoding*,MSBuild,ShaderCompileWorker -ErrorAction SilentlyContinue).Count){throw 'Wait for engine/build completion.'}
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace('\','/')
$out="$root/Saved/Validation/WholeProjectAudit_20261013/$RunName"
if(Test-Path -LiteralPath $out){throw 'Preserve prior evidence; choose a unique name.'}
New-Item -ItemType Directory -Path $out|Out-Null
$exe = if ($Executable) { (Resolve-Path -LiteralPath $Executable).Path } else { 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' }
$arguments=@('/Engine/Maps/Entry?game=/Script/Engine.GameModeBase','-game','-unattended','-nop4','-nosplash','-nosound','-NoDDCCleanup','-NoLiveCoding',
 '-RenderOffscreen','-windowed','-ResX=1280','-ResY=720','-ProjectJRenderedCrowdFixture',"-ProjectJRenderedCrowdOutput=$out","-ABSLOG=$out/Run.log",
 "-ExecCmds=t.MaxFPS 0,r.VSync 0,$ConsoleCommands", '-statnamedevents', "-UserDir=$out/User/", '-SaveToUserDir',"-ProjectJRenderedCrowdCount=$Count")
if ($Movement) { $arguments += '-ProjectJRenderedCrowdMovement' }
if (!$Executable) { $arguments = @("$root/Project_J.uproject") + $arguments }
if ($DisableEditorPython -and !$Executable) { $arguments += '-DisablePython' }
if ($LocalPlayer) { $arguments += '-ProjectJRenderedCrowdLocalPlayer' }
if($Trace){$arguments+=@('-trace=cpu,frame,gpu,bookmark,region,counters,task',"-tracefile=$out/Render.utrace")}
$files=@('Project_J.uproject','Content/Character_BPs/GreatSword/BP_GreatSword.uasset',
 'Content/Animation_Logic/ABPs/GreatSword/ABP_Greatsword_Woman_RunTIme.uasset','Content/Animation_Logic/ABPs/ABP_Player_Master.uasset')
if (!$Executable) { $files += @('Binaries/Win64/UnrealEditor-Project_J.dll','Binaries/Win64/UnrealEditor-Project_JCharacter.dll') }
$hashes=@($files|ForEach-Object{[ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath "$root/$_").Hash}})
[ordered]@{head=(& git -C $root -c core.fsmonitor=false rev-parse HEAD);cpu=(Get-CimInstance Win32_Processor).Name;gpu=(Get-CimInstance Win32_VideoController).Name;
 actors=$Count;movement=[bool]$Movement;consoleCommands=$ConsoleCommands;sourceProvenance=(Get-ProjectJSourceProvenance -ProjectRoot $root -RuntimePaths @($exe));localPlayer=[bool]$LocalPlayer;connections=0;nullRHI=$false;offscreenViewport=$true;resolution='1280x720';fpsCap=0;vsync=0;trace=[bool]$Trace;packaged=[bool]$Executable;executable=$exe;executableSHA256=(Get-FileHash -LiteralPath $exe).Hash;arguments=$arguments;artifacts=$hashes}|
 ConvertTo-Json -Depth 8|Set-Content "$out/manifest.json" -Encoding utf8
$quoted=($arguments|ForEach-Object{'"'+$_+'"'}) -join ' '
$p=Start-Process $exe -WorkingDirectory (Split-Path -Parent $exe) -ArgumentList $quoted -WindowStyle Hidden -PassThru
$nativeHandle=$p.Handle
Write-Output "Started rendered crowd PID=$($p.Id)"
$peakWorking=0L;$peakPrivate=0L
while(!$p.WaitForExit(500)){
 $p.Refresh()
 if(!$p.HasExited){$peakWorking=[Math]::Max($peakWorking,$p.WorkingSet64);$peakPrivate=[Math]::Max($peakPrivate,$p.PrivateMemorySize64)}
}
$p.WaitForExit()
[ordered]@{exitCode=$p.ExitCode;nativeHandleCapturedBeforeWait=$true;hasExited=$p.HasExited;sampledPeakWorkingSetBytes=$peakWorking;sampledPeakPrivateBytes=$peakPrivate;memoryScope='Entire editor game process including startup, sampled every 500ms; not incremental crowd memory.'}|
 ConvertTo-Json|Set-Content "$out/process.json" -Encoding utf8
if($null -eq $p.ExitCode){throw "Rendered crowd exit code unavailable: $out"}
if($p.ExitCode -ne 0 -or !(Test-Path "$out/result.txt")){throw "Rendered crowd process failed: $out"}
$result=Get-Content "$out/result.txt" -Raw
if($result -notmatch 'success=1' -or !(Test-Path "$out/phase2.png") -or !(Test-Path "$out/phase3.png")){throw "Rendered crowd assertions/captures failed: $result"}
if($Executable -and $result -notmatch 'cooked=1'){throw 'Packaged run did not require cooked data.'}
if($LocalPlayer -and ($result -notmatch 'local_player=1' -or $result -notmatch 'local_quality_samples=[1-9][0-9]*')){throw 'No verified local full-quality frames.'}
$rows=@(Import-Csv "$out/frames.csv")
$attackPhase=if($Movement){5}else{3}
if(!@($rows|Where-Object {[int]$_.phase -eq $attackPhase -and [int]$_.montages -eq $Count}).Count){throw 'No observed simultaneous authored R montages.'}
if($Movement){
 $return=@($rows|Where-Object {[int]$_.phase -eq 6})
 if(!$return.Count -or @($return|Where-Object {[double]$_.elapsed_s -ge ([double]$return[0].elapsed_s+1) -and [int]$_.montages -ne 0}).Count){throw 'Attack montages did not drain on return to locomotion.'}
 $work=@(Import-Csv "$out/workload.csv")
 if($work.Count -ne $Count -or @($work|Where-Object {[double]$_.travel_cm -le 100 -or [int]$_.reversals -lt 1 -or [int]$_.landings -lt 1}).Count){throw 'Moving/pivot/jump workload was not exercised for every participant.'}
}
[ordered]@{success=$true;exitCode=$p.ExitCode;frames=$rows.Count;scope='Real offscreen viewport/RHI and actual constructed retarget bodies; simplified camera/light scene; no remote clients. GPU counters may be delayed. Captures affect frame timing.'}|
 ConvertTo-Json|Set-Content "$out/verification.json" -Encoding utf8
Write-Output "Completed rendered crowd: $out"
