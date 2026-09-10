param(
 [Parameter(Mandatory=$true)][string]$Executable,
 [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$RunName,
 [Parameter(Mandatory=$true)][string]$UserDirectory,
 [switch]$Preload,
 [switch]$NoExceptionHandler,
 [switch]$Trace,
 [switch]$DisableRoughRefractionPrecache,
 [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Suite='CrowdE_20260910'
)
$ErrorActionPreference='Stop'
if (@(Get-Process UnrealEditor*,Project_J,UnrealBuildTool,dotnet,LiveCoding*,MSBuild,ShaderCompileWorker -ErrorAction SilentlyContinue).Count) {throw 'Wait for engine/build completion.'}
$projectRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace('\','/')
$exe=(Resolve-Path -LiteralPath $Executable).Path
$output="$projectRoot/Saved/Validation/$Suite/$RunName"
if(Test-Path -LiteralPath $output){throw 'Preserve existing evidence; choose a new run name.'}
New-Item -ItemType Directory -Path $output | Out-Null
$userWasPresent=Test-Path -LiteralPath $UserDirectory
New-Item -ItemType Directory -Path $UserDirectory -Force | Out-Null
$userPath=(Resolve-Path -LiteralPath $UserDirectory).Path.Replace('\','/')+'/'
[ordered]@{executable=$exe;binarySHA256=(Get-FileHash -LiteralPath $exe).Hash;userDirectory=$userPath;userDirectoryExisted=$userWasPresent;preload=[bool]$Preload;cacheScope='Application user directory only; OS and GPU-driver caches are preserved, not certified cold.'} | ConvertTo-Json | Set-Content "$output/manifest.json" -Encoding utf8
$arguments=@('/Engine/Maps/Entry?game=/Script/Engine.GameModeBase','-unattended','-nop4','-nosplash','-nosound','-RenderOffscreen','-windowed','-ResX=1280','-ResY=720','-ProjectJEffectsCookedFixture',"-UserDir=$userPath",'-SaveToUserDir',"-ProjectJEOutput=$output","-ABSLOG=$output/Run.log",'-ExecCmds=t.MaxFPS 60,r.PSOPrecaching,r.PSOPrecache.Validation,r.ShaderPipelineCache.Enabled','-ini:Engine:[SystemSettings]:r.PSOPrecache.Validation=2','-trace=cpu,frame,bookmark,region,gpu,task',"-tracefile=$output/Run.utrace")
if($Preload){$arguments+='-ProjectJEffectsPreload'}
if($DisableRoughRefractionPrecache){$arguments+='-ini:Engine:[ConsoleVariables]:ProjectJ.Rendering.PrecacheRoughRefraction=0'}
if(!$Trace){$arguments=@($arguments | Where-Object {$_ -notlike '-trace=*' -and $_ -notlike '-tracefile=*'})}
if($NoExceptionHandler){$arguments+='-noexceptionhandler'}
$quoted=@($arguments | ForEach-Object {if($_ -match '^([^=]+)=(.*)$'){$Matches[1]+'="'+$Matches[2]+'"'}else{$_}}) -join ' '
$arguments | ConvertTo-Json | Set-Content "$output/arguments.json" -Encoding utf8
$process=Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Parent $exe) -ArgumentList $quoted -WindowStyle Hidden -PassThru
$nativeHandle=$process.Handle
$process.WaitForExit()
[ordered]@{processId=$process.Id;exitCode=$process.ExitCode;hasExited=$process.HasExited} | ConvertTo-Json | Set-Content "$output/process.json" -Encoding utf8
if($process.ExitCode -ne 0 -or !(Test-Path "$output/result.txt")){throw "Cooked process failed: $output"}
$result=Get-Content "$output/result.txt" -Raw
if($result -notmatch 'success=1' -or $result -notmatch 'cooked=1'){throw "Cooked fixture assertion failed: $output"}
if(!(Test-Path "$output/phase2.png") -or !(Test-Path "$output/phase3.png")){throw 'Missing actual viewport captures.'}
Write-Output "Completed cooked fixture $output; inspect pixel captures and PSO log before interpreting performance."
