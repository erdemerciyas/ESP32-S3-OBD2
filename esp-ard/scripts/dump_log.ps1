# ESP32 flash logunu serial_boot.log dosyasina aktarir.
# Kullanim: .\scripts\dump_log.ps1 -Port COM3

param(
    [string]$Port = "COM3",
    [string]$OutFile = "serial_boot.log"
)

$pio = Get-Command pio -ErrorAction SilentlyContinue
if (-not $pio) {
    Write-Error "PlatformIO (pio) bulunamadi."
    exit 1
}

$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

Write-Host "Baglaniyor: $Port — 'log dump' gonderiliyor..."
$tmp = Join-Path $env:TEMP "obd_log_capture.txt"
Remove-Item $tmp -ErrorAction SilentlyContinue

# monitor kisa sure ac, komut gonder, cikti yakala
$cmd = @"
import serial, time, sys
port = sys.argv[1]
out = sys.argv[2]
ser = serial.Serial(port, 115200, timeout=0.5)
time.sleep(0.3)
ser.reset_input_buffer()
ser.write(b'log dump\r\n')
time.sleep(0.2)
buf = bytearray()
deadline = time.time() + 25
while time.time() < deadline:
    chunk = ser.read(4096)
    if chunk:
        buf.extend(chunk)
        if b'[LOG] dump bitti' in buf:
            break
    else:
        time.sleep(0.05)
ser.close()
open(out, 'wb').write(buf)
print('OK', len(buf), 'bytes ->', out)
"@

$py = Get-Command python -ErrorAction SilentlyContinue
if ($py) {
    $scriptPath = Join-Path $env:TEMP "obd_dump_serial.py"
    Set-Content -Path $scriptPath -Value $cmd -Encoding UTF8
    python $scriptPath $Port (Join-Path $root $OutFile)
    Copy-Item (Join-Path $root $OutFile) -Destination (Join-Path $root $OutFile) -Force
    Write-Host "Kaydedildi: $root\$OutFile"
} else {
    Write-Host "Python yok — manuel: pio device monitor -p $Port"
    Write-Host "Monitor acikken yazin: log dump"
}
