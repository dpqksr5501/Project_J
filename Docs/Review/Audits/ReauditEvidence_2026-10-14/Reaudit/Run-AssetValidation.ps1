param([ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name='AuthoredValidation1',
    [ValidatePattern('^[A-Za-z0-9_]+$')][string[]]$AssetTypes=@('Project_JLocomotionProfile','Project_JWeaponPresentationProfile'))
$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path "$PSScriptRoot/../../..").Path.Replace('\','/')
$root="$PSScriptRoot/$Name"
if(Test-Path -LiteralPath $root){throw 'Preserve previous evidence.'}
New-Item -ItemType Directory -Path $root | Out-Null
$results=@()
foreach($assetType in $AssetTypes){
    if(@(Get-Process UnrealEditor*,Project_J,UnrealBuildTool,dotnet,LiveCoding*,MSBuild,ShaderCompileWorker -ErrorAction SilentlyContinue).Count){throw 'Wait for normal engine/build completion.'}
    $out="$root/$assetType"
    New-Item -ItemType Directory -Path $out | Out-Null
    $arguments=@("$projectRoot/Project_J.uproject",'-run=DataValidation',"-AssetType=$assetType",'-IncludeOnlyOnDiskAssets','-unattended','-NullRHI','-NoDDCCleanup','-NoLiveCoding','-nop4','-nosound',"-ABSLOG=$out/Run.log",'-UTF8Output','-culture=en')
    $arguments | ConvertTo-Json | Set-Content "$out/arguments.json" -Encoding utf8
    $p=Start-Process 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' -ArgumentList (($arguments|ForEach-Object{'"'+$_+'"'})-join ' ') -WindowStyle Hidden -PassThru
    $nativeHandle=$p.Handle
    $p.WaitForExit()
    $validationLog=Get-Content -LiteralPath "$out/Run.log" -Raw
    $summaryPattern='Files Checked: (\d+), Passed: (\d+), Failed: (\d+), Skipped: (\d+), Unable to validate: (\d+)'
    $validationSummary=$null
    $matchesFound=[regex]::Matches($validationLog,$summaryPattern)
    if($matchesFound.Count){
        $summaryMatch=$matchesFound[$matchesFound.Count-1]
        $validationSummary=[ordered]@{checked=[int]$summaryMatch.Groups[1].Value;passed=[int]$summaryMatch.Groups[2].Value;failed=[int]$summaryMatch.Groups[3].Value;skipped=[int]$summaryMatch.Groups[4].Value;unableToValidate=[int]$summaryMatch.Groups[5].Value}
    }
    $result=[ordered]@{assetType=$assetType;exitCode=$p.ExitCode;hasExited=$p.HasExited;nativeHandleCapturedBeforeWait=$true;validationSummary=$validationSummary;validationSummaryAvailable=($null -ne $validationSummary);scope='On-disk project assets of the exact native class and subclasses, engine excluded; configured excluded directories can be skipped. Read-only native DataValidation commandlet. Exit alone is not proof all assets passed; retain invalid/skipped/unable counters and per-asset logs. Not every asset or visual quality.'}
    $result | ConvertTo-Json | Set-Content "$out/process.json" -Encoding utf8
    $results+=$result
    $results | ConvertTo-Json -Depth 5 | Set-Content "$root/results.json" -Encoding utf8
    if($null -eq $p.ExitCode){throw 'Exit unavailable. Preserve logs and wait for actual idle state.'}
    # A nonzero validation result remains evidence, not permission to alter assets.
    $result | ConvertTo-Json -Compress
}
