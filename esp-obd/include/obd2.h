#pragma once
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

enum OBD2_PID : uint8_t {
    PID_SUPPORTED_00      = 0x00,
    PID_ENGINE_LOAD       = 0x04,
    PID_COOLANT_TEMP      = 0x05,
    PID_SHORT_FUEL_TRIM1  = 0x06,
    PID_LONG_FUEL_TRIM1   = 0x07,
    PID_FUEL_PRESSURE     = 0x0A,
    PID_INTAKE_MAP        = 0x0B,
    PID_ENGINE_RPM        = 0x0C,
    PID_VEHICLE_SPEED     = 0x0D,
    PID_TIMING_ADVANCE    = 0x0E,
    PID_INTAKE_AIR_TEMP   = 0x0F,
    PID_MAF_FLOW          = 0x10,
    PID_THROTTLE_POS      = 0x11,
    PID_RUN_TIME          = 0x1F,
    PID_FUEL_LEVEL        = 0x2F,
    PID_BARO_PRESSURE     = 0x33,
    PID_AMBIENT_TEMP      = 0x46,
    PID_CONTROL_MODULE_V  = 0x42,
    PID_ABS_LOAD          = 0x43,
    PID_OIL_TEMP          = 0x5C,
    PID_FUEL_RATE         = 0x5E,
};

struct PIDQuery {
    OBD2_PID pid;
    const char* request;
    const char* name;
    const char* unit;
    uint8_t dataBytes;
};

static const PIDQuery PID_TABLE[] = {
    { PID_ENGINE_RPM,      "010C", "RPM",       "rpm", 4 },
    { PID_VEHICLE_SPEED,   "010D", "Speed",     "km/h", 2 },
    { PID_COOLANT_TEMP,    "0105", "Coolant",   "C",   2 },
    { PID_INTAKE_AIR_TEMP, "010F", "Intake",    "C",   2 },
    { PID_ENGINE_LOAD,     "0104", "Load",      "%",   2 },
    { PID_THROTTLE_POS,    "0111", "Throttle",  "%",   2 },
    { PID_INTAKE_MAP,      "010B", "MAP",       "kPa", 2 },
    { PID_FUEL_PRESSURE,   "010A", "Fuel P",    "kPa", 2 },
    { PID_SHORT_FUEL_TRIM1,"0106", "STFT1",     "%",   2 },
    { PID_LONG_FUEL_TRIM1, "0107", "LTFT1",     "%",   2 },
    { PID_MAF_FLOW,        "0110", "MAF",       "g/s", 4 },
    { PID_TIMING_ADVANCE,  "010E", "Timing",    "deg", 2 },
    { PID_FUEL_LEVEL,      "012F", "Fuel",      "%",   2 },
    { PID_CONTROL_MODULE_V,"0142", "ECU Volt",  "V",   4 },
    { PID_OIL_TEMP,        "015C", "Oil Temp",  "C",   2 },
    { PID_FUEL_RATE,       "015E", "Fuel Rate", "L/h", 4 },
    { PID_AMBIENT_TEMP,    "0146", "Ambient",   "C",   2 },
    { PID_BARO_PRESSURE,   "0133", "Baro",      "kPa", 2 },
    { PID_ABS_LOAD,        "0143", "Abs Load",  "%",   4 },
    { PID_RUN_TIME,        "011F", "Run Time",  "sec", 4 },
};

static const int PID_TABLE_SIZE = sizeof(PID_TABLE) / sizeof(PID_TABLE[0]);

struct OBD2Data {
    float value;
    bool valid;
    uint32_t lastUpdate;
};

inline uint8_t hexToByte(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

inline uint16_t hexToUint16(const char* hex) {
    return (hexToByte(hex[0]) << 12) | (hexToByte(hex[1]) << 8) |
           (hexToByte(hex[2]) << 4)  | hexToByte(hex[3]);
}

inline uint8_t hexToUint8(const char* hex) {
    return (hexToByte(hex[0]) << 4) | hexToByte(hex[1]);
}

inline float decodePIDValue(OBD2_PID pid, const char* data) {
    if (!data || strlen(data) < 2 || strcmp(data, "NODATA") == 0) return -999.0f;

    uint8_t A = (strlen(data) >= 2) ? hexToUint8(data) : 0;
    uint8_t B = (strlen(data) >= 4) ? hexToUint8(data + 2) : 0;
    uint16_t AB = (strlen(data) >= 4) ? hexToUint16(data) : A;

    switch (pid) {
        case PID_COOLANT_TEMP:
        case PID_INTAKE_AIR_TEMP:
        case PID_AMBIENT_TEMP:
        case PID_OIL_TEMP:
            return (float)(A - 40);
        case PID_ENGINE_RPM:
            return (float)AB / 4.0f;
        case PID_VEHICLE_SPEED:
            return (float)A;
        case PID_ENGINE_LOAD:
        case PID_THROTTLE_POS:
        case PID_FUEL_LEVEL:
        case PID_ABS_LOAD:
            return A * 100.0f / 255.0f;
        case PID_INTAKE_MAP:
        case PID_BARO_PRESSURE:
            return (float)A;
        case PID_FUEL_PRESSURE:
            return A * 3.0f;
        case PID_SHORT_FUEL_TRIM1:
        case PID_LONG_FUEL_TRIM1:
            return (A - 128.0f) * 100.0f / 128.0f;
        case PID_TIMING_ADVANCE:
            return A / 2.0f - 64.0f;
        case PID_MAF_FLOW:
            return (float)AB / 100.0f;
        case PID_CONTROL_MODULE_V:
            return (float)AB / 1000.0f;
        case PID_FUEL_RATE:
            return (float)AB / 20.0f;
        case PID_RUN_TIME:
            return (float)AB;
        default:
            return (float)A;
    }
}
