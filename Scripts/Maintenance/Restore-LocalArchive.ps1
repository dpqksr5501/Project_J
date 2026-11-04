param([Parameter(Mandatory)][string]$OriginalPath,[string]$ArchiveRoot)
$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path "$PSScriptRoot/../..").Path
if(!$ArchiveRoot){$ArchiveRoot=Join-Path $projectRoot 'Saved/StorageCleanup_20261104'}
$ArchiveRoot=[IO.Path]::GetFullPath($ArchiveRoot)
if(!$ArchiveRoot.StartsWith((Join-Path $projectRoot 'Saved')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Archive root must stay inside project Saved'}
. "$projectRoot/Scripts/Audit/Continuation-ScratchPaths.ps1"
$context=Get-ContinuationScratchContext $projectRoot
$target=[IO.Path]::GetFullPath($OriginalPath)
if(!(Test-ContinuationPathWithin $target (Join-Path $projectRoot 'Saved')) -and !(Test-ContinuationPathWithin $target $context.CheckoutRoot)){throw 'Outside preserved output roots'}
if(Test-Path -LiteralPath $target){throw 'Original path already exists; preserve it'}
$matches=@()
foreach($name in @('Compressed.jsonl','Deduplicated.jsonl')){
 $journal=Join-Path $ArchiveRoot $name
 if(Test-Path $journal){$matches+=@(Get-Content -LiteralPath $journal|ForEach-Object{$_|ConvertFrom-Json}|Where-Object{$_.original -ieq $target})}
}
if($matches.Count -ne 1){throw 'Expected one exact recovery mapping'}
$row=$matches[0];$gzip=!!$row.archive
$preserved=if($gzip){$row.archive}else{$row.preserved}
$preserved=[IO.Path]::GetFullPath($preserved)
$locationManifest=Join-Path $ArchiveRoot 'ArchiveLocations.json'
if(Test-Path -LiteralPath $locationManifest){
 $locations=Get-Content -LiteralPath $locationManifest -Raw|ConvertFrom-Json
 if($locations.schema -ne 1){throw 'Unsupported archive locations'}
 $location=@($locations.files|Where-Object{$_.oldPath -ieq $preserved})
 if($location.Count -gt 1){throw 'Ambiguous archive location'}
 if($location.Count -eq 1 -and (Test-Path -LiteralPath $location[0].centralPath)){
  $central=[IO.Path]::GetFullPath($location[0].centralPath)
  $store=Get-ContinuationFullPath (Join-Path $ArchiveRoot 'ArchiveStore')
  if(!(Test-ContinuationPathWithin $central $store)){throw 'Archive location outside central store'}
  $preserved=$central
 }
}
if(!(Test-ContinuationPathWithin $preserved (Join-Path $projectRoot 'Saved'))){throw 'Preserved source outside Saved'}
Assert-ContinuationNoReparse (Split-Path -Parent $preserved)
$expectedArchive=if($gzip){$row.archiveSHA256}else{$row.sha256}
if((Get-FileHash -LiteralPath $preserved).Hash.ToLowerInvariant() -cne $expectedArchive){throw 'Preserved source SHA mismatch'}
$parent=Split-Path -Parent $target
Assert-ContinuationNoReparse $parent
if(!(Test-Path $parent)){New-Item -ItemType Directory -Path $parent|Out-Null}
$temporary=$target+'.restore-part'
if(Test-Path -LiteralPath $temporary){throw 'Preserve earlier restore temporary'}
$inputStream=[IO.File]::OpenRead($preserved)
try{
 $outputStream=[IO.File]::Open($temporary,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
 try{
  if($gzip){$decoded=[IO.Compression.GZipStream]::new($inputStream,[IO.Compression.CompressionMode]::Decompress,$true);try{$decoded.CopyTo($outputStream)}finally{$decoded.Dispose()}}
  else{$inputStream.CopyTo($outputStream)}
 }finally{$outputStream.Dispose()}
}finally{$inputStream.Dispose()}
$expectedRaw=if($gzip){$row.originalSHA256}else{$row.sha256}
if((Get-FileHash -LiteralPath $temporary).Hash.ToLowerInvariant() -cne $expectedRaw){throw 'Restored SHA mismatch; original not installed'}
Move-Item -LiteralPath $temporary -Destination $target
[ordered]@{restored=$target;sha256=$expectedRaw;archiveRetained=$true}|ConvertTo-Json
