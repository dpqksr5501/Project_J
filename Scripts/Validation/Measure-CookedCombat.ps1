param(
 [Parameter(Mandatory=$true)][string]$Executable,
 [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$RunName,
 [Parameter(Mandatory=$true)][string]$UserDirectory,
 [switch]$DisableDefaultLightFunctionPrecache
)
$ErrorActionPreference='Stop'
if(@(Get-Process UnrealEditor*,Project_J,UnrealBuildTool,dotnet,LiveCoding*,MSBuild,ShaderCompileWorker -ErrorAction SilentlyContinue).Count){throw 'Wait for engine/build completion.'}
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace('\','/')
$exe=(Resolve-Path -LiteralPath $Executable).Path
$out="$root/Saved/Validation/EF_20260912/$RunName"
if(Test-Path -LiteralPath $out){throw 'Preserve evidence; choose a new RunName.'}
New-Item -ItemType Directory $out|Out-Null
$existed=Test-Path -LiteralPath $UserDirectory
New-Item -ItemType Directory -Force $UserDirectory|Out-Null
$userPath=(Resolve-Path -LiteralPath $UserDirectory).Path.Replace('\','/')+'/'
$arguments=@('/Engine/Maps/Entry?game=/Script/Engine.GameModeBase','-unattended','-nop4','-nosplash','-nosound','-RenderOffscreen','-windowed','-ResX=1280','-ResY=720','-ProjectJCombatCookedFixture',"-UserDir=$userPath",'-SaveToUserDir',"-ProjectJEOutput=$out","-ABSLOG=$out/Run.log",'-ExecCmds=t.MaxFPS 60','-ini:Engine:[SystemSettings]:r.PSOPrecache.Validation=2')
if($DisableDefaultLightFunctionPrecache){$arguments+='-ini:Engine:[ConsoleVariables]:ProjectJ.Rendering.PrecacheDefaultLightFunction=0'}
[ordered]@{head=(& git -C $root rev-parse HEAD);exe=$exe;binarySHA256=(Get-FileHash -LiteralPath $exe).Hash;userDirectory=$userPath;reused=$existed;cacheScope='Application directory only. OS/driver caches preserved; not certified cold.';disableDefaultLightFunction=[bool]$DisableDefaultLightFunctionPrecache;arguments=$arguments}|ConvertTo-Json -Depth 4|Set-Content "$out/manifest.json" -Encoding utf8
$quoted=@($arguments|ForEach-Object {if($_ -match '^([^=]+)=(.*)$'){$Matches[1]+'="'+$Matches[2]+'"'}else{$_}})-join ' '
$process=Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Parent $exe) -ArgumentList $quoted -WindowStyle Hidden -PassThru
$nativeHandle=$process.Handle
$process.WaitForExit()
[ordered]@{exitCode=$process.ExitCode}|ConvertTo-Json|Set-Content "$out/process.json" -Encoding utf8
if($process.ExitCode -ne 0 -or !(Test-Path "$out/result.txt")){throw "Cooked process failed: $out"}
$result=Get-Content "$out/result.txt" -Raw
if($result -notmatch 'success=1' -or $result -notmatch 'cooked=1'){throw "Cooked fixture assertions failed: $out"}
Write-Output "Completed $out"
