# ESP32-S3 + WiFi ELM327 OBD2 Dashboard - FULL SYSTEM PLAN

## 🎯 Proje Amacı
ESP32-S3 Touch LCD 2.1" ekran kullanarak WiFi üzerinden ELM327 OBD2 cihazına bağlanıp gerçek zamanlı araç verilerini (CAN/OBD-II PID) göstermek.

---

## 🧠 GENEL MİMARİ

```
[ESP32-S3]
   ↓ WiFi (STA Mode / AP Fallback)
[ELM327 WiFi Adapter]
   ↓ TCP Socket (Port 35000 / 23 / 3000 / 8080)
[AT Commands Layer]
   ↓
[OBD-II PID Response]
   ↓
[Parser Layer]
   ↓
[LVGL Dashboard UI]
```

---

## 📡 BAĞLANTI SENARYOLARI (TÜM İHTİMALLER)

### 1. Standart ELM327 WiFi
- SSID: OBDII / V-LINK / WiFi_OBDII
- IP: 192.168.0.10
- PORT: 35000

### 2. Alternatif IP yapılandırması
- 192.168.4.1 (AP mode)
- 192.168.1.10 (router mode)

### 3. Alternatif portlar
- 35000 (en yaygın)
- 23 (telnet mode)
- 8080 (HTTP bridge cihazlar)
- 3000 (custom firmware)

---

## 🔁 AUTO-CONNECT STRATEJİSİ

ESP32 şu sırayla dener:

1. **WiFi** (kodda uygulandı — telefon yok):
   - `config.h` özel SSID (opsiyonel)
   - Son başarılı OBD SSID (NVS)
   - Bilinen profiller: OBDII, WiFi_OBDII, V-LINK, …
   - WiFi taraması: SSID içinde OBD/ELM/VLINK geçen ağlar
2. **TCP** (kodda uygulandı):
   - Önce kilitli başarılı adres (NVS)
   - Kayıtlı ELM327 IP + gateway IP
   - 192.168.0.10, 192.168.4.1, 192.168.1.10
   - Portlar: 35000 → 23 → 8080 → 3000

Başarılı TCP çifti NVS’e kilitlenir (`app_storage`).

---

## ⚙️ ELM327 INIT SEQUENCE

Her bağlantıdan sonra:

```
ATZ      -> Reset
ATE0     -> Echo off
ATL0     -> Linefeeds off
ATS0     -> Spaces off
ATH0     -> Headers off
ATSP0    -> Auto protocol
```

---

## 📊 OBD-II PID LISTESİ

### Temel canlı veriler

| PID | Açıklama |
|-----|--------|
| 010C | RPM |
| 010D | Speed |
| 0105 | Coolant Temp |
| 010F | Intake Air Temp |
| 0111 | Throttle Position |
| 0104 | Engine Load |
| ATRV | Battery Voltage |

---

## 🔄 POLLING MEKANİZMASI

- Loop: 200–500ms
- Non-blocking TCP read
- Timeout: 1000–1500ms
- Buffer flush her cycle

---

## 🧠 PARSING LOGİĞİ

### Örnek response:
```
41 0C 1A F8
```

### RPM hesap:
```
(0x1A * 256 + 0xF8) / 4
```

---

## 📺 UI (LVGL 8.x)

### Ekran (480x480)

```
┌────────────────────┐
│        RPM         │
│     GAUGE          │
├─────────┬──────────┤
│ SPEED   │ TEMP     │
│         │          │
├─────────┴──────────┤
│ VOLTAGE | THROTTLE │
└────────────────────┘
```

---

## 🧱 MODÜLER YAPI

### wifi_manager
- connectWiFi()
- reconnect()

### elm327_client
- connectTCP()
- sendAT()
- sendPID()
- readResponse()

### obd_parser
- parseRPM()
- parseSpeed()
- parseTemp()

### dashboard_ui
- lvgl_init()
- update_gauges()

---

## 🔁 STATE MACHINE

```
BOOT
 ↓
WIFI_CONNECTING
 ↓
TCP_CONNECTING
 ↓
ELM_INIT
 ↓
RUNNING
 ↓
RETRY
```

---

## ⚠️ KRİTİK PROBLEMLER VE ÇÖZÜMLER

### ❌ WiFi bağlanmıyor
- Retry + fallback SSID scan

### ❌ TCP connect fail
- Port rotation

### ❌ No OBD response
- ATSP0 tekrar gönder

### ❌ Garbage response
- Buffer flush + delay

### ❌ ELM clone timeout
- 1000ms → 2000ms timeout artır

---

## 🚀 GELİŞTİRME STRATEJİSİ

### Phase 1
- WiFi + TCP connect

### Phase 2
- AT command layer

### Phase 3
- PID reading

### Phase 4
- LVGL dashboard

### Phase 5
- stability + retry system

---

## 🔋 OPTİMİZASYON

- FreeRTOS task separation:
  - Task 1: WiFi/TCP
  - Task 2: OBD polling
  - Task 3: UI update

---

## 🧪 DEBUG MODU

Serial output:
- raw AT responses
- PID raw hex
- connection state

---

## 🏁 SONUÇ

Bu yapı:
- %95 ELM327 uyumluluk
- düşük latency dashboard
- stabil reconnect
- ESP32-S3 optimized LVGL UI

hedefler.

