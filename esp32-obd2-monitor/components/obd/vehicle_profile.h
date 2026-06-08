#pragma once

/**
 * Chevrolet Kalos 2005 — OBD2 / CAN veri profili.
 * ISO 9141-2 / KWP2000 (ATSP0). Bkz. docs/vehicle-kalos-2005.md
 */

#define VEHICLE_PROFILE_NAME "Chevrolet Kalos 2005"

/** Mode 01 PID listesi — hızlı poll (40 ms) */
#define VEHICLE_PID_FAST_LIST \
    PID_RPM, PID_SPEED, PID_THROTTLE_POS

/** Mode 01 PID listesi — yavaş poll (2 s) */
#define VEHICLE_PID_SLOW_LIST \
    PID_COOLANT_TEMP, PID_FUEL_LEVEL, PID_ENGINE_LOAD, PID_INTAKE_TEMP, \
    PID_MAF_RATE, PID_CONTROL_MODULE_VOLTAGE
