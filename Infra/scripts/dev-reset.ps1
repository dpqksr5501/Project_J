param(
    [switch]$RebuildVolumes
)

$ErrorActionPreference = "Stop"
$ComposeFile = Join-Path $PSScriptRoot "..\docker-compose.yml"

Write-Host "[Project J] Resetting local Nakama + PostgreSQL + Valkey dev stack..."
if ($RebuildVolumes) {
    docker compose -f $ComposeFile down -v
} else {
    docker compose -f $ComposeFile down
}
docker compose -f $ComposeFile up -d
Write-Host "[Project J] Local dev backend is starting. Console: http://127.0.0.1:7351"
