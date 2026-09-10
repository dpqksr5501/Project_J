param([Parameter(Mandatory = $true)][string]$RunRoot)
$ErrorActionPreference = 'Stop'
$culture = [Globalization.CultureInfo]::InvariantCulture
function Numbers($rows, $name) { @($rows | ForEach-Object { [double]::Parse($_.$name, $culture) } | Sort-Object) }
function Percentile($values, $p) {
    if ($values.Count -eq 0) { return $null }
    $values[[Math]::Max(0, [Math]::Ceiling($values.Count * $p) - 1)]
}
$frames = Import-Csv -LiteralPath (Join-Path $RunRoot 'Metrics/frames.csv')
$requests = Import-Csv -LiteralPath (Join-Path $RunRoot 'Metrics/requests.csv')
$rows = foreach ($group in ($frames | Group-Object phase)) {
    $ticks = Numbers $group.Group 'world_tick_ms'
    $policies = Numbers $group.Group 'policy_ms'
    $matching = @($requests | Where-Object phase -eq $group.Name)
    $queue = Numbers $matching 'queue_ms'
    $engine = Numbers $matching 'engine_observed_ms'
    $delivery = Numbers $matching 'delivery_ms'
    $total = Numbers $matching 'total_ms'
    [pscustomobject][ordered]@{
        phase = $group.Name; frames = $group.Count; request_samples = $matching.Count
        succeeded_deliveries = @($matching | Where-Object status -eq '0').Count
        failed_deliveries = @($matching | Where-Object status -eq '1').Count
        expired_deliveries = @($matching | Where-Object status -eq '2').Count
        world_tick_p50_ms = Percentile $ticks 0.5; world_tick_p95_ms = Percentile $ticks 0.95; world_tick_p99_ms = Percentile $ticks 0.99
        policy_p95_ms = Percentile $policies 0.95
        queue_p95_ms = Percentile $queue 0.95; engine_observed_p95_ms = Percentile $engine 0.95
        delivery_p95_ms = Percentile $delivery 0.95; total_p95_ms = Percentile $total 0.95
        peak_requests = ($group.Group | Measure-Object requests -Maximum).Maximum
        peak_inflight = ($group.Group | Measure-Object inflight -Maximum).Maximum
        peak_build_remaining = ($group.Group | Measure-Object build_remaining -Maximum).Maximum
        peak_arrived = ($group.Group | Measure-Object arrived -Maximum).Maximum
    }
}
$rows | Export-Csv -LiteralPath (Join-Path $RunRoot 'phase-summary.csv') -NoTypeInformation -Encoding utf8
[ordered]@{
    semantics = 'Nearest-rank percentiles over delivered results, attributed to submission phase. Cancelled/rejected requests have no delivery latency sample. Service success does not imply Action accepted a current path. World tick is synthetic fixture GT tick, not frame/FPS. Engine observed wall latency includes engine queue and GT completion. In-flight limit is not worker count. Trace CPU must separate query worker, PostponeAsyncQueries GT wait, and tile rebuild; Mixed100 intentionally overlaps requests and tile work.'
    phases = @($rows)
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $RunRoot 'phase-summary.json') -Encoding utf8
$rows | Format-Table -AutoSize
