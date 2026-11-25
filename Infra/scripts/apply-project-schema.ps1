$ErrorActionPreference = 'Stop'

$infraRoot = Split-Path -Parent $PSScriptRoot
$composeFile = Join-Path $infraRoot 'docker-compose.yml'
$migrations = Get-ChildItem -LiteralPath (Join-Path $infraRoot 'migrations') -Filter '*.sql' -File | Sort-Object Name
if (!$migrations) { throw 'No Project J schema migrations found.' }

foreach ($migration in $migrations) {
    Write-Output "Applying $($migration.Name)"
    Get-Content -LiteralPath $migration.FullName -Raw -Encoding UTF8 |
        docker compose -f $composeFile exec -T postgres psql -X -U project_j -d project_j -v ON_ERROR_STOP=1
    if ($LASTEXITCODE -ne 0) { throw "Schema migration failed: $($migration.Name)" }
}
