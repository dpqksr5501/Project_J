param([int]$Offset=0,[int]$MaxCharacters=9500,[string]$Module='')
$projectRoot=(Resolve-Path "$PSScriptRoot/../../..").Path
$previous=Get-Content -LiteralPath "$projectRoot/Saved/Validation/DeepAudit_20261013/SourceReviewLedger.json" -Raw | ConvertFrom-Json
$pending=@($previous.files | Where-Object {$_.review -eq 'inventory_only' -and $_.kind -eq 'production_or_tooling' -and $_.path -match '\.cpp$' -and (!$Module -or $_.module -eq $Module)} | Sort-Object path)
$chars=0;$index=$Offset;$records=@()
while($index -lt $pending.Count){
    $entry=$pending[$index];$source=Get-Content -LiteralPath "$projectRoot/$($entry.path)" -Raw
    if($records.Count -and $chars+$source.Length -gt $MaxCharacters){break}
    $records+= [pscustomobject]@{index=$index;path=$entry.path;source=$source}
    $chars+=$source.Length;++$index
}
[pscustomobject]@{module=$Module;total=$pending.Count;next=$index;characters=$chars;records=$records} | ConvertTo-Json -Depth 4
