# Changelog — ESP32-S3 OBD-II Dashboard

Bu dosya proje geçmişini ve mevcut durumu tutar. **Yeni sohbetlerde önce burayı oku;** anlamlı değişiklik yaptıktan sonra güncelle.

## Mevcut durum (2026-06-18)

| Alan | Değer |
|------|-------|
| Hedef cihaz | ESP32-S3, 466×466 yuvarlak LCD, 8MB PSRAM |
| Araç profili | **Universal OBD-II** (`ATSP0`, runtime PID keşfi) |
| UI sekmeleri | Connect · Dash · Grid · Settings (DTC kaldırıldı) |
| Son flash | COM3 — `obd2_dashboard.bin` **0x135160** (~1.24 MB), 2026-06-18 |
| Git | `main` = `origin/main` (feature `1b365e6`, HEAD `213d06e`) |

---

## 2026-06-18 — OBD düzeltmeleri + DTC ekranı kaldırıldı

**Neden:** Universal profil sonrası bağlantı çok geç oluşuyordu, voltaj hiç gelmiyordu; DTC taraması tutarsızdı.

### `main/obd/obd_pids.c`

- **Erken bağlantı:** İlk PID bloğu (`0100`) bitince `OBD_STATE_READY`; kalan 6 blok düşük öncelikle arka planda
- **Voltaj:** Dashboard'da `dash_rpm_poll_due` ertelemesi kaldırıldı — kuyruk boşken `poll_voltage`
- **Voltaj 0x42:** Keşif bitmask kontrolü kaldırıldı (kısmi keşifte yanlış ATRV geçişi önlendi)
- **DTC:** Arka plan taraması ve `obd_dtc` bağımlılığı kaldırıldı

### `main/ui/`

- DTC (hata kodu) sekmesi tamamen kaldırıldı
- 50 ms timer yalnızca **aktif sekmeyi** günceller (connect / dash / grid / settings)

### Build / cihaz

- Derleme + flash COM3 başarılı (`idf.py -p COM3 build flash`)
- Derleme sırasında `PID_DISC_BLOCK_COUNT` sıra hatası düzeltildi

---

## 2026-06-18 — Universal OBD-II profili

**Neden:** Tek araç (Chevrolet Kalos) yerine her ELM327 uyumlu araçta çalışsın.

### `main/data/vehicle_profile.c`

- Profil: `Chevrolet Kalos 2005` → `Universal OBD-II`
- Protokol: `ATSP5` (KWP sabit) → `ATSP0` (otomatik)
- `known_pid_masks` sıfırlandı — her bağlantıda tam keşif
- `use_atrv_voltage = false` — önce PID `0x42`, yoksa ATRV
- `rpm_max = 8000`, `disc_timeout_ms = 2500`
- MAF (`0x10`) fast poll listesine eklendi

### `main/obd/obd_pids.c` (universal profil ile birlikte)

- PID keşfi 7 blok: `0100`, `0120`, `0140`, `0160`, `0180`, `01A0`, `01C0`
- `profile_has_known_pids()` kısayolu kaldırıldı
- Dashboard: RPM her zaman öncelikli; speed/temp `live_pids` üzerinden
- Grid: `fast_pids` + `slow_pids` round-robin
- Poll delay: `1ms` / `4ms` (queued / idle)
- MAF decode (`0x10`) eklendi

---

## 2026-06-18 — Yuvarlak LCD layout ve OBD stabilitesi (`29279ce`)

### UI / layout (`main/ui/`)

- `theme.h`: yuvarlak panel sabitleri — `UI_GAUGE_SZ`, `UI_GAUGE_Y_OFF`, `UI_DOT_BAR_LIFT`, `theme_safe_width()`, `ui_chord_width_at_y()`
- `screen_dash.c`: RPM gauge tam ekran ortalı, `s_sub_cells[]` crash fix, stats safe width
- `screen_connect.c`, `screen_grid.c`: safe-width / round içi layout
- BT ikonu üst-orta, dot navigasyon yukarı taşındı

### OBD (`main/obd/`)

- `elm327.c`, `obd_pids.c`: komut kuyruğu ve timeout iyileştirmeleri
- Voltaj EMA filtresi + spike rejection

### Simülatör

- `round_mask.c/h`: turkuaz/gri halka doğrulama overlay
- `scripts/verify_round_lcd_layout.py`: geometrik layout doğrulama
- `app_main.c`: `round_mask_init()` çağrısı

---

## Önceki sürümler

### `0e57dfd` — README

- Proje dokümantasyonu eklendi

### `c86ba24` — İlk sürüm

- ESP32-S3 OBD-II dashboard: LVGL UI, BLE ELM327, gauge, grid, DTC, buzzer uyarıları
- Araç profili: Chevrolet Kalos 2005 (KWP, önceden bilinen PID maskesi)

---

## Mimari notlar

### Bağlantı akışı (Universal)

1. BLE tarama / bağlantı → ELM327 init (`ATSP0`, `ATST32`)
2. PID keşif bloğu `0100` → **`OBD_STATE_READY`** (dashboard veri akışı başlar)
3. Bloklar `0120`…`01C0` arka planda (kuyruk boşken)
4. Voltaj: `0x42` dene → yoksa `ATRV`

### Dashboard poll (aktif sekme = Dash)

| Alan | PID | Aralık | Öncelik |
|------|-----|--------|---------|
| RPM | 0x0C | 50 ms | En yüksek |
| Speed | 0x0D | 100 ms | İkincil |
| Coolant | 0x05 | 500 ms | İkincil |
| Voltaj | 0x42 / ATRV | 1000 ms | Dash poll boşken |

### Korunan modüller (keyfi değiştirme)

- `main/obd/ble_obd.c` — BLE tarama, bağlantı, GATT
- `main/obd/elm327.c` — komut kuyruğu, yanıt işleme

Detay: `.cursor/rules/ble-elm327-stability.mdc`, `docs/GELISTIRME_KURALLARI.md`

### Build / flash

```powershell
# ESP-IDF 5.3.5
# Python: C:\Espressif\python_env\idf5.3_py3.11_env\Scripts\python.exe
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.3.5"
$env:IDF_TOOLS_PATH = "C:\Espressif"
$env:PATH = "C:\Espressif\python_env\idf5.3_py3.11_env\Scripts;C:\Espressif\tools\idf-git\2.44.0\cmd;" + $env:PATH
. C:\Espressif\frameworks\esp-idf-v5.3.5\export.ps1
idf.py -p COM3 build flash
```

### Simülatör

```cmd
simulator\build_simulator.cmd
```

---

## Açık işler / fikirler

- [x] Commit + push: universal profil, OBD düzeltmeleri, DTC kaldırma (`1b365e6`)
- [ ] İsteğe bağlı: `obd_dtc.c` kaynak dosyasını tamamen sil veya ileride yeniden ekle
- [ ] İsteğe bağlı: Kalos preset profili (çoklu profil seçimi)
- [ ] İsteğe bağlı: `ATDP` ile algılanan protokolü ayarlar ekranında göster
- [ ] Simülatör `round_mask` overlay'i production'dan ayır
