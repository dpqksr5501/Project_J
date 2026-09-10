param(
    [string]$Root = '',
    [string]$MassRun = 'Mass03',
    [string]$SpatialRun = 'NetworkSpatial02',
    [string]$AllRelevantRun = 'NetworkAllRelevant02',
    [string]$ParallelRun = 'NetworkParallel02'
)
$ErrorActionPreference = 'Stop'
if (!$Root) { $Root = Join-Path $PSScriptRoot '../../Saved/Validation/GroupD_20260910' }
$Root = (Resolve-Path -LiteralPath $Root).Path.Replace('\','/')
$destination = "$Root/Comparison"
if (Test-Path -LiteralPath $destination) { throw 'Comparison exists; preserve evidence.' }
function Percentile($values, [double]$fraction) {
    $sorted = @($values | Sort-Object)
    if (!$sorted.Count) { return $null }
    return [math]::Round([double]$sorted[[math]::Max(0, [math]::Ceiling($sorted.Count * $fraction) - 1)], 6)
}
function Milliseconds($events) { @($events | ForEach-Object { ([double]$_.EndTime - [double]$_.StartTime) * 1000 }) }
$sourceSignatures = @()
$network = @(foreach ($run in @($AllRelevantRun,$SpatialRun,$ParallelRun)) {
    $runRoot = "$Root/$run"
    $verification = Get-Content "$runRoot/verification.json" -Raw | ConvertFrom-Json
    if (!$verification.passed) { throw "Run did not pass: $run" }
    $manifest = Get-Content "$runRoot/manifest.json" -Raw | ConvertFrom-Json
    $sourceSignatures += (@($manifest.sourceFiles | Where-Object {$_.path -like 'Source/*'} | Sort-Object path | ForEach-Object {$_.path + ':' + $_.sha256}) -join '|')
    $threads = Import-Csv "$runRoot/TraceExportFinal/Threads.csv"
    $gameThread = ($threads | Where-Object Name -eq 'GameThread').Id
    $phases = Import-Csv "$runRoot/Server/network-phases.csv"
    if ($phases.Count -ne 5) { throw "Missing network phases: $run" }
    foreach ($phase in $phases) {
        $events = Import-Csv "$runRoot/TraceExportFinal/Events-ProjectJ.Network.Stage$($phase.stage).csv"
        $flush = Milliseconds @($events | Where-Object {$_.TimerName -eq 'NetDriver TickFlush' -and $_.ThreadId -eq $gameThread})
        $joined = Milliseconds @($events | Where-Object {$_.TimerName -eq 'STAT_NetDriver_TickClientConnections' -and $_.ThreadId -eq $gameThread})
        $ticks = @($events | Where-Object TimerName -eq 'NetConnection Tick' | Sort-Object {[double]$_.StartTime})
        $overlap = 0
        for ($i = 1; $i -lt $ticks.Count; ++$i) {
            if ($ticks[$i].ThreadId -ne $ticks[$i-1].ThreadId -and [double]$ticks[$i].StartTime -lt [double]$ticks[$i-1].EndTime) { ++$overlap }
        }
        if (!$flush.Count -or !$joined.Count -or !$ticks.Count) { throw "Missing actual CPU scopes: $run stage $($phase.stage)" }
        [pscustomobject][ordered]@{
            run=$run;stage=[int]$phase.stage;seconds=[double]$phase.seconds;connections=[int]$phase.connections
            out_bytes=[int]$phase.out_bytes;bytes_per_second=[math]::Round([double]$phase.out_bytes/[double]$phase.seconds,2)
            flush_count=$flush.Count;flush_p50_ms=(Percentile $flush 0.5);flush_p95_ms=(Percentile $flush 0.95);flush_p99_ms=(Percentile $flush 0.99)
            connection_join_p50_ms=(Percentile $joined 0.5);connection_join_p95_ms=(Percentile $joined 0.95)
            connection_tick_gt=@($ticks | Where-Object ThreadId -eq $gameThread).Count
            connection_tick_workers=@($ticks | Where-Object ThreadId -ne $gameThread).Count
            overlapping_connection_pairs=$overlap
        }
    }
})
if (@($sourceSignatures | Sort-Object -Unique).Count -ne 1) { throw 'Network comparisons used different Source snapshots.' }
$massVerification = Get-Content "$Root/$MassRun/verification.json" -Raw | ConvertFrom-Json
if (!$massVerification.passed) { throw 'Mass run did not pass.' }
$mass = @(Import-Csv "$Root/$MassRun/Metrics/mass-values.csv" | Group-Object count,mode | ForEach-Object {
    $values = @($_.Group | ForEach-Object {[double]$_.joined_step_ms})
    [pscustomobject][ordered]@{count=[int]$_.Group[0].count;mode=$_.Group[0].mode;samples=$values.Count;p50_ms=(Percentile $values 0.5);p95_ms=(Percentile $values 0.95);p99_ms=(Percentile $values 0.99)}
})
$massEvents = Import-Csv "$Root/$MassRun/TraceExportFinal/MassEvents.csv"
$massThreads = Import-Csv "$Root/$MassRun/TraceExportFinal/Threads.csv"
$massGameThread = ($massThreads | Where-Object Name -eq 'GameThread').Id
$chunks = @($massEvents | Where-Object TimerName -eq 'ProjectJ_MassMovement_Chunk')
$massWorkers = @($chunks | Where-Object ThreadId -ne $massGameThread)
if (!$massWorkers.Count) { throw 'No actual Mass worker chunks found.' }
New-Item -ItemType Directory -Path $destination | Out-Null
$network | Export-Csv "$destination/network.csv" -NoTypeInformation -Encoding utf8
$mass | Export-Csv "$destination/mass.csv" -NoTypeInformation -Encoding utf8
[ordered]@{
    networkSourceSnapshotsMatch=$true;massRun=$MassRun;network=$network;mass=$mass
    massChunkGT=@($chunks | Where-Object ThreadId -eq $massGameThread).Count;massChunkWorker=$massWorkers.Count
    massWorkerThreads=@($massThreads | Where-Object {$_.Id -in $massWorkers.ThreadId} | Select-Object Id,Name,Group)
    notes=@('Nearest-rank percentiles. Scope durations include waiting; nested CPU scopes are not summed into total CPU utilization.', 'One controlled local server+two client run per final mode, 30 FPS cap, NullRHI. AllRelevant is an experimental control, not historical production baseline.', 'Driver bytes include fixture RPC/control traffic and initial/reentry churn. No FPS, rendered crowd, Actor-memory reduction or party/guild priority claim.')
} | ConvertTo-Json -Depth 7 | Set-Content "$destination/results.json" -Encoding utf8
Write-Output "Actual trace/CSV comparison complete: $destination"
