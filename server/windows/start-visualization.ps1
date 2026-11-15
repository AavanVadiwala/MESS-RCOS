param([switch]$NoBrowser)

$ErrorActionPreference = 'Stop'
$software = Join-Path $PSScriptRoot '../../software'
$example = Join-Path $software 'web/.env.example'
$environment = Join-Path $software 'web/.env'

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    throw 'Docker Desktop with Docker Compose is required to start the visualization.'
}
if (-not (Test-Path -LiteralPath $environment)) {
    Copy-Item -LiteralPath $example -Destination $environment
}

Push-Location $software
try {
    docker compose up --build -d
    if ($LASTEXITCODE -ne 0) { throw 'Docker Compose could not start the visualization.' }
    $dashboard = 'http://localhost:3000'
    Write-Host "Sensor tables $dashboard"
    Write-Host 'Shiny graphs http://localhost:3838'
    if (-not $NoBrowser) { Start-Process $dashboard }
} finally {
    Pop-Location
}
