#pragma once

#include <Arduino.h>
#include <WiFiClient.h>
#include "config.h"

enum class ElmState : uint8_t {
    Idle,
    Connecting,
    Initializing,
    Probing,
    Ready,
    WaitingResponse,
    Error,
};

class Elm327Client {
public:
    void begin(const char *preferredHost);
    void loop();

    bool isReady() const { return state_ == ElmState::Ready; }
    bool isBusy() const { return state_ == ElmState::WaitingResponse; }
    ElmState state() const { return state_; }
    const char *statusText() const;
    const char *connectedEndpoint() const { return activeHost_; }
    uint16_t connectedPort() const { return activePort_; }
    char activeProtocol() const { return activeProtocol_; }

    bool sendCommand(const char *cmd);
    void setCommandTimeoutMs(uint32_t ms) { cmdTimeoutMs_ = ms; }
    uint32_t commandTimeoutMs() const { return cmdTimeoutMs_; }
    bool hasResponse() const { return responseReady_; }
    const String &lastResponse() const { return lastResponse_; }
    void clearResponse();

    void reset();
    void resetEndpointLock();

private:
    enum class InitPhase : uint8_t { Reset, Config, Protocol };

    bool tryConnect();
    void advanceEndpoint();
    void buildHostList();
    void startInitSequence();
    void beginProtocolAttempt();
    void sendInitCommand(const char *cmd);
    void sendProtocolCommand();
    void sendLine(const char *cmd);
    void wakeAdapter();
    void processRx();
    void drainRx(uint32_t ms);
    bool lineComplete() const;
    bool promptEndsAt(int &outIdx) const;
    bool promptLooksValid() const;
    bool responseOk() const;
    bool probeSucceeded() const;
    bool protocolUsesCanHeaders(char proto) const;
    bool responseHasCanError() const;
    void onPromptReceived();
    void lockCurrentEndpoint();
    void onInitStepDone();
    void onProbeFailed();
    void onTcpLost();
    void beginConfigPhase();
    void cycleLineEnding();
    uint32_t initTimeoutMs() const;
    void formatStatus();

    WiFiClient client_;
    char preferredHost_[40] = {};
    char activeHost_[40] = {};
    uint16_t activePort_ = 0;
    char activeProtocol_ = '0';
    mutable char statusBuf_[72] = {};

    ElmState state_ = ElmState::Idle;
    InitPhase initPhase_ = InitPhase::Reset;
    bool protocolHeaderSent_ = false;

    bool useFixedSweep_ = true;
    size_t fixedEndpointIndex_ = 0;
    size_t hostIndex_ = 0;
    size_t portIndex_ = 0;
    size_t hostCount_ = 0;
    char hostList_[12][20] = {};

    size_t configIndex_ = 0;
    size_t protocolIndex_ = 0;
    static const char PROTOCOL_IDS[];
    static const size_t PROTOCOL_COUNT;

    uint8_t lineEndingIndex_ = 0;
    bool usedAtws_ = false;
    bool usedAtz_ = false;
    bool tcpInitDone_ = false;
    uint8_t endpointRetries_ = 0;
    uint8_t fullCycleFails_ = 0;
    uint8_t consecutiveCanErrors_ = 0;

    uint32_t stateSinceMs_ = 0;
    uint32_t lastConnectAttemptMs_ = 0;
    uint32_t tcpBackoffUntilMs_ = 0;
    uint32_t tcpReadyMs_ = 0;

    String rxBuffer_;
    String lastResponse_;
    bool responseReady_ = false;

    const char *pendingCmd_ = nullptr;
    uint32_t cmdSentMs_ = 0;
    uint32_t cmdTimeoutMs_ = ELM327_CMD_TIMEOUT_MS;
};
