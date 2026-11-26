param(
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$RunName,
    [ValidateRange(1025,65535)][int]$SourcePort = 17951,
    [ValidateRange(1025,65535)][int]$DestinationPort = 17952,
    [ValidateRange(0,65535)][int]$SiblingPort = 0,
    [switch]$Reconnect,
    [ValidatePattern('^/(?:Game|Engine)/(?:[A-Za-z0-9_-]+/)*[A-Za-z0-9_-]+$')][string]$SourceMap = '/Engine/Maps/Entry',
    [ValidatePattern('^/(?:Game|Engine)/(?:[A-Za-z0-9_-]+/)*[A-Za-z0-9_-]+$')][string]$DestinationMap = '/Engine/Maps/Entry'
)
$ErrorActionPreference = 'Stop'
if ($SourcePort -eq $DestinationPort) { throw 'Choose two distinct loopback ports.' }
$ports = @($SourcePort, $DestinationPort)
if ($SiblingPort) {
    if ($SiblingPort -le 1024 -or $SiblingPort -in $ports) { throw 'Choose a distinct, nonprivileged sibling loopback port.' }
    $ports += $SiblingPort
}
$active = @(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object {
    $_.Name -match '^(UnrealEditor|Project_J|UnrealBuildTool|dotnet|LiveCoding|MSBuild|ShaderCompileWorker|Uba).*'
})
if ($active.Count) { throw 'Wait for existing engine and build processes.' }
foreach ($port in $ports) {
    if (Get-NetUDPEndpoint -LocalPort $port -ErrorAction SilentlyContinue) { throw "Loopback port $port is in use." }
}
$projectRoot = (Resolve-Path "$PSScriptRoot/../..").Path.Replace([IO.Path]::DirectorySeparatorChar, [char]'/')
. "$PSScriptRoot/../Audit/Continuation-ScratchPaths.ps1"
$scratchContext = Get-ContinuationScratchContext $projectRoot
$scratch = New-ContinuationScratchRun $scratchContext $RunName
$output = "$projectRoot/Saved/Validation/Continuation_20261014/$RunName"
if (Test-Path -LiteralPath $output) { throw 'Choose an unused evidence directory.' }
New-Item -ItemType Directory -Path $output | Out-Null
$exe = 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$runId = [guid]::NewGuid().ToString()

function Start-FixtureProcess($role, $target, $roleArguments) {
    $arguments = @("$projectRoot/Project_J.uproject", $target, '-unattended', '-nop4', '-nosound',
        '-NoLiveCoding', '-NoDDCCleanup', '-NullRHI', '-DisablePython', '-multihome=127.0.0.1',
        '-DisablePlugins=EditorToolset,AutomationTestToolset,ToolsetRegistry',
        "-UserDir=$($scratch.UserDirectory)/$role", "-ABSLOG=$output/$role.log") + $roleArguments
    $process = Start-Process -FilePath $exe -WindowStyle Hidden -PassThru -ArgumentList (
        ($arguments | ForEach-Object { '"' + $_ + '"' }) -join ' ')
    $null = $process.Handle
    return $process
}

function Wait-FixtureMarker($role, $marker, $process) {
    $deadline = [DateTime]::UtcNow.AddSeconds(75)
    $log = "$output/$role.log"
    while (!(Test-Path -LiteralPath $log) -or !(Select-String -LiteralPath $log -SimpleMatch $marker -Quiet)) {
        if ($process.HasExited -or [DateTime]::UtcNow -gt $deadline) { throw "No $marker marker in $log." }
        Start-Sleep -Milliseconds 300
    }
}

$fixtureMode = '?game=/Script/Project_J.Project_JLocalDungeonFixture'
$shared = @('-server', "-ProjectJLocalDungeonRun=$runId", "-ProjectJLocalDungeonDirectory=$($scratch.Root)",
    "-ProjectJLocalDungeonSourcePort=$SourcePort", "-ProjectJLocalDungeonDestinationPort=$DestinationPort")
if ($Reconnect) { $shared += '-ProjectJLocalDungeonReconnect' }
if ($SiblingPort) { $shared += "-ProjectJLocalDungeonSiblingPort=$SiblingPort" }
$source = Start-FixtureProcess Source ($SourceMap + $fixtureMode) ($shared + @('-ProjectJLocalDungeonRole=Source', "-port=$SourcePort"))
Wait-FixtureMarker Source "DUNGEON_FIXTURE_READY Source map=$SourceMap" $source
$destination = Start-FixtureProcess Destination ($DestinationMap + $fixtureMode) ($shared + @('-ProjectJLocalDungeonRole=Destination', "-port=$DestinationPort"))
Wait-FixtureMarker Destination "DUNGEON_FIXTURE_READY Destination map=$DestinationMap" $destination
$returnServer = $source
$returnServerRole = 'Source'
if ($SiblingPort) {
    $returnServer = Start-FixtureProcess Sibling ($SourceMap + $fixtureMode) ($shared + @('-ProjectJLocalDungeonRole=Sibling', "-port=$SiblingPort"))
    $returnServerRole = 'Sibling'
    Wait-FixtureMarker Sibling "DUNGEON_FIXTURE_READY Sibling map=$SourceMap" $returnServer
}
$client = Start-FixtureProcess Client "127.0.0.1:$SourcePort" @('-game')
Write-Output "Started local dungeon travel $($source.Id) → $($destination.Id), client PID $($client.Id)"
Wait-FixtureMarker Destination 'DUNGEON_FIXTURE_DESTINATION_JOINED' $destination
$reconnectedClient = $null
if ($Reconnect) {
    Wait-FixtureMarker Client 'DUNGEON_FIXTURE_CLIENT_DISCONNECTED' $client
    Wait-FixtureMarker Destination 'DUNGEON_FIXTURE_RECONNECT_TICKET_READY' $destination
    if (!$client.WaitForExit(15000) -or $client.ExitCode -ne 0) { throw 'First client did not disconnect cleanly.' }
    $reconnectUrl = (Get-Content -LiteralPath (Join-Path $scratch.Root 'reconnect.url') -Raw).Trim()
    if (!$reconnectUrl.StartsWith("127.0.0.1:$DestinationPort" + '?TicketInstance=')) { throw 'Unexpected reconnect destination.' }
    $reconnectedClient = Start-FixtureProcess ReconnectedClient $reconnectUrl @('-game')
    Wait-FixtureMarker Destination 'DUNGEON_FIXTURE_DESTINATION_RECONNECTED' $destination
}
Wait-FixtureMarker $returnServerRole "DUNGEON_FIXTURE_RETURNED_AT_ANCHOR $returnServerRole" $returnServer
$returnClient = if ($Reconnect) { $reconnectedClient } else { $client }
$returnRole = if ($Reconnect) { 'ReconnectedClient' } else { 'Client' }
Wait-FixtureMarker $returnRole 'DUNGEON_FIXTURE_CLIENT_FINISHED' $returnClient
$completed = @($returnClient, $destination, $source)
if ($SiblingPort) { $completed += $returnServer }
foreach ($process in $completed) {
    if (!$process.WaitForExit(15000) -or $process.ExitCode -ne 0) { throw "PID $($process.Id) did not exit cleanly." }
}
[ordered]@{ passed = $true; sourcePort = $SourcePort; destinationPort = $DestinationPort
    sourceMap = $SourceMap; destinationMap = $DestinationMap
    sourceExit = $source.ExitCode; destinationExit = $destination.ExitCode; clientExit = $client.ExitCode
    destinationTicketAccepted = $true; originalAnchorRestored = $true
    returnServer = $returnServerRole; returnServerExit = $returnServer.ExitCode
    reconnectedDuringGrace = $Reconnect.IsPresent
    reconnectedClientExit = if ($Reconnect) { $returnClient.ExitCode } else { $null } } |
    ConvertTo-Json | Set-Content -LiteralPath "$output/verification.json" -Encoding UTF8
Write-Output "Local dungeon travel and return passed. $output"
