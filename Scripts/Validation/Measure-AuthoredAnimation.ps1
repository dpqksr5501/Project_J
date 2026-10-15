param(
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$RunName,
    [int[]]$Counts = @(1,50,100,200),
    [ValidateRange(1,5)][int]$Repeats = 2,
    [ValidateRange(1,3600)][int]$WarmupTicks = 120,
    [ValidateRange(1,3600)][int]$MeasuredTicks = 120,
    [switch]$Trace,
    [switch]$NoDDCCleanup,
    [string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.8'
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace('\','/')
Set-Location -LiteralPath $projectRoot
if (@($Counts | Where-Object { $_ -lt 1 -or $_ -gt 200 }).Count -or !$Counts.Count) { throw 'Counts must be between 1 and 200.' }
$runRoot = "$projectRoot/Saved/Validation/AuthoredAnimation_20261005/$RunName"
if (Test-Path -LiteralPath $runRoot) { throw 'Use a unique run name.' }
New-Item -ItemType Directory -Path $runRoot | Out-Null
$sources = @(& git -c core.fsmonitor=false ls-files --modified --others --exclude-standard -- Source Scripts Config Project_J.uproject)
$hashes = @($sources | Sort-Object -Unique | ForEach-Object {
    $path = Join-Path $projectRoot $_
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        $copy = "$runRoot/SourceSnapshot/$_"
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $copy) | Out-Null
        Copy-Item -LiteralPath $path -Destination $copy
        [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
    }
})
$artifactPaths = @('Project_J.uproject','Binaries/Win64/UnrealEditor-Project_J.dll','Binaries/Win64/UnrealEditor-Project_JCharacter.dll',
    'Content/Animation_Logic/ABPs/ABP_Humanoid_Master.uasset','Content/Animation_Logic/PSS/PSS_Combat_Run_ForwardTrajectory.uasset',
    'Content/Animation_Logic/PSD/PSD_Player_Combat_Locomotion/PSD_Combat_Run_ForwardCycle.uasset','Content/DataAssetSets/Animation_Profiles/LocomotionProfiles/DA_Player_Profile.uasset')
$artifactHashes = @($artifactPaths | ForEach-Object { [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $projectRoot $_) -Algorithm SHA256).Hash} })
[ordered]@{head=(& git -c core.fsmonitor=false rev-parse HEAD);cpu=(Get-CimInstance Win32_Processor).Name;counts=$Counts;repeats=$Repeats;warmupTicks=$WarmupTicks;measuredTicks=$MeasuredTicks;simulationHz=60;trace=[bool]$Trace;noDDCCleanup=[bool]$NoDDCCleanup;nullRHI=$true;budgetMs=2;sourceFiles=$hashes;artifacts=$artifactHashes} | ConvertTo-Json -Depth 5 | Set-Content "$runRoot/manifest.json" -Encoding utf8
foreach ($count in $Counts) {
    if (@(Get-Process UnrealEditor*,Project_J,UnrealBuildTool,dotnet,LiveCodingConsole,MSBuild,ShaderCompileWorker -ErrorAction SilentlyContinue).Count) { throw 'An engine/build process is still active. Wait for normal completion.' }
    $output = "$runRoot/Count$count"
    New-Item -ItemType Directory -Path $output | Out-Null
    $arguments = @("$projectRoot/Project_J.uproject",'-unattended','-NullRHI','-nosound','-nosplash','-NoLiveCoding','-nop4',
        "-ProjectJAnimationCount=$count","-ProjectJAnimationRepeats=$Repeats","-ProjectJAnimationOutput=$output/cpu.json",
        "-ProjectJAnimationWarmupTicks=$WarmupTicks","-ProjectJAnimationMeasuredTicks=$MeasuredTicks",
        "-UserDir=$output/User",'-SaveToUserDir',"-ABSLOG=$output/Run.log","-ReportExportPath=$output/Automation",'-TestExit=Automation Test Queue Empty',
        '-ini:Engine:[SystemSettings]:a.Budget.Enabled=1', '-ini:Engine:[SystemSettings]:a.Budget.BudgetMs=2',
        '-ExecCmds=t.MaxFPS 0,Automation RunTests ProjectJ.Animation.AuthoredCPU; Quit')
    if ($Trace -and $count -eq ($Counts | Measure-Object -Maximum).Maximum) { $arguments += @('-trace=cpu,frame,bookmark,region,counters',"-tracefile=$output/CPU.utrace",'-statnamedevents') }
    if ($NoDDCCleanup) { $arguments += '-NoDDCCleanup' }
    $arguments | ConvertTo-Json | Set-Content "$output/arguments.json" -Encoding utf8
    $quoted = @($arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $processWatch = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath "$EngineRoot/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" -ArgumentList $quoted -WindowStyle Hidden -PassThru
    $nativeHandle = $process.Handle
    Write-Output "Started authored CPU count=$count PID=$($process.Id) output=$output"
    $process.WaitForExit()
    $processWatch.Stop()
    $osCpu = [ordered]@{available=$false;wholeProcessWallSeconds=$processWatch.Elapsed.TotalSeconds;logicalProcessors=[Environment]::ProcessorCount;
        scope_ko='전체 자식 프로세스 setup/warmup/측정/종료의 OS 누적 CPU. native world/GT/worker 측정 구간과 별도이며 FPS/GPU가 아니다.'}
    try {
        $osCpu.totalCpuSeconds=$process.TotalProcessorTime.TotalSeconds
        $osCpu.userCpuSeconds=$process.UserProcessorTime.TotalSeconds
        $osCpu.kernelCpuSeconds=$process.PrivilegedProcessorTime.TotalSeconds
        $osCpu.available=$true
    } catch { $osCpu.captureError=$_.Exception.Message }
    $osCpu | ConvertTo-Json | Set-Content "$output/ProcessCpu.json" -Encoding utf8
    [ordered]@{exitCode=$process.ExitCode;hasExited=$process.HasExited;nativeHandleCapturedBeforeWait=$true} |
        ConvertTo-Json | Set-Content "$output/process.json" -Encoding utf8
    if ($null -eq $process.ExitCode -or $process.ExitCode -ne 0 -or !(Test-Path -LiteralPath "$output/cpu.json")) { throw "CPU fixture failed for count=$count. Inspect $output/Run.log." }
    $report = Get-Content -LiteralPath "$output/Automation/index.json" -Raw | ConvertFrom-Json
    $measurement = Get-Content -LiteralPath "$output/cpu.json" -Raw | ConvertFrom-Json
    $matchingTests = @($report.tests | Where-Object { $_.fullTestPath -eq 'ProjectJ.Animation.AuthoredCPU' })
    if (!$measurement.completed -or $measurement.fixture_version -ne 2 -or $measurement.rows.Count -ne 8 * $Repeats -or $matchingTests.Count -ne 1 -or
        $report.tests.Count -ne 1 -or $report.failed -gt 0 -or $report.notRun -gt 0 -or $report.inProcess -gt 0) {
        throw "Incomplete or failed CPU fixture: $output"
    }
    if ($matchingTests[0].state -ne 'Success' -or $matchingTests[0].errors -gt 0 -or
        @($measurement.rows | Where-Object { $_.measured_ticks -ne $MeasuredTicks -or $_.warmup_ticks -ne $WarmupTicks -or
            $_.world_tick_samples_ms.Count -ne $MeasuredTicks -or $_.observer_samples_ms.Count -ne $MeasuredTicks -or
            $_.attack_montage_actor_counts_after_tick.Count -ne $MeasuredTicks }).Count) {
        throw "Measurement window or leaf-test validation failed: $output"
    }
    Write-Output "Completed authored CPU count=$count rows=$($measurement.rows.Count)"
}
Write-Output "Completed all measurements: $runRoot"
