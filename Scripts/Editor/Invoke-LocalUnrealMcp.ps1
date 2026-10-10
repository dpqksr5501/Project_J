param([string]$Method = 'tools/list', [string]$ParamsJson = '{}')
$ErrorActionPreference = 'Stop'
$sessionPath = Join-Path $PSScriptRoot '../../Saved/Validation/MMOUI_20261010/McpSession.txt'
$headers = @{ Accept = 'application/json, text/event-stream'; 'MCP-Protocol-Version' = '2025-06-18' }
function Send-Rpc([string]$rpcMethod, $rpcParams, [int]$id) {
    $body = @{jsonrpc='2.0'; id=$id; method=$rpcMethod; params=$rpcParams} | ConvertTo-Json -Depth 50 -Compress
    Invoke-WebRequest -Uri 'http://127.0.0.1:8000/mcp' -Method Post -ContentType 'application/json' -Headers $headers -Body $body -TimeoutSec 50
}
if ($Method -eq 'initialize' -or !(Test-Path -LiteralPath $sessionPath)) {
    $response = Send-Rpc 'initialize' @{protocolVersion='2025-06-18'; capabilities=@{}; clientInfo=@{name='ProjectJ-LocalValidation';version='1.0'}} 1
    $response.Headers['Mcp-Session-Id'] | Select-Object -First 1 | Set-Content -LiteralPath $sessionPath
    if ($Method -eq 'initialize') { $response.Content; exit }
}
$headers['Mcp-Session-Id'] = (Get-Content -LiteralPath $sessionPath -Raw).Trim()
(Send-Rpc $Method ($ParamsJson | ConvertFrom-Json -AsHashtable) 2).Content
