# Changelog — ESP32-S3 OBD-II Dashboard

Bu dosya proje geçmişini ve mevcut durumu tutar. **Yeni sohbetlerde önce burayı oku;** anlamlı değişiklik yaptıktan sonra güncelle.

## Mevcut durum (2026-06-23)

| Alan | Değer |
|------|-------|
| Hedef cihaz | ESP32-S3, 480×480 yuvarlak LCD (görünür 460 px), 8MB PSRAM |
| Araç profili | **Universal OBD-II** (`ATSP0`, runtime PID keşfi) |
| UI sekmeleri | Connect · Dash · Grid · Settings (DTC kaldırıldı) |
| Son build | `obd2_dashboard.bin` **0x137850** (~1.24 MB), 2026-06-23 |
| Son flash | COM4 — `obd2_dashboard.bin` **0x137850** (~1.24 MB), 2026-06-23 (hub gizli, alt sol hücre toggle)
| Git | `main` üzerinde commit bekleyen değişiklikler var (HEAD `3338fd5-dirty`)

---

## 2026-06-23 — Motorsporları temalı dashboard yenilemesi + veri senkronizasyonu

**Neden:** UI genel görünümü motorsporları estetiğine çevrildi; RPM ve Speed aynı frame'de senkron güncellensin, gauge/needle uyumu ve redline/shift-light davranışı simülatörde doğrulansın.

### Veri senkronizasyonu

- `main/data/vehicle_data.c`: `vehicle_data_snapshot()` atomic snapshot API'si eklendi.
- `main/ui/ui.c`: UI update timer 25 ms → **16 ms**; aktif ekran her frame'de snapshot alıyor.
- `main/obd/obd_pids.c`: `DASH_PID_RPM` ve `DASH_PID_SPEED` aynı polling turunda arka arkaya kuyruğa atılıyor.

### Tema (`main/ui/theme.c`, `theme.h`)

- Yeni renk paleti: koyuk arka plan, **cyan primary**, **turuncu secondary**, **racing red accent**.
- `theme_rpm_gradient_color()` / `theme_speed_gradient_color()`: cyan → orange → red geçişi.
- `theme_shift_light_color()`: RPM/redline oranına göre dim → cyan → orange → red.

### 8 px grid layout sistemi (`main/ui/theme.h`)

- `UI_GRID`, `UI_MARGIN`, `UI_GAP_SM/MD/LG/XL`, `UI_HEADER_H` gibi ortak spacing/ölçü sabitleri eklendi.
- Dashboard, Connect, Grid ve Settings ekranları aynı ızgaraya göre hizalandı; yuvarlak panel kenarına taşma ve üst üste binme önlendi.

### Dashboard (`main/ui/screen_dash.c`)

- Kalın arc gauge, redline segmenti, büyük merkezi dijital değer (**72 px bold**), ibre (needle).
- Merkezi **value disc**: ibreyi rakamların arkasında bırakarak okunabilirliği artırır.
- 9 segmentli **shift-light** şeridi; üst kısımda şeffaf zemin, sönük segmentler %30 opak, yananlar %100 opak.
- Alt 3 hücre data strip: **Speed · Temp · Volt**.
- Merkezde ibre pivotunu kapatan **hub cap** eklendi; arc/ibre/hub cap rengi RPM'ye göre değişir.

### Diğer ekranlar

- `screen_connect.c`: body artık flex `SPACE_BETWEEN`; arc üstte, info kartı ortada, buton altta; info kartına `theme_apply_card()` ile koyu zemin eklendi.
- `screen_grid.c`: flex column + 3 eşit satır; 3×3 metrik hücreler yuvarlak panel içine düzgün oturacak şekilde yeniden düzenlendi.
- `screen_settings.c`: başlık `theme_create_header` ile büyütüldü; info kartına cyan top accent; dropdown list dark tema.

### Simülatör

- `simulator/obd2_dashboard/vehicle_data_sim.c`: snapshot desteği.
- `simulator/obd2_dashboard/demo_feed.c`: senkron RPM/Speed rampası ve redline testi.

### Dashboard layout düzeltmesi (2026-06-23, ikinci iterasyon)

**Neden:** Cihazda gauge 300 px olarak küçük kalmış, data strip / status bar / gauge arasında üst üste binmeler görülmüştü.

- `main/ui/theme.h`: `UI_GAUGE_SZ` 300 → **348 px**; status bar ve data strip arası boşluk `UI_GAP_LG` → `UI_GAP_MD`.
- `main/ui/screen_dash.c`: status bar genişliği `theme_safe_width(UI_STATUS_H, UI_STATUS_H)` ile sınırlandırıldı.
- `main/ui/theme.c`: `theme_create_stat_cell()` ve `theme_create_metric_cell()` genişlikleri `LV_PCT(33)` + `flex_grow` ile sabitlendi; hücreler eşit ve yuvarlak panel içine oturuyor.
- Simülatörde son dashboard ekranı yeniden doğrulandı (`dash_fix3.png`).

### Kesin dashboard slot düzeni (2026-06-23, üçüncü iterasyon)

**Neden:** Cihazda ekran sığmamış, data strip ile alt navigasyon noktaları üst üste binmiş, her şeyin yuvarlak panele tam oturması istenmişti.

- `main/ui/theme.h`:
  - Dashboard için ayrılmış sabit slotlar tanımlandı: `UI_DASH_STATUS_H 16`, `UI_DASH_STATUS_TOP 0`, `UI_DASH_DATA_H 48`, `UI_DASH_BOTTOM_RES 40`, `UI_DASH_GAP 8`.
  - `UI_GAUGE_SZ` formülü değişti; gauge artık **340 px**, status bar (16 px) ile data strip (48 px) arasında net 8 px boşluk bırakıyor.
  - `UI_PAD_BOT` 6 → **40 px** yapılarak diğer ekranların içeriği de alt dot bar ile çakışmıyor.
  - `UI_SAFE_MARGIN` 10 → **5 px**; yuvarlak panel kenarına daha fazla alan kazandırıldı ancak hâlâ güvenli sınır içinde.
- `main/ui/screen_dash.c`:
  - Dashboard sekmesinin padding'i sıfırlandı; tüm koordinatlar 460×460 viewport'a göre mutlak hale getirildi.
  - Data pill yeniden tasarlandı: değer ve birim aynı satırda, 48 px yüksekliğe tam oturuyor.
- Simülatörde yeni düzen doğrulandı (`dash_fix6.png`); status bar, gauge, data strip ve dot bar arasında hiçbir üst üste binme kalmadı.

### Dashboard alt data strip genişletme (2026-06-23, dördüncü iterasyon)

**Neden:** Speed / Temp / Volt hücreleri dar ve içerik (label, değer, birim) birbirine yapışık görünüyordu; daha fazla iç boşluk isteniyordu.

- `main/ui/theme.h`:
  - `UI_DASH_DATA_H` **48 → 64**
  - `UI_DASH_BOTTOM_RES` **52 → 68**
  - Böylece her bir data pill daha geniş ve rahat oturuyor; gauge çapı 368 px’ye küçüldü (önceki 384 px) ama hâlâ ekranı büyük ölçüde dolduruyor.
- `main/ui/screen_dash.c`:
  - Data pill iç padding: `pad_ver 2 → 6`, `pad_hor 1 → 6`, `pad_row 0 → 4`.
  - Değer-birim arası boşluk: unit `pad_left 2 → 4`.
  - Label ile değer aynı satırda kalmaya devam ediyor ama artık kırpma/çakışma riski yok.
- **ESP32 build:** `rebuild.bat` ile başarılı — `obd2_dashboard.bin` **0x1288f0** (~1.21 MB), partition'da **%61** boş.
- **ESP32 flash:** COM4 üzerinden ESP32-S3'e başarıyla yüklendi; MAC `dc:b4:d9:23:18:04`.

### Gauge tam ekran sınıra, status/data circle üzerinde (2026-06-23, beşinci iterasyon)

**Neden:** Gauge etrafında hâlâ boşluk vardı; profil adı ("Universal OBD-II") circle’ın arkasında/üstünde kalmış, circle içine alınabilirdi.

- `main/ui/theme.h`:
  - `UI_GAUGE_TOP` **0** yapıldı; gauge artık viewport’un en tepesinden başlıyor.
  - `UI_GAUGE_SZ` **UI_VIEWPORT_SZ (460 px)** yapıldı; arc dış çapı neredeyse yuvarlak panelin fiziksel sınırına kadar ulaşıyor.
- `main/ui/screen_dash.c`:
  - Gauge container arka plana (`lv_obj_move_background`) atıldı; status bar ve data strip onun üzerine çiziliyor.
  - Status bar ve data strip ön plana (`lv_obj_move_foreground`) alındı; böylece gauge arc’ının üzerinde görünür kalıyorlar.
  - Shift-light şeridi, status bar’ın hemen altına kaydırıldı (`UI_DASH_STATUS_TOP + UI_DASH_STATUS_H + UI_GAP_MD`).
  - Ortadaki RPM değeri 72 px bold’dan **94 px bold**’a yükseltildi; büyük gauge’e daha orantılı duruyor.
- `main/ui/theme.c`:
  - `font_value` artık `lv_font_montserrat_94_bold`.
- **ESP32 build:** `rebuild.bat` ile başarılı — `obd2_dashboard.bin` **0x137800** (~1.24 MB), partition'da **%59** boş.
- **ESP32 flash:** COM4 üzerinden ESP32-S3'e başarıyla yüklendi; MAC `dc:b4:d9:23:18:04`.

### RPM değeri yukarı taşındı, data strip ön planda (2026-06-23, altıncı iterasyon)

**Neden:** RPM merkezdeki büyük değer alt tarafta Speed / Temp / Volt hücrelerinin üzerine/arkasına denk geliyordu; veri hücreleri ön planda kalmıyordu.

- `main/ui/screen_dash.c`:
  - Merkezi RPM değeri ve birimi **28 px yukarı** kaydırıldı (`LV_ALIGN_CENTER, 0, -28`).
  - Görünmez maskeleme diski de aynı şekilde yukarı alındı; böylece ibre hâlâ rakamların arkasında kalıyor.
  - Ibrenin dönüş noktası (hub cap) hâlâ gauge geometrik merkezinde; sadece okuma bölgesi yukarı çıktı.
  - Data strip ön planda (`lv_obj_move_foreground`) kalmaya devam ediyor; artık RPM değerinin arkasında kalmıyor.
- **ESP32 build:** `rebuild.bat` ile başarılı — `obd2_dashboard.bin` **0x137800** (~1.24 MB), partition'da **%59** boş.
- **ESP32 flash:** COM4 üzerinden ESP32-S3'e başarıyla yüklendi; MAC `dc:b4:d9:23:18:04`.

### Profil adı circle içine ve aşağıya çekildi (2026-06-23, yedinci iterasyon)

**Neden:** "Universal" yazan status bar circle’ın çok tepesinde/kenarında duruyordu; tamamen circle alanının içinde ve biraz daha aşağıda olması isteniyordu.

- `main/ui/theme.h`:
  - `UI_DASH_STATUS_TOP` **4 → 12**
  - Böylece status bar (y = 12..28) yuvarlak panelin içinde, üst kenardan uzakta konumlanıyor.
  - Shift-light şeridi de ona bağlı olarak aşağıya kayıyor (`UI_DASH_STATUS_TOP + UI_DASH_STATUS_H + UI_GAP_MD` = 36).
- **ESP32 build:** `rebuild.bat` ile başarılı — `obd2_dashboard.bin` **0x137800** (~1.24 MB), partition'da **%59** boş.
- **ESP32 flash:** COM4 üzerinden ESP32-S3'e başarıyla yüklendi; MAC `dc:b4:d9:23:18:04`.

### Profil adı circle çizgisinin altına çekildi (2026-06-23, sekizinci iterasyon)

**Neden:** "Universal OBD-II" yazısı hâlâ üst arc/circle çizgisinin üzerine deniyordu; tamamen arc’in altındaki boş alanda durması isteniyordu.

- `main/ui/theme.h`:
  - `UI_DASH_STATUS_TOP` **12 → 24**
  - Status bar artık y = 24..40 aralığında; arc’in iç kenarının (≈ y = 20) altında, merkezi boş alanda konumlanıyor.
  - Shift-light şeridi buna bağlı olarak daha aşağıda (y ≈ 48) konumlanıyor.
- **ESP32 build:** `rebuild.bat` ile başarılı — `obd2_dashboard.bin` **0x137800** (~1.24 MB), partition'da **%59** boş.
- **ESP32 flash:** COM4 üzerinden ESP32-S3'e başarıyla yüklendi; MAC `dc:b4:d9:23:18:04`.

### Profil adı 20 px daha aşağıya alındı (2026-06-23, dokuzuncu iterasyon)

**Neden:** "Universal OBD-II" yazısı hâlâ circle çizgisine çok yakındı; 20 px daha aşağıya inmesi isteniyordu.

- `main/ui/theme.h`:
  - `UI_DASH_STATUS_TOP` **24 → 44**
  - Status bar şimdi y = 44..60 aralığında; arc’in altındaki merkezi boşlukta, circle kenarından uzakta.
  - Shift-light şeridi buna bağlı olarak y ≈ 68’e kaydı.
- **ESP32 build:** `rebuild.bat` ile başarılı — `obd2_dashboard.bin` **0x137800** (~1.24 MB), partition'da **%59** boş.
- **ESP32 flash:** COM4 üzerinden ESP32-S3'e başarıyla yüklendi; MAC `dc:b4:d9:23:18:04`.

### Profil adı ortalandı ve daha da aşağıya indi (2026-06-23, onuncu iterasyon)

**Neden:** "Universal OBD-II" yazısı biraz daha aşağıda olmalı ve tam ortalı durmalıydı.

- `main/ui/theme.h`:
  - `UI_DASH_STATUS_TOP` **44 → 56**
- `main/ui/screen_dash.c`:
  - Status bar flex align `SPACE_BETWEEN` → `CENTER` yapıldı.
  - Profil label'ına `LV_TEXT_ALIGN_CENTER` eklendi; yazı status bar içinde ortalanıyor.
  - BT ikonu `LV_OBJ_FLAG_FLOATING` + `LV_ALIGN_RIGHT_MID` ile sağa sabitlendi; flex ortalamadan etkilenmiyor.
- **ESP32 build:** `rebuild.bat` ile başarılı — `obd2_dashboard.bin` **0x137810** (~1.24 MB), partition'da **%59** boş.
- **ESP32 flash:** COM4 üzerinden ESP32-S3'e başarıyla yüklendi; MAC `dc:b4:d9:23:18:04`.

### İbre dönüş noktası RPM değerinin merkezine hizalandı (2026-06-23, on birinci iterasyon)

**Neden:** İbrenin döndüğü merkez (hub cap) hâlâ ekran geometrik merkezindeydi; ibre, büyük RPM rakamlarının merkezinden dönerek daha doğal ve dengeli bir görünüm sağlamalıydı.

- `main/ui/screen_dash.c`:
  - Arc, redline arc, ibre pivot noktası, merkezi maskeleme diski ve hub cap’in hepsi `UI_GAUGE_VALUE_Y_OFF (-28)` ile yukarı kaydırıldı.
  - Böylece ibre dönüş noktası (230, 202) artık 94 px bold RPM değerinin merkezine denk geliyor.
  - Üst arc viewport dışına çıkıp kırpılıyor; bu sayede gauge alt kısmında daha geniş bir arc görünürken üstte status bar ve shift-light şeridi temiz duruyor.
- **ESP32 build:** `rebuild.bat` ile başarılı — `obd2_dashboard.bin` **0x137820** (~1.24 MB), partition'da **%59** boş.
- **ESP32 flash:** COM4 üzerinden ESP32-S3'e başarıyla yüklendi; MAC `dc:b4:d9:23:18:04`.

### Gauge arc tekrar ekran merkezine hizalandı (2026-06-23, on ikinci iterasyon)

**Neden:** Circle (arc) çizgisi yukarı taşmıştı; yuvarlak LCD’de tam ortalı ve ekran dışına taşmayan şekilde durması isteniyordu.

- `main/ui/theme.h`:
  - `UI_GAUGE_VALUE_Y_OFF` **-28 → 0** yapıldı.
  - Böylece arc, redline arc, ibre pivotu, maskeleme diski, hub cap ve RPM değeri tam viewport merkezine (230, 230) hizalandı.
  - Arc dış çapı 460 px ile yuvarlak panel sınırına denk geliyor ve ekran dışına taşmıyor.
- **ESP32 build:** `rebuild.bat` ile başarılı — `obd2_dashboard.bin` **0x137810** (~1.24 MB), partition'da **%59** boş.
- **ESP32 flash:** COM4 üzerinden ESP32-S3'e başarıyla yüklendi; MAC `dc:b4:d9:23:18:04`.

### İbre pivotu noktası gizlendi ve alt sol hücre toggle davranışı eklendi (2026-06-23, on üçüncü iterasyon)

**Neden:** İbre pivotundaki küçük yuvarlak işaret görünmemeliydi; ayrıca çift tıklama ile merkez gösterge Speed moduna geçtiğinde sol alt hücre hâlâ Speed gösteriyordu. Kullanıcı, sol alt hücrenin merkez göstergenin tersi olmasını istedi (RPM ↔ Speed).

- `main/ui/screen_dash.c`:
  - Hub cap (`s_hub`) `LV_OBJ_FLAG_HIDDEN` ile gizlendi; ibre pivotunda yuvarlak işaret kalmadı.
  - Alt data strip'in sol hücresi artık `rpm_mode` durumuna göre değişiyor:
    - Merkez gösterge **RPM** → sol alt hücre **Speed**
    - Merkez gösterge **Speed** → sol alt hücre **RPM**
- **ESP32 build:** Başarılı — `obd2_dashboard.bin` **0x137850** (~1.24 MB), partition'da **%59** boş.
- **ESP32 flash:** COM4 üzerinden ESP32-S3'e başarıyla yüklendi; MAC `dc:b4:d9:23:18:04`.

---

## 2026-06-22 — Kullanılmayan artıklar temizlendi + build/flash

**Neden:** Working tree'de kullanılmayan dosyalar ve DTC ekranından kalan artıklar vardı; proje temizlenip gerçek cihazda doğrulandı.

### Silinenler

- `main/obd/obd_dtc.c` ve `main/obd/obd_dtc.h` (DTC ekranı daha önce kaldırılmıştı)
- `main/ui/screen_fuel.c` (henüz UI'ya entegre edilmemiş yarım ekran)
- `nul` (Windows özel aygıt adıyla oluşmuş boş dosya)
- `vehicle_data.h`/`vehicle_data.c` içindeki DTC state alanları ve `vehicle_data_set_dtc_scan()`
- `screen_settings.c` info metninden `DTC: %d+%d` satırı

### Build / flash

- **Build:** Başarılı — `obd2_dashboard.bin` **0x136020** (~1.24 MB)
- **Flash:** COM4 üzerinden ESP32-S3'e başarıyla yüklendi
- Cihaz MAC: `dc:b4:d9:23:18:04`

---

## Mevcut durum (2026-06-18)

| Alan | Değer |
|------|-------|
| Hedef cihaz | ESP32-S3, 466×466 yuvarlak LCD, 8MB PSRAM |
| Araç profili | **Universal OBD-II** (`ATSP0`, runtime PID keşfi) |
| UI sekmeleri | Connect · Dash · Grid · Settings (DTC kaldırıldı) |
| Son flash | COM3 — `obd2_dashboard.bin` **0x135160** (~1.24 MB), 2026-06-18 |
| Git | `main` = `origin/main` (feature `1b365e6`, HEAD `213d06e`) |

---

## 2026-06-18 — Tüm PID'ler için EMA + spike filtresi

**Neden:** Bazı durumlarda değerler kendiliğinden anlık pik yapıp geri düzeliyordu — voltaj filtresi hariç PID'lerde filtreleme yoktu, BLE paket bozulması veya ECU glitch gibi geçici durumlar doğrudan ekrana yansıyordu.

### `main/obd/obd_pids.c`

- **Genel `pid_filter_t` yapısı:** EMA + spike rejection + cold-start seed + 3 ardışık spike'da reset (voltage'dan taşındı).
- **`pid_filter_apply()`:** Tek bir fonksiyon tüm PID'leri filtreler; config tablosundan alpha ve spike_max alır.
- **Per-PID konfigürasyon tablosu (`s_pid_filter_cfgs[256]`):**

  | PID | Ad | Alpha | Spike Max |
  |-----|-----|-------|-----------|
  | 0x0C | RPM | 0.50 | 250 RPM |
  | 0x0D | Speed | 0.40 | 25 km/h |
  | 0x05 | Coolant | 0.15 | 20°C |
  | 0x42 | Voltage | 0.30 | 0.6V |
  | 0x11 | TPS | 0.50 | 30% |
  | 0x0B | MAP | 0.40 | 30 kPa |
  | 0x04 | Load | 0.40 | 30% |
  | 0x0F | IAT | 0.20 | 20°C |
  | 0x0E | Timing | 0.30 | 15° |
  | 0x06/07 | Fuel Trim | 0.20 | 15% |
  | 0x14/15 | O2 V | 0.30 | 0.8V |
  | 0x10 | MAF | 0.40 | 30 g/s |
  | 0x0A | Fuel Press | 0.30 | 80 kPa |

  Enum/raw PIDs (0x03, 0x12) alpha=0 → filtresiz ham kullanılır.

- **`update_pid_value` refactor:** Her PID için `apply_filtered_float()` helper ile decode + filter + set_float tek satır.
- **Voltage filtresi taşındı:** Artık `s_pid_filters[0x42]` kullanıyor — `voltage_filter_apply` ve voltage'a özel state'ler kaldırıldı.
- **Disconnect reset:** `s_pid_filters` tamamı `memset(0)` ile sıfırlanır (her yeniden bağlanmada cold-start).
- **Spike logları:** `W (PID 0x%02X spike rejected: raw=%.2f filtered=%.2f ...)` — debug için PID + raw + filtered.

---

## 2026-06-18 — Voltaj OOR fallback + Speed iyileştirmesi

**Neden:** Voltaj hücresi `--V` kalıyordu (PID 0x42 aralık dışı değer döndürüp fallback tetiklemiyordu); Speed 100ms hedefine rağmen RPM'in gölgesinde kalıyordu.

### `main/obd/obd_pids.c`

- **`voltage_pid_cb`**: `else` branch eklendi — 3 ardışık aralık dışı (OOR) okumada ATRV'ye otomatik geçiş. Ucuz ELM327 klonlarının yanlış encoding'i veya absürt ECU yanıtları artık sessizce yutulmuyor.
- **Periyodik 0x42 re-probe**: ATRV modunda her **30 saniyede** bir kısa 0x42 denemesi — init sonrası 0x42 çalışmaya başlayan araçlarda geri dönüş sağlar.
- **Voltage aralık genişletme**: `VOLTAGE_MAX_V` 15.5V → **16.5V** (yüksek şarjda reject sorunu).
- **Poll iterasyon hızı**: kuyruk boşken `vTaskDelay` 4ms → **2ms** (RPM boşluklarında Speed daha çabuk pollanır).

### `main/data/vehicle_profile.c`

- **Speed interval**: 100ms → **75ms** (RPM boşluklarında daha sık slot).
- **Coolant interval**: 500ms → **250ms** (sub-cell daha canlı).

### `main/obd/elm327.c`

- `atrv_response_cb` ham response'u loglar (parse başarısız olduğunda neyin yanlış olduğu görülür).

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
- [x] `obd_dtc.c` / `obd_dtc.h` ve DTC veri artıklarını tamamen sil (2026-06-22)
- [ ] İsteğe bağlı: Kalos preset profili (çoklu profil seçimi)
- [ ] İsteğe bağlı: `ATDP` ile algılanan protokolü ayarlar ekranında göster
- [ ] Simülatör `round_mask` overlay'i production'dan ayır
