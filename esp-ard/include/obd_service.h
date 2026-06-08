#pragma once

#include <Arduino.h>
#include "elm327_client.h"

struct ObdSnapshot {
    float rpm = 0;
    float speedKmh = 0;
    float coolantC = 0;
    float intakeC = 0;
    float throttlePct = 0;
    float voltage = 0;
    float engineLoadPct = 0;
    float mapPsi = 0;
    float fuelPct = 0;
    bool rpmValid = false;
    bool speedValid = false;
    bool coolantValid = false;
    bool intakeValid = false;
    bool throttleValid = false;
    bool voltageValid = false;
    bool engineLoadValid = false;
    bool mapValid = false;
    bool fuelValid = false;
    uint32_t lastUpdateMs = 0;
};

enum class ObdPid : uint8_t {
    Rpm,
    Speed,
    Coolant,
    Intake,
    Throttle,
    Voltage,
    EngineLoad,
    Map,
    Fuel,
    Count,
};

class ObdService {
public:
    void begin(Elm327Client *client);
    void loop();

    const ObdSnapshot &snapshot() const { return data_; }
    const char *lastPidName() const;

private:
    void dispatchCommand();
    void handleResponse();

    Elm327Client *client_ = nullptr;
    ObdSnapshot data_;
    ObdPid currentPid_ = ObdPid::Rpm;
    uint32_t lastPollMs_ = 0;
    bool awaiting_ = false;
};
