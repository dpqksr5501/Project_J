param([string]$Toolset,[ValidateSet('get_default_object','list_properties','get_properties','get_referencers','get_dependencies','is_dirty')][string]$Tool,[string]$ArgumentsJson,[ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Record)
$ErrorActionPreference='Stop'
$path="$PSScriptRoot/$Record.json"
if(Test-Path -LiteralPath $path){throw 'Preserve previous RPC evidence.'}
$headers=@{Accept='application/json, text/event-stream';'MCP-Protocol-Version'='2025-06-18';'Mcp-Session-Id'=(Get-Content "$PSScriptRoot/McpEquipment2.txt" -Raw).Trim()}
$body=@{jsonrpc='2.0';id=2;method='tools/call';params=@{name='call_tool';arguments=@{toolset_name=$Toolset;tool_name=$Tool;arguments=($ArgumentsJson|ConvertFrom-Json -AsHashtable)}}}|ConvertTo-Json -Depth 24 -Compress
$response=Invoke-WebRequest -Uri 'http://127.0.0.1:8000/mcp' -Method Post -ContentType 'application/json' -Headers $headers -Body $body -TimeoutSec 45
$response.Content | Set-Content -LiteralPath $path -Encoding utf8
$rpc=$response.Content|ConvertFrom-Json
foreach($block in $rpc.result.content){
    if($block.type -ne 'text'){continue}
    $value=($block.text|ConvertFrom-Json).returnValue
    if($value -is [string]){try{$value=$value|ConvertFrom-Json}catch{}}
    $value|ConvertTo-Json -Depth 12
}
if($rpc.error -or $rpc.result.isError){throw 'RPC failure retained; do not interpret as empty asset.'}

