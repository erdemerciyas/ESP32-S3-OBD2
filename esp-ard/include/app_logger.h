#pragma once

#include <Arduino.h>

class AppLogger {
public:
    static void begin();
    static bool ready() { return mounted_; }

    static void logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
    static void logLine(const char *line);

    static void tick(uint32_t nowMs);
    static void dumpToSerial(bool includeOld = true);
    static void printStats();
    static void clear();

    static uint32_t lineCount() { return lineCount_; }
    static uint32_t fileBytes();
    static uint32_t bootCount() { return bootCount_; }

private:
    static void rotateIfNeeded();
    static void appendToBuffer(const char *data, size_t len);
    static void flushBuffer();
    static void printTailFromFile(const char *path, size_t maxLines);
    static void bumpBootCounter();

    static bool mounted_;
    static uint32_t lineCount_;
    static uint32_t bootCount_;
    static uint32_t lastFlushMs_;
    static bool dirty_;
    static char writeBuf_[4096];
    static size_t writeBufLen_;
};

#define OBD_LOG(...) AppLogger::logf(__VA_ARGS__)
