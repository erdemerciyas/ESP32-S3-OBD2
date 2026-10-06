# Changelog — ESP32-S3 OBD-II Dashboard

Bu dosya proje geçmişini ve mevcut durumu tutar. **Yeni sohbetlerde önce burayı oku;** anlamlı değişiklik yaptıktan sonra güncelle.

**Sürüm kuralı (zorunlu):** her commit yeni bir sürüm alır. `scripts/bump_version.sh [patch|minor|major]` → `version.txt` (firmware + Android ortak tek kaynak), bu dosyaya `## vX.Y.Z — tarih — başlık` girdisi. `.githooks/pre-commit` ikisini denetler, `post-commit` `vX.Y.Z` etiketini atar; push: `git push --follow-tags`. Yeni klonda bir kez: `git config core.hooksPath .githooks`. patch = düzeltme / küçük değişiklik, minor = yeni özellik, major = uyumsuz değişiklik (protokol, NVS biçimi).

## Mevcut durum (2026-10-05)

| Alan | Değer |
|------|-------|
| Hedef cihaz | Waveshare ESP32-S3-Touch-LCD-2.1, 480×480 yuvarlak LCD (görünür 460 px), 8MB PSRAM |
| Hedef araç | 2005 Chevrolet Kalos 1.4 benzin — K-line, KWP2000 fast (`ATSP5`); adaptör: "ELM327 Blue" klon (BLE) |
| Araç profili | **Universal OBD-II** (`ATSP0` → tespit edilen protokol NVS'de, sonraki bağlantı `ATSPA<n>`) |
| UI sekmeleri | Connect · Dash · Grid · **DTC (Arıza)** · Gyro · Settings |
| Son build | `obd2_dashboard.bin` **0x1b65e0** (~1.79 MB, %43 boş — WiFi yığını eklendi), 2026-10-05 |
| Son flash | COM3 (USB-JTAG) — BLE/WiFi taşıma seçimi, varsayılan BLE, 2026-10-05 |
| Git | 2026-10-05 tüm değişiklikler (K-line optimizasyonu, DTC, yuvarlak UI, WiFi adaptör, README) `main`e commit edilip `origin`e gönderildi |
| Açık işler | WiFi adaptörle araçta test · Araçta ölçüm (Settings → `Link:` satırı) · Faz 3 K-line P3 ayarı · Faz 4 BLE CCCD · `scripts/verify_round_lcd_layout.py` `UI_VIEWPORT_SZ` parse hatası (önceden var) |

---

## v1.2.0 — 2026-10-07 — ROLL: performans ölçümü (F0 ölçüm zinciri) + ayarlar + kayıt görüntüleyici

Üçüncü mod **ROLL**: Dragy benzeri hızlanma / mesafe / fren ölçümü. Harici GNSS yok; hız kaynağı önceliği: telefon bağlı ve GPS fix'i yeterliyse **telefon GPS'i** (Doppler), değilse **BLE ELM327 üzerinden ECU hızı**; aradaki boşlukları IMU (100 Hz) doldurur. Bu sürüm F0: ölçüm altyapısı + kayıt + analiz; cihazdaki koşu motoru (hazırlanma, canlı süre, timeslip) F2'de, gerçek kayıtlarla ayarlanacak.

**Araştırma özeti (karar gerekçesi):** zamanlama hatası ≈ hız hatası ÷ ivme. Kalos'ta (100 km/h'de ~1.3 m/s²) 1 km/h hata ≈ 0.2 s, %0.1 eğim ≈ 0.055 s. Beklenen 0-100 doğruluğu: yalnız OBD ±0.2–0.5 s, yalnız telefon GPS (1 Hz) ±0.2–0.5 s, **telefon GPS + IMU + koşu sonrası yumuşatma ≈ ±0.1 s**, harici 10–25 Hz GNSS ±0.05 s. PID 0x0D: 1 km/h adım, 255 km/h tavanı, ~3 km/h altı yok, ECU filtre gecikmesi.

**ESP**
- **Paylaşılan NimBLE yığını** (`ble_host.c`): `ble_obd` (central) ve `nav_transport_ble` (peripheral) artık yığını kendileri açıp kapatmıyor, istemci olarak katılıyor; ilk katılan açar, son çıkan kapatır. `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=2`, host görev yığını 6144 (iki bağlantı + telemetri + rcfg JSON; ilk denemede bir kez taşma görüldü). NimBLE log seviyesi WARN (her bildirimi INFO yazması telemetriyi düşürüyordu). Yığın paylaşılırken OBD tarama/bağlantı başlatma penceresi %100 → %30 (adaptör yokken sürekli tarama telefon bağlantısının radyo zamanını yiyordu).
- **`APP_MODE_ROLL`** (`app_mode.c`): mod başına radyo politikası — OBD: OBD; NAV: telefon; ROLL: telefon + BLE OBD (WiFi seçiliyse yalnız telefon). NAV ↔ ROLL geçişinde telefon bağlantısı kopmaz; mod değişince telefona `status` (`mode`) gönderilir.
- **`roll/roll_feed.c`**: tüm örnekler alındıkları anda `esp_timer` µs ile damgalanır. Telefon GNSS (`gnss`), OBD hızı (ham, filtresiz; istek gönderim + yanıt zamanı — `elm327_inflight_tx_us()`), IMU (araç ekseni ivme, yaw, pitch). Kaynak seçimi + IMU ile hız taşıma (≤1.5 s), oran ölçümü, TEL halkaları (IMU 128 / OBD 32 kayıt) ve 20 Hz telemetri görevi.
- **OBD**: ROLL'de yalnız hız PID'i sorgulanır (K-line'da RPM'i bırakmak hız örnek sıklığını ~2 katına çıkarır).
- **Protokol** (geriye uyumlu eklemeler): `gnss` (Doppler hızı, doğruluk, MSL rakım, uydu; `e` = ESP saatinde fix zamanı), `ping.m` / `pong.m,e` (NTP tarzı saat senkronu), `status.mode`, `rcfg` / `rcfgack`, yeni **TEL** karakteristiği `7c6a0005` (ikili IMU 14 B / OBD 9 B kayıt çerçeveleri).
- **`roll/roll_cfg.c`**: kişisel ROLL ayarları (NVS `roll/cfg`): kaynak (otomatik / GPS / OBD), birim, ≤10 hız aralığı, ≤6 mesafe, ≤3 fren hedefi, başlatma (otomatik / ağaç 0.4–0.5 s), rollout, eğim sınırı + düzeltmeli süre, GPS hız doğruluğu ve uydu eşikleri, OBD hız çarpanı, araç adı / kütle / çekiş, bip, sonuç süresi. Şimdiden uygulananlar: kaynak tercihi, GPS eşikleri, mph, araç adı.
- **ROLL ekranı** (`screen_roll.c`): 300° hız yayı (renk = kaynak), büyük hız, araç adı, kaynak rozeti, doğruluk notu, GPS / OBD / IMU hapları, kayıt göstergesi, MENÜ. **Ana menü**: üçgen dizilmiş üç karo (OBD, NAV, çizilmiş kronometre simgeli ROLL).
- Ekran görüntüsü turuna ROLL eklendi (demo anlık görüntüsü, `roll_feed_demo`); `docs/screenshots/roll.png` + yeni `home.png`.

**Android (AURA Köprü)**
- `Roll.kt`: ESP ROLL'e geçince en hızlı `GPS_PROVIDER` + kullanılan uydu, 1 sn saat senkronu (en düşük RTT örneği), her fix için `gnss`, TEL çözme, CSV kaydı (`filesDir/roll/roll_*.csv`: G / I / O / S / M satırları, ayrı yazma iş parçacığı).
- **ROLL ayarları** (`RollSettingsActivity`): tüm ayarlar kart kart, anında kaydedilir, 300 ms birleştirmeyle ESP'ye gönderilir, senkron durumu üstte; mph'de gösterim dönüştürülür, km/h saklanır; ham kayıt aç/kapa, otomatik silme.
- **ROLL kayıtları / görüntüleyici** (`RollLogActivity`, `RollRunActivity`, `RollAnalysis.kt`, `RollChart.kt`): listede koşu özeti; dokununca en iyi sonuç kutucukları, hız–zaman grafiği (füzyon eğrisi, GPS / OBD noktaları, kalkış işaretleri, yakınlaştırma, imleç) + boyuna g şeridi, koşu kartları (GPS / OBD karşılaştırması, eğim, doğrulama), kaynak kalitesi; **sonuç CSV** ve **ham CSV** dışa aktarma (Download/AURA) ve paylaşma. Analiz `scripts/roll_analyze.py` ile birebir aynı sayıları verir (JVM'de sentetik kayıtla doğrulandı).

**Araçlar**
- `scripts/roll_analyze.py`: kaynak istatistikleri, OBD ölçek k ve gecikme τ (GPS'e göre), kalkış algısı (yalnız duruştan), aralık süreleri (GPS / füzyon / OBD / OBD+IMU / OBD-k), eğim (GPS rakımı / IMU), `--plot`. Sentetik koşu (gerçek 0-100 = 15.17 s): füzyon 15.22 s, ham OBD 16.76 s, k/τ düzeltmeli OBD 15.09 s.
- `scripts/roll_bridge_sim.py`: telefonsuz ROLL testi (saat senkronu, sentetik GNSS rampası, TEL sayaçları).

**Doğrulama:** firmware derlenir (%40 boş), cihazda ROLL açılışı + telefon bağlantısı + TEL akışı 60 sn kayıpsız. Henüz yapılmadı: telefon + BLE ELM327 aynı anda sahada, gerçek sürüş kayıtları, rcfg gidiş-dönüşü telefonda.

---

## v1.1.1 — 2026-10-06 — Kanca betikleri LF

- `.gitattributes`: `*.sh` ve `.githooks/*` her platformda LF (Windows'ta CRLF'ye çevrilirse Linux/macOS klonunda bash `\r` hatası verir).
- Kanca doğrulandı: sürüm artırılmadan yapılan commit `pre-commit: sürüm artırılmadı (1.1.0 → 1.1.0)` ile reddedildi.

---

## v1.1.0 — 2026-10-06 — Sürümleme kuralı + AURA ilk sürüm numarası

- `version.txt` = **1.1.0** (v1.0.0 = yalnız OBD; 1.1.0 = AURA: NAV + Android köprü + saat + fx3d — aşağıdaki 2026-10-06 girdileri, commit `e599120`). ESP-IDF bunu uygulama sürümü yapar: açılış logu `AURA v1.1.0`, ana menüde altta `v1.1.0`.
- Android köprü sürümünü `version.txt`'den alır: `versionName` = `1.1.0`, `versionCode` = major·10000 + minor·100 + patch (= 10100; eski 8'den büyük, güncelleme olarak kurulur).
- `scripts/bump_version.sh`, `.githooks/pre-commit` (sürüm artmadıysa / CHANGELOG girdisi yoksa commit'i reddeder), `.githooks/post-commit` (`vX.Y.Z` açıklamalı etiket).

---

## 2026-10-06 — Saat: 4 sn basılı tutma · ekran görüntüsü turu · İngilizce README

- **Saat açma = 4 sn basılı tutma** (her görünümde; 24 px tolerans, kaydırınca süre baştan, basış başına bir kez; bırakış alttaki ekrana tıklamaz). **Pinch kaldırıldı** (CST820 tek nokta — `lvgl_v8_port.cpp` ek I²C okuması ve `bsp_touch_take_pinch` silindi).
- **NAV'da saat kendiliğinden asla gelmez**; 30 sn boşta otomatik yalnız OBD / ana menüde ve Ayarlar → Clock açıkken.
- NAV kökündeki "uzun basış → menü" kaldırıldı (4 sn tutuşu 0.4 sn'de bölüyordu); MENÜ hapı duruyor.
- **Ekran görüntüsü turu** (`CONFIG_UI_SHOT_TOUR`, `ui/ui_shots.c`, ayrı `build_shots/` derlemesi): saat ayarlanır, demo OBD verisi + mock rota, tüm görünümler gezilir, `lv_snapshot` kareleri numaralı base64 satırlarıyla **iki geçişte** USB'ye yazılır. `scripts/capture_screens.py` (yalnız pyserial, PIL yok) toplu okur, eksik satırı diğer geçişten tamamlar, yuvarlak alfa maskeli PNG yazar → `docs/screenshots/` (11 görüntü). İlk denemede pyserial `readline()` yavaşlığından ~%30 satır kaybı → blok okuma + cihazda 4 satırda 2 ms bekleme ile kayıpsız.
- **README İngilizce baştan yazıldı** (AURA: OBD + NAV + Android köprü + saat, gerçek ekran görüntüleri, protokol, mimari, fx3d, NVS anahtarları, geliştirici araçları, sorun giderme, sınırlamalar).

---

## 2026-10-06 — NAV ilk sayfa: canlı arka plan + kullanışlılık

- **Canlı arka plan** (`ui/screen_nav_bg.c`, 440 px fx3d canvas, ~15 fps, yalnız rehberlik sayfasında): lacivert vinyet + sıcak turuncu-mor ufuk ışıltısı (tek sefer hesaplanan taban, karede memcpy), üstte dalgalı kuzey ışığı perdeleri (yeşil / camgöbeği / mor), altta perspektif yol: kenar çizgileri, telefon hızıyla akan orta şerit ve ızgara; dönüşe 400 m kala yol dönüş yönüne kıvrılır. Veri eskiyse sahne söner ve durur.
- **Yaklaşma halkası**: kenarda 300° yay, manevraya yaklaştıkça dolar; >300 m camgöbeği, 100–300 m turuncu, <100 m yeşil.
- **Cam bilgi hapları**: VARIŞ (yeşil) · KALAN (turuncu) · HIZ (camgöbeği); radar uyarısında hız limiti aşılırsa HIZ hapı kırmızı.
- **Durum satırında saat** (rehberlikte) — `14:37 • Telefon bağlı`.
- **Boşta ekranı** (telefon yok / rota yok / geçiş): büyük saat + "SALI • 6 EKİM", durum mesajı, telefondan anlık hız • bulunulan yol.
- Gün/ay adları `clock_day_name` / `clock_month_name`'e taşındı (saat koruyucusuyla ortak).

---

## 2026-10-06 — Saat / takvim + 3D saat ekran koruyucusu

**Saat senkronu:** telefon `hello` ve her `ping`de (2 sn) `ts` (UTC epoch) + `tz` (yerel ofset dk, yaz saati dahil) gönderir → `clock_set_from_phone` (`main/data/clock.c`); 2 sn'den küçük sapmada saate dokunulmaz. Saat dilimi NVS'de (`clock/tz`). PCF85063 RTC (0x51) açılışta yoklanır — **bu kartta yok** (log: `RTC absent`), yani güç kesilince saat telefon tekrar bağlanana kadar "SAAT AYARLANMADI". Android **AURA Köprü 0.8** (`dist/AURA-Bridge-0.8.apk`).

**Ekran koruyucu (`ui/screen_clock.c`, üst katman):** fx3d holografik kadran — geriye yatık, yavaş salınan, derinliğe ayrılmış halkalar (dakika çentikleri arkada, saat turuncu yay, dakika camgöbeği yay, saniye kuyruklu yıldız önde → paralaks), kameraya akan yıldız alanı, 6 katman kabartma HH:MM, Türkçe gün/tarih. 440 px canvas, 20 fps; görünürken alttaki ekranlar güncellenmez.
- Ayarlar → **Clock** karosu (alt orta; NVS `clock/saver`): açıkken 30 sn dokunulmazsa gelir.
- **İki parmak (pinch)** ayar kapalıyken de açar. CST820 tek nokta raporlar → `lvgl_v8_port.cpp`: ham parmak sayısı (reg 0x02 ≥ 2) **veya** aynı basışta ters yönlü iki >80 px sıçrama.
- Herhangi bir dokunuş kapatır, alttaki NAV/OBD ekranına geri döner (bırakış tıklamaya dönüşmez). NAV'da yeni uyarı (radar vb.) gelirse kendiliğinden kapanır.

---

## 2026-10-06 — Gyro düzeltmesi (QMI8658 sürücü kayıt haritası) + AURA markası

**Belirti:** kalibre/sıfırla sonrası gyro ekranı sabit. Log: kalibrasyondan sonra sapma tam 0.0000.

**Kök neden — `qmi8658.c` yanlış kayıtlara yazıyordu (datasheet ile karşılaştırıldı):** CTRL1'e ivme ayarı (aslında arayüz: `0x60` → big-endian açılıyordu, sürücü little-endian okuyordu), CTRL2'ye jiro ayarı (aslında ivme), CTRL3'e "enable" bitleri (aslında jiro), CTRL7=`0x01` (yalnız ivme — **jiroskop hiç açılmamış**), soft reset yanlış kayıtta. Düzeltildi: CTRL1=`0x40` (adres artırma, LE), CTRL2=±8g/250 Hz, CTRL3=±512 dps/250 Hz, CTRL5 LPF ~33 Hz (motor titreşimi), CTRL7=`0x03`, RESET(0x60)=`0xB0`; sıcaklık `T_H + T_L/256`. Log: `|a|=9.41 m/s²`, jiro `-0.05 rad/s` (gerçek sapma).

**`imu_data.c` sağlamlaştırma:** hatalı veriyle yapılmış eski kayıt yok sayılır (yeni NVS anahtarı `imu/cfg2` + doğrulama); kalibrasyon 1 g görünmüyorsa reddedilir; bu sensör ~%4 düşük okuyor → kalibrasyonda ölçülen **1 g referansı** (`g_ref`); durma algısı büyüklükten değil **sabitlikten** (ivme + jiro titreşimi) — sapma bilinmeden de durma algılanır ve sapma öğrenilir; kalibre edilmemişken açılış duruşu 3 sn durunca süzülmüş ivmeyle "düz" alınır; SIFIRLA dururken süzülmüş ivmeyi kullanır. Log doğrulaması: dururken `pitch=-0.0 roll=0.2`, sapma kendiliğinden −0.036'ya yaklaşıyor.

**Marka: AURA** (OBD + NAV + ileride sensörler için genel ad): açılış "AURA / AKILLI ARAÇ SİSTEMİ / OBD | NAV | SENSÖR", ana menü başlığı, BLE adı `AURA` (telefon UUID ile bulur, eşleşme bozulmaz), NAV mesajları; Android **AURA Köprü 0.7** (`dist/AURA-Bridge-0.7.apk`): uygulama adı, başlık, bildirim, imza "AURA · Erdem ERCİYAS", galeri klasörü Resimler/AURA.

---

## 2026-10-06 — Gyro ekranı: gerçek eğim ölçer + G-metre, kalibrasyon

**Önceki durum:** ekran yalnız ham ivmeölçerden eğim hesaplıyordu (jiroskop kullanılmıyordu) → fren/gaz/virajda eğim kayıyordu; SIFIRLA açı çıkarmasıydı (eğik montajda yanlış); açılışta motor titreşimi altında 2 sn bloklayan jiroskop kalibrasyonu.

- **`imu_data.c` (yeniden yazıldı):** montaj dönüşümü (yukarı yön + ileri yön → araç ekseni x ileri / y sol / z yukarı); yerçekimi yönü jiroskopla taşınır, ivmeölçer güvene göre düzeltir (|a|≈1 g ve düşük dönüş hızında; dururken hızlı); **OBD hızı varsa** viraj ivmesi (v·ω) ve boyuna ivme (dv/dt, ölü bant 0.5 m/s²) çıkarılır; araç dururken jiroskop sapması sürekli öğrenilir; **ileri yön sürüşten öğrenilir** (düz giderken ≥1.3 m/s² hızlanma/fren, toplam 4 sn). Açılışta bloklayan kalibrasyon kaldırıldı. Ayarlar NVS `imu/cfg`. Varsayılan: dikey montaj (sensör −X yukarı, +Z ileri — eski ekranın varsayımı).
- **API:** `imu_calib_start` (3 sn hareketsiz örnekleme: sapma + yukarı yön, hareket varsa reddeder), `imu_level_zero`, `imu_reset_peaks`, `imu_calib_clear`; snapshot: pitch/roll, boyuna/yanal G, tepe değerler, durum bayrakları.
- **`screen_gyro.c`:** merceğe dokun → **EĞİM / G-METRE**. G-metre: 0.5/1 g halkaları, hissedilen kuvvet topu + iz, yaylar ±1 g. Üstte **SIFIRLA** (şu an = düz + tepeler sıfır) ve **KALİBRE** (ilerleme halkalı panel: BAŞLAT / TAMAM / TEKRAR, KAPAT, SİL). Altta EĞİM + MAKS ve durum satırı (Kalibre edilmedi / Duruyor / Hız telafisi açık).
- İşaret kontrolü (sentetik): burun yukarı 10° → +10, sağ aşağı 5° → +5, 0.5 g fren → −0.5. Build uyarısız, COM3'ten flash, açılış sonrası log temiz. Cihazda fiziksel eğimle doğrulanmadı.

---

## 2026-10-06 — Neon 3D açılış, ana menü ve geçişler (three.js esintili)

- three.js (WebGL) ESP32'de çalışamaz → görsel dili kendi küçük yazılım 3D çizicimizle: **`ui/fx3d.c`** — RGB565 tuvale toplamalı Xiaolin Wu çizgileri (üst üste binen çizgiler parlar), neon hale, yumuşak noktalar, döndürme + perspektif, ikozahedron modeli.
- **Açılış (`screen_splash.c`, ~4.7 sn, 30 fps):** warp yıldız akışı → ufka akan magenta perspektif ızgara + çizgili gün batımı → dönen parlayan tel kafes ikozahedron → amblem olarak yukarı süzülür, "OBD2 / DASHBOARD + NAV" harf aralığı daralarak belirir, profil + canlı durum, karartma. Tuval 460×460 PSRAM, açılış bitince serbest.
- **Buzzer:** karttaki buzzer IO genişletici (EXIO8) üzerinden yalnız aç/kapa (aktif buzzer, perde yok) → başlıkla eşzamanlı ritmik "ta-ta-taaam" (LVGL görevinden, I2C dokunmatikle çakışmaz). Gerçek melodi için pasif buzzer + GPIO/LEDC gerekir.
- **Ana menü:** arkada yavaş dönen sönük ikozahedron + yörünge noktaları (360×360 tuval, yalnız görünürken 15 fps), "OBD / NAV" başlığı, karolarda ışıltı halkası (seçili mod vurgu renginde).
- **Geçişler:** görünüm değişiminde 260 ms koyudan açılan örtü (OBD ekranlarının içine dokunulmadı).
- **NAV okları:** her okun arkasında kalın, %20 saydam aynı renkte neon hale.
- Build uyarısız 0x1c3780 (%41 boş), COM4'ten flash. Cihazda görsel olarak doğrulanmadı.

---

## 2026-10-06 — Harita yakınlaştırma (+/−/TÜMÜ) ve kolay ana menü

**Geri bildirim:** harita geldi ama çok uzaktan; zoom +/− olmalı; önce tüm rota sığsın, sonra yakınlaştırılabilsin; ESP'de ev simgesiyle geçiş zor.
- **ESP harita sayfası:** "−" / "+" yuvarlak düğmeler (sol/sağ alt), "TÜMÜ" hapı. +/− anında LVGL önizlemesi (resim + rota çizgisi aynı oranla, `lv_img_set_zoom`), aynı anda telefondan o zoom'da net harita istenir (`{"t":"mapreq","mode":"follow","z":16}`); TÜMÜ → `mode:"fit"`. Gelen resim seçili zoom'da değilse (telefon yeniden başlamış) istek yinelenir.
- **Telefon `MapFeeder` (0.6):** varsayılan **FIT** — sürüş kaydının tamamı + konum daireye sığar (pad 72, en fazla z17), rota büyüdükçe zoom azalır; **FOLLOW** — araca ortalı, seçili zoom, 110 px'te yenilenir. En sık 4 sn'de bir; ana ekranda mod gösterilir.
- **Kolay menü:** NAV'da altta geniş "⌂ MENÜ" hapı + ekranın herhangi bir yerine **uzun basış** → ana menü (sayfa geçişi artık SHORT_CLICKED, uzun basıştan sonra sayfa değişmez). Sayfa noktaları sağ kenara dikey. OBD'de ev simgesi 20 px, camgöbeği, dokunma alanı ±30 px (satır taşmaya açık).
- Build uyarısız, COM4'ten flash. APK `dist/OBD-Nav-Bridge-0.6.apk`.

---

## 2026-10-06 — Harita teşhisi + Android 0.5 profesyonel arayüz

**Belirti:** Her şey çalışıyor, ESP harita sayfasında harita görünmüyor. ESP yalnız COM4'te (uygulama logu yok) → kesin neden görülemedi; olası nedenler kapatıldı:
- **ESP:** MAP karakteristiği artık **onaylı write** da kabul ediyor; harita sonucu telefona `{"t":"mapack","id":n,"ok":…,"why":"gap|busy|size|decode"}` ile bildiriliyor (`nav_map_set_result_cb`). COM4'ten flash.
- **Android:** harita parçaları **onaylı** yazılıyor (yanıtsız yazmada ESP tamponu taşınca tek parça kaybı resmi çöpe atıyordu). GPS yokken (kapalı alan) **ağ konumu** harita merkezi için kullanılıyor; rota kaydı/radar/ESP `loc` yalnız ≤50 m doğrulukta. İndirilen karo sayısı ve ESP onayı günlükte ("Harita #n ESP'de gösterime hazır" / "başarısız: …"); başarısızsa yeniden gönderilir.

**Android 0.5 arayüz** (`dist/OBD-Nav-Bridge-0.5.apk`): ESP paletiyle koyu tema, logolu başlık, canlı ESP/navigasyon kartı, GPS/Radar/Harita/Sürüş kutuları, yalnız eksik ayar varken görünen kurulum kartı, işlemler, araçlar, açılır günlük; alt imza **"Geliştiren: Erdem ERCİYAS"**. Uyarlanabilir uygulama simgesi (halka içinde camgöbeği ok), bildirim simgesi, sürüş listesi/görüntü ekranları aynı tasarımda; sürüş görüntüsüne "OBD-Nav Köprü · Erdem ERCİYAS" imzası.

---

## 2026-10-06 — NAV v2: konum, rota kaydı, harita, radar/trafik uyarıları, sürüş geçmişi

**ESP (derlendi + flash; PC BLE testi bekliyor — PC Bluetooth kapalıydı):**
- Protokol: `loc` (lat/lon/spd/hdg), `alert` (k: cam/traffic/hazard/info/none, d, lim, txt), `map` başlığı (id, len, w, h, clat, clon, z). Yeni BLE karakteristiği **MAP `…0004`** (write-no-rsp): `[id][offset 3B LE][JPEG]`.
- `nav_track.c`: sürüş kaydı PSRAM'de (16384 nokta, dolunca yarıya seyreltme, 8 m eşik, haversine mesafe), geçilen yollar (48). Yeni sürüş yalnızca açık sürüş yokken `start` ile (BLE kopup bağlanınca kayıt silinmez); `stop` kapatır, kayıt bir sonraki rotaya kadar durur.
- `nav_map.c`: JPEG parçalarını birleştirir, ayrı görevde `esp_jpeg` (ROM tjpgd) ile 460×460 RGB565'e çözer, çift tampon (UI kullanırken üzerine yazılmaz). Yeni bağımlılık `espressif/esp_jpeg ^1.3.0`. ~1 MB PSRAM (ilk haritada ayrılır).
- NAV ekranı: dokununca **Rehberlik → Harita → Rota özeti**, alt nokta göstergesi. Harita sayfası: OSM resmi + rota çizgisi (Web Mercator, resim yoksa rotayı sığdırır) + konum, üstte km/süre. Özet: mesafe, süre, ort. hız, son 6 yol. **Uyarı şeridi** (radar kırmızı, trafik sarı, tehlike turuncu; 8 sn).
- Build uyarısız 0x1c0ab0 (%42 boş).

**Android 0.4 (`dist/OBD-Nav-Bridge-0.4.apk`, derlendi, telefonda denenmedi):**
- `TripService`: ön plan konum servisi (kalıcı bildirim), GPS 1 Hz → ESP `loc` (HIZ alanı da dolar), sürüş kaydı, radar, harita.
- `Radar`: OSM `highway=speed_camera` (Overpass, ±0.3°, önbellek), gidiş yönünde 600 m içindeki kamera → `alert cam` (+ maxspeed), geçince `none`.
- Bildirimlerden uyarı: radar/kamera, kaza/yol çalışması/dikkat, trafik/yoğun (TR/RU/EN); metin değişince gönderilir. **Waze** (`com.waze`) eklendi.
- `MapFeeder`: zoom 15, 460×460 karanlık OSM (ters + 180° ton), JPEG q70; araç 110 px (~400 m) uzaklaşınca yenilenir. Mesajlar harita parçalarından önce gider; bağlantı önceliği HIGH.
- `TripRecorder` + **Sürüş geçmişi**: `files/trips/*.json` (60 sn'de bir ve bitişte), liste + görüntü (1080×1640: harita, rota, başlangıç/bitiş/uyarı işaretleri, mesafe, süre, ort./maks hız, uyarı sayıları, geçilen yollar, OSM atfı) → galeriye kaydet (Resimler/OBD-Nav) / paylaş / sil.

---

## 2026-10-06 — Navigasyon modu, adım 4: Android köprü uygulaması (`android-bridge/`)

- Kotlin, androidx'siz (yalnız framework), minSdk 26 / targetSdk 34. APK: `dist/OBD-Nav-Bridge-0.1.apk` (633 KB, debug anahtarıyla imzalı, `dist/` git dışı).
- **`NavListenerService`** (NotificationListenerService): Yandex Navigator / Yandex Haritalar / Google Maps'in kalıcı (ongoing) rehberlik bildirimini okur → `start` + `upd`; bildirim kalkınca `stop`; 2 sn'de bir `ping` (ESP "Veri eski" demesin). BLE yeniden bağlanınca süren rotayı hemen gönderir.
- **`NavParser`**: extras + RemoteViews şişirilip tüm TextView'lar (YaNaviPeb yaklaşımı); mesafe (m/km/м/км), ETA (HH:MM), manevra anahtar kelimeleri (TR/RU/EN), yol adı (Cd./Sk./Blv./улица…). Biçim belgelenmemiş → "Son navigasyon bildirimini kopyala" ile ham döküm alınıp kurallar ayarlanacak.
- **`BleLink`**: servis UUID filtresiyle tarama, adres kaydı, `autoConnect` (ESP NAV'a geçince kendiliğinden bağlanır), MTU 247, notify, yazma kuyruğu (yanıtlı yazma, 8 mesaj sınırı), koparsa yeniden bağlanma.
- **`MainActivity`**: kurulum adımları (bildirim erişimi, Bluetooth izni, ESP'yi bul), durum, demo rota, günlük.
- Derleme ortamı: `C:\Users\erdem\.android-build` (Temurin JDK 17, Gradle 8.9, SDK platform 34 + build-tools 34). AGP 8.5.2, Kotlin 1.9.24. `gradle assembleRelease` uyarısız.
- **Henüz telefonda denenmedi.**

---

## 2026-10-06 — Navigasyon modu, adım 2–3: protokol v1 (JSON) + BLE GATT sunucusu

- **Yön:** NAV modunda ESP32 **peripheral**, telefon köprüsü **central** (telefon ESP'ye bağlanır). Reklam adı `OBD-Nav`.
- **GATT:** servis `7c6a0001-2f4b-4b8e-9d3a-5e1f0c2a9b10`; RX `…0002` (write / write-no-rsp, telefon → ESP), TX `…0003` (notify, ESP → telefon). Her yazma tek mesaj.
- **`nav_protocol.h`**: taşıma/kodlamadan bağımsız `nav_msg_t`; `nav_codec_t` arayüzü (bugün JSON, ileride ikili + CRC). `upd`'de yalnız gelen alanlar uygulanır (Yandex sokak adı vermezse ekrandaki korunur).
- **`nav_codec_json.c`** (IDF `json`/cJSON): `hello`→`status`, `start`, `upd`, `stop`, `ping`→`pong`. Manevra kodları `S L R SL SR U RB A`; `eta` gece yarısından dakika. UTF-8 metinler karakter ortasından kesilmez.
- **`nav_transport.h` / `nav_transport_ble.c`**: NimBLE'ı NAV'da kendisi açar/kapatır (OBD `ble_obd` o sırada kapalı). Kopunca yeniden reklam. IDF 5.3 `nimble_port_deinit` GATT tanımlarını sıfırlıyor → NAV↔OBD geçişlerinde temiz yeniden kayıt.
- `CONFIG_NAV_MOCK` artık varsayılan **kapalı** (gerçek BLE). `sdkconfig.defaults`: peripheral/broadcaster rolleri açıkça yazıldı.
- **Düzeltme:** IDF 5.3 NimBLE'da peripheral `BLE_GAP_EVENT_CONNECT.status` uzak özellik okumasının sonucu; Windows PC `26` (0x1A, Unsupported Remote Feature) dönünce bağlantı açıkken "başarısız" sanılıyor, `s_conn` atanmıyor, notify (status/pong) gitmiyordu. Artık `ble_gap_conn_find()` belirleyici.
- Build uyarısız (0x1bc1e0, %42 boş), COM3'ten flash. **Test (PC `bleak`, `scripts/nav_bridge_sim.py`): hello→status OK, 21 upd (Türkçe/uzun yol adı, kısmi upd), bozuk mesaj reddi, ping→pong OK, 7 sn sessizlik, stop, kopup yeniden bağlanma OK** — iki oturum da geçti, cihaz logunda hata yok.
- Not: Windows "Cihaz ekle" listesinde görünmemesi normal; eşleştirme gerekmez, uygulama doğrudan GATT ile bağlanır.

---

## 2026-10-06 — Navigasyon modu, adım 1: mod seçimi + NAV ekranı (mock veri)

**Hedef:** Telefon navigasyonunu (Yandex/Google bildirimi → Android köprü → BLE) gösteren NAV modu. OBD ve NAV **birbirini dışlar**: NAV'dayken OBD radyosu (BLE/WiFi) tamamen kapalı; çıkınca normal OBD süreci baştan başlar.

- **`app_mode.c`**: OBD/NAV modu, NVS'de (`app/mode`), açılışta son mod başlar. Geçiş tek görevde sırayla: OBD→NAV `obd_link_suspend()` → `nav_service_start()`; NAV→OBD `nav_service_stop()` → `obd_link_resume()`. Geçiş sürerken gelen istekler son hedefe birleşir.
- **`obd_link.c`**: yalnız `suspend`/`resume` eklendi (seçili taşımayı durdurur / başlatır). `ble_obd`, `wifi_obd`, `elm327`, `obd_pids` **değişmedi** — bağlantı yokken zaten boşta bekleyip durumlarını sıfırlıyorlar.
- **`main/nav/`**: `nav_state` (OBD'den bağımsız, mutex + rev sayacı), `nav_service` (şimdilik yalnız mock), `nav_mock` (Ankara rotası, 54 km/h, 1 sn). `CONFIG_NAV_MOCK` (varsayılan açık).
- **UI**: açılış animasyonundan sonra son modun görünümü. `screen_home` (OBD / NAV yuvarlak karoları), `screen_nav` (lv_line ile çizilmiş büyük manevra oku, mesafe, manevra metni, yol, VARIŞ/KALAN/HIZ, bağlantı durumu, 5 sn'de "Veri eski"). Ana menüye dönüş: OBD'de nokta çubuğunun solundaki ev simgesi, NAV'da alttaki ev simgesi. Etiketler yalnız değişince yazılır.
- Build uyarısız, imaj ~1.81 MB. COM3'ten flash, açılış OK (`app_mode: Boot mode: OBD`, WiFi taşıma aramada; çökme yok). CST816S I2C okuma hataları önceden de vardı (dokunulmadığında). Mod geçişleri cihazda elle test edilecek.

---

## 2026-10-06 — WiFi: ESP güç kaybı sonrası adaptörde kalan ölü TCP oturumu

**Belirti (fotoğraflı):** İlk bağlantı hızlı ve veri akıyor. Araç kapatılıp ESP sökülüp takılınca "Opening TCP..."ta kalıyor, ya da "Connected" olup veri gelmiyor / hemen kopuyor.

**Teşhis:** Adaptör OBD soketinden sürekli besleniyor; ESP'nin elektriği kesilince FIN gitmiyor, adaptör ölü oturumu açık sanıp **tek istemci yuvasını** tutuyor. Yeni bağlantı reddediliyor/askıda kalıyor ya da veri eski oturuma gidiyor. Önceki keepalive/bekçi düzeltmeleri bunu çözmüyordu.

- **Sabit yerel TCP portu** (`wifi_obd.c`): 35001–35008 aralığı, son kullanılan NVS'de (`obd_wifi/lport`). Yeniden açılışta aynı IP + aynı port ile gelen SYN adaptördeki ölü oturuma denk gelir → adaptör ACK döner → lwIP (SYN_SENT) RST basıp SYN'i hemen yineler → adaptör ölü oturumu bırakır (RFC 793/5961 yarı-açık bağlantı kurtarma). Kendi TIME_WAIT'imiz porta denk gelirse (`EADDRINUSE`) aralıktaki sonraki port.
- **`CONFIG_LWIP_DHCP_RESTORE_LAST_IP=y`**: yeniden açılışta adaptörün DHCP'sinden önceki IP istenir (aynı 4'lü için gerekli). Statik yedek `.123` zaten sabit.
- TCP connect zaman aşımı 3 → 4 sn (SYN yinelemesine yer), durdurma bekleme 9 → 11 sn. Log: yerel port, errno, alınan IP.
- Dash üst durum simgesi WiFi'deyken WiFi simgesi gösteriyor (önce hep Bluetooth'tu).
- Build uyarısız, COM3'ten flash, açılış OK (`v1.0.0-2-gbb560ec`). Adaptör o an kapalıydı (`reason=201` AP yok); `reconnect_test.ps1` (RTS ile donanım reset = FIN'siz kopma, 4 tur) adaptörü bekliyor.

---

## 2026-10-06 — WiFi: zaman aşımı payı + adaptör başına voltaj kalibrasyonu

**Belirti:** WiFi zor bağlanıyor; "Connected" olup veri hiç/kısmen gelmiyor. BLE hızlı. Voltaj: WiFi klon 14.x, BLE klon ~16 V (aynı araç).

**Teşhis (kod):** canlı PID zaman aşımı 250 ms. K-line yanıtı BLE'de bile ~150–250 ms; WiFi köprüsünün gecikmesi eklenince yanıtlar zaman aşımına düşüyor, geç gelenler atılıyor. 3 zaman aşımında yanıt sayısı eki (`010C1`) kapanıyor → her yanıt +200 ms (ATST) → kısır döngü.

- `elm327.c`: init'te AT komutlarının gidiş-dönüşü ölçülür; **yalnız WiFi'de** pay = 1.5×RTT + 100 ms (150–800 ms), tüm kuyruk zaman aşımlarına ve prompt beklemesine (+pay/2) eklenir. Ölçüm yoksa 800 ms. BLE'de pay 0 (davranış aynı). `elm327_timeout_margin_ms()`.
- `obd_pids.c`: pending/ATRV/batch/keşif kontrolleri aynı payı ekliyor (çift sorgu olmasın).
- **Voltaj kalibrasyonu** (`obd_volt_cal_get/set`): ham × çarpan (0.70–1.30), BLE ve WiFi için ayrı NVS anahtarı (`obd_volt/cal_ble`, `cal_wifi`). Değişince filtre sıfırlanır, ekran hemen güncellenir.
- Settings: **alttaki bilgi yazısına dokun → BATTERY CALIBRATION** penceresi: düzeltilmiş değer, ham değer + kaynak + çarpan, −0.1 / RESET / +0.1, DONE. Açıkken 0.5 sn'de bir canlı yenilenir.
- Build uyarısız, COM4'ten flash, açılış tek sefer. **Araçta doğrulama bekliyor.** Not: PC simülatörü önceki DTC değişikliğinden beri derlenmiyor (`main/obd` include yolu + DTC kaynakları vcxproj'da yok) — ayrı iş.

---

## 2026-10-05 — WiFi kararlılığı: kontak kapat/aç sonrası PIDS'te kopma

**Belirti:** WiFi adaptör ilk seferde bağlandı; araç kapatılıp açılınca LINK → ELM → PIDS'e kadar gelip bağlantı gidip geliyordu. Cihazda log alınamadı (yalnız COM4 bağlıydı); kodla teşhis.

- **Keepalive 5/2/3 → 15/5/3** (`wifi_obd.c`): bazı klon TCP yığınları keepalive yoklamasına cevap vermiyor; 0100 beklemesinde (6–12 sn sessizlik) oturum ~11 sn'de bizim taraftan öldürülüyordu.
- **Uygulama seviyesi bekçi:** gönderimden sonra 15 sn hiç bayt gelmezse TCP yeniden kurulur. Hiç veri alınamadan biten 2 oturumda WiFi baştan bağlanır (ESP'nin elektriği kesildiğinde adaptörde kalan eski oturum / takılmış köprü için).
- **Kopma sebebi ekranda:** "Adapter closed link", "No reply from adapter", "WiFi lost, rejoining", "Link error, retrying".
- `elm327.c`: init sırasında gönderim hatası ERROR durumunda kalıcı takılıyordu → 1 sn sonra init baştan.
- Build uyarısız, COM4'ten flash, açılış tek seferde (ROM banner). **Araçta doğrulama bekliyor.**

---

## 2026-10-05 — README yeniden yazıldı + GitHub bulunabilirliği

- `README.md` baştan yazıldı (İngilizce + Türkçe özet): BLE/WiFi adaptör desteği, ekranlar, donanım, kurulum, Kconfig seçenekleri, mimari şeması, PID tablosu, sorun giderme, anahtar kelimeler. Arama motorları için başlık ve ilk paragraf "ESP32-S3 OBD2 dashboard / ELM327 BLE & WiFi" ifadelerini içeriyor.
- GitHub repo açıklaması ve konu etiketleri (topics) güncellendi (`gh repo edit`).
- `LICENSE` (MIT) eklendi.
- **GitHub Pages** sitesi: `docs/index.html` (SEO meta, Open Graph, schema.org `SoftwareSourceCode` JSON-LD, açık/koyu tema), `docs/_config.yml` (`jekyll-sitemap`, `jekyll-seo-tag`), `docs/robots.txt` → `https://erdemerciyas.github.io/ESP32-S3-OBD2/` repo "homepage" olarak ayarlandı.
- **Sürüm v1.0.0**: tek dosya `esp32s3-obd2-dashboard-merged.bin` (`0x0`'a yazılır) + ayrı bootloader/partition/app dosyaları. README'ye "hazır firmware'i yükle" bölümü.
- `docs/` SVG↔PNG dönüştürücü taslakları (`IMPLEMENTATION_GUIDE.md`, `QUICK_REFERENCE.md`, `SVG_PNG_CONVERTER_PLAN.md`), `.freebuff/`, `skills-lock.json` eklendi. `.claude/` (üçüncü taraf skill kopyaları + oturum kilidi) `.gitignore`'a alındı; skill'ler `skills-lock.json`'dan yeniden kurulabilir.

---

## 2026-10-05 — WiFi ELM327 adaptör desteği (BLE'ye ek)

**Neden:** Elimizde ELM327 klonunun WiFi modeli de var.

### Araştırma (klon WiFi adaptörleri)
- Adaptör **AP** yayınlar, genelde şifresiz: `WiFi_OBDII`, `OBDII`, `OBD2`, Vgate'te `V-LINK`. ELM **192.168.0.10:35000** üzerinde şeffaf TCP↔UART köprüsü (içeride XLW/USR/HLK modülleri, ELM tarafı 38400 baud).
- **Tek TCP istemcisi**: telefon uygulaması bağlıysa cihaz bağlanamaz.
- Bazı klonlarda DHCP bozuk → uygulamalar statik IP öneriyor. Bağlantı kopmaları yaygın.

### Kod
- `obd/obd_link.c/h`: taşıma soyutlaması. `elm327.c` artık `obd_link_*` kullanıyor; BLE ve WiFi aynı ayrıştırıcıyı paylaşıyor. Seçim NVS'de (`obd_link/type`); aynı anda yalnız seçilen radyo yığını çalışır. Geçiş **yeniden başlatmadan**, ayrı görevde: eskisi durur (`ble_obd_stop` = `nimble_port_stop/deinit`, kontrolcü dahil; `wifi_obd_stop` = TCP kapat + `esp_wifi_stop`), yenisi başlar ve hemen aramaya geçer.
- `obd/wifi_obd.c/h`: STA → tarama (SSID'de `OBD/ELM/V-LINK/VLINK/ICAR/VGATE/KONNWEI`, en güçlü RSSI) → katılım (kayıtlı SSID NVS'de; 2 hatada taramaya döner) → DHCP 5 sn, yoksa adaptör alt ağında **statik `.123`** → TCP (sırasıyla DHCP ağ geçidi, 192.168.0.10). `TCP_NODELAY`, keepalive 5/2/3, `WIFI_PS_NONE` (modem-sleep istek başına ~100-300 ms ekliyordu). RX ayrı görevde (lwIP full-duplex), TX mutex'li. 3 TCP hatasında WiFi baştan; geri çekilme 1.5→10 sn; `auto_connect` kapalıysa dokunuş bekler.
- Kconfig `OBD adapter link`: varsayılan taşıma, sabit SSID / şifre / IP / port (35000).
- `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` (WiFi/LWIP tamponları PSRAM'de).
- UI: **Settings → "Link" karosu (BLE / WiFi)** — dokununca geçer ve seçilen aramaya başlar. Settings 3+2 düzene geçti (karo Ø100: Units · Link · Auto / Centre · Profile); Auto simgesi `LOOP`. Connect çekirdeği yalnız tarar, simgesi seçili taşımayı canlı gösterir.
- Düzeltme: ilk sürümde geçiş Connect çekirdeğinde LVGL'in 400 ms uzun basmasına bağlıydı → normal dokunuş cihazı yeniden başlatıyordu (NVS'de `obd_link:type` yazılmış olmasından teşhis edildi). Geçiş Settings'e taşındı, yeniden başlatma kaldırıldı.
- Doğrulama: geçici test görevi 4 tur WiFi⇄BLE geçti — her geçiş 5–15 ms, çökme yok; WiFi durumunda boş iç RAM her turda 112.6 KB'ye dönüyor (sızıntı yok), BLE durumunda ~69–75 KB.
- Düzeltme (`ble_obd.c`): kayıtlı adrese bağlanma sürerken dokununca `ble_gap_disc` EBUSY dönüyor, `s_scan_active` takılı kalıp yeniden bağlanma tamamen duruyordu. Şimdi bekleyen bağlantı iptal edilir ve iptal olayı taramayı başlatır; tarama başlatılamazsa durum sıfırlanıp yeniden denenir.

### Doğrulama
- Uyarısız build. Cihazda BLE modu eskisi gibi açılıyor. WiFi modu (geçici varsayılan ile) açıldı: `ps type: 0`, 12-15 AP taranıp "No OBD access point" + geri çekilme — çökme yok. **Gerçek WiFi adaptörle araçta test bekliyor.**

---

## 2026-10-05 — Yuvarlak panel UI yenilemesi + açılış animasyonu

**Neden:** Live Data ekranının yuvarlak dili (dış halka + merkez lens) diğer ekranlara taşınsın; kare/düz kart düzenleri yuvarlak maskede kesiliyordu. Açılışa gösterge paneli tarzı animasyon.

### Ortak (`theme.c/h`)
- `UI_C`, `UI_RING_D` (444), `UI_RING_ROT` (120), `UI_RING_SWEEP` (300): 6 yönündeki 60° boşluk nokta navigasyonuna ayrılmış ortak halka.
- `theme_create_root` (sekme flex'ini yok sayan 460×460 kök), `theme_create_arc`, `theme_apply_lens` (cam disk), `theme_place_in_ring` (kutuyu açıdan bağımsız halkanın içine oturtur).

### Ekranlar
- **Live Data** (`screen_grid.c`): dış halkada desteklenen her PID için segment + isim/değer çipi; merkezde seçili metrik (270° yay, oturum min/max). Dokun → sonraki, çipe dokun → seç, uzun bas → min/max sıfırla. Trim'ler merkezden dolar. Ortak yardımcılara taşındı.
- **Connect**: 4 aşamalı halka (SCAN · LINK · ELM · PIDS) — tamamlanan yeşil, aktif nabız atar, hatalı kırmızı. Radar halkasında dönen tarama (yalnız açı aralığı yeniden çizilir). Ortadaki Bluetooth çekirdeğine dokununca tarar (eski düğme kaldırıldı).
- **Gyro**: sol halka pitch, sağ halka roll (±45°, merkezden); merkezde dönen/kayan ufuk çizgisi + sabit araç işareti; EMA yumuşatma, tam derece adımları; değer/renk yalnız değişince güncellenir. SIFIRLA üstte.
- **Settings**: 2×2 yuvarlak karo (Birim · Otomatik bağlan · Merkez gösterge · Profil). Profil açılır listesi yuvarlak ekrana taşmasın diye dokunarak döngüye çevrildi. Altta 3 satır bağlantı özeti (yalnız değişince çizilir). Başlangıç durumları artık `vehicle_data`'dan okunuyor.
- **Açılış** (`screen_splash.c`): dış halka çizilir → 270° ibre 0 → max → 0 süpürür (cyan→turuncu→kırmızı, kırmızı bölge) → OBD2 yazısı harf aralığı daralarak belirir → profil adı + canlı açılış durumu → arka plana kararır, UI üst katmandaki perdeyle yumuşakça açılır. Toplam ~2.95 sn; her stil yalnız değişince uygulanır.

### Doğrulama
- `idf.py build` uyarısız. Halka/çip/etiket yerleşimi 1–12 PID için çakışma ve kenar boşluğu açısından sayısal kontrol edildi. Simülatör derlenemedi (Visual Studio yok) — görsel kontrol cihazda.

---

## 2026-10-05 — Arıza kodu (DTC) tarama sistemi + Supernova UI

**Neden:** Araçta arıza var mı, kod neyi ifade ediyor (Türkçe), geçmiş flash'ta tutulsun, kodlar silinebilsin; Kalos'a özel kod tablosu; yavaşlama/çökme olmasın.

### OBD (`main/obd/obd_dtc.c/.h`, yeni)
- Tarama obd_poll görevinde adım adım: `0101` (MIL, kod sayısı, hazırlık monitörleri) → `03` kayıtlı → `07` bekleyen → `020200` donmuş veri kodu → `02xx00` (devir, hız, su, yük, STFT/LTFT, MAP) → `0121`/`0131` (km, destekleniyorsa).
- Çalışırken PID polling duraklar; ELM kuyruğu boşalmadan komut gönderilmez (K-line'da tek komut uçuşta). Geç yanıtlar sıra numarasıyla elenir; her adımda watchdog.
- Bağlantıdan 8 sn sonra otomatik tarama; sonra 60 sn'de bir yalnız `0101` — MIL/sayı değişirse tam tarama.
- Silme: `04` → 1.5 sn bekle → yeniden tarama ile **doğrulama** (ECU reddederse "silinemedi").
- `07` timeout verirse o bağlantıda atlanır. K-line 7 baytlık çerçeve, CAN sayı baytı ve ATS1 klonları ayrıştırılır.
- Geçmiş NVS'de (`obd_dtc/hist`, 24 kod): ilk/son tarama no, görülme sayısı, silindi bayrağı. Yalnız değişiklik olunca yazılır (temiz → temiz yazmaz). Açılışta son tarama kayıttan gösterilir.
- `elm327.c`: yalnız mode 02 yanıt token'ı (`42`) 4 kontrole eklendi; kuyruk/zaman aşımı mantığı değişmedi.

### Veritabanı (`main/data/dtc_db.c/.h`, yeni)
- ~190 Türkçe açıklama + olası neden + önem (bilgi/uyarı/kritik). Genel SAE P0/P2 ve GM-Daewoo P1xxx (2004 Aveo T200 GM servis kılavuzu DTC dizini — Kalos ile aynı ECU ailesi). Tabloda olmayan kodlara grup açıklaması; C/B/U için sistem açıklaması.

### UI (`main/ui/screen_dtc.c`, yeni; `ui.c/h`, `theme.c/h`, `screen_dash.c`)
- Supernova düzeni: ışın halkası (tek nesne, özel çizim), iki hale, ilerleme yayı, gradyanlı çekirdek (kod sayısı / %ilerleme). Renk durumu anlatır: cyan tarıyor, yeşil temiz, sarı bekleyen, turuncu kayıtlı, kırmızı kritik/MIL.
- Şok dalgası halkaları yalnız tarama sırasında; kartlar yalnız kodlar değişince yeniden kurulur. LVGL çekirdek 1, OBD çekirdek 0.
- Kod kartı → detay (açıklama, neden, donmuş veri, kayıt); GEÇMİŞ ekranı (kaydı silme onaylı); SİL onay penceresi (kontak açık/motor kapalı uyarısı). Çekirdeğe dokunmak da tarar.
- Dash: shift ışıklarının altında `⚠ N ARIZA` göstergesi, dokununca DTC sekmesi.
- Türkçe fontlar `lv_font_tr_16/20`: yalnız ç ğ ı İ ö ş ü glifleri, geri kalanı fallback ile yerleşik Montserrat (lv_font_conv, LVGL'in Montserrat-Medium.ttf'i).

### Açık
- Araçta test: otomatik tarama logu, `03` çok çerçeveli yanıt, silme doğrulaması, donmuş veri desteği.
- CAN çok çerçeveli (`0:`/`1:` önekli) DTC yanıtı elm327 katmanında birleştirilmiyor — Kalos K-line olduğundan etkisiz.

---

## 2026-10-05 — Voltaj sabit 16.5 V: üst sınır + ham değer teşhisi

**Neden:** ATRV geldikten sonra ekranda sabit 16.5 V. `VOLTAGE_MAX_V` 16.5 idi; üstündeki okumalar atılıyor, sınıra yakın kabul edilenler ekranda kalıyordu. Adaptör kalibrasyonu mu yoksa gerçek aşırı şarj mı ayırt edilemiyordu.

- `obd_pids.c`: `VOLTAGE_MAX_V` 16.5 → 18.0; ham okuma + kaynak (`ATRV`/`0142`) `obd_link_stats_t.volt_raw/volt_src`'ye yazılıyor (aralık dışı ham ATRV dahil).
- `vehicle_data.c`: aşırı gerilim seviyeleri — >15.0 V uyarı, >15.8 V kritik.
- `screen_settings.c`: bilgi satırında `14.2V (ATRV 14.23)` biçiminde ham değer.
- Build + flash (COM3) OK, açılış temiz. Not: build diğer oturumların commit edilmemiş değişikliklerini de (DTC, TR fontlar, settings yeniden tasarımı) içeriyor.
- **Açık:** motor kapalı/çalışır ham değerlere göre kalibrasyon katsayısı veya alternatör kontrolü.

---

## 2026-10-05 — Voltaj boş geliyordu: 0142 timeout → ATRV düşüşü

**Neden:** Araçta UI sorunsuz, ancak voltaj hep `--`. K-line ECU (Kalos) desteklenmeyen `0142`'ye NO DATA yerine negatif yanıt veriyor / yanıt vermiyor → ELM timeout; kod yalnız re-probe timeout'unda ATRV'ye dönüyordu, ilk `0142` timeout'unda sonsuza dek `0142` tekrarlanıyordu.

- `obd_pids.c`: herhangi bir `0142` timeout'u → kalıcı ATRV (`s_voltage_via_pid`, `s_pid42_dead`; bağlantı başına sıfırlanır), ölü 0142 için 30 sn re-probe yapılmaz.
- `ATRV_TIMEOUT_MS` 150 → 300 ms (BLE + klon gecikmesine pay).
- Build OK; COM4 (CH343 UART portu) üzerinden flash edildi, açılış OK. Araçta doğrulama bekliyor.

---

## 2026-10-05 — UI yeniden yapımı: senkron RPM/Speed + performans + desteklenen PID'ler

**Neden:** RPM ve Speed tam senkron ve akıcı görünmeli; ekran yalnız aracın desteklediği verileri (yağ dahil) göstermeli; çizim yükü düşmeli.

### Dashboard (`main/ui/screen_dash.c`, `theme.h`)
- **Hareket motoru:** her örnek, ölçülen örnek aralığı (EMA, 60–600 ms) boyunca doğrusal segment başlatır; RPM ve Speed aynı kare saatinde ilerler → basamaksız, eşit gecikmeli hareket (sabit 80 ms ease-out kaldırıldı).
- **Çift okuma:** büyük değer (94 px) + ikincil değer (48 px, cyan); çift dokunma yer değiştirir. Yay her zaman takometre.
- **Sabit aralıklı rakamlar:** hane başına etiket → rakamlar kaymaz, yalnız değişen hane yeniden çizilir.
- **İbre kaldırıldı:** yerine yay ucunda beyaz nokta (arc knob) — geniş bölge invalidation'ı bitti. Kadran: 1000 rpm'de numaralı majör, 500'de minör tick (statik).
- Shift-light'lar yalnız durum değişince güncellenir; redline'a göre (%70→%100).
- Alt kartlar: Coolant · Oil (yalnız PID 0x5C destekleniyorsa) · Battery. Üstte protokol + istek/sn.
- Veri 3 sn bayatsa / bağlantı yoksa `--` ve sönük renk.

### Live Data (`main/ui/screen_grid.c`)
- 12 metrik (Throttle, Load, MAP, Intake, Oil, Timing, MAF, STFT, LTFT, O2 S1/S2, Fuel); **yalnız desteklenenler** gösterilir (flex-wrap yeniden akar), her kartta aralık çubuğu.

### Veri / polling
- `oil_temp` (PID 0x5C) eklendi: decode + filtre, slow listede 2 sn, dash'te de destekleniyorsa sorgulanır.
- `should_poll_pid`: yalnız dash canlı PID'leri koşulsuz; diğer "priority" PID'ler artık desteklenmiyorsa sorgulanmıyor (K-line'da boşa timeout slotu yakıyordu).

### Performans
- `CONFIG_LV_DISP_DEF_REFR_PERIOD` 30 → **16 ms** (~60 FPS).
- Dokunmatik (`lvgl_v8_port.cpp`): kesme bir kez görüldükten sonra yalnız kesmede/basılıyken I2C okunur (CST816 uykudayken 30 ms'de bir başarısız okuma + log yağmuru bitti). Kesme hiç gelmezse eski davranış.
- `ui_show_dash()` aktif sekmeyi de günceller (bağlanınca dash güncellenmiyordu).

### Doğrulama
- Build + flash OK; geçici snapshot firmware'i ile cihazdan dash/grid ekran görüntüsü alındı (debug kodu kaldırıldı). Simülatör: `demo_feed.c`/`vehicle_data_sim.c` güncellendi (Visual Studio yok, derlenmedi).

---

## 2026-10-05 — K-line hız optimizasyonu (Faz 0+1+2) + bağlantı teşhisi

**Neden:** Kalos 2005 K-line (KWP2000) kullanıyor; her istek sonrası ATST (~200 ms) bekleniyor, `ATSP0` araması her bağlantıda saniyeler sürüyor ve ilk `0100` 2 sn'de timeout olup aramayı kesiyordu. Çoklu-PID (`010C0D`) K-line'da desteklenmiyor.

### ELM327 (`main/obd/elm327.c/.h`)
- **Prompt kapısı:** yeni komut, önceki komutun `>` prompt'u gelmeden gönderilmez (en çok 100 ms) — meşgul ELM'e karakter gönderip isteği `STOPPED` ile kesme riski kalktı.
- **Serbest AT yanıtları:** `ATDPN` (`A5`), `ATPPS` gibi yanıtlar artık yakalanıyor ve `>` ile teslim ediliyor (eskiden timeout oluyordu).
- **Protokol önbelleği:** `ATDPN` sonucu NVS'ye (`obd_elm/proto`) yazılıyor; profil `ATSP0` ise sonraki init `ATSPA<n>` kullanıyor (önce bilinen protokol, olmazsa otomatik arama).
- Her TX logu INFO → DEBUG. Teşhis sayaçları: `elm327_done_count()`, `elm327_timeout_count()`.

### PID polling (`main/obd/obd_pids.c`, `main/data/vehicle_profile.c`)
- **Yanıt sayısı eki (`010C1`):** `0100` tek ECU yanıtı verirse açılır; ELM ATST beklemeden döner. 3 ardışık timeout'ta kendiliğinden kapanır.
- **İlk `0100` timeout:** 2 sn → 12 sn (protokol önbellekteyse 6 sn); arama sürerken kuyruğa ATRV yığılmıyor.
- **Çoklu-PID probe:** protokol CAN değilse hiç denenmiyor.
- **Slot dağılımı:** Coolant 150 → 1000 ms, voltaj (ATRV) 200 → 1000 ms; K-line kapasitesinin çoğu RPM/Speed'e gidiyor.
- Bağlantı sonrası `ATDPN` + `ATI` sorgulanıyor.

### Teşhis (`main/data/vehicle_data.*`, `main/ui/screen_settings.c`, simülatör)
- `obd_link_stats_t` (istek/sn, RPM Hz, timeout, protokol, ELM kimliği, x1 eki) snapshot'a eklendi.
- Settings bilgi kartı: `Protocol: A5 (x1)`, `Link: 6.8 req/s  RPM 3.2 Hz  TO 0`. Seri portta 5 sn'de bir `link ...` logu.

### Build / Flash
- Build başarılı **0x13ae00**; COM3'e flash edildi, açılış ve BLE bağlantı denemesi doğrulandı. Araçta PID testi henüz yapılmadı.
- Not: açılışta `CST816S: I2C read failed` hatası sürekli loglanıyor (dokunmatik denetleyici); bu değişiklikle ilgisiz, önceki sürümde olup olmadığı doğrulanmadı.

---

## 2026-07-07 — Proje temizliği (referans artıkları + geçici dosyalar)

**Neden:** Repo ~3500+ gereksiz dosya içeriyordu (referans projeler, yedek snapshot'lar, build logları, otomatik wiki); firmware/simülatör dışı her şey kaldırıldı.

### Silinenler (git'ten)

- `_ref_download/`, `_ref_tdisplay2/` — SquareLine/PlatformIO referans projeleri (~1500 dosya)
- `_restore_jun10/` — Haziran yedek snapshot'ları (40 dosya)
- `_ref_lib_tree.json`, `_ref_src_tree.json` — referans ağaç dökümleri
- `simulator/Screenshot.png` — eski doğrulama ekran görüntüsü
- `main/ui/lv_font_montserrat_72_bold.c` — kullanılmayan font (tema 94 px kullanıyor)
- `scripts/revert_jun11.py`, `scripts/revert_ba3bbf10.py`, `scripts/extract_writes.py`, `scripts/extract_old_strings.py` — tek seferlik geri alma araçları

### Silinenler (yerel, untracked)

- `.qoder/` — otomatik üretilmiş wiki
- `.cache/`, `build/` — önbellek ve derleme çıktısı
- `bf.bat`, `build_esp32.bat`, `build_flash.bat`, `_flash_now.bat`, `_flash_now.ps1` — yinelenen build scriptleri (`rebuild.bat` korundu)
- `build_out*.txt`, `build_err*.txt`, `build_sync*.txt`, `build_flash_out*.txt` — build log dökümleri
- `simulator/Screenshot_*.png`, `simulator/Output/` — yerel doğrulama görüntüleri ve MSBuild çıktısı
- `tools/` — boş klasör (font TTF'leri zaten derlenmiş `.c` dosyalarında)

### Korunanlar

- `main/` — firmware
- `simulator/` — PC simülatörü (LVGL + `obd2_dashboard/`)
- `scripts/verify_round_lcd_layout.py` — layout doğrulama
- `rebuild.bat` — tek build/flash scripti
- `docs/GELISTIRME_KURALLARI.md`, `.cursor/rules/`

### `.gitignore` güncellendi

- `.qoder/`, `.cache/`, `simulator/Output/`, `build_*.txt`, `_ref_*/`, `_restore_*/`, `simulator/Screenshot*.png`

---

## 2026-07-07 — Dashboard görsel yenileme geri alındı

**Neden:** Kalın arc / cyan bezel / büyük data pill tasarımı beğenilmedi; bir önceki dashboard görünümüne dönüldü.

- `theme.h`, `theme.c`, `screen_dash.c`: arc 18→8 px, dış halka/glow/profil rozeti/cyan metin/48 px data fontu kaldırıldı; glass pill ve orijinal layout geri yüklendi.
- OBD düzeltmeleri (`ble_obd.c`, `obd_pids.c`, `elm327.c`) **korundu**.

---

## 2026-07-07 — BLE otomatik bağlantı + Temp/Volt starvation + volt re-probe + RPM/Speed batch + performans UI

**Neden:** Otomatik bağlantı bazen kurulmuyor/çok geç kuruluyor (elle tetikleme gerekiyordu), RPM/Speed gecikmeli ve Temp/Volt aralıklı/hiç gelmiyordu.

### BLE bağlantı güvenilirliği (`main/obd/ble_obd.c`)

- **NVS MAC artık silinmiyor:** connect timeout / connect-fail durumunda kayıtlı adres korunuyor. Ardışık `DIRECT_FAIL_MAX (3)` hatadan sonra geçici `s_prefer_scan` ile scan'e düşülüyor (scan adaptörün yayında olduğunu doğrular), adres korunuyor. Başarılı GATT'ta sayaçlar sıfırlanıp direkt bağlantıya geri dönülüyor.
- **GATT keşfi watchdog'u:** `BLE_GAP_EVENT_CONNECT` sonrası watchdog iptal edilmeyip `GATT_DISCOVERY_TIMEOUT_MS (6s)` ile yeniden başlatılıyor; keşif asılırsa terminate + reconnect. `start_connect_watchdog(timeout_ms)` parametreli hale getirildi, `s_gatt_phase` bayrağı eklendi.
- **Scan backoff:** adaptör bulunamayınca artan gecikme (üst sınır `BACKOFF_MAX_MS 10s`), `s_scan_fail_count`; eşleşme bulununca sıfırlanıyor.

### PID polling — Temp/Volt starvation (`main/obd/obd_pids.c`)

- `run_dash_poll`: Tier 2 (Coolant `0x05` + Voltage) artık RPM/Speed'e kapı KOYMUYOR. 60 ms'lik RPM/Speed turları Temp/Volt'u aç bırakıyordu; her PID zaten kendi `poll_due` + `pending` ile sınırlı, kuyruk sığ kalıyor.

### Voltage re-probe kurtarma (`main/obd/obd_pids.c`)

- **Bug:** ATRV modunda periyodik `0142` re-probe `s_use_atrv=false` yapıyor; o `0142` timeout olursa mod ATRV'ye dönmüyor, adaptör kalıcı `0142` timeout döngüsüne girip voltaj hiç gelmiyordu.
- **Fix:** `s_reprobe_inflight` bayrağı eklendi; `poll_voltage` timeout'unda re-probe uçuştaysa `s_use_atrv=true`'ya geri dönülüyor. Bayrak her iki callback'te ve disconnect reset'inde temizleniyor.

### Filtre notu

- EMA konvansiyonu `filtered += alpha*(raw-filtered)` → yüksek alpha = hızlı tepki. RPM 0.90 / Speed 0.94 zaten hızlı; algılanan gecikme filtreden değil polling starvation'ından kaynaklanıyordu (yukarıda düzeltildi). Filtre değerleri değiştirilmedi.

### RPM+Speed batch — tek komut senkron okuma (`main/obd/obd_pids.c`, `main/obd/elm327.c`)

- **`elm327.c` expect-token:** çoklu-PID komutunda (`010C0D`) yalnızca ilk PID (`410C`) eşleştiriliyor; aksi halde `410C..0D..` yanıtı "stale" diye atılıyordu. Tek-PID davranışı değişmedi.
- **`obd_pids.c` batch:** `010C0D` ile RPM+Speed tek BLE round-trip'te senkron geliyor. **Varsayılan kapalı**; başarılı probe sonrası açılıyor, 3 başarısızlıkta (parse/timeout) kalıcı olarak ayrı polling'e dönüyor (regresyonsuz). Batch yanıtı sentetik tek-PID dizesine çevrilip mevcut `update_pid_value()` decode+filtre yolunu tekrar kullanıyor.

### UI — performans tako (`main/ui/screen_dash.c`, `main/ui/theme.h`)

- Majör tick'ler arasına **minör tick** graduation eklendi (250 rpm'de bir, `TICK_MINORS_PER_INTERVAL=4`); dairesel ekranda yoğun spor-tako görünümü.
- Majör tick'ler belirginleştirildi: kalınlık 2→3, opaklık %60→tam, boy 6→10 px; minör tick'ler 5 px / %40 dim.
- Not: Gauge animasyonu zaten 80 ms (snappy) — algılanan RPM/Speed gecikmesi animasyondan değil polling starvation'ından geliyordu (Faz 2'de düzeldi).

### Teşhis (`main/obd/ble_obd.c`)

- GATT hazır olduğunda **bağlantı süresi (ms)** loglanıyor; GATT/connect timeout faz bilgisiyle, batch fallback ve voltage revert olayları loglanıyor — "neden geç bağlandı / veri gelmedi" sahada izlenebilir.

### Build

- **Build:** Başarılı — `obd2_dashboard.bin` **0x13a610** (~1.29 MB), partition'da **%59** boş.
- **Flash:** COM3 üzerinden ESP32-S3'e başarıyla yüklendi; MAC `dc:b4:d9:23:18:04`.

---

## 2026-07-06 — ELM327 desync düzeltmesi + DTC tamamen kaldırıldı

**Neden:** Cihazda BLE ELM327'ye geç bağlanıyor, RPM/Speed anlık gelip donuyordu; DTC sekmesi yeniden eklenmişti ve PID polling'i duraklatıyordu.

### ELM327 komut/yanıt eşleştirme (`main/obd/elm327.c`)

- Her komut için beklenen yanıt token'ı türetiliyor (`010C` → `410C`, `03` → `43`); eşleşmeyen geç yanıtlar atılıyor.
- Timeout sonrası 60 ms flush penceresi: RX buffer + pending + semafor temizliği.
- Init döngüsünde komut başına stale semafor drenajı; sabit 20 ms gecikme kaldırıldı (ATZ 500 ms korundu).

### PID polling (`main/obd/obd_pids.c`)

- `pid_response_cb`: yanlış PID yanıtında `last_poll=0` ile anında yeniden sorgu.
- Filtre: RPM alpha 0.96→**0.90**, Speed spike_max 40→**30**.
- DTC pause/resume (`s_paused`, `obd_pids_pause/resume`) kaldırıldı.

### DTC kaldırma

- Silindi: `main/obd/obd_dtc.c/.h`, `main/ui/screen_dtc.c`, `docs/OBD2_DTC_IMPLEMENTATION.md`
- `main/main.c`, `main/ui/ui.c/.h`, `main/CMakeLists.txt` güncellendi — 5 sekme (Connect/Dash/Grid/Gyro/Settings)

### Build

- **Build:** Başarılı — `obd2_dashboard.bin` **0x139fd0** (~1.26 MB), partition'da **%59** boş.
- **Flash:** COM3 üzerinden ESP32-S3'e başarıyla yüklendi; MAC `dc:b4:d9:23:18:04`.

---

## Mevcut durum (2026-06-23) — arşiv

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
