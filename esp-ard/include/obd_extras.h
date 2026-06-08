#pragma once

#include <Arduino.h>
#include "elm327_client.h"

struct VehicleExtras {
    char vin[20] = {};
    bool vinValid = false;
    bool milOn = false;
    uint8_t dtcCount = 0;
    char dtcCodes[8][6] = {};
    char statusLine[64] = "Hazir";
    bool busy = false;
};

class ObdExtras {
public:
    void begin(Elm327Client *client);

    bool requestVinRead();
    bool requestDtcRead();
    bool requestDtcClear();

    void loop();
    const VehicleExtras &data() const { return data_; }
    bool isBusy() const { return busy_; }

private:
    enum class Phase : uint8_t {
        Idle,
        Send,
        Wait,
        Parse,
        Done,
    };

    enum class Job : uint8_t { None, Vin, DtcMil, DtcList, DtcClear };

    bool startSend(const char *cmd);
    void finishJob(const char *status);
    void parseCurrent();

    Elm327Client *client_ = nullptr;
    VehicleExtras data_;
    Job job_ = Job::None;
    Phase phase_ = Phase::Idle;
    uint8_t attempt_ = 0;
    uint32_t phaseSinceMs_ = 0;
    bool busy_ = false;
    uint8_t milByte_ = 0;
};
