param([ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name='CookedDamage1',
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$PackageName='Packaged1')
$ErrorActionPreference='Stop'
if(@(Get-Process UnrealEditor*,Project_J,UnrealBuildTool,dotnet,LiveCoding*,MSBuild,ShaderCompileWorker -ErrorAction SilentlyContinue).Count){throw 'Wait for normal engine/build completion.'}
$exe="$PSScriptRoot/$PackageName/Windows/Project_J/Binaries/Win64/Project_J.exe"
$out="$PSScriptRoot/$Name"
if(Test-Path -LiteralPath $out){throw 'Preserve earlier evidence.'}
New-Item -ItemType Directory -Path $out | Out-Null
$runArgs=@('/Engine/Maps/Entry?game=/Script/Engine.GameModeBase','-unattended','-NullRHI','-nosound','-nop4',"-ABSLOG=$out/Run.log","-ReportExportPath=$out/Report",'-TestExit=Automation Test Queue Empty','-ExecCmds=Automation RunTests ProjectJ.Combat.AuthoredDamageContract; Quit',"-UserDir=$out/User/",'-SaveToUserDir')
$runArgs | ConvertTo-Json | Set-Content "$out/arguments.json" -Encoding utf8
@{executable=$exe;sha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash;scope='Real cooked AttackDefinition/GameplayEffect loads and authority hit processing. Not socket SSR or every montage timing.'} | ConvertTo-Json | Set-Content "$out/manifest.json" -Encoding utf8
$p=Start-Process $exe -ArgumentList (($runArgs|ForEach-Object{'"'+$_+'"'})-join ' ') -WindowStyle Hidden -PassThru
$nativeHandle=$p.Handle
$p.WaitForExit()
@{exitCode=$p.ExitCode;hasExited=$p.HasExited;nativeHandleCapturedBeforeWait=$true} | ConvertTo-Json | Set-Content "$out/process.json" -Encoding utf8
$r=Get-Content "$out/Report/index.json" -Raw | ConvertFrom-Json
if($null -eq $p.ExitCode -or $p.ExitCode -ne 0 -or $r.failed -or $r.notRun -or $r.inProcess -or ($r.succeeded+$r.succeededWithWarnings -ne 1)){throw 'Cooked damage failure/unavailable; retain evidence.'}
$r | Select-Object succeeded,succeededWithWarnings,failed,notRun
