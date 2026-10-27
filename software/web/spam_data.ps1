param(
    [string]$Url = "http://localhost:3000/data"
)

# Sends one fake reading per second, cycling through the sensors. Ctrl+C to stop.
$sensors = @("bme280", "sgp30", "bh1750")
$i = 0

while ($true) {
    $sensor = $sensors[$i % $sensors.Count]
    $i++
    $time = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ss.fffZ")

    switch ($sensor) {
        "bme280" {
            $body = @{
                sensor      = "bme280"
                time        = $time
                temperature = [math]::Round(22 + (Get-Random -Minimum -100 -Maximum 100) / 100, 2)
                humidity    = [math]::Round(45 + (Get-Random -Minimum -200 -Maximum 200) / 100, 2)
                pressure    = [math]::Round(1013 + (Get-Random -Minimum -100 -Maximum 100) / 100, 2)
            }
        }
        "sgp30" {
            $body = @{
                sensor = "sgp30"
                time   = $time
                eco2   = Get-Random -Minimum 400 -Maximum 600
                tvoc   = Get-Random -Minimum 0 -Maximum 50
            }
        }
        "bh1750" {
            $body = @{
                sensor = "bh1750"
                time   = $time
                lux    = [math]::Round(320 + (Get-Random -Minimum -2000 -Maximum 2000) / 100, 1)
            }
        }
    }

    $json = $body | ConvertTo-Json -Compress
    try {
        $res = Invoke-RestMethod -Uri $Url -Method Post -ContentType "application/json" -Body $json
        Write-Host "$json -> $res"
    } catch {
        Write-Host "$json -> FAILED: $($_.Exception.Message)" -ForegroundColor Red
    }

    Start-Sleep -Seconds 1
}
