# Builds and flashes one board of the sensor hotspot setup (Windows).
#   .\setup.ps1 brain              # the board that runs the hotspot and phone page
#   .\setup.ps1 sensor             # a board with BME280 / BH1750 / SGP30 wired to it
#   .\setup.ps1 sensor -Port COM5  # pick the USB port yourself
#   .\setup.ps1 brain -NoMonitor
# Run it in an ESP-IDF PowerShell terminal (idf.py on PATH). Settings shared by both boards are
# asked once and kept in the git-ignored hotspot.local; each board's sdkconfig.local is written from it.
param(
    [Parameter(Mandatory = $true, Position = 0)][ValidateSet('brain', 'sensor')][string]$Role,
    [string]$Port,
    [switch]$NoMonitor
)
$ErrorActionPreference = 'Stop'
$env:PYTHONUTF8 = '1'

function Invoke-Idf {
    & idf.py @args
    if ($LASTEXITCODE -ne 0) { throw "idf.py $($args -join ' ') failed (exit code $LASTEXITCODE)." }
}

# Quotes, backslashes, $ and ` would break the config files.
function Test-Plain([string]$Value) { return $Value -notmatch '[''"\\$`]' }

function Read-Secret([string]$Prompt) {
    $secure = Read-Host -Prompt $Prompt -AsSecureString
    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
    try { return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr) }
    finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr) }
}

# Writes UTF-8 without a byte-order mark and with LF line endings, which ESP-IDF expects.
function Write-Text([string]$Path, [string[]]$Lines) {
    [IO.File]::WriteAllText($Path, (($Lines -join "`n") + "`n"))
}

if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    throw 'idf.py not found. Open an ESP-IDF PowerShell terminal and try again.'
}

# ---- Shared settings: asked once, then reused for every board.
$settingsPath = Join-Path $PSScriptRoot 'hotspot.local'
if (-not (Test-Path -LiteralPath $settingsPath)) {
    Write-Host 'First run: choosing the hotspot settings shared by the brain and every sensor.'
    do {
        $ssid = Read-Host -Prompt 'Hotspot name [MESS-BME280]'
        if (-not $ssid) { $ssid = 'MESS-BME280' }
        $ok = $ssid.Length -le 32 -and (Test-Plain $ssid)
        if (-not $ok) { Write-Host 'Use up to 32 characters, without quotes, backslashes, $ or `.' }
    } until ($ok)
    do {
        $password = Read-Secret 'Hotspot password (8-63 characters)'
        $ok = $password.Length -ge 8 -and $password.Length -le 63 -and (Test-Plain $password)
        if (-not $ok) { Write-Host 'Use 8 to 63 characters, without quotes, backslashes, $ or `.' }
    } until ($ok)
    do {
        $visitor = Read-Secret 'Visitor key for uploading to the live site (Enter to skip)'
        $ok = Test-Plain $visitor
        if (-not $ok) { Write-Host 'Use a key without quotes, backslashes, $ or `.' }
    } until ($ok)
    # The live site's Supabase project. Both values are public by design (see README).
    Write-Text $settingsPath @(
        "HOTSPOT_SSID='$ssid'",
        "HOTSPOT_PASSWORD='$password'",
        "VISITOR_KEY='$visitor'",
        "SUPABASE_URL='https://imaoyqoegvrdtqiqzlvh.supabase.co'",
        "SUPABASE_KEY='sb_publishable_cTJj9EWJ9x8CFQF3MKAPFA_o5ySAsIK'")
    Write-Host "Saved in $settingsPath (git-ignored). Delete it to choose again."
}
$settings = @{}
foreach ($line in Get-Content -LiteralPath $settingsPath) {
    if ($line -match "^(\w+)='(.*)'$") { $settings[$Matches[1]] = $Matches[2] }
}

# ---- This board's sdkconfig.local, unless someone wrote their own by hand.
$project = Join-Path $PSScriptRoot $Role
$localConfig = Join-Path $project 'sdkconfig.local'
$marker = '# Written by setup.sh from hotspot.local'
if ((Test-Path -LiteralPath $localConfig) -and -not ((Get-Content -LiteralPath $localConfig -TotalCount 1) -like "$marker*")) {
    Write-Host "Keeping your own $localConfig (it was not written by setup.ps1)."
} else {
    $lines = @("$marker; edit hotspot.local instead.")
    if ($Role -eq 'brain') {
        $lines += "CONFIG_MESS_GW_SSID=""$($settings.HOTSPOT_SSID)"""
        $lines += "CONFIG_MESS_GW_PASSWORD=""$($settings.HOTSPOT_PASSWORD)"""
        $lines += "CONFIG_MESS_GW_SUPABASE_URL=""$($settings.SUPABASE_URL)"""
        $lines += "CONFIG_MESS_GW_SUPABASE_KEY=""$($settings.SUPABASE_KEY)"""
        $lines += "CONFIG_MESS_GW_VISITOR_KEY=""$($settings.VISITOR_KEY)"""
    } else {
        $lines += "CONFIG_MESS_BME_WIFI_SSID=""$($settings.HOTSPOT_SSID)"""
        $lines += "CONFIG_MESS_BME_WIFI_PASSWORD=""$($settings.HOTSPOT_PASSWORD)"""
        $lines += 'CONFIG_MESS_BME_SERVER_URL="http://192.168.4.1/readings"'
    }
    $new = ($lines -join "`n") + "`n"
    $old = if (Test-Path -LiteralPath $localConfig) { [IO.File]::ReadAllText($localConfig) } else { '' }
    if ($new -ne $old) {
        [IO.File]::WriteAllText($localConfig, $new)
        Remove-Item -LiteralPath (Join-Path $project 'sdkconfig') -ErrorAction SilentlyContinue # regenerate the build configuration
    }
}

# ---- USB port: the only ESP32-looking port, or ask.
if (-not $Port) {
    $boards = @(Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '\((COM\d+)\)' -and $_.Name -match 'CP210|CH340|CH910|CH9102|USB.*(Serial|UART)' } |
        ForEach-Object { if ($_.Name -match '\((COM\d+)\)') { [pscustomobject]@{Port = $Matches[1]; Name = $_.Name} } })
    if ($boards.Count -eq 0) {
        throw 'No ESP32 found on USB. Use a data (not charge-only) cable; if Device Manager shows an unknown device, install the Silicon Labs CP210x or CH340 driver.'
    } elseif ($boards.Count -eq 1) {
        $Port = $boards[0].Port
    } else {
        Write-Host 'More than one board is plugged in:'
        for ($i = 0; $i -lt $boards.Count; $i++) { Write-Host "  $($i + 1)) $($boards[$i].Port)  $($boards[$i].Name)" }
        $choice = [int](Read-Host -Prompt "Which one is the $Role")
        if ($choice -lt 1 -or $choice -gt $boards.Count) { throw 'No such port.' }
        $Port = $boards[$choice - 1].Port
    }
}
Write-Host "Flashing the $Role firmware to $Port"

# ---- Build, flash and (optionally) watch it start.
Push-Location $project
try {
    if (-not (Test-Path -LiteralPath 'sdkconfig')) { Invoke-Idf -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.local' set-target esp32 }
    Invoke-Idf -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.local' build
    Invoke-Idf -p $Port flash
    if (-not $NoMonitor) {
        Write-Host 'Watching the board; press Ctrl+] to stop.'
        Invoke-Idf -p $Port monitor
    }
} finally {
    Pop-Location
}
