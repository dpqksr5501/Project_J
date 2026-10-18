# Shared collector/comparator contract. Paths are project-relative; absent tracked
# files remain explicit tombstones. Hashes describe the actual run's saved bytes.
function Get-ProjectJSourceProvenance {
 param([Parameter(Mandatory)][string]$ProjectRoot,[string[]]$RuntimePaths=@())
 $head=(& git -C $ProjectRoot -c core.fsmonitor=false rev-parse HEAD)
 if($LASTEXITCODE -or $head -notmatch '^[0-9a-fA-F]{40}$'){throw 'No valid source HEAD provenance.'}
 $paths=@(& git -C $ProjectRoot -c core.fsmonitor=false ls-files --cached --others --exclude-standard -- Source Scripts Config Plugins Project_J.uproject)
 if($LASTEXITCODE -or !$paths.Count){throw 'No source provenance files.'}
 $files=@($paths|Sort-Object -Unique|ForEach-Object{
  $path=$_;$full=Join-Path $ProjectRoot $path
  if(Test-Path -LiteralPath $full -PathType Leaf){[ordered]@{path=$path;sha256=(Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash.ToLowerInvariant();deleted=$false}}
  else{[ordered]@{path=$path;sha256=$null;deleted=$true}}
 })
 $binaries=@(Get-ChildItem -LiteralPath "$ProjectRoot/Binaries/Win64" -File -ErrorAction Stop|Where-Object{$_.Extension -in @('.exe','.dll','.modules','.target')}|Sort-Object Name|ForEach-Object{[ordered]@{path="Binaries/Win64/$($_.Name)";sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}})
 foreach($runtime in $RuntimePaths){
  if(!(Test-Path -LiteralPath $runtime -PathType Leaf)){throw "Missing runtime binary: $runtime"}
  $binaries += [ordered]@{path="EngineHost/$([IO.Path]::GetFileName($runtime))";sha256=(Get-FileHash -LiteralPath $runtime -Algorithm SHA256).Hash.ToLowerInvariant()}
 }
 if(!$binaries.Count){throw 'No runtime binary provenance.'}
 [ordered]@{schema=1;head=$head.ToLowerInvariant();files=$files;binaries=$binaries}
}
function Get-ProjectJProvenanceSignature {
 param([Parameter(Mandatory)]$Provenance)
 if($Provenance.schema -ne 1 -or $Provenance.head -notmatch '^[0-9a-fA-F]{40}$' -or !$Provenance.files.Count -or !$Provenance.binaries.Count){throw 'Missing/invalid source provenance; historical empty overlays cannot certify a comparison.'}
 $fileRows=@($Provenance.files|Sort-Object path|ForEach-Object{
  if(!$_.path -or (!$_.deleted -and $_.sha256 -notmatch '^[0-9a-fA-F]{64}$')){throw 'Invalid source provenance row.'}
  "$($_.path):$($_.deleted):$($_.sha256)"
 })
 $binaryRows=@($Provenance.binaries|Sort-Object path|ForEach-Object{
  if(!$_.path -or $_.sha256 -notmatch '^[0-9a-fA-F]{64}$'){throw 'Invalid binary provenance row.'}
  "$($_.path):$($_.sha256)"
 })
 if(@($Provenance.files.path|Sort-Object -Unique).Count -ne $Provenance.files.Count -or @($Provenance.binaries.path|Sort-Object -Unique).Count -ne $Provenance.binaries.Count){throw 'Duplicate provenance paths.'}
 @($Provenance.head.ToLowerInvariant(),($fileRows -join '|'),($binaryRows -join '|')) -join "`n"
}
function Assert-ProjectJMatchingProvenance {
 param([Parameter(Mandatory)][object[]]$Provenance)
 if($Provenance.Count -lt 2){throw 'At least two nonempty run provenances are required.'}
 $signatures=@($Provenance|ForEach-Object{Get-ProjectJProvenanceSignature -Provenance $_})
 if(@($signatures|Sort-Object -Unique).Count -ne 1){throw 'Network comparisons used different HEAD/source/configuration/binary provenance.'}
}
