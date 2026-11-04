# Large generated outputs only; evidence remains under the caller's Saved run.
# No cleanup, environment mutation, engine setting or external sync configuration.
function Get-ContinuationFullPath([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path) -or $Path -notmatch '^[A-Za-z]:[\\/]') { throw 'Expected a local absolute filesystem path.' }
    return [IO.Path]::GetFullPath($Path).Replace([IO.Path]::AltDirectorySeparatorChar,[IO.Path]::DirectorySeparatorChar).TrimEnd([IO.Path]::DirectorySeparatorChar)
}

function Test-ContinuationPathWithin([string]$Path,[string]$Root) {
    return [string]::Equals($Path,$Root,[StringComparison]::OrdinalIgnoreCase) -or $Path.StartsWith($Root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)
}

function Assert-ContinuationNoReparse([string]$Path) {
    $cursor = Get-ContinuationFullPath $Path
    while ($cursor) {
        $item = $null
        try { $item = Get-Item -LiteralPath $cursor -Force -ErrorAction Stop }
        catch [System.Management.Automation.ItemNotFoundException] { }
        if ($item) {
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or !$item.PSIsContainer) { throw "Directory/reparse guard rejected: $cursor" }
        }
        $parent = [IO.Directory]::GetParent($cursor)
        $cursor = if ($parent) { $parent.FullName } else { $null }
    }
}

function Get-ContinuationScratchContext([string]$ProjectRoot) {
    $project = Get-ContinuationFullPath $ProjectRoot
    # A tool sandbox may replace process TEMP for every invocation. Read the
    # persisted OS TEMP so a later Stage can reuse this checkout's earlier Cook.
    $configuredTemp = [Environment]::GetEnvironmentVariable('TEMP','User')
    if ([string]::IsNullOrWhiteSpace($configuredTemp)) { $configuredTemp = [Environment]::GetEnvironmentVariable('TEMP','Machine') }
    $tempRoot = Get-ContinuationFullPath ([Environment]::ExpandEnvironmentVariables($configuredTemp))
    $documents = Get-ContinuationFullPath ([Environment]::GetFolderPath('MyDocuments'))
    foreach ($protected in @($project,$documents)) {
        if ((Test-ContinuationPathWithin $tempRoot $protected) -or (Test-ContinuationPathWithin $protected $tempRoot)) { throw 'OS Temp must be outside and not contain the checkout/Documents.' }
    }
    Assert-ContinuationNoReparse $project
    Assert-ContinuationNoReparse $tempRoot
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { $digest = $hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($project.ToUpperInvariant())) }
    finally { $hasher.Dispose() }
    $suffix = ([BitConverter]::ToString($digest)).Replace('-','').Substring(0,16).ToLowerInvariant()
    $leaf = ([IO.Path]::GetFileName($project) -replace '[^A-Za-z0-9_-]','_').ToLowerInvariant()
    $checkoutRoot = Join-Path (Join-Path $tempRoot 'ProjectJContinuation') "$leaf-$suffix"
    Assert-ContinuationNoReparse $checkoutRoot
    return [pscustomobject]@{ProjectRoot=$project;TempRoot=$tempRoot;CheckoutRoot=$checkoutRoot}
}

function New-ContinuationScratchRun($Context,[string]$Name) {
    if ($Name -notmatch '^[A-Za-z0-9_-]+$') { throw 'Invalid continuation scratch run name.' }
    $run = Join-Path $Context.CheckoutRoot $Name
    Assert-ContinuationNoReparse $run
    if (Test-Path -LiteralPath $run) { throw 'Preserve prior scratch run; choose a new evidence name.' }
    if (!(Test-Path -LiteralPath $Context.CheckoutRoot)) { New-Item -ItemType Directory -Path $Context.CheckoutRoot | Out-Null }
    Assert-ContinuationNoReparse $Context.CheckoutRoot
    New-Item -ItemType Directory -Path $run | Out-Null
    Assert-ContinuationNoReparse $run
    return [pscustomobject]@{Root=$run;UserDirectory=(Join-Path $run 'User');CookDirectory=(Join-Path $run 'Cooked');PackageDirectory=(Join-Path $run 'Package')}
}

function Resolve-ContinuationCookDirectory($Context,[string]$CookDirectory) {
    # Preserve the existing runner's checkout-relative Cook input contract.
    # Canonical scope/reparse/marker checks below still decide what is allowed.
    if (![string]::IsNullOrWhiteSpace($CookDirectory) -and ![IO.Path]::IsPathRooted($CookDirectory)) {
        $CookDirectory = [IO.Path]::GetFullPath($CookDirectory,$Context.ProjectRoot)
    }
    $cook = Get-ContinuationFullPath $CookDirectory
    Assert-ContinuationNoReparse $cook
    if (!(Test-Path -LiteralPath $cook -PathType Container)) { throw 'Cook directory does not exist.' }
    # These are the two exact retained legacy outputs, not arbitrary Saved history.
    $legacyRoot = Join-Path $Context.ProjectRoot 'Saved/Validation/Continuation_20261014'
    $allowed = $false
    foreach ($run in @('MMOFoundation_20261028_UICook14','MMOFoundation_20261028_UICook15')) {
        $legacyCook = Get-ContinuationFullPath (Join-Path (Join-Path $legacyRoot $run) 'Cooked')
        if ([string]::Equals($cook,$legacyCook,[StringComparison]::OrdinalIgnoreCase)) { $allowed = $true; break }
    }
    $prefix = $Context.CheckoutRoot+[IO.Path]::DirectorySeparatorChar
    if (!$allowed -and $cook.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) {
        $parts = $cook.Substring($prefix.Length).Split([IO.Path]::DirectorySeparatorChar)
        $allowed = $parts.Count -eq 2 -and $parts[0] -match '^[A-Za-z0-9_-]+$' -and $parts[1] -ieq 'Cooked'
    }
    if (!$allowed) { throw 'Use the retained exact Cook14/15 output or this checkout OS-Temp run/Cooked.' }
    if (!(Test-Path -LiteralPath (Join-Path $cook 'Windows/ue.projectstore')) -and !(Test-Path -LiteralPath (Join-Path $cook 'Windows/Project_J/Metadata/packagestore.manifest'))) { throw 'Official Windows package store layout missing; do not stage a double-wrapped or incomplete Cook.' }
    return $cook
}
