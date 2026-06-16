# Geliştirme Kuralları

Bu dosya, projede yapılacak yeni geliştirmeler sırasında korunması gereken davranışları ve sınırları tanımlar.

---

## Kural: OBD portu ELM327 ↔ ESP32-S3 Bluetooth iletişimi dokunulmazdır

**Öncelik:** Kritik — diğer tüm geliştirmelerin üstünde.

OBD portuna takılı ELM327 Bluetooth okuyucu, ESP32-S3 ile **sorunsuz iletişim kuruyor ve sorunsuz bağlanıyor**. Bu çalışan durum sonraki tüm süreçlerde **asla kaybedilmemelidir**; regresyona yol açacak değişiklikler yapılmamalıdır.

### Korunan davranışlar

- OBD portundaki ELM327 okuyucunun ESP32-S3 ile Bluetooth üzerinden keşfi ve bağlantısı
- BLE tarama, eşleşme ve otomatik yeniden bağlanma
- Kayıtlı adaptör adresiyle (`NVS`) hızlı bağlantı
- GATT keşfi, bildirim (notify) ve yazma (write) kanalları
- ELM327 komut gönderimi, yanıt bekleme ve kuyruk yönetimi
- Bağlantı durumunun UI ve `vehicle_data` katmanına doğru yansıması

### Dokunulmaması gereken dosyalar

Aşağıdaki dosyalarda yapılacak her değişiklik, **bilinçli ve gerekçeli** olmalı; bağlantı stabilitesi mutlaka doğrulanmalıdır:

| Dosya | Sorumluluk |
|-------|------------|
| `main/obd/ble_obd.c` | BLE tarama, bağlantı, GATT, yeniden bağlanma |
| `main/obd/ble_obd.h` | BLE OBD arayüzü |
| `main/obd/elm327.c` | ELM327 protokolü, komut kuyruğu, yanıt işleme |
| `main/obd/elm327.h` | ELM327 arayüzü |
| `main/ui/screen_connect.c` | Bağlantı ekranı ve kullanıcı akışı |

`obd_pids.c`, `obd_dtc.c` gibi üst katman dosyalarında değişiklik yapılırken de ELM327 komut sırası, zaman aşımı ve öncelik mantığı bozulmamalıdır.

### Yapılırken

- Yeni özellik eklerken BLE/ELM327 katmanını mümkün olduğunca **değiştirmeden** üzerine inşa et.
- Zorunlu değişiklik varsa en küçük diff ile ilerle; davranışı yeniden yazma.
- Değişiklik sonrası gerçek cihazda bağlantı, yeniden bağlanma ve PID okuma testini yap.
- Bağlantıyı etkileyebilecek refactor, stack değişikliği veya NimBLE ayarı yapma.

### Yapılmaz

- “Temizlik” veya “iyileştirme” adıyla çalışan bağlantı kodunu yeniden yazmak
- Zaman aşımı, yeniden bağlanma aralığı veya GATT UUID profillerini keyfi değiştirmek
- ELM327 komut kuyruğu / öncelik mantığını test etmeden değiştirmek
- BLE ile ilgili değişiklikleri UI veya veri katmanı değişiklikleriyle aynı PR’da karıştırmak

---

## Dipnot

> **Durum kaydı (12 Haziran 2026):** OBD portuna takılı ELM327 Bluetooth okuyucu, ESP32-S3 ile sorunsuz iletişim içinde ve sorunsuz bağlanıyor — stabilite doğrulandı. Bu dosyadaki kural, bu çalışan durumun sonraki süreçlerde asla kaybedilmemesi için referans olarak tutulmaktadır. Bağlantı katmanında değişiklik yapılacaksa önce bu kural okunmalı; değişiklik sonrası aynı OBD portu + ELM327 + ESP32-S3 bağlantı testi yeniden yapılmalıdır.
