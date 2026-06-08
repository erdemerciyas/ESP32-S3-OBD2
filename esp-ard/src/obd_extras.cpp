#include "obd_extras.h"
#include "obd_parse.h"
#include "config.h"
#include <cstring>

void ObdExtras::begin(Elm327Client *client) { client_ = client; }

bool ObdExtras::requestVinRead() {
    if (busy_ || !client_ || !client_->isReady()) {
        return false;
    }
    job_ = Job::Vin;
    phase_ = Phase::Send;
    attempt_ = 0;
    busy_ = true;
    strncpy(data_.statusLine, "VIN okunuyor...", sizeof(data_.statusLine) - 1);
    return true;
}

bool ObdExtras::requestDtcRead() {
    if (busy_ || !client_ || !client_->isReady()) {
        return false;
    }
    data_.dtcCount = 0;
    data_.milOn = false;
    job_ = Job::DtcMil;
    phase_ = Phase::Send;
    attempt_ = 0;
    busy_ = true;
    strncpy(data_.statusLine, "MIL kontrol...", sizeof(data_.statusLine) - 1);
    return true;
}

bool ObdExtras::requestDtcClear() {
    if (busy_ || !client_ || !client_->isReady()) {
        return false;
    }
    job_ = Job::DtcClear;
    phase_ = Phase::Send;
    attempt_ = 0;
    busy_ = true;
    strncpy(data_.statusLine, "DTC siliniyor...", sizeof(data_.statusLine) - 1);
    return true;
}

bool ObdExtras::startSend(const char *cmd) {
    client_->setCommandTimeoutMs(OBD_EXTRAS_TIMEOUT_MS);
    if (!client_->sendCommand(cmd)) {
        client_->setCommandTimeoutMs(ELM327_CMD_TIMEOUT_MS);
        return false;
    }
    phase_ = Phase::Wait;
    phaseSinceMs_ = millis();
    return true;
}

void ObdExtras::finishJob(const char *status) {
    if (client_) {
        client_->setCommandTimeoutMs(ELM327_CMD_TIMEOUT_MS);
    }
    strncpy(data_.statusLine, status, sizeof(data_.statusLine) - 1);
    data_.statusLine[sizeof(data_.statusLine) - 1] = '\0';
    job_ = Job::None;
    phase_ = Phase::Idle;
    busy_ = false;
}

void ObdExtras::parseCurrent() {
    const String &resp = client_->lastResponse();

    switch (job_) {
    case Job::Vin: {
        char vin[20] = {};
        if (ObdParse::parseVinFromMode49(resp, vin, sizeof(vin))) {
            strncpy(data_.vin, vin, sizeof(data_.vin) - 1);
            data_.vinValid = true;
            finishJob("VIN okundu");
        } else {
            data_.vinValid = false;
            finishJob("VIN okunamadi");
        }
        break;
    }
    case Job::DtcMil: {
        uint8_t a = 0, b = 0;
        if (!ObdParse::parseMode41(resp, 0x01, a, b)) {
            if (++attempt_ < 3) {
                phase_ = Phase::Send;
                return;
            }
            finishJob("MIL okunamadi");
            return;
        }
        milByte_ = a;
        if (a == 0x00) {
            data_.milOn = false;
            data_.dtcCount = 0;
            finishJob("MIL kapali, DTC yok");
            return;
        }
        data_.milOn = (a & 0x80) != 0;
        job_ = Job::DtcList;
        phase_ = Phase::Send;
        attempt_ = 0;
        strncpy(data_.statusLine, "DTC listesi...", sizeof(data_.statusLine) - 1);
        break;
    }
    case Job::DtcList: {
        data_.dtcCount = static_cast<uint8_t>(ObdParse::parseDtcCodes(
            resp, data_.dtcCodes, sizeof(data_.dtcCodes) / sizeof(data_.dtcCodes[0])));
        if (data_.dtcCount == 0 && ++attempt_ < 5) {
            phase_ = Phase::Send;
            return;
        }
        if (data_.dtcCount == 0) {
            snprintf(data_.statusLine, sizeof(data_.statusLine),
                     data_.milOn ? "MIL acik, kod yok" : "DTC bulunamadi");
        } else {
            snprintf(data_.statusLine, sizeof(data_.statusLine), "%u DTC",
                     static_cast<unsigned>(data_.dtcCount));
        }
        finishJob(data_.statusLine);
        break;
    }
    case Job::DtcClear:
        data_.dtcCount = 0;
        data_.milOn = false;
        finishJob("MIL / DTC temizlendi");
        break;
    default:
        finishJob("Hazir");
        break;
    }
}

void ObdExtras::loop() {
    if (!busy_ || !client_) {
        return;
    }

    if (phase_ == Phase::Send) {
        const char *cmd = nullptr;
        switch (job_) {
        case Job::Vin:
            cmd = "0902";
            break;
        case Job::DtcMil:
            cmd = "0100";
            break;
        case Job::DtcList:
            cmd = "03";
            break;
        case Job::DtcClear:
            cmd = "04";
            break;
        default:
            finishJob("Hazir");
            return;
        }
        if (!startSend(cmd)) {
            finishJob("ELM mesgul");
        }
        return;
    }

    if (phase_ == Phase::Wait) {
        if (client_->hasResponse()) {
            phase_ = Phase::Parse;
            parseCurrent();
            client_->clearResponse();
            return;
        }
        if (millis() - phaseSinceMs_ > OBD_EXTRAS_TIMEOUT_MS) {
            if (job_ == Job::DtcList && ++attempt_ < 5) {
                phase_ = Phase::Send;
                phaseSinceMs_ = millis();
                return;
            }
            finishJob("Zaman asimi");
        }
    }
}
