# Tak-calistir: derle ve ESP32'ye yukle (COM3)
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
if (-not (Test-Path $pio)) {
    $pio = "$env:LOCALAPPDATA\Programs\Python\Python312\Scripts\pio.exe"
}
if (-not (Test-Path $pio)) {
    Write-Host "PlatformIO kuruluyor..."
    py -3.12 -m pip install -U platformio --quiet
    $pio = "$env:LOCALAPPDATA\Programs\Python\Python312\Scripts\pio.exe"
}

$ports = [System.IO.Ports.SerialPort]::GetPortNames()
if ($ports.Count -eq 0) {
    Write-Host "HATA: USB seri port bulunamadi. ESP32'yi takin."
    exit 1
}
$port = $ports[0]
Write-Host "Port: $port"
(Get-Content platformio.ini) -replace 'upload_port = COM\d+', "upload_port = $port" `
    -replace 'monitor_port = COM\d+', "monitor_port = $port" | Set-Content platformio.ini

& $pio run -t upload -e waveshare_lcd_21
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ""
Write-Host "=== Yukleme tamam (kart yeniden basladi) ==="
Write-Host "1. OBD WiFi adaptörünü acin (OBDII / V-LINK vb.)"
Write-Host "2. ESP32 otomatik baglanir — telefon gerekmez"
Write-Host "3. SSID listede yoksa config.h icinde OBD_WIFI_SSID tanimlayin"
Write-Host ""
& $pio device monitor -e waveshare_lcd_21
