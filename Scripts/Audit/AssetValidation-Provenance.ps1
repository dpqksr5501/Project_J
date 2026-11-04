# Selected package-input provenance only; this is not a transitive dependency closure proof.
if (!(Get-Command Get-ProjectJSourceProvenance -ErrorAction SilentlyContinue) -or !(Get-Command Assert-ProjectJMatchingProvenance -ErrorAction SilentlyContinue)) {
 . "$PSScriptRoot/../Validation/Source-Provenance.ps1"
}
function Get-ProjectJAssetValidationEditorPath {
 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
}
function Read-ProjectJValidationJson {
 param([Parameter(Mandatory)][string]$Path,[int]$MaxBytes=1048576)
 $file=Get-Item -LiteralPath $Path -ErrorAction Stop
 if($file.PSIsContainer -or $file.Length -le 0 -or $file.Length -gt $MaxBytes){throw "Missing/oversized JSON: $Path"}
 $bytes=[IO.File]::ReadAllBytes($file.FullName)
 $json=[Text.UTF8Encoding]::new($false,$true).GetString($bytes).TrimStart([char]0xfeff)
 $options=[System.Text.Json.JsonDocumentOptions]::new();$options.MaxDepth=20
 $doc=[System.Text.Json.JsonDocument]::Parse($json,$options)
 function Assert-UniqueJsonKeys([System.Text.Json.JsonElement]$Element){
  if($Element.ValueKind -eq [System.Text.Json.JsonValueKind]::Object){
   $keys=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
   foreach($p in $Element.EnumerateObject()){if(!$keys.Add($p.Name)){throw "Duplicate JSON key: $($p.Name)"};Assert-UniqueJsonKeys $p.Value}
  }elseif($Element.ValueKind -eq [System.Text.Json.JsonValueKind]::Array){foreach($v in $Element.EnumerateArray()){Assert-UniqueJsonKeys $v}}
 }
 try{Assert-UniqueJsonKeys $doc.RootElement;if($doc.RootElement.ValueKind -ne [System.Text.Json.JsonValueKind]::Object){throw 'Expected a JSON object.'}}
 finally{$doc.Dispose()}
 [pscustomobject]@{value=($json|ConvertFrom-Json -Depth 20);sha256=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant();path=$file.FullName}
}
function Assert-ProjectJValidationFields {
 param($Object,[string[]]$Fields)
 if($null -eq $Object){throw 'Missing validation object.'}
 $names=if($Object -is [Collections.IDictionary]){@($Object.Keys)}elseif($Object -is [pscustomobject]){@($Object.PSObject.Properties.Name)}else{throw 'Expected validation object.'}
 if($names.Count -ne $Fields.Count -or @($names|Where-Object {$_ -cnotin $Fields}).Count){throw "Unknown/missing validation fields: $($names -join ',')"}
}
function Test-ProjectJValidationInteger($Value){($Value -is [int] -or $Value -is [long]) -and $Value -ge 0}
function Get-ProjectJSelectedAssetManifest {
 param([Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$SelectionManifest)
 $root=(Resolve-Path -LiteralPath $ProjectRoot -ErrorAction Stop).Path
 if([IO.Path]::GetExtension($SelectionManifest) -ine '.json'){throw 'SelectionManifest must be JSON.'}
 $read=Read-ProjectJValidationJson -Path $SelectionManifest
 $m=$read.value;Assert-ProjectJValidationFields $m @('schema','assetPaths','inputFiles')
 if(!(Test-ProjectJValidationInteger $m.schema) -or $m.schema -ne 1 -or $m.assetPaths -isnot [Array] -or $m.inputFiles -isnot [Array] -or
    $m.assetPaths.Count -lt 1 -or $m.assetPaths.Count -gt 128 -or $m.inputFiles.Count -lt 1 -or $m.inputFiles.Count -gt 512){throw 'Invalid schema or selected asset/input bounds.'}
 $assets=[Collections.Generic.List[string]]::new();$seenAssets=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
 $inputs=[Collections.Generic.List[string]]::new();$seenInputs=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
 foreach($asset in $m.assetPaths){
  if($asset -isnot [string] -or $asset -cnotmatch '^/Game/(?:[\p{L}\p{N}_-]+/)*[\p{L}\p{N}_-]+\.[\p{L}\p{N}_-]+$' -or !$seenAssets.Add($asset)){throw "Invalid/duplicate selected object path: $asset"}
  $assets.Add($asset)
 }
 foreach($packageInput in $m.inputFiles){
  if($packageInput -isnot [string] -or $packageInput -cnotmatch '^Content/(?:[\p{L}\p{N}_-]+/)*[\p{L}\p{N}_-]+\.(uasset|umap|uexp|ubulk|uptnl)$' -or !$seenInputs.Add($packageInput)){throw "Invalid/duplicate package input: $packageInput"}
  $inputs.Add($packageInput)
 }
 foreach($asset in $assets){
  $stem='Content/'+$asset.Substring(6,$asset.LastIndexOf('.')-6)
  $payloads=@(@(($stem+'.uasset'),($stem+'.umap'))|Where-Object {Test-Path -LiteralPath (Join-Path $root $_) -PathType Leaf})
  if($payloads.Count -ne 1){throw "Selected package requires exactly one existing .uasset/.umap payload: $stem"}
  $target=$payloads[0];if($seenInputs.Add($target)){$inputs.Add($target)}
 }
 if($inputs.Count -gt 512){throw 'Auto-included target packages exceed the512-input bound.'}
 foreach($packageInput in $inputs){
  $full=[IO.Path]::GetFullPath((Join-Path $root $packageInput));$content=[IO.Path]::GetFullPath((Join-Path $root 'Content'))+[IO.Path]::DirectorySeparatorChar
  if(!$full.StartsWith($content,[StringComparison]::OrdinalIgnoreCase) -or !(Test-Path -LiteralPath $full -PathType Leaf)){throw "Missing/outside Content package input: $packageInput"}
  $cursor=Get-Item -LiteralPath $full
  while($cursor -and !$cursor.FullName.Equals($root,[StringComparison]::OrdinalIgnoreCase)){
   if($cursor.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Reparse input is unsupported: $packageInput"}
   $cursor=if($cursor -is [IO.FileInfo]){$cursor.Directory}else{$cursor.Parent}
  }
 }
 [pscustomobject]@{manifestPath=$read.path;manifestSHA256=$read.sha256;assetPaths=$assets.ToArray();inputFiles=$inputs.ToArray()}
}
function Get-ProjectJSelectedInputHashes {
 param([Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)]$Selection)
 @($Selection.inputFiles|ForEach-Object{[ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $ProjectRoot $_) -Algorithm SHA256 -ErrorAction Stop).Hash.ToLowerInvariant()}})
}
function Get-ProjectJSelectedInputSignature {
 param([Parameter(Mandatory)]$Rows)
 if($Rows -isnot [Array] -or $Rows.Count -lt 1 -or $Rows.Count -gt 512){throw 'Missing selected input hashes.'}
 $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
 @($Rows|ForEach-Object{Assert-ProjectJValidationFields $_ @('path','sha256');if($_.path -isnot [string] -or $_.sha256 -isnot [string] -or $_.sha256 -cnotmatch '^[0-9a-f]{64}$' -or !$seen.Add($_.path)){throw 'Invalid/duplicate selected input hash.'};"$($_.path):$($_.sha256)"}) -join "`n"
}
function Get-ProjectJStrictValidationLog {
 param([Parameter(Mandatory)][string]$LogPath,[Parameter(Mandatory)][string[]]$AssetPaths)
 $file=Get-Item -LiteralPath $LogPath -ErrorAction Stop;if($file.Length -gt 33554432){throw 'Selected validation log exceeds32MiB.'}
 $log=Get-Content -LiteralPath $file.FullName -Raw -ErrorAction Stop
 $counts=[regex]::Matches($log,'(?m)^(?:\[[^\]\r\n]*\])*\s*LogTemp: Display: Files Checked: (\d+), Passed: (\d+), Failed: (\d+), Skipped: (\d+), Unable to validate: (\d+)\r?$')
 $warnings=[regex]::Matches($log,'(?m)^(?:\[[^\]\r\n]*\])*\s*LogTemp: Display: Validation Warnings: (\d+), Requested: (\d+), Asset limit reached: (\d+)\r?$')
 $selected=[regex]::Matches($log,'(?m)^(?:\[[^\]\r\n]*\])*\s*LogTemp: Display: ProjectJ Selected Asset: ([^\r\n]+)\r?$');$totals=[regex]::Matches($log,'(?m)^(?:\[[^\]\r\n]*\])*\s*LogTemp: Display: ProjectJ Selected Assets: (\d+)\r?$')
 if($counts.Count -ne 1 -or $warnings.Count -ne 1 -or $totals.Count -ne 1 -or $selected.Count -ne $AssetPaths.Count){throw 'Missing/ambiguous strict native completion/selection lines.'}
 $actual=@($selected|ForEach-Object{$_.Groups[1].Value.Trim()})
 if(($actual -join "`n") -cne ($AssetPaths -join "`n") -or [int]$totals[0].Groups[1].Value -ne $AssetPaths.Count){throw 'Native selection differs from the exact manifest order/identity.'}
 $c=$counts[0];$w=$warnings[0]
 $result=[ordered]@{checked=[int]$c.Groups[1].Value;passed=[int]$c.Groups[2].Value;failed=[int]$c.Groups[3].Value;skipped=[int]$c.Groups[4].Value;unableToValidate=[int]$c.Groups[5].Value;warnings=[int]$w.Groups[1].Value;requested=[int]$w.Groups[2].Value;assetLimitReached=([int]$w.Groups[3].Value -ne 0)}
 if($result.checked -ne $AssetPaths.Count -or $result.passed -ne $AssetPaths.Count -or $result.requested -ne $AssetPaths.Count -or $result.failed -ne 0 -or $result.skipped -ne 0 -or $result.unableToValidate -ne 0 -or $result.warnings -ne 0 -or $result.assetLimitReached){throw 'Strict native validation did not pass every requested asset without skips/limits/warnings.'}
 $result
}
function Assert-ProjectJSelectedAssetValidation {
 param([Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ValidationRunDirectory,[Parameter(Mandatory)][string]$SelectionManifest)
 $root=(Resolve-Path -LiteralPath $ProjectRoot -ErrorAction Stop).Path;$run=(Resolve-Path -LiteralPath $ValidationRunDirectory -ErrorAction Stop).Path
 $selection=Get-ProjectJSelectedAssetManifest -ProjectRoot $root -SelectionManifest $SelectionManifest
 $read=Read-ProjectJValidationJson -Path (Join-Path $run 'SelectedValidation.json') -MaxBytes 8388608;$r=$read.value
 Assert-ProjectJValidationFields $r @('schema','kind','status','projectRoot','validationRun','manifestPath','manifestSHA256','assetPaths','inputsBefore','inputsAfter','sourceProvenanceBefore','sourceProvenanceAfter','editorExecutable','native','completedUTC','failure','scope')
 if(!(Test-ProjectJValidationInteger $r.schema) -or $r.schema -ne 1 -or $r.kind -cne 'projectj_selected_asset_validation' -or $r.status -cne 'passed' -or $r.failure -cne '' -or !$r.completedUTC -or $r.scope -cne 'Declared/auto target saved package inputs only; not a transitive asset dependency closure proof.' -or
    $r.projectRoot -isnot [string] -or !$r.projectRoot.Equals($root,[StringComparison]::OrdinalIgnoreCase) -or $r.validationRun -isnot [string] -or !$r.validationRun.Equals($run,[StringComparison]::OrdinalIgnoreCase) -or
    $r.manifestPath -isnot [string] -or !$r.manifestPath.Equals($selection.manifestPath,[StringComparison]::OrdinalIgnoreCase) -or $r.manifestSHA256 -cne $selection.manifestSHA256 -or
    $r.assetPaths -isnot [Array] -or ($r.assetPaths -join "`n") -cne ($selection.assetPaths -join "`n") -or $r.editorExecutable -cne (Get-ProjectJAssetValidationEditorPath)){throw 'Not a current completed strict selected-validation receipt.'}
 Assert-ProjectJValidationFields $r.native @('processSHA256','argumentsSHA256','logSHA256','processId','hasExited','exitCode','summary')
 if(!(Test-ProjectJValidationInteger $r.native.processId) -or $r.native.processId -le 0 -or $r.native.hasExited -isnot [bool] -or !$r.native.hasExited -or !(Test-ProjectJValidationInteger $r.native.exitCode) -or $r.native.exitCode -ne 0){throw 'No actual successful completed native receipt.'}
 foreach($pair in @(@('Selected/process.json','processSHA256'),@('Selected/arguments.json','argumentsSHA256'),@('Selected/Run.log','logSHA256'))){
  $expected=$r.native.($pair[1]);if($expected -isnot [string] -or $expected -cnotmatch '^[0-9a-f]{64}$' -or (Get-FileHash -LiteralPath (Join-Path $run $pair[0]) -Algorithm SHA256 -ErrorAction Stop).Hash.ToLowerInvariant() -cne $expected){throw "Changed/missing native artifact: $($pair[0])"}
 }
 $process=(Read-ProjectJValidationJson -Path (Join-Path $run 'Selected/process.json')).value
 Assert-ProjectJValidationFields $process @('schema','mode','processId','hasExited','exitCode','validationStatus','summary','failure')
 if($process.schema -ne 1 -or $process.mode -cne 'strict-selected' -or $process.validationStatus -cne 'passed' -or $process.failure -cne '' -or
    $process.processId -ne $r.native.processId -or $process.hasExited -isnot [bool] -or !$process.hasExited -or ($process.exitCode -isnot [long] -and $process.exitCode -isnot [int]) -or $process.exitCode -ne 0){throw 'Invalid/partial native process receipt.'}
 $summary=Get-ProjectJStrictValidationLog -LogPath (Join-Path $run 'Selected/Run.log') -AssetPaths $selection.assetPaths
 $expectedFields=@('checked','passed','failed','skipped','unableToValidate','warnings','requested','assetLimitReached')
 foreach($record in @($process.summary,$r.native.summary)){
  Assert-ProjectJValidationFields $record $expectedFields
  foreach($key in $expectedFields){if($key -eq 'assetLimitReached'){if($record.$key -isnot [bool]){throw 'Invalid limit flag.'}}elseif(!(Test-ProjectJValidationInteger $record.$key)){throw 'Invalid native counter.'};if($record.$key -ne $summary[$key]){throw 'Native log/process/root counters differ.'}}
 }
 $arguments=@(Get-Content -LiteralPath (Join-Path $run 'Selected/arguments.json') -Raw|ConvertFrom-Json)
 $pathArg='-ProjectJAssetPaths='+($selection.assetPaths -join '+')
 if(@($arguments|Where-Object {$_ -ceq '-run=Project_JAssetValidation'}).Count -ne 1 -or @($arguments|Where-Object {$_ -ceq $pathArg}).Count -ne 1 -or
    @($arguments|Where-Object {$_ -like '-AssetType=*' -or $_ -like '-AssetPaths=*'}).Count -or $arguments[0] -cne ($root.Replace('\','/')+'/Project_J.uproject') -or
    @($arguments|Where-Object {$_ -ceq ('-ABSLOG='+$run.Replace('\','/')+'/Selected/Run.log')}).Count -ne 1){throw 'Native arguments are not the exact selected project commandlet.'}
 $now=@(Get-ProjectJSelectedInputHashes -ProjectRoot $root -Selection $selection)
 $signature=Get-ProjectJSelectedInputSignature $now
 if((Get-ProjectJSelectedInputSignature $r.inputsBefore) -cne $signature -or (Get-ProjectJSelectedInputSignature $r.inputsAfter) -cne $signature){throw 'Declared/automatic selected inputs changed before/after/current validation.'}
 foreach($prov in @($r.sourceProvenanceBefore,$r.sourceProvenanceAfter)){
  if($prov.files -isnot [Array] -or $prov.binaries -isnot [Array]){throw 'Missing source/runtime arrays.'}
  foreach($row in $prov.files){if($row.deleted -isnot [bool]){throw 'Invalid source tombstone.'}}
  if(@($prov.binaries|Where-Object {$_.path -ceq 'EngineHost/UnrealEditor-Cmd.exe'}).Count -ne 1){throw 'Missing validation Editor executable provenance.'}
 }
 $current=Get-ProjectJSourceProvenance -ProjectRoot $root -RuntimePaths @((Get-ProjectJAssetValidationEditorPath))
 Assert-ProjectJMatchingProvenance -Provenance @($r.sourceProvenanceBefore,$r.sourceProvenanceAfter,$current)
 [ordered]@{schema=1;status='passed';manifestSHA256=$selection.manifestSHA256;receiptSHA256=$read.sha256;validationRun=$run;assetCount=$selection.assetPaths.Count;inputCount=$selection.inputFiles.Count}
}
