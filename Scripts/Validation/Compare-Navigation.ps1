param(
    [Parameter(Mandatory = $true)][string]$Before,
    [Parameter(Mandatory = $true)][string]$After,
    [Parameter(Mandatory = $true)][string]$Output
)
$ErrorActionPreference = 'Stop'
$culture = [Globalization.CultureInfo]::InvariantCulture
function Number($value) { [double]::Parse($value, $culture) }
function DurationMs($events) {
    $sum = 0.0
    foreach ($event in $events) { $sum += ((Number $event.EndTime) - (Number $event.StartTime)) * 1000 }
    $sum
}
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$rows = foreach ($run in @($Before, $After)) {
    $verification = Get-Content "$run/verification.json" -Raw | ConvertFrom-Json
    if ($verification.failed -or $verification.errors -or $verification.editorExit -or !$verification.metricsAvailable) { throw "Invalid run: $run" }
    $phases = Import-Csv "$run/phase-summary.csv"
    $frames = Import-Csv "$run/Metrics/frames.csv"
    foreach ($phase in $phases) {
        $events = @(Import-Csv "$run/TraceExport/Events-ProjectJ.NavLoad.$($phase.phase).csv")
        $gtIds = @($events | Where-Object TimerName -eq 'ProjectJ_NavLoad_WorldTick' | Select-Object -ExpandProperty ThreadId -Unique)
        $queries = @($events | Where-Object TimerName -eq 'FSimpleDelegateGraphTask.NavigationSystem batched async queries')
        $tiles = @($events | Where-Object TimerName -eq 'Recast: do work')
        $gtEvents = @($events | Where-Object { $_.ThreadId -in $gtIds })
        $waits = @($gtEvents | Where-Object TimerName -eq 'WaitUntilTasksComplete')
        $navPostTicks = @($gtEvents | Where-Object TimerName -eq 'UNavigationSystemV1::OnWorldPostActorTick')
        $navWaits = @($waits | Where-Object {
            $wait = $_
            @($navPostTicks | Where-Object {
                $_.ThreadId -eq $wait.ThreadId -and (Number $_.StartTime) -le (Number $wait.StartTime) -and (Number $_.EndTime) -ge (Number $wait.EndTime)
            }).Count -gt 0
        })
        $phaseFrames = @($frames | Where-Object phase -eq $phase.phase)
        $last = $phaseFrames[-1]
        $firstIndex = [array]::IndexOf($frames, $phaseFrames[0])
        $previous = if ($firstIndex -gt 0) { $frames[$firstIndex - 1] } else { $null }
        [pscustomobject][ordered]@{
            run = Split-Path $run -Leaf; phase = $phase.phase
            frames = [int]$phase.frames; arrivals = [int]$phase.peak_arrived
            delivered = [int]$phase.request_samples; failed = [int]$phase.failed_deliveries
            action_failed = [int]$last.action_failed - [int]$previous.action_failed
            action_stale = [int]$last.action_stale - [int]$previous.action_stale
            action_backoffs = [int]$last.retry_scheduled - [int]$previous.retry_scheduled
            rejected = [int]$last.rejected - [int]$previous.rejected
            world_tick_p95_ms = $phase.world_tick_p95_ms; policy_p95_ms = $phase.policy_p95_ms
            queue_p95_ms = $phase.queue_p95_ms; engine_observed_p95_ms = $phase.engine_observed_p95_ms
            delivery_p95_ms = $phase.delivery_p95_ms; total_p95_ms = $phase.total_p95_ms
            query_batch_count = $queries.Count; query_worker_scope_ms = DurationMs $queries
            query_on_gt_count = @($queries | Where-Object { $_.ThreadId -in $gtIds }).Count
            query_thread_ids = ($queries.ThreadId | Sort-Object -Unique) -join ';'
            tile_work_count = $tiles.Count; tile_worker_scope_ms = DurationMs $tiles
            tile_on_gt_count = @($tiles | Where-Object { $_.ThreadId -in $gtIds }).Count
            nav_post_tick_wait_count = $navWaits.Count; nav_post_tick_wait_ms = DurationMs $navWaits
            all_gt_task_wait_ms = DurationMs $waits
            project_policy_scope_ms = DurationMs @($gtEvents | Where-Object TimerName -eq 'ProjectJ_NPCPath_AdmissionAndDelivery')
            nav_tick_scope_ms = DurationMs @($gtEvents | Where-Object TimerName -eq 'Nav Tick Time')
        }
    }
}
$rows | Export-Csv "$Output/comparison.csv" -NoTypeInformation -Encoding utf8
[ordered]@{
    before = (Resolve-Path $Before).Path; after = (Resolve-Path $After).Path
    semantics = @(
        'Same fixture, one run per variant; this is not a statistical FPS/performance improvement claim.'
        'Worker numbers are sums of inclusive elapsed CPU trace scopes, not OS scheduled CPU time. Query batch and tile do-work scopes are separate; never sum their child timers again.'
        'Game thread is identified from the fixture WorldTick scope. Nav wait includes only WaitUntilTasksComplete nested in OnWorldPostActorTick on that thread. The engine PostponeAsyncQueries call is inside that scope; unrelated GT waits are separate.'
        'Engine observed request latency includes engine queue and GT delivery and is not worker execution time. Cancelled/rejected requests have no delivered latency.'
        'Action counters are phase deltas. Service success is distinct from consumer path validity. Timer events and counters are preserved in each trace export.'
    )
    phases = @($rows)
} | ConvertTo-Json -Depth 6 | Set-Content "$Output/comparison.json" -Encoding utf8
$rows | Where-Object phase -in @('Pursuit100','Mixed100','DirtyStorm') | Format-Table run,phase,action_backoffs,rejected,query_worker_scope_ms,tile_worker_scope_ms,nav_post_tick_wait_ms
