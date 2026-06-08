# .NET Framework'ü kullan
[System.Reflection.Assembly]::LoadWithPartialName("System.IO.Ports") | Out-Null

$port = New-Object System.IO.Ports.SerialPort
$port.PortName = "COM3"
$port.BaudRate = 115200
$port.Parity = [System.IO.Ports.Parity]::None
$port.DataBits = 8
$port.StopBits = [System.IO.Ports.StopBits]::One
$port.Handshake = [System.IO.Ports.Handshake]::None
$port.ReadTimeout = 100

try {
    $port.Open()
    Write-Host "[+] COM3 opened" -ForegroundColor Green
    
    $logs = @()
    $stopTime = (Get-Date).AddSeconds(15)
    
    Write-Host "[*] Reading (15 sec)..."
    
    while ((Get-Date) -lt $stopTime) {
        try {
            $line = $port.ReadLine()
            if ($line) {
                Write-Host $line
                $logs += $line
            }
        } catch {
            Start-Sleep -Milliseconds 50
        }
    }
    
    # "log dump" komutunu gönder
    Write-Host "`n[*] Sending: log dump" -ForegroundColor Cyan
    $port.WriteLine("log dump")
    Start-Sleep -Seconds 2
    
    # Logları oku
    $stopTime2 = (Get-Date).AddSeconds(15)
    while ((Get-Date) -lt $stopTime2) {
        try {
            $line = $port.ReadLine()
            if ($line) {
                Write-Host $line
                $logs += $line
            }
        } catch {
            Start-Sleep -Milliseconds 50
        }
    }
    
    $logs | Out-File -FilePath "device_current.log" -Encoding UTF8 -Force
    
    Write-Host ""
    Write-Host "[+] Saved: device_current.log ($(($logs | Measure-Object).Count) lines)" -ForegroundColor Green
    
} catch {
    Write-Host "[!] Error: $_" -ForegroundColor Red
} finally {
    if ($port.IsOpen) {
        $port.Close()
        $port.Dispose()
    }
}
