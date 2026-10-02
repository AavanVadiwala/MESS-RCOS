param(
    [string]$Port,
    [switch]$Monitor,
    [switch]$CheckEnvironment,
    [string]$ActivationScript = $env:MESS_IDF_ACTIVATE
)

$ErrorActionPreference = 'Stop'
$env:PYTHONUTF8 = '1'
# Use the native Windows toolchain even when launched from Git Bash.
Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
try {
    if (-not $CheckEnvironment -and [string]::IsNullOrWhiteSpace($Port)) {
        throw 'Specify your board port, for example: .\build_and_flash.ps1 -Port COM4'
    }

    # Reuse an activated terminal, or locate the EIM-installed environment.
    if ($ActivationScript -or -not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
        if (-not $ActivationScript) {
            $registryPath = 'C:/Espressif/tools/eim_idf.json'
            if (-not (Test-Path -LiteralPath $registryPath)) {
                throw 'Open an ESP-IDF terminal, or pass -ActivationScript with your EIM PowerShell activation script.'
            }
            $registry = Get-Content -Raw -LiteralPath $registryPath | ConvertFrom-Json
            $setup = $null
            $settingsPath = Join-Path $PSScriptRoot '.vscode/settings.json'
            if (Test-Path -LiteralPath $settingsPath) {
                $settings = Get-Content -Raw -LiteralPath $settingsPath | ConvertFrom-Json
                if ($settings.'idf.currentSetup') {
                    $setup = $registry.idfInstalled | Where-Object { $_.path -eq $settings.'idf.currentSetup' } | Select-Object -First 1
                    if (-not $setup) { throw 'The VS Code ESP-IDF selection is not in the EIM registry. Pass -ActivationScript explicitly.' }
                }
            }
            if (-not $setup) {
                $setup = $registry.idfInstalled | Where-Object { $_.id -eq $registry.idfSelectedId } | Select-Object -First 1
            }
            if (-not $setup) { throw 'No selected EIM installation found. Pass -ActivationScript explicitly.' }
            $ActivationScript = $setup.activationScript
        }
        if (-not (Test-Path -LiteralPath $ActivationScript -PathType Leaf)) {
            throw "Activation script not found: $ActivationScript"
        }
        . $ActivationScript
    }

    if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
        throw 'Activation did not provide idf.py. Check your ESP-IDF installation.'
    }
    $projectPath = Join-Path $PSScriptRoot 'firmware'
    if (-not (Test-Path -LiteralPath (Join-Path $projectPath 'CMakeLists.txt'))) {
        throw "No ESP-IDF project found at $projectPath"
    }
    Push-Location $projectPath
    try {
        if ($CheckEnvironment) {
            & idf.py --version
            if ($LASTEXITCODE -ne 0) { throw "ESP-IDF check failed ($LASTEXITCODE)." }
        } else {
            & idf.py build
            if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE); flashing was not started." }
            $idfArguments = @('-p', $Port, 'flash')
            if ($Monitor) { $idfArguments += 'monitor' }
            & idf.py @idfArguments
            if ($LASTEXITCODE -ne 0) { throw "Flash/monitor failed ($LASTEXITCODE)." }
        }
    } finally {
        Pop-Location
    }
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
