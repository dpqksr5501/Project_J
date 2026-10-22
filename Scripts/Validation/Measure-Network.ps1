param(
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$RunName,
    [ValidateRange(2,8)][int]$ClientCount = 2,
    [ValidateRange(100,2048)][int]$NPCCount = 100,
    [switch]$AllRelevant,
    [switch]$ParallelNet,
    [switch]$Animation,
    [switch]$CombatInput,
    [switch]$MoverBuffers,
    [ValidateRange(0,200)][int]$PacketLag = 0,
    [ValidateRange(0,20)][int]$PacketLoss = 0,
    [int]$Port = 17871,
    [string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.8'
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/Source-Provenance.ps1"
if ($CombatInput -and !$Animation) { throw 'CombatInput requires the authored Animation fixture.' }
if ($MoverBuffers -and !$Animation) { throw 'MoverBuffers requires the authored Animation fixture.' }
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace('\','/')
Set-Location -LiteralPath $projectRoot
$busy = @(Get-Process UnrealEditor*,Project_J,UnrealBuildTool,dotnet,LiveCodingConsole,MSBuild,ShaderCompileWorker -ErrorAction SilentlyContinue)
if ($busy.Count) { throw 'Engine processes active; finish them normally before another run/build.' }
if ($Port -lt 1024 -or $Port -gt 65535) { throw 'Invalid local test port.' }
$runRoot = "$projectRoot/Saved/Validation/GroupD_20260910/$RunName"
if (Test-Path -LiteralPath $runRoot) { throw 'Run directory already exists; choose a unique name.' }
New-Item -ItemType Directory -Path $runRoot | Out-Null
$files = @(& git -c core.fsmonitor=false ls-files --modified --others --exclude-standard -- Source Scripts Config Plugins Project_J.uproject)
$snapshot = @($files | Sort-Object -Unique | ForEach-Object {
    $sourcePath = Join-Path $projectRoot $_
    if (Test-Path -LiteralPath $sourcePath -PathType Leaf) {
        $destination = "$runRoot/SourceSnapshot/$_"
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath $sourcePath -Destination $destination
        [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash}
    }
})
$provenance=Get-ProjectJSourceProvenance -ProjectRoot $projectRoot -RuntimePaths @("$EngineRoot/Engine/Binaries/Win64/UnrealEditor-Cmd.exe")
[ordered]@{run=$RunName;allRelevant=[bool]$AllRelevant;parallelNet=[bool]$ParallelNet;animation=[bool]$Animation;combatInput=[bool]$CombatInput;packetLag=$PacketLag;packetLoss=$PacketLoss;port=$Port;head=(& git -c core.fsmonitor=false rev-parse HEAD);cpu=(Get-CimInstance Win32_Processor).Name;clientCount=$ClientCount;npcCount=$NPCCount;scenario='Dedicated server plus separate real socket clients and static AOI counter probes, NullRHI; authored animation uses actual Mover player avatars. Includes production PlayerState owner-only inventory and equipment FastArrays. Optional authored player animation: Start, Stop, landing, hidden cancellation, coalesced boundaries; simulated proxies, no fake roles. Total driver bytes include fixture control traffic.';sourceFiles=$snapshot;sourceProvenance=$provenance} |
    ConvertTo-Json -Depth 5 | Set-Content "$runRoot/manifest.json" -Encoding utf8
$exe = "$EngineRoot/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
$common = @("$projectRoot/Project_J.uproject",'-game','-unattended','-nop4','-nosplash','-NullRHI','-nosound','-NoLiveCoding','-NoDDCCleanup','-SaveToUserDir','-ProjectJNetworkFixture',"-ProjectJFixtureClients=$ClientCount","-ProjectJFixtureNPCs=$NPCCount",'-statnamedevents','-trace=cpu,frame,bookmark,region,counters,net','-NetTrace=1','-ExecCmds=t.MaxFPS 30')
if ($Animation) { $common += '-ProjectJAnimationFixture' }
if ($MoverBuffers) {
    $common += '-ProjectJCaptureMoverBuffers'
}
if ($CombatInput) {
    $common += '-ProjectJCombatInputFixture'
    $common = @($common | ForEach-Object { if ($_ -like '-ExecCmds=*') { $_ + ',Project_J.Combat.ComboDebug 1,ProjectJ.Combat.Presentation.Debug 1' } else { $_ } })
}
$common += @("-PktLag=$PacketLag","-PktLoss=$PacketLoss")
# These EditorOnly toolsets require editor UClasses. This host runs -game/-server;
# normal editor/MCP and native Automation RunTests keep their project plugin configuration.
$disabledEditorToolsets = @('EditorToolset','AutomationTestToolset','ToolsetRegistry')
$common += "-DisablePlugins=$($disabledEditorToolsets -join ',')"
# Native game/server fixtures have no Python dependency. ToolsetRegistry can be
# compiled into the host despite plugin selection; skip Python startup itself here.
$common += '-DisablePython'
$disabledEditorToolsets | ConvertTo-Json | Set-Content -LiteralPath "$runRoot/disabled-editor-toolsets.json" -Encoding utf8
# Start-Process needs quotes retained around arguments containing spaces.
function Start-Fixture($arguments) {
    $quoted = @($arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    Start-Process -FilePath $exe -ArgumentList $quoted -WindowStyle Hidden -PassThru
}
$serverArgs = $common + @('/Engine/Maps/Entry?game=/Script/Project_J.Project_JNetworkLoadGameMode','-server',"-port=$Port","-ABSLOG=$runRoot/Server.log","-ProjectJDOutput=$runRoot/Server","-tracefile=$runRoot/Server.utrace","-UserDir=$runRoot/ServerUser/")
if ($AllRelevant) { $serverArgs += '-ProjectJDAllRelevant' }
if ($ParallelNet) {
    $serverArgs += @(
        '-ini:Engine:[/Script/Engine.Engine]:+IrisNetDriverConfigs=(NetDriverName=GameNetDriver,bCanUseIris=true,bCanUseParallelNetConnectionTick=true)',
        '-ini:Engine:[/Script/OnlineSubsystemUtils.IpNetDriver]:ReplicationSystemConfigServer=(bAllowParallelTasks=true,MinConnectionsForParallelTick=2)',
        '-ini:Engine:[SystemSettings]:net.iris.AllowParallelNetTick=1'
    )
}
$serverArgs | ConvertTo-Json | Set-Content "$runRoot/server-arguments.json" -Encoding utf8
$server = Start-Fixture $serverArgs
$deadline = [DateTime]::UtcNow.AddSeconds(120)
while (!(Test-Path "$runRoot/Server.log") -or !(Select-String -LiteralPath "$runRoot/Server.log" -SimpleMatch 'READY Iris=1' -Quiet)) {
    $server.Refresh()
    if ($server.HasExited -or [DateTime]::UtcNow -gt $deadline) { throw "Server did not become ready. Inspect $runRoot/Server.log; do not terminate remaining engine processes." }
    Start-Sleep -Milliseconds 500
}
$clients = @(1..$ClientCount | ForEach-Object {
    Start-Fixture ($common + @("127.0.0.1:$Port","-ABSLOG=$runRoot/Client$_.log","-ProjectJDOutput=$runRoot/Client$_","-tracefile=$runRoot/Client$_.utrace","-UserDir=$runRoot/Client${_}User/"))
})
# Retain all handles before waiting for the first process. Clients may finish
# while the server wait is still in progress.
$fixtureProcesses = @($server) + $clients
$nativeHandles = @($fixtureProcesses | ForEach-Object { $_.Handle })
@($server) + $clients | Select-Object Id,ProcessName | ConvertTo-Json | Set-Content "$runRoot/processes.json" -Encoding utf8
Write-Output "Started server and $ClientCount clients: $runRoot"
foreach ($process in $fixtureProcesses) { $process.WaitForExit() }
$resultFiles = @('Server/server.json') + @(1..$ClientCount | ForEach-Object {"Client$_/client.json"})
$results = @($resultFiles | ForEach-Object {
    $path = "$runRoot/$_"
    if (!(Test-Path -LiteralPath $path)) { [ordered]@{file=$_;success=$false;reason='Missing result'} }
    else { $result = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json; [ordered]@{file=$_;success=$result.success;reason=$result.reason} }
})
$exitCodes = @($fixtureProcesses | ForEach-Object {$_.ExitCode})
$timeouts = @(Get-ChildItem -LiteralPath $runRoot -Recurse -Filter timeout.json)
$bufferValidations = @()
$bufferDiagnostics = @()
if ($MoverBuffers) {
    foreach ($slot in 1..$ClientCount) {
        $captures = @(Get-ChildItem -LiteralPath "$runRoot/Client${slot}User/Saved/Validation/MoverBuffers" -Filter '*.csv' -ErrorAction SilentlyContinue | Where-Object Name -notlike '*.actors.csv')
        $valid = $false
        if ($captures.Count -eq 1) {
            & python "$PSScriptRoot/Validate-MoverBufferCapture.py" $captures[0].FullName --log "$runRoot/Client$slot.log" --min-receive-hz 10 --output "$runRoot/Client$slot/mover-buffer-validation.json"
            $valid = $LASTEXITCODE -eq 0
        }
        $bufferValidations += [ordered]@{client=$slot;passed=$valid}
    }
    $bufferDiagnostics = @(Get-ChildItem -LiteralPath $runRoot -Filter '*.log' | Select-String -Pattern 'LogNetworkPrediction: Warning:|LogNetworkPrediction.*Interpolation out of sync|NetworkPrediction.*SystemFault' | ForEach-Object { $_.Line })
}
$passed = @($results | Where-Object { !$_.success }).Count -eq 0 -and @($exitCodes | Where-Object { $null -eq $_ -or $_ -ne 0 }).Count -eq 0 -and $timeouts.Count -eq 0
$passed = $passed -and @($bufferValidations | Where-Object { !$_.passed }).Count -eq 0
# Client CSV/log validation rejects every diagnostic after the explicit Stage 2
# ready boundary. Preserve startup diagnostics above; the authority has no
# interpolation warmup exemption and all its diagnostics remain failures.
$serverBufferDiagnostics=@()
if($MoverBuffers){$serverBufferDiagnostics=@(Select-String -LiteralPath "$runRoot/Server.log" -Pattern 'LogNetworkPrediction: Warning:|LogNetworkPrediction.*Interpolation out of sync|NetworkPrediction.*SystemFault')}
$passed = $passed -and $serverBufferDiagnostics.Count -eq 0
$startupErrors = @(Get-ChildItem -LiteralPath $runRoot -Filter '*.log' | Select-String -Pattern 'LogPython: Error:' | ForEach-Object { $_.Line })
[ordered]@{passed=$passed;results=$results;exitCodes=$exitCodes;bufferValidations=$bufferValidations;bufferDiagnostics=$bufferDiagnostics;nativeHandleCapturedBeforeWait=$true;timeouts=$timeouts.Count;startupPythonErrors=$startupErrors;note='Fixture assertions and exit codes are authoritative. Buffer capture starts after all clients pass Stage 1; every later NP diagnostic fails CSV/log validation. Raw bufferDiagnostics also retains pre-capture startup diagnostics. A passing fixture does not mean the whole startup log is clean.'} | ConvertTo-Json -Depth 5 | Set-Content "$runRoot/verification.json" -Encoding utf8
if (!$passed) { throw "Network fixture failed. Inspect $runRoot/verification.json and logs." }
Write-Output "All processes completed: $runRoot"
