param(
    [Parameter(Mandatory)][string]$Executable,
    [string]$EvidenceName = 'CookedUISmoke',
    [int[]]$Widths = @(1280, 1920),
    [int[]]$Heights = @(720, 1080)
)
$ErrorActionPreference = 'Stop'
if ($Widths.Count -ne $Heights.Count -or $Widths.Count -eq 0) { throw 'Resolution pairs required' }
if ($EvidenceName -notmatch '^[A-Za-z0-9_-]+$') { throw 'EvidenceName must be a simple directory name' }
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$exePath = (Resolve-Path -LiteralPath $Executable).Path
$outputRoot = Join-Path $projectRoot "Saved/Validation/$EvidenceName"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
$results = @()
for ($index = 0; $index -lt $Widths.Count; $index++) {
    if ($Widths[$index] -lt 640 -or $Heights[$index] -lt 480) { throw 'Invalid resolution' }
    $busy = Get-Process UnrealEditor,UnrealEditor-Cmd,UnrealBuildTool,dotnet,LiveCodingConsole,MSBuild,ShaderCompileWorker,Project_J -ErrorAction SilentlyContinue
    if ($busy) { throw 'Close engine/build/game processes before smoke validation' }
    $runDir = Join-Path $outputRoot "$($Widths[$index])x$($Heights[$index])"
    $userDir = Join-Path $runDir 'User'
    if (Test-Path -LiteralPath $userDir) { throw "Use a fresh EvidenceName; user directory already exists: $userDir" }
    New-Item -ItemType Directory -Force -Path $userDir | Out-Null
    $logPath = Join-Path $runDir 'Game.log'
    $arguments = @('-ProjectJUIRuntimeSmoke', '-unattended', '-nosound', '-windowed', '-RenderOffscreen',
        "-ResX=$($Widths[$index])", "-ResY=$($Heights[$index])", '-SaveToUserDir',
        '-csvCategories=Slate', '-csvCompression=0', '-ExecCmds="t.MaxFPS 60,csvprofile FRAMES=240"',
        ('-UserDir="' + $userDir.Replace('\', '/') + '/"'), ('-ABSLOG="' + $logPath + '"'))
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $game = Start-Process -FilePath $exePath -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $game.WaitForExit()
    $watch.Stop()
    $logErrors = if (Test-Path -LiteralPath $logPath) { @(Select-String -LiteralPath $logPath -Pattern 'Log\w+: Error:|Fatal error:') } else { @() }
    $passed = $game.ExitCode -eq 0 -and $logErrors.Count -eq 0 -and (Test-Path -LiteralPath $logPath) -and
        [bool](Select-String -LiteralPath $logPath -SimpleMatch 'PROJECT_J_UI_RUNTIME_SMOKE success=1 cooked=1' -Quiet)
    $result = [pscustomobject]@{ width=$Widths[$index]; height=$Heights[$index]; passed=$passed;
        exitCode=$game.ExitCode; logErrorCount=$logErrors.Count; seconds=$watch.Elapsed.TotalSeconds; log=$logPath; arguments=$arguments }
    $results += $result
    [pscustomobject]@{ executable=$exePath; sha256=(Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash;
        utc=[DateTime]::UtcNow.ToString('o'); runs=$results } | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath (Join-Path $outputRoot 'Results.json') -Encoding utf8
    $result | Select-Object width,height,passed,exitCode,seconds
    if (!$passed) { throw "Cooked UI smoke failed; inspect $logPath" }
}
