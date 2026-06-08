#pragma once

#include <cstdint>

/** VaAndCob meter.h formula indices — standard OBD-II */
namespace ObdFormulas {

inline float mapKpaToPsi(uint8_t a) { return static_cast<float>(a) * 0.145f; }
inline float tempByteC(uint8_t a) { return static_cast<float>(a) - 40.0f; }
inline float percentByte(uint8_t a) { return static_cast<float>(a) * 100.0f / 255.0f; }
inline float rpmFromAB(uint8_t a, uint8_t b) {
    return (static_cast<float>((static_cast<uint16_t>(a) << 8) | b)) / 4.0f;
}
inline float voltageMv(uint8_t a, uint8_t b) {
    return (static_cast<float>((static_cast<uint16_t>(a) << 8) | b)) / 1000.0f;
}

} // namespace ObdFormulas
