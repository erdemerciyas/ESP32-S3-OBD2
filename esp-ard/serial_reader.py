#!/usr/bin/env python3
import serial
import time
import sys

try:
    # COM3 portuna bağlan
    ser = serial.Serial('COM3', 115200, timeout=1)
    print("[*] COM3'e bağlandı", file=sys.stderr)
    
    # 40 saniye logları oku
    start_time = time.time()
    lines = []
    
    while time.time() - start_time < 40:
        try:
            if ser.in_waiting > 0:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    print(line)
                    lines.append(line)
        except Exception as e:
            print(f"[E] {e}", file=sys.stderr)
            break
        time.sleep(0.05)
    
    ser.close()
    
    # Logları dosyaya kaydet
    with open('device_boot.log', 'w', encoding='utf-8') as f:
        for line in lines:
            f.write(line + '\n')
    
    print(f"[*] {len(lines)} satır kaydedildi → device_boot.log", file=sys.stderr)
    
except Exception as e:
    print(f"[ERROR] {e}", file=sys.stderr)
    sys.exit(1)
