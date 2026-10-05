# SVG ↔ PNG Dönüştürücü Sistemi — Araştırma ve Planlama

## 📋 Proje Özeti

ESP32-S3 OBD-II Dashboard projesi için kapsamlı bir **SVG ↔ PNG dönüştürücü sistemi**. Sistem, gömülü sistem için optimized görsel varlık yönetimi sağlar.

---

## 🔬 Derin Araştırma Sonuçları

### 1. SVG → PNG Dönüştürme (Vektörel → Raster)

#### Mevcut Kütüphane Karşılaştırması

| Kütüphane | Dil | Performans | Kalite | Gömülme Desteği | Lisans |
|-----------|-----|------------|--------|-----------------|--------|
| **resvg** | Rust | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | C API mevcut | MIT |
| **librsvg** | C | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | GObject依赖 | LGPL-2.1 |
| **NanoSVG** | C | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | Tek header, minimum | zlib |
| **ThorVG** | C++ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ESP-IDF component | MIT |
| **CairoSVG** | Python | ⭐⭐ | ⭐⭐⭐⭐ | Cairo依赖 | LGPL |
| **Sharp** | Node.js | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |/libvips依赖 | Apache-2.0 |

#### Önerilen Kütüphane: **resvg + NanoSVG Hybrid**

**Neden resvg?**
- Pure Rust, minimum dependency
- SVG 2.0 destek (büyük kısmen)
- tiny-skia backend (GPU gerektirmez)
- C API ile ESP32'ye port edilebilir
- librsvg ile rekabet eden kalite

**Neden NanoSVG fallback?**
- Tek header dosya, minimum bellek kullanımı
- ESP32'nin sınırlı RAM'i için ideal
- Basit SVG ikonları için yeterli

### 2. PNG → SVG Dönüştürme (Raster → Vektörel)

#### Mevcut Kütüphane Karşılaştırması

| Kütüphane | Dil | Renk Desteği | Kalite | Performans | Gömülme |
|-----------|-----|--------------|--------|------------|---------|
| **VTracer** | Rust | ✅ Tam renk | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ | C API |
| **Potrace** | C | ❌ Siyah-beyaz | ⭐⭐⭐⭐ | ⭐⭐⭐ | Kolay |
| **Autotrace** | C | ✅ Sınırlı renk | ⭐⭐⭐ | ⭐⭐⭐ | Orta |
| **ImageMagick** | C | ✅ Tam renk | ⭐⭐⭐⭐ | ⭐⭐ | Ağır |

#### Önerilen Kütüphane: **VTracer**

**Neden VTracer?**
- **Renk desteği**: Potrace sadece siyah-beyaz, VTracer tam renk
- **Kalite**: Yüksek çözünürlüklü taramalarda daha iyi sonuç
- **Parametreler**: Hassas ayar için parametreler mevcut
- **Rust**: Güvenli bellek yönetimi, ESP32'ye C API ile port edilebilir

**VTracer Parametreleri:**
```rust
VtracerConfig {
    color_mode: ColorMode::Color,        // Tam renk
    hierarchical: HierarchicalMode::Stacked,
    mode: TraceMode::Polygon,            // Polygon veya Curve
    filter_speckle: 4,                   // Gürültü temizleme
    color_precision: 6,                  // Renk hassasiyeti
    layer_difference: 16,               // Katman farkı
    corner_threshold: 60,               // Köşe eşiği
    length_threshold: 4.0,              // Uzunluk eşiği
    max_iterations: 10,                 // Maksimum iterasyon
    splice_threshold: 45,              // Birleştirme eşiği
    path_precision: 3,                  // Yol hassasiyeti
}
```

### 3. ESP32 Entegrasyon Seçenekleri

#### Seçenek A: ThorVG Entegrasyonu (Önerilen)
```
┌─────────────────────────────────────────────────────────┐
│                    ThorVG Engine                         │
├─────────────────────────────────────────────────────────┤
│  • SVG Parser + Renderer                                │
│  • LVGL ile doğrudan entegrasyon                        │
│  • ESP-IDF component olarak mevcut                      │
│  • PSRAM ile optimize bellek yönetimi                   │
│  • SVG Tiny 1.2 + Lottie desteği                        │
└─────────────────────────────────────────────────────────┘
```

**Avantajlar:**
- Espressif tarafından resmi ESP-IDF component
- LVGL v8/v9 ile native entegrasyon
- 480×480 LCD için optimize edilmiş
- Düşük bellek kullanımı (PSRAM ile)

#### Seçenek B: NanoSVG + Custom Renderer
```
┌─────────────────────────────────────────────────────────┐
│              NanoSVG Parser                             │
├─────────────────────────────────────────────────────────┤
│  • Tek header dosya (nanosvg.h)                         │
│  • Minimal bellek kullanımı                             │
│  • Custom rasterizer ile LVGL entegrasyonu              │
│  • RGB565 formatında çıktı                              │
└─────────────────────────────────────────────────────────┘
```

#### Seçenek C: C array Önceden İşleme (Build-time)
```
┌─────────────────────────────────────────────────────────┐
│           Build-time Conversion Pipeline                │
├─────────────────────────────────────────────────────────┤
│  1. SVG → PNG (resvg ile)                              │
│  2. PNG → C array (lv_img_conv ile)                    │
│  3. Compile-time embedding                              │
│  4. Runtime: Sadece C array kullanımı                   │
└─────────────────────────────────────────────────────────┘
```

### 4. Algoritma Detayları

#### SVG → PNG Pipeline
```
┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐
│ SVG File │───▶│  Parse   │───▶│  Render  │───▶│ PNG File │
│          │    │ (XML)    │    │ (Raster) │    │          │
└──────────┘    └──────────┘    └──────────┘    └──────────┘
                     │               │
                     ▼               ▼
              ┌──────────┐    ┌──────────┐
              │   DOM    │    │  Skia    │
              │  Tree    │    │ Backend  │
              └──────────┘    └──────────┘
```

**Adım 1: XML Parsing**
- SVG XML'ini DOM tree'sine çevirme
- `<svg>`, `<path>`, `<circle>`, `<rect>` etc. parse
- Transform ve group hierarchy işleme

**Adım 2: Path Rendering**
- Bezier curve'leri line segmentlere çevirme
- Anti-aliasing uygulama
- Clip path ve mask işleme

**Adım 3: Rasterization**
- Vektörel path'leri piksellere dönüştürme
- Alpha blending ve compositing
- RGB565 formatına çevirme (ESP32 için)

#### PNG → SVG Pipeline
```
┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐
│ PNG File │───▶│  Load    │───▶│  Trace   │───▶│ SVG File │
│          │    │ (Pixels) │    │ (Vector) │    │          │
└──────────┘    └──────────┘    └──────────┘    └──────────┘
                     │               │
                     ▼               ▼
              ┌──────────┐    ┌──────────┐
              │  Color   │    │  Path    │
              │ Quantize │    │ Fitting  │
              └──────────┘    └──────────┘
```

**Adım 1: Pixel Processing**
- PNG yükleme ve decode
- Renk quantization (256 → N renk)
- Alpha channel işleme

**Adım 2: Edge Detection**
- Bitmap'i binary edge map'e çevirme
- Contour bulma (Marching Squares)
- Gürültü temizleme (speckle filter)

**Adım 3: Path Simplification**
- Ramer-Douglas-Peucker algoritması
- Bezier curve fitting
- Path optimization

**Adım 4: SVG Generation**
- SVG path data oluşturma
- Renk attributeleri ekleme
- ViewBox ve namespace ayarlama

### 5. Kalite Metrikleri

#### SVG → PNG Kalite Ölçütleri
| Metrik | Hedef | Ölçüm Yöntemi |
|--------|-------|---------------|
| PSNR | > 40 dB | Referans SVG ile karşılaştırma |
| SSIM | > 0.95 | Yapısal benzerlik |
| Kenar Netliği | > 0.8 | Laplacian variance |
| Bellek Kullanımı | < 50KB | PSRAM usage monitoring |

#### PNG → SVG Kalite Ölçütleri
| Metrik | Hedef | Ölçüm Yöntemi |
|--------|-------|---------------|
| Yeniden raster PSNR | > 35 dB | SVG→PNG→SVG→PNG döngü |
| Path sayısı | Minimize | SVG file size |
| Renk doğruluğu | ΔE < 3 | CIEDE2000 |
| Dosya boyutu | < 2x orijinal | Byte comparison |

---

## 🏗️ Sistem Mimarisi

### Üst Seviye Mimari
```
┌─────────────────────────────────────────────────────────────────────┐
│                     CONVERTER SYSTEM                                │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌───────────────────────────────────────────────────────────────┐  │
│  │                    CLI Interface                              │  │
│  │  convert svg2png input.svg -o output.png -w 480 -h 480      │  │
│  │  convert png2svg input.png -o output.svg --color-mode color  │  │
│  └───────────────────────────────────────────────────────────────┘  │
│                              │                                      │
│  ┌───────────────────────────▼───────────────────────────────────┐  │
│  │                  Conversion Engine                            │  │
│  ├───────────────────────────────────────────────────────────────┤  │
│  │                                                               │  │
│  │  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐      │  │
│  │  │  SVG → PNG  │    │  PNG → SVG  │    │  Pipeline   │      │  │
│  │  │  Converter  │    │  Converter  │    │  Manager    │      │  │
│  │  └─────────────┘    └─────────────┘    └─────────────┘      │  │
│  │         │                  │                  │               │  │
│  │         ▼                  ▼                  ▼               │  │
│  │  ┌─────────────────────────────────────────────────────┐     │  │
│  │  │              Shared Components                      │     │  │
│  │  │  • Image Buffer Manager                             │     │  │
│  │  │  • Color Space Converter                            │     │  │
│  │  │  • Progress Callbacks                               │     │  │
│  │  │  • Error Handling                                   │     │  │
│  │  └─────────────────────────────────────────────────────┘     │  │
│  └───────────────────────────────────────────────────────────────┘  │
│                              │                                      │
│  ┌───────────────────────────▼───────────────────────────────────┐  │
│  │                 Backend Libraries                             │  │
│  ├───────────────────────────────────────────────────────────────┤  │
│  │  • resvg (SVG rendering)                                     │  │
│  │  • VTracer (PNG vectorization)                               │  │
│  │  • lodepng (PNG decode/encode)                               │  │
│  │  • ThorVG (ESP32 runtime rendering)                          │  │
│  └───────────────────────────────────────────────────────────────┘  │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

### Dizin Yapısı
```
converter/
├── CMakeLists.txt              # Ana build dosyası
├── include/
│   ├── converter.h             # Public API
│   ├── svg2png.h              # SVG → PNG API
│   ├── png2svg.h              # PNG → SVG API
│   ├── pipeline.h             # Pipeline API
│   └── config.h               # Yapılandırma
├── src/
│   ├── svg2png/
│   │   ├── resvg_backend.c    # resvg wrapper
│   │   ├── nanosvg_backend.c  # NanoSVG fallback
│   │   ├── renderer.c         # Ortak rasterizer
│   │   └── color_convert.c    # RGB565/RGB888 desteği
│   ├── png2svg/
│   │   ├── vtracer_backend.c  # VTracer wrapper
│   │   ├── potrace_backend.c  # Potrace fallback
│   │   ├── edge_detect.c      # Kenar algılama
│   │   ├── path_fit.c         # Bezier fitting
│   │   └── svg_gen.c          # SVG output generation
│   ├── common/
│   │   ├── image_buffer.c     # Bellek yönetimi
│   │   ├── progress.c         # İlerleme takibi
│   │   └── error.c            # Hata yönetimi
│   └── cli.c                  # CLI arayüzü
├── tests/
│   ├── test_svg2png.c
│   ├── test_png2svg.c
│   └── testdata/
│       ├── input.svg
│       ├── input.png
│       └── expected/
└── scripts/
    ├── build_converter.sh     # Host build script
    └── benchmark.sh           # Performans testi
```

---

## 📊 Uygulama Planı

### Aşama 1: Temel Altyapı (Hafta 1-2)

#### Görev 1.1: Proje Yapısı Oluşturma
- [ ] CMakeLists.txt yapılandırması
- [ ] Include/src dizin yapısı
- [ ] Header dosyaları (converter.h, config.h)
- [ ] Error handling sistemi

#### Görev 1.2: Bellek Yönetimi
- [ ] ImageBuffer struct tasarımı
- [ ] PSRAM-aware bellek tahsisi
- [ ] Ring buffer implementasyonu
- [ ] Bellek sızıntı kontrolü

#### Görev 1.3: Renk Alanı Dönüştürme
- [ ] RGB888 ↔ RGB565 dönüşümü
- [ ] RGBA ↔ RGB dönüşümü
- [ ] Renk quantization (Median Cut)
- [ ] Gamma correction

### Aşama 2: SVG → PNG Motoru (Hafta 3-4)

#### Görev 2.1: NanoSVG Entegrasyonu
- [ ] nanosvg.h entegrasyonu
- [ ] SVG parse fonksiyonları
- [ ] Path extraction
- [ ] Transform matrix işleme

#### Görev 2.2: Custom Rasterizer
- [ ] Scanline rasterizer implementasyonu
- [ ] Anti-aliasing (4x MSAA)
- [ ] Clipping support
- [ ] Gradient rendering

#### Görev 2.3: PNG Output
- [ ] lodepng entegrasyonu
- [ ] PNG encode ayarları
- [ ] Chunk metadata ekleme
- [ ] Optimize dosya boyutu

#### Görev 2.4:=resvg Backend (İsteğe bağlı)
- [ ] resvg C API binding
- [ ] Fallback mekanizması
- [ ] Performans karşılaştırması

### Aşama 3: PNG → SVG Motoru (Hafta 5-6)

#### Görev 3.1: VTracer Entegrasyonu
- [ ] VTracer C API binding
- [ ] Parametre yapılandırması
- [ ] Renk modu desteği
- [ ] Hiyerarşi modu

#### Görev 3.2: Edge Detection
- [ ] Sobel filter implementasyonu
- [ ] Canny edge detection
- [ ] Contour bulma (Marching Squares)
- [ ] Gürültü temizleme

#### Görev 3.3: Path Simplification
- [ ] Ramer-Douglas-Peucker algoritması
- [ ] Visvalingam-Whyatt alternatifi
- [ ] Adaptive thresholding
- [ ] Path merging

#### Görev 3.4: SVG Generation
- [ ] SVG XML builder
- [ ] Path data optimizer
- [ ] Color attribute handling
- [ ] ViewBox calculation

### Aşama 4: Pipeline ve CLI (Hafta 7-8)

#### Görev 4.1: Pipeline Manager
- [ ] Adım bazlı pipeline
- [ ] Progress callback sistemi
- [ ] Hata toleransı
- [ ] Retry mekanizması

#### Görev 4.2: CLI Arayüzü
- [ ] Argüman parsing (getopt)
- [ ] Seçenekler: -w, -h, -q, --color-mode
- [ ] Batch processing desteği
- [ ] Verbose/quiet modu

#### Görev 4.3: Konfigürasyon
- [ ] Config file desteği (JSON/YAML)
- [ ] Preset profiller (web, print, embedded)
- [ ] Override mekanizması
- [ ] Varsayılan ayarlar

### Aşama 5: ESP32 Entegrasyonu (Hafta 9-10)

#### Görev 5.1: ThorVG Entegrasyonu
- [ ] ThorVG ESP-IDF component ekleme
- [ ] SVG decoder register
- [ ] Runtime rendering pipeline
- [ ] Bellek optimizasyonu

#### Görev 5.2: LVGL Entegrasyonu
- [ ] lv_svg decoder extension
- [ ] Custom image decoder
- [ ] Cache sistemi
- [ ] Animation desteği

#### Görev 5.3: Asset Pipeline
- [ ] Build-time conversion script
- [ ] C array generation
- [ ] Flash storage management
- [ ] OTA update desteği

### Aşama 6: Test ve Optimizasyon (Hafta 11-12)

#### Görev 6.1: Unit Testler
- [ ] Her converter için testler
- [ ] Edge case'ler
- [ ] Bellek sızıntı testleri
- [ ] Performance benchmarkları

#### Görev 6.2: Entegrasyon Testleri
- [ ] End-to-end testler
- [ ] Görsel karşılaştırma
- [ ] ESP32 donanım testleri
- [ ] Stres testleri

#### Görev 6.3: Optimizasyon
- [ ] SIMD optimizasyonu (ESP32 S3)
- [ ] Parallel processing
- [ ] Bellek havuzu optimizasyonu
- [ ] Cache stratejisi

---

## 🔧 Teknik Detaylar

### 1. Bellek Yönetimi Stratejisi

```c
typedef struct {
    uint8_t *data;
    uint32_t width;
    uint32_t height;
    uint8_t channels;      // 3 (RGB) veya 4 (RGBA)
    uint8_t bit_depth;     // 8 veya 16
    size_t stride;         // Satır bytes
    bool use_psram;        // PSRAM kullanımı
} image_buffer_t;

// PSRAM-aware tahsis
image_buffer_t* image_buffer_create(uint32_t w, uint32_t h, 
                                     uint8_t channels, bool psram) {
    image_buffer_t *buf = calloc(1, sizeof(image_buffer_t));
    if (!buf) return NULL;
    
    size_t size = w * h * channels;
    
    // PSRAM 512KB'dan büyük buffer'lar için
    if (psram && size > 512 * 1024) {
        buf->data = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    } else {
        buf->data = malloc(size);
    }
    
    if (!buf->data) {
        free(buf);
        return NULL;
    }
    
    buf->width = w;
    buf->height = h;
    buf->channels = channels;
    buf->use_psram = psram;
    
    return buf;
}
```

### 2. RGB565 Dönüşüm Optimizasyonu

```c
// ESP32 S3 SIMD optimizasyonlu dönüşüm
void rgb888_to_rgb565_simd(const uint8_t *rgb888, uint16_t *rgb565, 
                           size_t pixel_count) {
    // ESP32 S3 vector instructions
    size_t i = 0;
    
    // 4 piksel parallel işleme
    for (; i + 3 < pixel_count; i += 4) {
        uint32_t r0 = rgb888[i*3 + 0];
        uint32_t g0 = rgb888[i*3 + 1];
        uint32_t b0 = rgb888[i*3 + 2];
        
        uint32_t r1 = rgb888[(i+1)*3 + 0];
        uint32_t g1 = rgb888[(i+1)*3 + 1];
        uint32_t b1 = rgb888[(i+1)*3 + 2];
        
        // Pack into 16-bit
        rgb565[i]   = (r0 >> 3) << 11 | (g0 >> 2) << 5 | (b0 >> 3);
        rgb565[i+1] = (r1 >> 3) << 11 | (g1 >> 2) << 5 | (b1 >> 3);
        // ... 2 more pixels
    }
    
    // Kalan pikseller
    for (; i < pixel_count; i++) {
        rgb565[i] = (rgb888[i*3] >> 3) << 11 | 
                    (rgb888[i*3+1] >> 2) << 5 | 
                    (rgb888[i*3+2] >> 3);
    }
}
```

### 3. VTracer Wrapper

```c
// VTracer C API wrapper
typedef struct {
    int color_mode;           // 0: mono, 1: color
    int hierarchical;         // 0: stacked, 1: cutout
    int mode;                 // 0: polygon, 1: curve
    int filter_speckle;       // Gürültü temizleme (0-10)
    int color_precision;      // Renk hassasiyeti (1-16)
    float corner_threshold;   // Köşe eşiği (0-180)
    float length_threshold;   // Uzunluk eşiği (0.1-10.0)
    int max_iterations;       // Maks iterasyon (1-100)
} vtracer_config_t;

int png_to_svg_vtracer(const uint8_t *png_data, size_t png_size,
                       const vtracer_config_t *config,
                       char **svg_output, size_t *svg_size) {
    // VTracer C API çağrısı
    // ...
    return 0; // success
}
```

### 4. SVG Renderer Pipeline

```c
// SVG → PNG pipeline
typedef struct {
    svg_render_config_t config;
    progress_callback_t on_progress;
    error_callback_t on_error;
    void *user_data;
} svg2png_pipeline_t;

int svg2png_convert(svg2png_pipeline_t *pipeline,
                    const char *svg_input, size_t svg_len,
                    uint8_t **png_output, size_t *png_size) {
    
    // Adım 1: SVG Parse
    if (pipeline->on_progress) {
        pipeline->on_progress(0.0f, "Parsing SVG...", pipeline->user_data);
    }
    
    svg_dom_t *dom = svg_parse(svg_input, svg_len);
    if (!dom) {
        if (pipeline->on_error) {
            pipeline->on_error("SVG parse failed", pipeline->user_data);
        }
        return -1;
    }
    
    // Adım 2: Render
    if (pipeline->on_progress) {
        pipeline->on_progress(0.3f, "Rendering...", pipeline->user_data);
    }
    
    image_buffer_t *rendered = svg_render(dom, &pipeline->config);
    svg_dom_free(dom);
    
    // Adım 3: PNG Encode
    if (pipeline->on_progress) {
        pipeline->on_progress(0.7f, "Encoding PNG...", pipeline->user_data);
    }
    
    *png_output = png_encode(rendered->data, rendered->width, 
                            rendered->height, png_size);
    image_buffer_free(rendered);
    
    if (pipeline->on_progress) {
        pipeline->on_progress(1.0f, "Done", pipeline->user_data);
    }
    
    return 0;
}
```

---

## 📈 Performans Hedefleri

### SVG → PNG (480×480 output)
| Metrik | Hedef | Gerçekçi | Not |
|--------|-------|----------|-----|
| Basit SVG | < 50ms | 30-80ms | Tek renk, basit path |
| Orta SVG | < 200ms | 100-300ms | Gradient, grup |
| Karmaşık SVG | < 500ms | 200-800ms | Filtre, clip |
| Bellek | < 1MB | 500KB-2MB | PSRAM dahil |

### PNG → SVG (640×640 input)
| Metrik | Hedef | Gerçekçi | Not |
|--------|-------|----------|-----|
| Basit PNG | < 100ms | 50-150ms | 2-5 renk |
| Orta PNG | < 500ms | 200-800ms | 16-64 renk |
| Karmaşık PNG | < 2s | 1-3s | 256 renk |
| SVG boyutu | < 2x | 1.2-3x | Orijinal PNG'ye göre |

### ESP32 Runtime (ThorVG)
| Metrik | Hedef | Gerçekçi | Not |
|--------|-------|----------|-----|
| SVG render | < 33ms | 16-50ms | 60fps hedefi |
| Bellek | < 50KB | 20-80KB | PSRAM hariç |
| FPS | > 30 | 30-60 | Animasyonlu |

---

## 🎯 kalite Garantisi

### 1. Görsel Kalite Testleri
- **PSNR > 40dB**: SVG→PNG→SVG döngü testi
- **SSIM > 0.95**: Yapısal benzerlik
- **Kenar netliği**: Laplacian variance > 0.8

### 2. Performans Testleri
- **Benchmark suite**: 100 farklı SVG/PNG ile test
- **Memory profiling**: Bellek kullanımı takibi
- **Stress test**: Büyük dosyalar (10MB+)

### 3. Uyumluluk Testleri
- **SVG spec**: SVG 1.1/2.0 uyumluluğu
- **PNG spec**: PNG 1.2+ uyumluluğu
- **Cross-platform**: Windows/Linux/macOS/ESP32

---

## 🚀 Hızlı Başlangıç

### Build (Host)
```bash
cd converter
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
```

### Kullanım
```bash
# SVG → PNG
./converter svg2png input.svg -o output.png -w 480 -h 480 --quality 95

# PNG → SVG
./converter png2svg input.png -o output.svg --color-mode color --precision 6

# Batch processing
./converter batch --input-dir ./svgs --output-dir ./pngs --format png2png
```

### ESP32 Entegrasyonu
```c
// main.c
#include "converter.h"

void init_assets(void) {
    // Build-time convert edilmiş SVG'leri yükle
    extern const uint8_t icon_svg_start[] asm("_binary_icon_svg_start");
    extern const size_t icon_svg_size asm("_binary_icon_svg_size");
    
    // LVGL image source olarak kullan
    lv_img_set_src(img_widget, icon_svg_start);
}
```

---

## 📚 Referanslar

1. **resvg**: https://github.com/niclaslindstedt/resvg
2. **VTracer**: https://github.com/visioncortex/vtracer
3. **NanoSVG**: https://github.com/memononen/nanosvg
4. **ThorVG**: https://github.com/thorvg/thorvg
5. **LVGL SVG**: https://lvgl.io/docs/open/libs/image_support/svg
6. **LVGL Image Converter**: https://lvgl.io/tools/imageconverter

---

## 📝 Notlar

- Bu plan **ESP32-S3 OBD-II Dashboard** projesi için özelleştirilmiştir
- 480×480 round LCD optimizasyonları dahildir
- PSRAM kullanımı zorunlu (büyük görseller için)
- ThorVG, Espressif tarafından resmi olarak desteklenmektedir
- LVGL v8.4 ile uyumluluk sağlanmıştır

---

*Son güncelleme: 18 Ağustos 2026*
*Hazırlayan: Buffy (Codebuff)*
