#include "elm327_client.h"
#include "app_storage.h"
#include "app_logger.h"
#include "config.h"
#include <WiFi.h>

static AppStorage g_elmStorage;

/* Universal OBD-II: echo/space off, CAN flow-ctrl, headers on, long frames, ECU timeout */
static const char *CONFIG_COMMANDS[] = {
    "ATE0", "ATL0", "ATS0", "ATFC1", "ATH1", "ATAL", "ATST32", nullptr};

/*
 * Protocol sweep — CAN-first (2008+ araclar), sonra AUTO, legacy OBD-II
 * 6=CAN11/500  7=CAN29/500  8=CAN11/250  9=CAN29/250
 * 0=AUTO  5=KWP fast  4=KWP  3=ISO9141  2=J1850 VPW  1=J1850 PWM
 */
const char Elm327Client::PROTOCOL_IDS[] = {'6', '7', '8', '9', '0',
                                           '5', '4', '3', '2', '1'};
const size_t Elm327Client::PROTOCOL_COUNT =
    sizeof(Elm327Client::PROTOCOL_IDS) / sizeof(Elm327Client::PROTOCOL_IDS[0]);

void Elm327Client::begin(const char *preferredHost) {
    if (preferredHost) {
        strncpy(preferredHost_, preferredHost, sizeof(preferredHost_) - 1);
        preferredHost_[sizeof(preferredHost_) - 1] = '\0';
    }
    g_elmStorage.begin();
    if (g_elmStorage.hasLockedEndpoint()) {
        OBD_LOG("[ELM] Eski endpoint kilidi temizlendi — tam sweep\n");
        g_elmStorage.clearLockedEndpoint();
    }
    fixedEndpointIndex_ = 0;
    hostIndex_ = 0;
    portIndex_ = 0;
    endpointRetries_ = 0;
    fullCycleFails_ = 0;
    lineEndingIndex_ = 1; /* WiFi ELM: CRLF (loglarda CR cevap vermedi) */
    useFixedSweep_ = true;
    buildHostList();
    reset();
    lastConnectAttemptMs_ = 0;
}

void Elm327Client::buildHostList() {
    hostCount_ = 0;
    auto addHost = [this](const char *h) {
        if (!h || !h[0] || hostCount_ >= 12) {
            return;
        }
        for (size_t i = 0; i < hostCount_; ++i) {
            if (strcmp(hostList_[i], h) == 0) {
                return;
            }
        }
        strncpy(hostList_[hostCount_], h, sizeof(hostList_[0]) - 1);
        hostList_[hostCount_][sizeof(hostList_[0]) - 1] = '\0';
        ++hostCount_;
    };

    if (g_elmStorage.hasLockedEndpoint()) {
        String lh;
        uint16_t lp = 0;
        g_elmStorage.getLockedEndpoint(lh, lp);
        addHost(lh.c_str());
        for (size_t pi = 0; pi < ELM327_PORT_COUNT; ++pi) {
            if (ELM327_PORTS[pi] == lp) {
                portIndex_ = pi;
                break;
            }
        }
        useFixedSweep_ = false;
    }

    if (WiFi.status() == WL_CONNECTED) {
        const String gw = WiFi.gatewayIP().toString();
        if (gw != "0.0.0.0") {
            addHost(gw.c_str());
        }
    }
    addHost(preferredHost_);
    for (size_t i = 0; i < ELM327_DISCOVERY_HOST_COUNT; ++i) {
        addHost(ELM327_DISCOVERY_HOSTS[i]);
    }
    for (size_t i = 0; i < ELM327_HOST_CANDIDATE_COUNT; ++i) {
        addHost(ELM327_HOST_CANDIDATES[i]);
    }

    OBD_LOG("[ELM] %u host, %u sabit endpoint, sweep=%s\n", hostCount_,
                  static_cast<unsigned>(ELM327_FIXED_ENDPOINT_COUNT),
                  useFixedSweep_ ? "sabit" : "kilitli");
}

void Elm327Client::reset() {
    if (client_.connected()) {
        client_.stop();
    }
    state_ = ElmState::Idle;
    initPhase_ = InitPhase::Reset;
    configIndex_ = 0;
    protocolIndex_ = 0;
    protocolHeaderSent_ = false;
    usedAtws_ = false;
    usedAtz_ = false;
    tcpInitDone_ = false;
    rxBuffer_ = "";
    lastResponse_ = "";
    responseReady_ = false;
    pendingCmd_ = nullptr;
    activeHost_[0] = '\0';
    activePort_ = 0;
    activeProtocol_ = '0';
    tcpReadyMs_ = 0;
    consecutiveCanErrors_ = 0;
    statusBuf_[0] = '\0';
}

void Elm327Client::resetEndpointLock() {
    g_elmStorage.clearLockedEndpoint();
    fixedEndpointIndex_ = 0;
    hostIndex_ = 0;
    portIndex_ = 0;
    endpointRetries_ = 0;
    fullCycleFails_ = 0;
    useFixedSweep_ = true;
    buildHostList();
}

void Elm327Client::sendLine(const char *cmd) {
    if (!client_.connected()) {
        return;
    }
    if (cmd) {
        client_.print(cmd);
    }
    switch (lineEndingIndex_) {
    case 1:
        client_.print("\r\n");
        break;
    case 2:
        client_.print('\n');
        break;
    default:
        client_.print('\r');
        break;
    }
}

void Elm327Client::cycleLineEnding() {
    lineEndingIndex_ = static_cast<uint8_t>((lineEndingIndex_ + 1) % 3);
    static const char *names[] = {"CR", "CRLF", "LF"};
    OBD_LOG("[ELM] Satir sonu: %s\n", names[lineEndingIndex_]);
}

void Elm327Client::wakeAdapter() {
    for (int i = 0; i < 5; ++i) {  /* 3 yerine 5 deneme */
        sendLine("");
        drainRx(200);  /* 120 yerine 200ms */
    }
}

void Elm327Client::drainRx(uint32_t ms) {
    const uint32_t until = millis() + ms;
    while (millis() < until) {
        processRx();
        if (!client_.available()) {
            yield();
        }
    }
}

void Elm327Client::sendInitCommand(const char *cmd) {
    if (!cmd || !client_.connected()) {
        return;
    }
    rxBuffer_ = "";
    responseReady_ = false;
    sendLine(cmd);
    stateSinceMs_ = millis();
    OBD_LOG("[ELM] >> %s\n", cmd);
}

bool Elm327Client::protocolUsesCanHeaders(char proto) const {
    return proto == '6' || proto == '7' || proto == '8' || proto == '9' ||
           proto == '0';
}

bool Elm327Client::responseHasCanError() const {
    const String &r = lastResponse_;
    if (r.indexOf("CAN ERROR") >= 0 || r.indexOf("BUS INIT") >= 0 ||
        r.indexOf("UNABLE TO CONNECT") >= 0 || r.indexOf("STOPPED") >= 0) {
        return true;
    }
    return false;
}

void Elm327Client::beginProtocolAttempt() {
    initPhase_ = InitPhase::Protocol;
    protocolHeaderSent_ = false;
    state_ = ElmState::Initializing;
    const char proto = PROTOCOL_IDS[protocolIndex_];
    const char *hdr = protocolUsesCanHeaders(proto) ? "ATH1" : "ATH0";
    OBD_LOG("[ELM] Protokol %u/%u ATSP%c (%s)\n",
                  static_cast<unsigned>(protocolIndex_ + 1),
                  static_cast<unsigned>(PROTOCOL_COUNT), proto, hdr);
    sendInitCommand(hdr);
}

void Elm327Client::sendProtocolCommand() {
    char cmd[8];
    snprintf(cmd, sizeof(cmd), "ATSP%c", PROTOCOL_IDS[protocolIndex_]);
    sendInitCommand(cmd);
}

uint32_t Elm327Client::initTimeoutMs() const {
    if (state_ == ElmState::Probing) {
        const char p = PROTOCOL_IDS[protocolIndex_];
        if (p == '0') {
            return ELM327_PROBE_TIMEOUT_MS;
        }
        if (protocolUsesCanHeaders(p)) {
            return ELM327_PROBE_TIMEOUT_MS * 2 / 3;
        }
        return ELM327_PROBE_TIMEOUT_MS / 2;
    }
    if (initPhase_ == InitPhase::Reset) {
        return usedAtz_ ? ELM327_ATZ_TIMEOUT_MS : ELM327_ATWS_TIMEOUT_MS;
    }
    if (initPhase_ == InitPhase::Protocol) {
        return ELM327_ATSP_TIMEOUT_MS;
    }
    return ELM327_CMD_TIMEOUT_MS;
}

bool Elm327Client::promptEndsAt(int &outIdx) const {
    int idx = rxBuffer_.lastIndexOf('>');
    if (idx < 0) {
        idx = rxBuffer_.lastIndexOf('?');
    }
    outIdx = idx;
    return idx >= 0;
}

bool Elm327Client::promptLooksValid() const {
    int idx = -1;
    if (!promptEndsAt(idx)) {
        return false;
    }
    if (idx == 0) {
        return true;
    }
    static const char *markers[] = {
        "ELM327", "ELM", "OBD", "STN", "ICAR", "VLINK", "VGATE", "OK",
        "SEARCHING", "UNABLE", "NO DATA"};
    for (const char *m : markers) {
        if (rxBuffer_.indexOf(m) >= 0) {
            return true;
        }
    }
    return rxBuffer_.length() > 1;
}

bool Elm327Client::responseOk() const {
    const String &r = lastResponse_;
    if (r.indexOf('?') >= 0 && r.indexOf("OK") < 0) {
        return false;
    }
    if (r.indexOf("ERROR") >= 0) {
        return false;
    }
    return r.indexOf("OK") >= 0 || r.indexOf("ELM") >= 0 ||
           r.indexOf("SEARCHING") >= 0 || initPhase_ == InitPhase::Reset;
}

bool Elm327Client::probeSucceeded() const {
    const String &r = lastResponse_;
    String u = r;
    u.toUpperCase();
    if (u.indexOf("UNABLE") >= 0 || u.indexOf("NO DATA") >= 0 ||
        u.indexOf("ERROR") >= 0 || u.indexOf("CAN ERROR") >= 0 ||
        u.indexOf("BUS INIT") >= 0) {
        return false;
    }
    if (u.indexOf("SEARCHING") >= 0) {
        return false;
    }
    String hex;
    for (unsigned i = 0; i < u.length(); ++i) {
        const char c = u[i];
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')) {
            hex += c;
        }
    }
    if (hex.indexOf("4100") >= 0 || hex.indexOf("410C") >= 0 ||
        hex.indexOf("410D") >= 0 || hex.indexOf("4105") >= 0) {
        return true;
    }
    return hex.length() >= 6 && hex.startsWith("41");
}

void Elm327Client::onInitStepDone() {
    switch (initPhase_) {
    case InitPhase::Reset:
        if (lastResponse_.indexOf("ELM") >= 0 ||
            lastResponse_.indexOf("STN") >= 0 ||
            lastResponse_.indexOf("ICAR") >= 0) {
            OBD_LOG("[ELM] Adaptör: %s\n", lastResponse_.c_str());
        }
        initPhase_ = InitPhase::Config;
        configIndex_ = 0;
        sendInitCommand(CONFIG_COMMANDS[0]);
        break;

    case InitPhase::Config:
        if (CONFIG_COMMANDS[configIndex_] &&
            lastResponse_.indexOf('?') >= 0 &&
            lastResponse_.indexOf("OK") < 0) {
            OBD_LOG("[ELM] %s desteklenmiyor, atlaniyor\n",
                          CONFIG_COMMANDS[configIndex_]);
        }
        ++configIndex_;
        if (CONFIG_COMMANDS[configIndex_]) {
            sendInitCommand(CONFIG_COMMANDS[configIndex_]);
        } else {
            beginProtocolAttempt();
        }
        break;

    case InitPhase::Protocol:
        if (!protocolHeaderSent_) {
            protocolHeaderSent_ = true;
            sendProtocolCommand();
            break;
        }
        if (!responseOk() && lastResponse_.indexOf('?') >= 0) {
            OBD_LOG("[ELM] ATSP reddedildi");
            onProbeFailed();
            return;
        }
        state_ = ElmState::Probing;
        stateSinceMs_ = millis();
        sendInitCommand("0100");
        break;

    default:
        break;
    }
}

void Elm327Client::onProbeFailed() {
    ++protocolIndex_;
    if (protocolIndex_ < PROTOCOL_COUNT) {
        beginProtocolAttempt();
        return;
    }

    OBD_LOG("[ELM] Tum protokoller basarisiz");
    ++fullCycleFails_;
    if (fullCycleFails_ >= 1) {
        cycleLineEnding();
    }
    /* Adaptör yanıt vermiyorsa (2 kez) → endpoint'i temizle */
    if (fullCycleFails_ >= 2) {
        if (g_elmStorage.hasLockedEndpoint()) {
            OBD_LOG("[ELM] Adaptör unuttuk — endpoint temizleniyor");
            g_elmStorage.clearLockedEndpoint();
        }
        fullCycleFails_ = 0;
        useFixedSweep_ = true;
        fixedEndpointIndex_ = 0;
        buildHostList();
    }
    protocolIndex_ = 0;
    reset();
    advanceEndpoint();
    state_ = ElmState::Idle;
    lastConnectAttemptMs_ = millis();
}

void Elm327Client::loop() {
    if (WiFi.status() != WL_CONNECTED) {
        if (state_ != ElmState::Idle && state_ != ElmState::Error) {
            reset();
            state_ = ElmState::Idle;
        }
        return;
    }

    switch (state_) {
    case ElmState::Idle:
        if (tcpBackoffUntilMs_ != 0 && millis() < tcpBackoffUntilMs_) {
            break;
        }
        tcpBackoffUntilMs_ = 0;
        if (millis() - lastConnectAttemptMs_ >= ELM327_CONNECT_RETRY_MS) {
            lastConnectAttemptMs_ = millis();
            state_ = ElmState::Connecting;
            stateSinceMs_ = millis();
            tcpReadyMs_ = 0;
            tcpInitDone_ = false;
            if (!tryConnect()) {
                state_ = ElmState::Idle;
            }
        }
        break;

    case ElmState::Connecting:
        if (client_.connected()) {
            if (tcpReadyMs_ == 0) {
                tcpReadyMs_ = millis();
                OBD_LOG("[ELM] TCP OK %s:%u\n", activeHost_, activePort_);
                client_.setNoDelay(true);
                rxBuffer_ = "";
                wakeAdapter();
            }
            if (!tcpInitDone_ && millis() - tcpReadyMs_ >= ELM327_TCP_WAKE_MS) {
                tcpInitDone_ = true;
                drainRx(200);
                protocolIndex_ = 0;
                fullCycleFails_ = 0;
                if (lineComplete() && promptLooksValid()) {
                    onPromptReceived();
                    beginConfigPhase();
                } else {
                    startInitSequence();
                }
            }
        } else if (millis() - stateSinceMs_ > ELM327_CONNECT_TIMEOUT_MS) {
            OBD_LOG("[ELM] TCP zaman asimi");
            if (client_.connected()) {
                client_.stop();
            }
            endpointRetries_++;
            /* TCP timeout bile olsa, uzun backoff yap */
            uint32_t backoff = ELM327_TCP_FAIL_BACKOFF_MS * 2;
            tcpBackoffUntilMs_ = millis() + backoff;
            if (endpointRetries_ >= ELM327_ENDPOINT_RETRIES) {
                endpointRetries_ = 0;
                advanceEndpoint();
            }
            state_ = ElmState::Idle;
            lastConnectAttemptMs_ = millis();
        }
        break;

    case ElmState::Initializing:
        processRx();
        if (client_.available()) {
            stateSinceMs_ = millis();
        }
        if (lineComplete()) {
            onPromptReceived();
            onInitStepDone();
        } else if (millis() - stateSinceMs_ > initTimeoutMs()) {
            OBD_LOG("[ELM] Init timeout (phase=%u proto=%u)\n",
                          static_cast<unsigned>(initPhase_),
                          static_cast<unsigned>(protocolIndex_));
            if (initPhase_ == InitPhase::Protocol) {
                onProbeFailed();
                break;
            }
            if (initPhase_ == InitPhase::Reset) {
                OBD_LOG("[ELM] ATWS yanit yok — satir sonu degistiriliyor\n");
                cycleLineEnding();
                /* Hala yanıt yoksa port değiş */
                if (fullCycleFails_ >= 1) {
                    OBD_LOG("[ELM] Tum satir sonlari denendi, porta geçiliyor\n");
                    fullCycleFails_ = 0;
                    portIndex_ = (portIndex_ + 1) % ELM327_PORT_COUNT;
                    endpointRetries_ = 0;
                } else {
                    fullCycleFails_++;
                }
            } else {
                cycleLineEnding();
            }
            reset();
            endpointRetries_++;
            /* Init timeout bile olsa backoff süresini arttır */
            uint32_t backoff = ELM327_TCP_FAIL_BACKOFF_MS * 2;
            tcpBackoffUntilMs_ = millis() + backoff;
            if (endpointRetries_ >= ELM327_ENDPOINT_RETRIES) {
                endpointRetries_ = 0;
                advanceEndpoint();
            }
            state_ = ElmState::Idle;
            lastConnectAttemptMs_ = millis();
        }
        break;

    case ElmState::Probing:
        processRx();
        if (client_.available()) {
            stateSinceMs_ = millis();
        }
        if (lineComplete()) {
            onPromptReceived();
            if (probeSucceeded()) {
                activeProtocol_ = PROTOCOL_IDS[protocolIndex_];
                lockCurrentEndpoint();
                state_ = ElmState::Ready;
                fullCycleFails_ = 0;
                endpointRetries_ = 0;
                consecutiveCanErrors_ = 0;
                OBD_LOG("[ELM] Ready — ECU OK (ATSP%c, %s:%u)\n", activeProtocol_,
                        activeHost_, activePort_);
            } else if (protocolUsesCanHeaders(PROTOCOL_IDS[protocolIndex_]) &&
                       lastResponse_.indexOf("410C") < 0 &&
                       lastResponse_.indexOf("4100") < 0) {
                OBD_LOG("[ELM] 0100 bos, 010C deneniyor");
                sendInitCommand("010C");
            } else {
                OBD_LOG("[ELM] probe basarisiz: %s\n", lastResponse_.c_str());
                onProbeFailed();
            }
        } else if (millis() - stateSinceMs_ > initTimeoutMs()) {
            OBD_LOG("[ELM] probe zaman asimi");
            onProbeFailed();
        }
        break;

    case ElmState::Ready:
        processRx();
        break;

    case ElmState::WaitingResponse:
        processRx();
        if (lineComplete()) {
            onPromptReceived();
            /* AT-komutu disinda CAN hata takibi */
            if (pendingCmd_ && pendingCmd_[0] != 'A') {
                if (responseHasCanError()) {
                    consecutiveCanErrors_++;
                    OBD_LOG("[ELM] CAN hata (%u): %s\n",
                                  consecutiveCanErrors_, lastResponse_.c_str());
                    if (consecutiveCanErrors_ >= 5) {
                        OBD_LOG("[ELM] Ardışık CAN hata — yeniden baslatiliyor\n");
                        consecutiveCanErrors_ = 0;
                        reset();
                        lastConnectAttemptMs_ = millis();
                        break;
                    }
                } else if (lastResponse_.indexOf("NO DATA") < 0) {
                    consecutiveCanErrors_ = 0;
                }
            }
            if (pendingCmd_ && strcmp(pendingCmd_, "ATDPN") == 0) {
                OBD_LOG("[ELM] Aktif protokol: %s\n", lastResponse_.c_str());
                clearResponse();
            }
            state_ = ElmState::Ready;
            pendingCmd_ = nullptr;
        } else if (millis() - cmdSentMs_ > cmdTimeoutMs_) {
            if (pendingCmd_ && strcmp(pendingCmd_, "ATDPN") == 0) {
                state_ = ElmState::Ready;
                pendingCmd_ = nullptr;
                break;
            }
            OBD_LOG("[ELM] Timeout: %s\n", pendingCmd_ ? pendingCmd_ : "?");
            lastResponse_ = "";
            responseReady_ = true;
            state_ = ElmState::Ready;
            pendingCmd_ = nullptr;
        }
        break;

    case ElmState::Error:
        if (millis() - lastConnectAttemptMs_ >= ELM327_CONNECT_RETRY_MS) {
            state_ = ElmState::Idle;
        }
        break;
    }

    const bool hadTcp =
        (state_ == ElmState::Connecting && tcpReadyMs_ != 0) ||
        state_ == ElmState::Initializing || state_ == ElmState::Probing ||
        state_ == ElmState::Ready || state_ == ElmState::WaitingResponse;
    if (hadTcp && !client_.connected()) {
        onTcpLost();
    }
}

bool Elm327Client::tryConnect() {
    if (client_.connected()) {
        client_.stop();
    }

    if (g_elmStorage.hasLockedEndpoint()) {
        if (hostCount_ == 0) {
            buildHostList();
        }
        strncpy(activeHost_, hostList_[hostIndex_], sizeof(activeHost_) - 1);
        activeHost_[sizeof(activeHost_) - 1] = '\0';
        activePort_ = ELM327_PORTS[portIndex_];
    } else if (useFixedSweep_ &&
               fixedEndpointIndex_ < ELM327_FIXED_ENDPOINT_COUNT) {
        strncpy(activeHost_, ELM327_FIXED_ENDPOINTS[fixedEndpointIndex_].host,
                sizeof(activeHost_) - 1);
        activeHost_[sizeof(activeHost_) - 1] = '\0';
        activePort_ = ELM327_FIXED_ENDPOINTS[fixedEndpointIndex_].port;
    } else {
        if (hostCount_ == 0) {
            buildHostList();
        }
        strncpy(activeHost_, hostList_[hostIndex_], sizeof(activeHost_) - 1);
        activeHost_[sizeof(activeHost_) - 1] = '\0';
        activePort_ = ELM327_PORTS[portIndex_];
    }

    OBD_LOG("[ELM] TCP %s:%u (deneme %u)\n", activeHost_, activePort_,
                  static_cast<unsigned>(endpointRetries_ + 1));
    client_.setTimeout(ELM327_CONNECT_TIMEOUT_MS);
    const bool ok =
        client_.connect(activeHost_, activePort_, ELM327_CONNECT_TIMEOUT_MS);
    if (!ok) {
        OBD_LOG("[ELM] TCP baglanamadi — %u sn sonra tekrar",
                static_cast<unsigned>(ELM327_TCP_FAIL_BACKOFF_MS / 1000));
        endpointRetries_++;
        /* TCP başarısızlık sayısına göre backoff süresini arttır */
        uint32_t backoff = ELM327_TCP_FAIL_BACKOFF_MS;
        if (endpointRetries_ >= 2) {
            backoff = ELM327_TCP_FAIL_BACKOFF_MS * 3; /* 15 sn */
        }
        tcpBackoffUntilMs_ = millis() + backoff;
        lastConnectAttemptMs_ = millis();
        if (endpointRetries_ >= ELM327_ENDPOINT_RETRIES) {
            endpointRetries_ = 0;
            advanceEndpoint();
        }
    }
    return ok;
}

void Elm327Client::advanceEndpoint() {
    if (g_elmStorage.hasLockedEndpoint()) {
        portIndex_ = (portIndex_ + 1) % ELM327_PORT_COUNT;
        if (portIndex_ == 0) {
            hostIndex_ = (hostIndex_ + 1) % hostCount_;
        }
        return;
    }

    if (useFixedSweep_ && fixedEndpointIndex_ + 1 < ELM327_FIXED_ENDPOINT_COUNT) {
        ++fixedEndpointIndex_;
        return;
    }

    useFixedSweep_ = false;
    portIndex_ = (portIndex_ + 1) % ELM327_PORT_COUNT;
    if (portIndex_ == 0) {
        hostIndex_ = (hostIndex_ + 1) % hostCount_;
        if (hostIndex_ == 0) {
            OBD_LOG("[ELM] Tum adresler denendi");
            fixedEndpointIndex_ = 0;
            useFixedSweep_ = true;
            buildHostList();
        }
    }
}

void Elm327Client::lockCurrentEndpoint() {
    g_elmStorage.setLockedEndpoint(activeHost_, activePort_);
    useFixedSweep_ = false;
}

void Elm327Client::onTcpLost() {
    OBD_LOG("[ELM] TCP koptu");
    reset();
    advanceEndpoint();
    lastConnectAttemptMs_ = 0;
}

void Elm327Client::beginConfigPhase() {
    state_ = ElmState::Initializing;
    initPhase_ = InitPhase::Config;
    configIndex_ = 0;
    protocolIndex_ = 0;
    protocolHeaderSent_ = false;
    usedAtws_ = true;
    usedAtz_ = true;
    OBD_LOG("[ELM] Prompt hazir — ATZ atlandi\n");
    sendInitCommand(CONFIG_COMMANDS[0]);
}

void Elm327Client::startInitSequence() {
    state_ = ElmState::Initializing;
    initPhase_ = InitPhase::Reset;
    configIndex_ = 0;
    protocolHeaderSent_ = false;
    usedAtws_ = true;
    usedAtz_ = false;
    OBD_LOG("[ELM] WiFi TCP — once ATWS (warm start)\n");
    sendInitCommand("ATWS");
}

void Elm327Client::processRx() {
    while (client_.available()) {
        char c = static_cast<char>(client_.read());
        if (c == '\r') {
            continue;
        }
        rxBuffer_ += c;
        if (rxBuffer_.length() > 768) {
            rxBuffer_.remove(0, rxBuffer_.length() - 384);
        }
    }
}

bool Elm327Client::lineComplete() const {
    int idx = -1;
    return promptEndsAt(idx);
}

void Elm327Client::onPromptReceived() {
    int prompt = -1;
    promptEndsAt(prompt);
    String body = rxBuffer_.substring(0, prompt);
    body.trim();
    body.replace("\n", " ");
    while (body.indexOf("  ") >= 0) {
        body.replace("  ", " ");
    }
    lastResponse_ = body;
    responseReady_ = true;
    rxBuffer_ = "";
    if (body.length() > 0) {
        OBD_LOG("[ELM] << %s\n", body.c_str());
    }
}

bool Elm327Client::sendCommand(const char *cmd) {
    if (state_ != ElmState::Ready || !client_.connected()) {
        return false;
    }
    responseReady_ = false;
    lastResponse_ = "";
    rxBuffer_ = "";
    pendingCmd_ = cmd;
    sendLine(cmd);
    cmdSentMs_ = millis();
    state_ = ElmState::WaitingResponse;
    stateSinceMs_ = cmdSentMs_;
    return true;
}

void Elm327Client::clearResponse() {
    responseReady_ = false;
    lastResponse_ = "";
}

void Elm327Client::formatStatus() {
    if (activeHost_[0]) {
        snprintf(statusBuf_, sizeof(statusBuf_), "OBD: %s:%u", activeHost_,
                 activePort_);
    } else {
        strncpy(statusBuf_, "OBD: Araniyor...", sizeof(statusBuf_) - 1);
        statusBuf_[sizeof(statusBuf_) - 1] = '\0';
    }
}

const char *Elm327Client::statusText() const {
    switch (state_) {
    case ElmState::Connecting:
        const_cast<Elm327Client *>(this)->formatStatus();
        return statusBuf_;
    case ElmState::Initializing:
        if (initPhase_ == InitPhase::Reset) {
            return "OBD: ELM uyandir...";
        }
        if (initPhase_ == InitPhase::Protocol) {
            snprintf(const_cast<Elm327Client *>(this)->statusBuf_,
                     sizeof(statusBuf_), "OBD: P%c %u/%u",
                     PROTOCOL_IDS[protocolIndex_],
                     static_cast<unsigned>(protocolIndex_ + 1),
                     static_cast<unsigned>(PROTOCOL_COUNT));
            return statusBuf_;
        }
        return "OBD: ELM ayar...";
    case ElmState::Probing:
        snprintf(const_cast<Elm327Client *>(this)->statusBuf_, sizeof(statusBuf_),
                 "OBD: ECU %c %u/%u", PROTOCOL_IDS[protocolIndex_],
                 static_cast<unsigned>(protocolIndex_ + 1),
                 static_cast<unsigned>(PROTOCOL_COUNT));
        return statusBuf_;
    case ElmState::Ready: {
        auto *self = const_cast<Elm327Client *>(this);
        snprintf(self->statusBuf_, sizeof(statusBuf_), "OBD: ATSP%c",
                 self->activeProtocol_);
        return statusBuf_;
    }
    case ElmState::WaitingResponse:
        return "OBD: Okuma...";
    default:
        return "OBD: Araniyor...";
    }
}
