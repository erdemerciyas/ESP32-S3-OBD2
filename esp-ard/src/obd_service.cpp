#include "obd_service.h"
#include "obd_formulas.h"
#include "obd_parse.h"
#include "config.h"

void ObdService::begin(Elm327Client *client) {
    client_ = client;
    data_ = ObdSnapshot{};
}

const char *ObdService::lastPidName() const {
    switch (currentPid_) {
    case ObdPid::Rpm:
        return "RPM";
    case ObdPid::Speed:
        return "Speed";
    case ObdPid::Coolant:
        return "Coolant";
    case ObdPid::Intake:
        return "Intake";
    case ObdPid::Throttle:
        return "Throttle";
    case ObdPid::Voltage:
        return "Voltage";
    case ObdPid::EngineLoad:
        return "Load";
    case ObdPid::Map:
        return "MAP";
    case ObdPid::Fuel:
        return "Fuel";
    default:
        return "";
    }
}

void ObdService::loop() {
    if (!client_ || !client_->isReady()) {
        awaiting_ = false;
        return;
    }

    if (awaiting_) {
        if (client_->hasResponse()) {
            handleResponse();
            client_->clearResponse();
            awaiting_ = false;
            currentPid_ = static_cast<ObdPid>(
                (static_cast<uint8_t>(currentPid_) + 1) %
                static_cast<uint8_t>(ObdPid::Count));
            lastPollMs_ = millis();
        }
        return;
    }

    if (client_->isBusy()) {
        return;
    }

    if (millis() - lastPollMs_ < OBD_PID_INTERVAL_MS) {
        return;
    }

    dispatchCommand();
}

void ObdService::dispatchCommand() {
    const char *cmd = nullptr;
    switch (currentPid_) {
    case ObdPid::Rpm:
        cmd = "010C";
        break;
    case ObdPid::Speed:
        cmd = "010D";
        break;
    case ObdPid::Coolant:
        cmd = "0105";
        break;
    case ObdPid::Intake:
        cmd = "010F";
        break;
    case ObdPid::Throttle:
        cmd = "0111";
        break;
    case ObdPid::Voltage:
        cmd = "ATRV";
        break;
    case ObdPid::EngineLoad:
        cmd = "0104";
        break;
    case ObdPid::Map:
        cmd = "010B";
        break;
    case ObdPid::Fuel:
        cmd = "012F";
        break;
    default:
        break;
    }

    if (cmd && client_->sendCommand(cmd)) {
        awaiting_ = true;
    }
}

void ObdService::handleResponse() {
    const String &resp = client_->lastResponse();
    data_.lastUpdateMs = millis();

    if (currentPid_ == ObdPid::Voltage) {
        float v = 0;
        bool ok = false;
        int vIdx = resp.indexOf('V');
        if (vIdx > 0) {
            String num = resp.substring(0, vIdx);
            num.trim();
            v = num.toFloat();
            ok = v > 0 && v < 20;
        } else {
            v = resp.toFloat();
            ok = v > 0 && v < 20;
        }
        if (ok) {
            data_.voltage = v;
            data_.voltageValid = true;
        }
        return;
    }

    uint8_t pidByte = 0;
    switch (currentPid_) {
    case ObdPid::Rpm:
        pidByte = 0x0C;
        break;
    case ObdPid::Speed:
        pidByte = 0x0D;
        break;
    case ObdPid::Coolant:
        pidByte = 0x05;
        break;
    case ObdPid::Intake:
        pidByte = 0x0F;
        break;
    case ObdPid::Throttle:
        pidByte = 0x11;
        break;
    case ObdPid::EngineLoad:
        pidByte = 0x04;
        break;
    case ObdPid::Map:
        pidByte = 0x0B;
        break;
    case ObdPid::Fuel:
        pidByte = 0x2F;
        break;
    default:
        return;
    }

    uint8_t a = 0, b = 0;
    if (!ObdParse::parseMode41(resp, pidByte, a, b)) {
        return;
    }

    switch (currentPid_) {
    case ObdPid::Rpm:
        data_.rpm = ObdFormulas::rpmFromAB(a, b);
        data_.rpmValid = true;
        break;
    case ObdPid::Speed:
        data_.speedKmh = a;
        data_.speedValid = true;
        break;
    case ObdPid::Coolant:
        data_.coolantC = ObdFormulas::tempByteC(a);
        data_.coolantValid = true;
        break;
    case ObdPid::Intake:
        data_.intakeC = ObdFormulas::tempByteC(a);
        data_.intakeValid = true;
        break;
    case ObdPid::Throttle:
        data_.throttlePct = ObdFormulas::percentByte(a);
        data_.throttleValid = true;
        break;
    case ObdPid::EngineLoad:
        data_.engineLoadPct = ObdFormulas::percentByte(a);
        data_.engineLoadValid = true;
        break;
    case ObdPid::Map:
        data_.mapPsi = ObdFormulas::mapKpaToPsi(a);
        data_.mapValid = true;
        break;
    case ObdPid::Fuel:
        data_.fuelPct = ObdFormulas::percentByte(a);
        data_.fuelValid = true;
        break;
    default:
        break;
    }
}
