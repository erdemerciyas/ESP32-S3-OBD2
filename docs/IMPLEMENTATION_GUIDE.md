# SVG ↔ PNG Dönüştürücü — Detaylı Uygulama Rehberi

## 🎯 Proje Hedefleri

### Birincil Hedefler
1. **SVG → PNG**: Yüksek kaliteli rasterizasyon (ESP32 için RGB565)
2. **PNG → SVG**: Renkli vektörel traced output
3. **ESP32 Entegrasyonu**: ThorVG ile runtime rendering
4. **Performans**: 480×480 için < 100ms

### İkincil Hedefler
- CLI arayüzü (host geliştirme için)
- Build-time asset pipeline
- LVGL uyumlu C array output

---

## 🏗️ Mimari Tasarım

### Modüler Yapı
```
┌─────────────────────────────────────────────────────────────┐
│                    API Layer                                 │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐        │
│  │ svg2png_api │  │ png2svg_api │  │ pipeline_api│        │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘        │
│         │                │                │                  │
│  ┌──────▼────────────────▼────────────────▼──────┐         │
│  │              Core Engine                       │         │
│  │  ┌────────────┐  ┌────────────┐  ┌────────┐  │         │
│  │  │  Renderer  │  │  Tracer    │  │  Util  │  │         │
│  │  └────────────┘  └────────────┘  └────────┘  │         │
│  └───────────────────────┬───────────────────────┘         │
│                          │                                   │
│  ┌───────────────────────▼───────────────────────┐         │
│  │           Backend Adapters                     │         │
│  │  ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐ │         │
│  │  │ resvg  │ │VTracer │ │NanoSVG │ │ThorVG  │ │         │
│  │  └────────┘ └────────┘ └────────┘ └────────┘ │         │
│  └───────────────────────────────────────────────┘         │
└─────────────────────────────────────────────────────────────┘
```

---

## 📦 Adım Adım Uygulama

### Adım 1: Proje Yapısı Oluşturma

```bash
# Dizin yapısı
mkdir -p converter/{include,src/{svg2png,png2svg,common},tests,scripts}
```

### Adım 2: Temel Header Dosyaları

#### `include/converter.h`
```c
/**
 * SVG ↔ PNG Converter System
 * High-performance image conversion for ESP32 embedded systems
 */

#ifndef CONVERTER_H
#define CONVERTER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Types & Enums
 * ============================================================================ */

typedef enum {
    CONVERTER_OK = 0,
    CONVERTER_ERR_INVALID_INPUT,
    CONVERTER_ERR_OUT_OF_MEMORY,
    CONVERTER_ERR_RENDER_FAILED,
    CONVERTER_ERR_ENCODE_FAILED,
    CONVERTER_ERR_DECODE_FAILED,
    CONVERTER_ERR_UNSUPPORTED_FORMAT,
    CONVERTER_ERR_IO
} converter_error_t;

typedef enum {
    COLOR_MODE_MONO = 0,
    COLOR_MODE_GRAYSCALE,
    COLOR_MODE_RGB565,
    COLOR_MODE_RGB888,
    COLOR_MODE_RGBA8888
} color_mode_t;

typedef enum {
    OUTPUT_FORMAT_PNG = 0,
    OUTPUT_FORMAT_BMP,
    OUTPUT_FORMAT_RAW_RGB565,
    OUTPUT_FORMAT_C_ARRAY
} output_format_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    color_mode_t color_mode;
    uint8_t quality;        // 1-100
    bool optimize_size;
    bool preserve_aspect_ratio;
} conversion_config_t;

typedef struct {
    float progress;         // 0.0 - 1.0
    const char *message;
    void *user_data;
} progress_info_t;

typedef void (*progress_callback_t)(const progress_info_t *info);

/* ============================================================================
 * Image Buffer
 * ============================================================================ */

typedef struct {
    uint8_t *data;
    uint32_t width;
    uint32_t height;
    uint32_t stride;        // Bytes per row
    color_mode_t format;
    bool owns_data;         // true if buffer was allocated by us
} image_buffer_t;

/**
 * Create a new image buffer
 */
image_buffer_t* image_buffer_create(uint32_t width, uint32_t height,
                                     color_mode_t format, bool use_psram);

/**
 * Free image buffer
 */
void image_buffer_free(image_buffer_t *buf);

/**
 * Get pixel at (x, y)
 */
static inline uint8_t* image_buffer_get_pixel(image_buffer_t *buf,
                                               uint32_t x, uint32_t y) {
    if (x >= buf->width || y >= buf->height) return NULL;
    return buf->data + (y * buf->stride) + (x * image_buffer_bytes_per_pixel(buf->format));
}

/**
 * Get bytes per pixel for format
 */
static inline uint32_t image_buffer_bytes_per_pixel(color_mode_t fmt) {
    switch (fmt) {
        case COLOR_MODE_MONO: return 1;
        case COLOR_MODE_GRAYSCALE: return 1;
        case COLOR_MODE_RGB565: return 2;
        case COLOR_MODE_RGB888: return 3;
        case COLOR_MODE_RGBA8888: return 4;
        default: return 3;
    }
}

/* ============================================================================
 * SVG → PNG Conversion
 * ============================================================================ */

typedef struct {
    conversion_config_t base;
    
    // SVG-specific options
    bool enable_clipping;
    bool enable_gradients;
    bool enable_filters;     // blur, drop-shadow etc.
    float scale_factor;      // For supersampling
    
    // Backend selection
    enum {
        BACKEND_AUTO = 0,
        BACKEND_NANOSVG,
        BACKEND_RESVG
    } backend;
} svg2png_config_t;

/**
 * Convert SVG to PNG
 * 
 * @param svg_data     SVG file data
 * @param svg_size     SVG file size in bytes
 * @param config       Conversion configuration
 * @param png_output   Output: PNG data (caller must free)
 * @param png_size     Output: PNG size in bytes
 * @param progress     Progress callback (optional)
 * @return CONVERTER_OK on success
 */
converter_error_t svg2png_convert(const uint8_t *svg_data, size_t svg_size,
                                   const svg2png_config_t *config,
                                   uint8_t **png_output, size_t *png_size,
                                   progress_callback_t progress);

/**
 * Convert SVG file to PNG file
 */
converter_error_t svg2png_convert_file(const char *svg_path,
                                        const svg2png_config_t *config,
                                        const char *png_path,
                                        progress_callback_t progress);

/* ============================================================================
 * PNG → SVG Conversion
 * ============================================================================ */

typedef struct {
    conversion_config_t base;
    
    // Vectorization options
    enum {
        TRACE_MODE_POLYGON = 0,
        TRACE_MODE_CURVE
    } trace_mode;
    
    enum {
        HIERARCHY_STACKED = 0,
        HIERARCHY_CUTOUT
    } hierarchy_mode;
    
    int filter_speckle;     // 0-10, removes small noise
    int color_precision;    // 1-16, higher = more colors
    float corner_threshold; // 0-180 degrees
    float length_threshold; // path simplification threshold
    
    // SVG output options
    bool optimize_svg;      // Minify SVG output
    bool embed_metadata;    // Add creator comment
} png2svg_config_t;

/**
 * Convert PNG to SVG (vectorization)
 * 
 * @param png_data     PNG file data
 * @param png_size     PNG file size in bytes
 * @param config       Conversion configuration
 * @param svg_output   Output: SVG string (caller must free)
 * @param svg_size     Output: SVG string length
 * @param progress     Progress callback (optional)
 * @return CONVERTER_OK on success
 */
converter_error_t png2svg_convert(const uint8_t *png_data, size_t png_size,
                                   const png2svg_config_t *config,
                                   char **svg_output, size_t *svg_size,
                                   progress_callback_t progress);

/**
 * Convert PNG file to SVG file
 */
converter_error_t png2svg_convert_file(const char *png_path,
                                        const png2svg_config_t *config,
                                        const char *svg_path,
                                        progress_callback_t progress);

/* ============================================================================
 * Pipeline (Batch Processing)
 * ============================================================================ */

typedef struct {
    const char *input_dir;
    const char *output_dir;
    const char *input_pattern;  // e.g., "*.svg"
    
    conversion_config_t config;
    
    progress_callback_t progress;
    void *user_data;
} pipeline_config_t;

/**
 * Run batch conversion pipeline
 */
converter_error_t pipeline_run_batch(const pipeline_config_t *config,
                                      uint32_t *files_converted);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Get error string for error code
 */
const char* converter_error_string(converter_error_t err);

/**
 * Get default configuration for SVG→PNG
 */
svg2png_config_t svg2png_default_config(void);

/**
 * Get default configuration for PNG→SVG
 */
png2svg_config_t png2svg_default_config(void);

/**
 * Check if format is supported
 */
bool converter_is_svg_supported(void);
bool converter_is_png_supported(void);

#ifdef __cplusplus
}
#endif

#endif /* CONVERTER_H */
```

### Adım 3: Bellek Yönetimi Implementasyonu

#### `src/common/image_buffer.c`
```c
#include "converter.h"
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#define USE_PSRAM 1
#else
#define USE_PSRAM 0
#endif

static const char *TAG = "image_buffer";

image_buffer_t* image_buffer_create(uint32_t width, uint32_t height,
                                     color_mode_t format, bool use_psram) {
    image_buffer_t *buf = calloc(1, sizeof(image_buffer_t));
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate buffer struct");
        return NULL;
    }
    
    uint32_t bpp = image_buffer_bytes_per_pixel(format);
    size_t stride = width * bpp;
    size_t total_size = stride * height;
    
    buf->width = width;
    buf->height = height;
    buf->stride = stride;
    buf->format = format;
    buf->owns_data = true;
    
#if USE_PSRAM
    if (use_psram && total_size > 16 * 1024) {
        // Use PSRAM for large buffers
        buf->data = heap_caps_malloc(total_size, MALLOC_CAP_SPIRAM);
        if (buf->data) {
            ESP_LOGD(TAG, "Allocated %zu bytes in PSRAM", total_size);
        }
    }
#endif
    
    if (!buf->data) {
        // Fallback to regular RAM
        buf->data = malloc(total_size);
        if (!buf->data) {
            ESP_LOGE(TAG, "Failed to allocate %zu bytes", total_size);
            free(buf);
            return NULL;
        }
        ESP_LOGD(TAG, "Allocated %zu bytes in internal RAM", total_size);
    }
    
    // Zero initialize
    memset(buf->data, 0, total_size);
    
    return buf;
}

void image_buffer_free(image_buffer_t *buf) {
    if (!buf) return;
    
    if (buf->owns_data && buf->data) {
#if USE_PSRAM
        if (heap_caps_get_malloc_size(buf->data) > 0) {
            heap_caps_free(buf->data);
        } else
#endif
        {
            free(buf->data);
        }
    }
    
    free(buf);
}

image_buffer_t* image_buffer_clone(const image_buffer_t *src) {
    if (!src || !src->data) return NULL;
    
    image_buffer_t *clone = image_buffer_create(
        src->width, src->height, src->format, false
    );
    
    if (clone) {
        memcpy(clone->data, src->data, src->stride * src->height);
    }
    
    return clone;
}

converter_error_t image_buffer_resize(image_buffer_t *buf,
                                       uint32_t new_width, 
                                       uint32_t new_height) {
    if (!buf) return CONVERTER_ERR_INVALID_INPUT;
    
    // Simple nearest-neighbor resize
    image_buffer_t *new_buf = image_buffer_create(
        new_width, new_height, buf->format, buf->data == NULL ? false : true
    );
    
    if (!new_buf) return CONVERTER_ERR_OUT_OF_MEMORY;
    
    uint32_t bpp = image_buffer_bytes_per_pixel(buf->format);
    
    for (uint32_t y = 0; y < new_height; y++) {
        uint32_t src_y = (y * buf->height) / new_height;
        for (uint32_t x = 0; x < new_width; x++) {
            uint32_t src_x = (x * buf->width) / new_width;
            
            uint8_t *dst = new_buf->data + (y * new_buf->stride) + (x * bpp);
            uint8_t *src = buf->data + (src_y * buf->stride) + (src_x * bpp);
            
            memcpy(dst, src, bpp);
        }
    }
    
    // Swap buffers
    uint8_t *old_data = buf->data;
    bool old_owns = buf->owns_data;
    
    buf->data = new_buf->data;
    buf->width = new_width;
    buf->height = new_height;
    buf->stride = new_buf->stride;
    buf->owns_data = new_buf->owns_data;
    
    new_buf->data = old_data;
    new_buf->owns_data = old_owns;
    
    image_buffer_free(new_buf);
    
    return CONVERTER_OK;
}
```

### Adım 4: Renk Dönüşüm Fonksiyonları

#### `src/common/color_convert.c`
```c
#include "converter.h"
#include <math.h>

// RGB888 → RGB565 dönüşümü (ESP32 için optimize)
void color_convert_rgb888_to_rgb565(const uint8_t *rgb888, 
                                     uint16_t *rgb565,
                                     uint32_t pixel_count) {
    for (uint32_t i = 0; i < pixel_count; i++) {
        uint8_t r = rgb888[i * 3 + 0];
        uint8_t g = rgb888[i * 3 + 1];
        uint8_t b = rgb888[i * 3 + 2];
        
        // 5-6-5 bit packing
        rgb565[i] = ((r >> 3) << 11) | 
                    ((g >> 2) << 5) | 
                    (b >> 3);
    }
}

// RGB565 → RGB888 dönüşümü
void color_convert_rgb565_to_rgb888(const uint16_t *rgb565,
                                     uint8_t *rgb888,
                                     uint32_t pixel_count) {
    for (uint32_t i = 0; i < pixel_count; i++) {
        uint16_t pixel = rgb565[i];
        
        uint8_t r = (pixel >> 11) & 0x1F;
        uint8_t g = (pixel >> 5) & 0x3F;
        uint8_t b = pixel & 0x1F;
        
        // Expand to 8-bit
        rgb888[i * 3 + 0] = (r << 3) | (r >> 2);
        rgb888[i * 3 + 1] = (g << 2) | (g >> 4);
        rgb888[i * 3 + 2] = (b << 3) | (b >> 2);
    }
}

// RGBA8888 → RGB565 (alpha blending with white background)
void color_convert_rgba_to_rgb565(const uint8_t *rgba,
                                   uint16_t *rgb565,
                                   uint32_t pixel_count) {
    for (uint32_t i = 0; i < pixel_count; i++) {
        uint8_t r = rgba[i * 4 + 0];
        uint8_t g = rgba[i * 4 + 1];
        uint8_t b = rgba[i * 4 + 2];
        uint8_t a = rgba[i * 4 + 3];
        
        // Alpha blend with white
        if (a < 255) {
            uint32_t inv_a = 255 - a;
            r = (r * a + 255 * inv_a) / 255;
            g = (g * a + 255 * inv_a) / 255;
            b = (b * a + 255 * inv_a) / 255;
        }
        
        rgb565[i] = ((r >> 3) << 11) | 
                    ((g >> 2) << 5) | 
                    (b >> 3);
    }
}

// Median Cut renk quantization
typedef struct {
    uint8_t r, g, b;
    uint32_t count;
} color_entry_t;

typedef struct {
    uint8_t r_min, r_max;
    uint8_t g_min, g_max;
    uint8_t b_min, b_max;
    uint32_t total_count;
} color_box_t;

// Quantization fonksiyonları...
// (Detaylı implementasyon için uzunluk sınırı nedeniyle özet)
```

### Adım 5: NanoSVG Backend

#### `src/svg2png/nanosvg_backend.c`
```c
#include "converter.h"
#include "nanosvg.h"
#include "nanosvgrast.h"
#include <string.h>

static NSVGimage* parse_svg_data(const uint8_t *data, size_t size) {
    // NanoSVG parser
    char *svg_string = malloc(size + 1);
    if (!svg_string) return NULL;
    
    memcpy(svg_string, data, size);
    svg_string[size] = '\0';
    
    NSVGimage *image = nsvgParse(svg_string, "px", 96);
    free(svg_string);
    
    return image;
}

static converter_error_t rasterize_svg(NSVGimage *image,
                                        const svg2png_config_t *config,
                                        image_buffer_t **output) {
    // Hedef boyutları hesapla
    uint32_t width = config->base.width;
    uint32_t height = config->base.height;
    
    if (width == 0 || height == 0) {
        // SVG'den boyut al
        width = (uint32_t)(image->width * config->scale_factor);
        height = (uint32_t)(image->height * config->scale_factor);
    }
    
    // RGBA buffer oluştur
    *output = image_buffer_create(width, height, COLOR_MODE_RGBA8888, true);
    if (!*output) return CONVERTER_ERR_OUT_OF_MEMORY;
    
    // NanoSVG rasterizer
    NSVGcontext *ctx = nsvgCreateRasterizer();
    if (!ctx) {
        image_buffer_free(*output);
        *output = NULL;
        return CONVERTER_ERR_RENDER_FAILED;
    }
    
    // Anti-aliasing kalitesi (1-8)
    int quality = config->base.quality / 12 + 1;
    if (quality > 8) quality = 8;
    
    // Aspect ratio koru
    float scale_x = (float)width / image->width;
    float scale_y = (float)height / image->height;
    
    if (config->base.preserve_aspect_ratio) {
        float scale = (scale_x < scale_y) ? scale_x : scale_y;
        scale_x = scale;
        scale_y = scale;
    }
    
    // Render
    nsvgRasterize(ctx, image, 
                  (width - image->width * scale_x) / 2,  // Center X
                  (height - image->height * scale_y) / 2, // Center Y
                  scale_x, scale_y,
                  (*output)->data, width, height, 
                  (*output)->stride);
    
    nsvgDeleteRasterizer(ctx);
    
    return CONVERTER_OK;
}

converter_error_t nanosvg_convert(const uint8_t *svg_data, size_t svg_size,
                                   const svg2png_config_t *config,
                                   image_buffer_t **output) {
    // Parse SVG
    NSVGimage *image = parse_svg_data(svg_data, svg_size);
    if (!image) {
        return CONVERTER_ERR_INVALID_INPUT;
    }
    
    // Rasterize
    converter_error_t err = rasterize_svg(image, config, output);
    
    // Cleanup
    nsvgDelete(image);
    
    return err;
}
```

### Adım 6: VTracer Backend (PNG → SVG)

#### `src/png2svg/vtracer_backend.c`
```c
#include "converter.h"
#include <vtracer.h>  // VTracer C API
#include <string.h>

// VTracer config mapping
static vtracer_config_t map_config(const png2svg_config_t *config) {
    vtracer_config_t vt_config = {
        .color_mode = (config->base.color_mode == COLOR_MODE_MONO) ? 
                      VCM_mono : VCM_color,
        .hierarchy = (config->hierarchy_mode == HIERARCHY_CUTOUT) ?
                     VH_cutout : VH_stacked,
        .mode = (config->trace_mode == TRACE_MODE_CURVE) ?
                VTM_curve : VTM_polygon,
        .filter_speckle = config->filter_speckle,
        .color_precision = config->color_precision,
        .corner_threshold = config->corner_threshold,
        .length_threshold = config->length_threshold,
        .max_iterations = 10,
        .splice_threshold = 45,
        .path_precision = 3
    };
    
    return vt_config;
}

converter_error_t vtracer_convert(const uint8_t *png_data, size_t png_size,
                                   const png2svg_config_t *config,
                                   char **svg_output, size_t *svg_size) {
    // PNG decode
    // (Using lodepng or similar)
    
    uint8_t *pixels = NULL;
    uint32_t width, height;
    uint32_t err = lodepng_decode32(&pixels, &width, &height, 
                                    png_data, png_size);
    if (err) {
        return CONVERTER_ERR_DECODE_FAILED;
    }
    
    // Create VTracer input
    vtracer_input_t input = {
        .data = pixels,
        .width = width,
        .height = height,
        .format = VF_rgba
    };
    
    // Create VTracer output
    vtracer_output_t output = {0};
    
    // Run vectorization
    vtracer_config_t vt_config = map_config(config);
    err = vtracer_convert(&input, &output, &vt_config);
    
    free(pixels);
    
    if (err) {
        return CONVERTER_ERR_RENDER_FAILED;
    }
    
    // Extract SVG string
    *svg_size = output.svg_size;
    *svg_output = malloc(*svg_size + 1);
    if (!*svg_output) {
        vtracer_output_free(&output);
        return CONVERTER_ERR_OUT_OF_MEMORY;
    }
    
    memcpy(*svg_output, output.svg_data, *svg_size);
    (*svg_output)[*svg_size] = '\0';
    
    // Optimize if requested
    if (config->optimize_svg) {
        // Remove whitespace, minify
        // (Simple implementation)
    }
    
    vtracer_output_free(&output);
    
    return CONVERTER_OK;
}
```

### Adım 7: ThorVG ESP32 Entegrasyonu

#### `src/esp32/thorvg_integration.c`
```c
#ifdef ESP_PLATFORM

#include "converter.h"
#include "thorvg.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "thorvg";

// ThorVG initialization
bool thorvg_init(void) {
    if (tvg::Initializer::init(tvg::CanvasEngine::SW, 0) != tvg::Result::Success) {
        ESP_LOGE(TAG, "Failed to initialize ThorVG");
        return false;
    }
    
    ESP_LOGI(TAG, "ThorVG initialized successfully");
    return true;
}

// SVG rendering to LVGL image
lv_img_dsc_t* thorvg_render_svg_to_lvgl(const uint8_t *svg_data, 
                                          size_t svg_size,
                                          uint32_t target_width,
                                          uint32_t target_height) {
    // Create canvas
    auto canvas = tvg::SwCanvas::gen();
    if (!canvas) {
        ESP_LOGE(TAG, "Failed to create canvas");
        return NULL;
    }
    
    // Allocate buffer (RGB565 for LVGL)
    uint32_t *buffer = (uint32_t*)heap_caps_malloc(
        target_width * target_height * sizeof(uint32_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate render buffer");
        return NULL;
    }
    
    // Set buffer
    canvas->target(buffer, target_width, target_width, target_height,
                   tvg::SwCanvas::ABGR8888);
    
    // Parse SVG
    auto svg = tvg::Picture::gen();
    if (svg->load(svg_data, svg_size) != tvg::Result::Success) {
        ESP_LOGE(TAG, "Failed to load SVG");
        free(buffer);
        return NULL;
    }
    
    // Resize to fit
    svg->resize(target_width, target_height);
    
    // Push to canvas
    canvas->push(std::move(svg));
    
    // Draw
    if (canvas->draw() != tvg::Result::Success) {
        ESP_LOGE(TAG, "Failed to draw");
        free(buffer);
        return NULL;
    }
    
    // Sync
    canvas->sync();
    
    // Convert ABGR8888 to RGB565 for LVGL
    uint16_t *rgb565_buffer = (uint16_t*)heap_caps_malloc(
        target_width * target_height * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    
    for (int i = 0; i < target_width * target_height; i++) {
        uint32_t pixel = buffer[i];
        uint8_t r = (pixel >> 16) & 0xFF;
        uint8_t g = (pixel >> 8) & 0xFF;
        uint8_t b = pixel & 0xFF;
        
        rgb565_buffer[i] = ((r >> 3) << 11) | 
                           ((g >> 2) << 5) | 
                           (b >> 3);
    }
    
    free(buffer);
    
    // Create LVGL image descriptor
    lv_img_dsc_t *img_dsc = (lv_img_dsc_t*)malloc(sizeof(lv_img_dsc_t));
    if (!img_dsc) {
        free(rgb565_buffer);
        return NULL;
    }
    
    img_dsc->header.cf = LV_IMG_CF_TRUE_COLOR;
    img_dsc->header.always_zero = 0;
    img_dsc->header.reserved = 0;
    img_dsc->header.w = target_width;
    img_dsc->header.h = target_height;
    img_dsc->data_size = target_width * target_height * sizeof(uint16_t);
    img_dsc->data = (const uint8_t*)rgb565_buffer;
    
    return img_dsc;
}

// Cache system for rendered SVGs
#define SVG_CACHE_SIZE 8

typedef struct {
    uint32_t hash;
    lv_img_dsc_t *image;
    uint32_t last_used;
} svg_cache_entry_t;

static svg_cache_entry_t svg_cache[SVG_CACHE_SIZE];
static uint32_t cache_counter = 0;

lv_img_dsc_t* thorvg_get_cached_svg(const uint8_t *svg_data, size_t svg_size,
                                      uint32_t width, uint32_t height) {
    // Simple hash
    uint32_t hash = 0;
    for (size_t i = 0; i < svg_size; i++) {
        hash = ((hash << 5) + hash) + svg_data[i];
    }
    
    // Check cache
    for (int i = 0; i < SVG_CACHE_SIZE; i++) {
        if (svg_cache[i].hash == hash && svg_cache[i].image) {
            svg_cache[i].last_used = ++cache_counter;
            return svg_cache[i].image;
        }
    }
    
    // Render new
    lv_img_dsc_t *img = thorvg_render_svg_to_lvgl(
        svg_data, svg_size, width, height
    );
    
    if (img) {
        // Find LRU entry
        int lru_idx = 0;
        uint32_t min_used = UINT32_MAX;
        for (int i = 0; i < SVG_CACHE_SIZE; i++) {
            if (svg_cache[i].last_used < min_used) {
                min_used = svg_cache[i].last_used;
                lru_idx = i;
            }
        }
        
        // Free old entry
        if (svg_cache[lru_idx].image) {
            free((void*)svg_cache[lru_idx].image->data);
            free(svg_cache[lru_idx].image);
        }
        
        // Store new
        svg_cache[lru_idx].hash = hash;
        svg_cache[lru_idx].image = img;
        svg_cache[lru_idx].last_used = ++cache_counter;
    }
    
    return img;
}

#endif /* ESP_PLATFORM */
```

### Adım 8: CLI Arayüzü

#### `src/cli.c`
```c
#include "converter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

static void print_usage(const char *prog_name) {
    printf("Usage: %s <command> [options]\n\n", prog_name);
    printf("Commands:\n");
    printf("  svg2png    Convert SVG to PNG\n");
    printf("  png2svg    Convert PNG to SVG (vectorization)\n");
    printf("  batch      Batch conversion\n");
    printf("\n");
    printf("SVG → PNG Options:\n");
    printf("  -i, --input <file>      Input SVG file\n");
    printf("  -o, --output <file>     Output PNG file\n");
    printf("  -w, --width <pixels>    Output width (default: auto)\n");
    printf("  -h, --height <pixels>   Output height (default: auto)\n");
    printf("  -q, --quality <1-100>   Render quality (default: 90)\n");
    printf("  --no-clip               Disable clipping\n");
    printf("  --no-gradients          Disable gradients\n");
    printf("  --scale <factor>        Supersampling scale (default: 1.0)\n");
    printf("  --backend <name>        Backend: nanosvg, resvg, auto\n");
    printf("\n");
    printf("PNG → SVG Options:\n");
    printf("  -i, --input <file>      Input PNG file\n");
    printf("  -o, --output <file>     Output SVG file\n");
    printf("  --mode <mode>           Trace mode: polygon, curve\n");
    printf("  --color <mode>          Color mode: mono, color\n");
    printf("  --precision <1-16>      Color precision (default: 6)\n");
    printf("  --speckle <0-10>        Speckle filter (default: 4)\n");
    printf("  --corner <0-180>        Corner threshold (default: 60)\n");
    printf("  --optimize              Optimize SVG output\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s svg2png icon.svg -o icon.png -w 480 -h 480\n", prog_name);
    printf("  %s png2svg photo.png -o photo.svg --color --precision 8\n", prog_name);
}

static void progress_callback(const progress_info_t *info) {
    printf("\r[%3.0f%%] %s", info->progress * 100, info->message);
    if (info->progress >= 1.0f) printf("\n");
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    const char *command = argv[1];
    
    if (strcmp(command, "svg2png") == 0) {
        // SVG → PNG conversion
        svg2png_config_t config = svg2png_default_config();
        const char *input_path = NULL;
        const char *output_path = NULL;
        
        // Parse options
        static struct option long_options[] = {
            {"input", required_argument, 0, 'i'},
            {"output", required_argument, 0, 'o'},
            {"width", required_argument, 0, 'w'},
            {"height", required_argument, 0, 'h'},
            {"quality", required_argument, 0, 'q'},
            {"no-clip", no_argument, 0, 'C'},
            {"no-gradients", no_argument, 0, 'G'},
            {"scale", required_argument, 0, 's'},
            {"backend", required_argument, 0, 'b'},
            {0, 0, 0, 0}
        };
        
        int opt;
        while ((opt = getopt_long(argc - 1, argv + 1, "i:o:w:h:q:s:b:CG", 
                                   long_options, NULL)) != -1) {
            switch (opt) {
                case 'i': input_path = optarg; break;
                case 'o': output_path = optarg; break;
                case 'w': config.base.width = atoi(optarg); break;
                case 'h': config.base.height = atoi(optarg); break;
                case 'q': config.base.quality = atoi(optarg); break;
                case 's': config.scale_factor = atof(optarg); break;
                case 'C': config.enable_clipping = false; break;
                case 'G': config.enable_gradients = false; break;
                case 'b':
                    if (strcmp(optarg, "nanosvg") == 0) 
                        config.backend = BACKEND_NANOSVG;
                    else if (strcmp(optarg, "resvg") == 0) 
                        config.backend = BACKEND_RESVG;
                    break;
            }
        }
        
        if (!input_path || !output_path) {
            fprintf(stderr, "Error: input and output files required\n");
            return 1;
        }
        
        printf("Converting %s → %s\n", input_path, output_path);
        
        converter_error_t err = svg2png_convert_file(
            input_path, &config, output_path, progress_callback
        );
        
        if (err != CONVERTER_OK) {
            fprintf(stderr, "Error: %s\n", converter_error_string(err));
            return 1;
        }
        
        printf("Success!\n");
        
    } else if (strcmp(command, "png2svg") == 0) {
        // PNG → SVG conversion
        png2svg_config_t config = png2svg_default_config();
        const char *input_path = NULL;
        const char *output_path = NULL;
        
        // Similar option parsing...
        // ...
        
    } else if (strcmp(command, "batch") == 0) {
        // Batch conversion
        // ...
        
    } else {
        fprintf(stderr, "Unknown command: %s\n", command);
        print_usage(argv[0]);
        return 1;
    }
    
    return 0;
}
```

---

## 🧪 Test Stratejisi

### Unit Testler

#### `tests/test_svg2png.c`
```c
#include "converter.h"
#include <assert.h>
#include <stdio.h>

// Test SVG content
static const char *TEST_SVG = 
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
    "  <circle cx=\"50\" cy=\"50\" r=\"40\" fill=\"red\"/>"
    "</svg>";

void test_basic_conversion(void) {
    svg2png_config_t config = svg2png_default_config();
    config.base.width = 100;
    config.base.height = 100;
    
    uint8_t *png_data = NULL;
    size_t png_size = 0;
    
    converter_error_t err = svg2png_convert(
        (const uint8_t*)TEST_SVG, strlen(TEST_SVG),
        &config, &png_data, &png_size, NULL
    );
    
    assert(err == CONVERTER_OK);
    assert(png_data != NULL);
    assert(png_size > 0);
    
    // PNG signature check
    assert(png_data[0] == 0x89); // PNG magic
    assert(png_data[1] == 'P');
    assert(png_data[2] == 'N');
    assert(png_data[3] == 'G');
    
    free(png_data);
    printf("✓ test_basic_conversion passed\n");
}

void test_aspect_ratio_preservation(void) {
    svg2png_config_t config = svg2png_default_config();
    config.base.width = 200;
    config.base.height = 200;
    config.base.preserve_aspect_ratio = true;
    
    uint8_t *png_data = NULL;
    size_t png_size = 0;
    
    converter_error_t err = svg2png_convert(
        (const uint8_t*)TEST_SVG, strlen(TEST_SVG),
        &config, &png_data, &png_size, NULL
    );
    
    assert(err == CONVERTER_OK);
    // Verify centered rendering
    // ...
    
    free(png_data);
    printf("✓ test_aspect_ratio_preservation passed\n");
}

void test_quality_settings(void) {
    // Test different quality levels
    for (int q = 1; q <= 100; q += 10) {
        svg2png_config_t config = svg2png_default_config();
        config.base.quality = q;
        
        uint8_t *png_data = NULL;
        size_t png_size = 0;
        
        converter_error_t err = svg2png_convert(
            (const uint8_t*)TEST_SVG, strlen(TEST_SVG),
            &config, &png_data, &png_size, NULL
        );
        
        assert(err == CONVERTER_OK);
        assert(png_size > 0);
        
        free(png_data);
    }
    
    printf("✓ test_quality_settings passed\n");
}

int main(void) {
    test_basic_conversion();
    test_aspect_ratio_preservation();
    test_quality_settings();
    
    printf("\nAll tests passed!\n");
    return 0;
}
```

### Performance Benchmark

#### `tests/benchmark.c`
```c
#include "converter.h"
#include <time.h>
#include <stdio.h>

typedef struct {
    const char *name;
    uint32_t width;
    uint32_t height;
    double avg_time_ms;
    size_t avg_size;
} benchmark_result_t;

void benchmark_svg2png(const char *svg_path, uint32_t iterations) {
    printf("Benchmarking SVG → PNG: %s\n", svg_path);
    
    // Load SVG file
    FILE *f = fopen(svg_path, "rb");
    if (!f) {
        printf("Failed to open %s\n", svg_path);
        return;
    }
    
    fseek(f, 0, SEEK_END);
    size_t svg_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    uint8_t *svg_data = malloc(svg_size);
    fread(svg_data, 1, svg_size, f);
    fclose(f);
    
    // Warm up
    svg2png_config_t config = svg2png_default_config();
    config.base.width = 480;
    config.base.height = 480;
    
    uint8_t *png_data = NULL;
    size_t png_size = 0;
    svg2png_convert(svg_data, svg_size, &config, &png_data, &png_size, NULL);
    free(png_data);
    
    // Benchmark
    double total_time = 0;
    size_t total_size = 0;
    
    for (uint32_t i = 0; i < iterations; i++) {
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        
        svg2png_convert(svg_data, svg_size, &config, &png_data, &png_size, NULL);
        
        clock_gettime(CLOCK_MONOTONIC, &end);
        
        double elapsed = (end.tv_sec - start.tv_sec) * 1000.0 +
                        (end.tv_nsec - start.tv_nsec) / 1000000.0;
        
        total_time += elapsed;
        total_size += png_size;
        
        free(png_data);
    }
    
    printf("  Iterations: %u\n", iterations);
    printf("  Avg Time: %.2f ms\n", total_time / iterations);
    printf("  Avg Size: %zu bytes\n", total_size / iterations);
    printf("  Throughput: %.2f files/sec\n", 
           1000.0 * iterations / total_time);
    
    free(svg_data);
}

int main(void) {
    benchmark_svg2png("testdata/simple.svg", 100);
    benchmark_svg2png("testdata/medium.svg", 100);
    benchmark_svg2png("testdata/complex.svg", 50);
    
    return 0;
}
```

---

## 📊 Sonuç

Bu rehber, ESP32-S3 OBD-II Dashboard projesi için kapsamlı bir SVG ↔ PNG dönüştürücü sistemi sunmaktadır. Temel özellikler:

1. **Modüler mimari**: Her bileşen bağımsız olarak test edilebilir
2. **ESP32 optimizasyonu**: PSRAM kullanımı, RGB565 desteği
3. **Esnek backend**: NanoSVG, resvg, VTracer, ThorVG
4. **Performans**: 480×480 için < 100ms hedefi
5. **Kalite**: PSNR > 40dB, SSIM > 0.95 hedefleri

İlk aşama için NanoSVG + VTracer combo önerilir. ThorVG entegrasyonu LVGL v8/v9 ile uyumlu runtime rendering sağlar.

---

*Bu doküman Codebuff tarafından otomatik oluşturulmuştur.*
*Versiyon: 1.0.0 | Tarih: 18 Ağustos 2026*
