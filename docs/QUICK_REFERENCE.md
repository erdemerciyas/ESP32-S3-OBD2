# SVG ↔ PNG Dönüştürücü — Hızlı Referans

## 🎯 Tek Bakışta Özet

| Özellik | Değer | Not |
|---------|-------|-----|
| **SVG → PNG Hedefi** | < 100ms | 480×480 için |
| **PNG → SVG Hedefi** | < 500ms | 640×640 için |
| **Bellek** | < 2MB | PSRAM dahil |
| **Kalite** | PSNR > 40dB | Referans SVG ile |

---

## 🛠️ Önerilen Kütüphaneler

### SVG → PNG
| Kütüphane | Öncelik | Kullanım |
|-----------|---------|----------|
| **NanoSVG** | 🥇 Birincil | Tek header, minimum bellek |
| **resvg** | 🥈 Alternatif | Daha iyi kalite, Rust |

### PNG → SVG
| Kütüphane | Öncelik | Kullanım |
|-----------|---------|----------|
| **VTracer** | 🥇 Birincil | Renk desteği, Rust |
| **Potrace** | 🥈 Alternatif | Sadece siyah-beyaz |

### ESP32 Runtime
| Kütüphane | Öncelik | Kullanım |
|-----------|---------|----------|
| **ThorVG** | 🥇 Birincil | ESP-IDF component |
| **LVGL SVG** | 🥈 Entegrasyon | lv_svg decoder |

---

## 🚀 Hızlı Başlangıç

### Build (Host)
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
```

### Kullanım
```bash
# SVG → PNG
./converter svg2png input.svg -o output.png -w 480 -h 480

# PNG → SVG  
./converter png2svg input.png -o output.svg --color
```

### ESP32 Entegrasyonu
```c
#include "converter.h"

// Runtime SVG rendering
lv_img_dsc_t *img = thorvg_get_cached_svg(
    svg_data, svg_size, 480, 480
);
lv_img_set_src(widget, img);
```

---

## 📁 Dizin Yapısı (Özet)

```
converter/
├── include/
│   └── converter.h          # Ana API
├── src/
│   ├── svg2png/             # SVG → PNG motoru
│   ├── png2svg/             # PNG → SVG motoru
│   ├── common/              # Ortak fonksiyonlar
│   └── cli.c                # CLI arayüzü
├── tests/                   # Testler
└── docs/                    # Dokümantasyon
```

---

## 🎨 Renk Desteği

| Format | Bits | Kullanım |
|--------|------|----------|
| RGB565 | 16 | ESP32 LCD (ana) |
| RGB888 | 24 | Yüksek kalite |
| RGBA8888 | 32 | Şeffaflık |
| Mono | 1 | Siyah-beyaz |

---

## ⚡ Performans İpuçları

1. **PSRAM kullan**: 16KB+ buffer'lar için zorunlu
2. **Scale factor**: 1.0正常, 2.0 supersampling
3. **Quality**: 80-90 arası optimal
4. **Cache**: Sık kullanılan SVG'leri önbelleğe al

---

## 🔧 Yaygın Sorunlar

| Sorun | Çözüm |
|-------|-------|
| Bellek hatası | `use_psram=true` |
| Düşük kalite | `quality=95`, `scale=2.0` |
| Yavaş render | `backend=nanosvg` |
| Büyük dosya | `filter_speckle=6` |

---

## 📚 Temel API'ler

```c
// SVG → PNG
converter_error_t svg2png_convert(
    const uint8_t *svg_data, size_t svg_size,
    const svg2png_config_t *config,
    uint8_t **png_output, size_t *png_size,
    progress_callback_t progress
);

// PNG → SVG
converter_error_t png2svg_convert(
    const uint8_t *png_data, size_t png_size,
    const png2svg_config_t *config,
    char **svg_output, size_t *svg_size,
    progress_callback_t progress
);

// Varsayılan config
svg2png_config_t svg2png_default_config(void);
png2svg_config_t png2svg_default_config(void);
```

---

*Hızlı Referans v1.0 | 18 Ağustos 2026*
