param(
    [string]$Port,
    [Alias('b')]
    [switch]$Build,
    [switch]$Monitor,
    [switch]$MonitorOnly,
    [switch]$CheckEnvironment,
    [string]$ActivationScript = $env:MESS_IDF_ACTIVATE
)

$ErrorActionPreference = 'Stop'
$env:PYTHONUTF8 = '1'
# Use the native Windows toolchain even when launched from Git Bash.
Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
try {
    if ($MonitorOnly -and ($Build -or $Monitor)) {
        throw 'Use -Port COM13 -MonitorOnly without -Build or -Monitor to monitor an already flashed board.'
    }
    if (-not $CheckEnvironment -and [string]::IsNullOrWhiteSpace($Port) -and (-not $Build -or $Monitor -or $MonitorOnly)) {
        throw 'Specify -Port COM4 to flash an existing build, -Build to build only, or -Build -Port COM4 to build and flash. -Monitor requires -Port.'
    }

    # Reuse an activated terminal, or locate the EIM-installed environment.
    $activeIdf = $env:IDF_PATH -and $env:IDF_PYTHON_ENV_PATH -and
        (Test-Path -LiteralPath (Join-Path $env:IDF_PATH 'tools/idf.py') -PathType Leaf) -and
        (Test-Path -LiteralPath (Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts/python.exe') -PathType Leaf)
    if ($ActivationScript -or -not $activeIdf) {
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

    if (-not $env:IDF_PATH -or -not $env:IDF_PYTHON_ENV_PATH) {
        throw 'Activation did not provide IDF_PATH and IDF_PYTHON_ENV_PATH. Check your ESP-IDF installation.'
    }
    # Invoke the selected environment explicitly. Bare idf.py can resolve through
    # Windows file associations/aliases; bare python can select Conda instead.
    $idfPython = Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts/python.exe'
    $idfEntryPoint = Join-Path $env:IDF_PATH 'tools/idf.py'
    if (-not (Test-Path -LiteralPath $idfPython -PathType Leaf) -or
        -not (Test-Path -LiteralPath $idfEntryPoint -PathType Leaf)) {
        throw 'The selected ESP-IDF Python executable or tools/idf.py is missing.'
    }
    $projectPath = Join-Path $PSScriptRoot 'firmware'
    if (-not (Test-Path -LiteralPath (Join-Path $projectPath 'CMakeLists.txt'))) {
        throw "No ESP-IDF project found at $projectPath"
    }
    Push-Location $projectPath
    try {
        if ($CheckEnvironment) {
            & $idfPython $idfEntryPoint --version
            if ($LASTEXITCODE -ne 0) { throw "ESP-IDF check failed ($LASTEXITCODE)." }
        } else {
            if ($Build) {
                & $idfPython $idfEntryPoint -B builds build
                if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE); flashing was not started." }
            }
            if (-not [string]::IsNullOrWhiteSpace($Port) -and -not $MonitorOnly) {
                $buildPath = Join-Path $projectPath 'builds'
                $flashConfigPath = Join-Path $buildPath 'flasher_args.json'
                if (-not (Test-Path -LiteralPath $flashConfigPath -PathType Leaf)) {
                    throw 'No existing firmware build found. Run with -Build (or -b) first.'
                }
                $flashConfig = Get-Content -Raw -LiteralPath $flashConfigPath | ConvertFrom-Json
                if (-not $flashConfig.flash_files -or -not $flashConfig.extra_esptool_args.chip) {
                    throw 'Invalid flash metadata. Run with -Build to regenerate the firmware build.'
                }
                $flashArguments = @('-m', 'esptool', '--chip', $flashConfig.extra_esptool_args.chip,
                    '-p', $Port, '-b', '460800', '--before', $flashConfig.extra_esptool_args.before,
                    '--after', $flashConfig.extra_esptool_args.after)
                if ($flashConfig.extra_esptool_args.stub -eq $false) { $flashArguments += '--no-stub' }
                $flashArguments += 'write-flash'
                $flashArguments += @($flashConfig.write_flash_args)
                foreach ($flashFile in $flashConfig.flash_files.PSObject.Properties) {
                    if (-not (Test-Path -LiteralPath (Join-Path $buildPath $flashFile.Value) -PathType Leaf)) {
                        throw "Missing firmware file: $($flashFile.Value). Run with -Build first."
                    }
                    $flashArguments += @($flashFile.Name, $flashFile.Value)
                }
                # idf.py flash implicitly builds; use generated metadata directly to avoid that.
                Push-Location $buildPath
                try {
                    & $idfPython @flashArguments
                    if ($LASTEXITCODE -ne 0) { throw "Flash failed ($LASTEXITCODE)." }
                } finally {
                    Pop-Location
                }
            }
            if ($Monitor -or $MonitorOnly) {
                & $idfPython $idfEntryPoint -B builds -p $Port monitor
                if ($LASTEXITCODE -ne 0) { throw "Monitor failed ($LASTEXITCODE)." }
            }
        }
    } finally {
        Pop-Location
    }
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
