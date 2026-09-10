param(
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$RunName,
    [ValidateRange(2,8)][int]$ClientCount = 2,
    [ValidateRange(100,2048)][int]$NPCCount = 100,
    [switch]$AllRelevant,
    [switch]$ParallelNet,
    [int]$Port = 17871,
    [string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.8'
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace('\','/')
Set-Location -LiteralPath $projectRoot
$busy = @(Get-Process UnrealEditor*,UnrealBuildTool,dotnet,LiveCodingConsole,MSBuild,ShaderCompileWorker -ErrorAction SilentlyContinue)
if ($busy.Count) { throw 'Engine processes active; finish them normally before another run/build.' }
if ($Port -lt 1024 -or $Port -gt 65535) { throw 'Invalid local test port.' }
$runRoot = "$projectRoot/Saved/Validation/GroupD_20260910/$RunName"
if (Test-Path -LiteralPath $runRoot) { throw 'Run directory already exists; choose a unique name.' }
New-Item -ItemType Directory -Path $runRoot | Out-Null
$files = @(& git -c core.fsmonitor=false ls-files --modified --others --exclude-standard -- Source Scripts Config Project_J.uproject)
$snapshot = @($files | Sort-Object -Unique | ForEach-Object {
    $sourcePath = Join-Path $projectRoot $_
    if (Test-Path -LiteralPath $sourcePath -PathType Leaf) {
        $destination = "$runRoot/SourceSnapshot/$_"
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath $sourcePath -Destination $destination
        [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash}
    }
})
[ordered]@{run=$RunName;allRelevant=[bool]$AllRelevant;parallelNet=[bool]$ParallelNet;port=$Port;head=(& git -c core.fsmonitor=false rev-parse HEAD);cpu=(Get-CimInstance Win32_Processor).Name;clientCount=$ClientCount;npcCount=$NPCCount;scenario='Dedicated server plus separate real socket clients and native NPCs, NullRHI. Includes production PlayerState owner-only inventory and equipment FastArrays. Total driver bytes include fixture control traffic.';sourceFiles=$snapshot} |
    ConvertTo-Json -Depth 5 | Set-Content "$runRoot/manifest.json" -Encoding utf8
$exe = "$EngineRoot/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
$common = @("$projectRoot/Project_J.uproject",'-game','-unattended','-nop4','-nosplash','-NullRHI','-nosound','-ProjectJNetworkFixture',"-ProjectJFixtureClients=$ClientCount","-ProjectJFixtureNPCs=$NPCCount",'-statnamedevents','-trace=cpu,frame,bookmark,region,counters,net','-NetTrace=1','-ExecCmds=t.MaxFPS 30')
# Start-Process needs quotes retained around arguments containing spaces.
function Start-Fixture($arguments) {
    $quoted = @($arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    Start-Process -FilePath $exe -ArgumentList $quoted -WindowStyle Hidden -PassThru
}
$serverArgs = $common + @('/Engine/Maps/Entry?game=/Script/Project_J.Project_JNetworkLoadGameMode','-server',"-port=$Port","-ABSLOG=$runRoot/Server.log","-ProjectJDOutput=$runRoot/Server","-tracefile=$runRoot/Server.utrace")
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
    Start-Fixture ($common + @("127.0.0.1:$Port","-ABSLOG=$runRoot/Client$_.log","-ProjectJDOutput=$runRoot/Client$_","-tracefile=$runRoot/Client$_.utrace"))
})
@($server) + $clients | Select-Object Id,ProcessName | ConvertTo-Json | Set-Content "$runRoot/processes.json" -Encoding utf8
Write-Output "Started server and $ClientCount clients: $runRoot"
foreach ($process in (@($server) + $clients)) { $process.WaitForExit() }
$resultFiles = @('Server/server.json') + @(1..$ClientCount | ForEach-Object {"Client$_/client.json"})
$results = @($resultFiles | ForEach-Object {
    $path = "$runRoot/$_"
    if (!(Test-Path -LiteralPath $path)) { [ordered]@{file=$_;success=$false;reason='Missing result'} }
    else { $result = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json; [ordered]@{file=$_;success=$result.success;reason=$result.reason} }
})
$exitCodes = @((@($server)+$clients) | ForEach-Object {$_.ExitCode})
$timeouts = @(Get-ChildItem -LiteralPath $runRoot -Recurse -Filter timeout.json)
$passed = @($results | Where-Object { !$_.success }).Count -eq 0 -and @($exitCodes | Where-Object { $_ -ne 0 }).Count -eq 0 -and $timeouts.Count -eq 0
$startupErrors = @(Get-ChildItem -LiteralPath $runRoot -Filter '*.log' | Select-String -Pattern 'LogPython: Error:' | ForEach-Object { $_.Line })
[ordered]@{passed=$passed;results=$results;exitCodes=$exitCodes;timeouts=$timeouts.Count;startupPythonErrors=$startupErrors;note='Fixture assertions and exit codes are authoritative. Engine ToolsetRegistry Python startup errors, if present, are recorded separately; a passing fixture does not mean the whole log is clean.'} | ConvertTo-Json -Depth 5 | Set-Content "$runRoot/verification.json" -Encoding utf8
if (!$passed) { throw "Network fixture failed. Inspect $runRoot/verification.json and logs." }
Write-Output "All processes completed: $runRoot"
