param(
    [ValidateSet('Inspect','Preview','Apply','CreatePolicy')][string]$Action='Inspect',
    [string[]]$Assets=@(),
    [ValidateSet('Unclassified','Surface','Hair')][string]$Role='Unclassified',
    [string]$Policy,
    [string]$EditorExecutable,
    [string]$RunName=('MeshLOD_'+[guid]::NewGuid().ToString('N'))
)
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if(@(Get-Process UnrealEditor*,Project_J,UnrealBuildTool,dotnet,LiveCoding*,MSBuild,ShaderCompileWorker -ErrorAction SilentlyContinue).Count){throw 'Finish active editor/build/validation normally before running authoring.'}
if($RunName -notmatch '^[A-Za-z0-9_-]+$'){throw 'Use a simple unique run name.'}
if($Action -ne 'CreatePolicy' -and ($Assets.Count -lt 1 -or $Assets.Count -gt 32)){throw 'Supply 1..32 explicit asset paths.'}
if(@($Assets|Where-Object{[string]::IsNullOrWhiteSpace($_)}).Count){throw 'Empty asset entries are not accepted.'}
foreach($value in @($Assets)+@($Policy)){if($value -and $value -notmatch '^/Game/[A-Za-z0-9_/]+(?:\.[A-Za-z0-9_]+)?$'){throw "Invalid project asset path: $value"}}
if(!$EditorExecutable){
    $project=Get-Content (Join-Path $root 'Project_J.uproject') -Raw|ConvertFrom-Json
    $key=Get-ItemProperty -LiteralPath "HKLM:/SOFTWARE/EpicGames/Unreal Engine/$($project.EngineAssociation)" -ErrorAction SilentlyContinue
    if($key.InstalledDirectory){$EditorExecutable=Join-Path $key.InstalledDirectory 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'}
}
if(!$EditorExecutable -or !(Test-Path -LiteralPath $EditorExecutable)){throw 'Supply -EditorExecutable for the installed matching UnrealEditor-Cmd.exe.'}
$out=Join-Path $root "Saved/MeshLODAuthoring/$RunName"
if(Test-Path -LiteralPath $out){throw 'Preserve prior reports; use another run name.'}
New-Item -ItemType Directory -Path $out|Out-Null
$arguments=@((Join-Path $root 'Project_J.uproject'),'-run=Project_JMeshLOD','-unattended','-NullRHI','-nosound','-NoLiveCoding','-NoDDCCleanup','-DisablePython',
    "-Action=$Action","-Role=$Role","-Assets=$($Assets -join ',')","-Report=$out/report.json","-ABSLOG=$out/Run.log")
if($Policy){$arguments+="-Policy=$Policy"}
$arguments|ConvertTo-Json|Set-Content -LiteralPath "$out/arguments.json" -Encoding utf8
$quoted=($arguments|ForEach-Object{'"'+$_+'"'}) -join ' '
$p=Start-Process -FilePath $EditorExecutable -ArgumentList $quoted -WindowStyle Hidden -PassThru
$handle=$p.Handle;$p.WaitForExit()
[ordered]@{exitCode=$p.ExitCode;hasExited=$p.HasExited;nativeHandleCapturedBeforeWait=$true}|ConvertTo-Json|Set-Content "$out/process.json" -Encoding utf8
if(!(Test-Path -LiteralPath "$out/report.json")){throw "No report: $out"}
Get-Content -LiteralPath "$out/report.json"
$report=Get-Content -LiteralPath "$out/report.json" -Raw|ConvertFrom-Json
if($p.ExitCode -in 0,2 -and $Action -ne 'CreatePolicy' -and $report.assets.Count -ne $Assets.Count){throw 'Input/report asset count mismatch; inspect arguments and report before continuing.'}
if($p.ExitCode -eq 2){Write-Warning 'Some assets require manual review and were not generated.'}
elseif($p.ExitCode -ne 0){throw "Authoring failed; inspect $out"}
