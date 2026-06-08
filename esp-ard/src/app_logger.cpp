#include "app_logger.h"
#include "config.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <cstring>
#include <stdarg.h>
#include <stdio.h>

namespace {

constexpr size_t kWriteBufSize = 4096;

} // namespace

bool AppLogger::mounted_ = false;
uint32_t AppLogger::lineCount_ = 0;
uint32_t AppLogger::bootCount_ = 0;
uint32_t AppLogger::lastFlushMs_ = 0;
bool AppLogger::dirty_ = false;
char AppLogger::writeBuf_[kWriteBufSize] = {};
size_t AppLogger::writeBufLen_ = 0;

void AppLogger::bumpBootCounter() {
    Preferences p;
    if (!p.begin("obd_log", false)) {
        return;
    }
    bootCount_ = p.getUInt("boots", 0) + 1;
    p.putUInt("boots", bootCount_);
    p.end();
}

void AppLogger::begin() {
    mounted_ = LittleFS.begin(true);
    if (!mounted_) {
        Serial.println("[LOG] LittleFS mount basarisiz");
        return;
    }
    bumpBootCounter();
    lineCount_ = 0;
    lastFlushMs_ = millis();
    dirty_ = false;

    if (LittleFS.exists(OBD_LOG_PATH)) {
        Serial.println("[LOG] Onceki oturum son satirlar:");
        printTailFromFile(OBD_LOG_PATH, OBD_LOG_BOOT_TAIL_LINES);
    }
    logf("\n======== SESSION #%lu ========\n", bootCount_);
}

uint32_t AppLogger::fileBytes() {
    if (!mounted_ || !LittleFS.exists(OBD_LOG_PATH)) {
        return 0;
    }
    File f = LittleFS.open(OBD_LOG_PATH, "r");
    if (!f) {
        return 0;
    }
    const size_t sz = f.size();
    f.close();
    return static_cast<uint32_t>(sz);
}

void AppLogger::rotateIfNeeded() {
    if (!mounted_) {
        return;
    }
    if (!LittleFS.exists(OBD_LOG_PATH)) {
        return;
    }
    File f = LittleFS.open(OBD_LOG_PATH, "r");
    if (!f) {
        return;
    }
    const size_t sz = f.size();
    f.close();
    if (sz < OBD_LOG_MAX_BYTES) {
        return;
    }
    if (LittleFS.exists(OBD_LOG_OLD_PATH)) {
        LittleFS.remove(OBD_LOG_OLD_PATH);
    }
    LittleFS.rename(OBD_LOG_PATH, OBD_LOG_OLD_PATH);
    logLine("[LOG] Dosya donduruldu -> obd.log.old");
}

void AppLogger::flushBuffer() {
    if (!mounted_ || writeBufLen_ == 0) {
        return;
    }

    rotateIfNeeded();
    File f = LittleFS.open(OBD_LOG_PATH, "a");
    if (f) {
        f.write(reinterpret_cast<const uint8_t *>(writeBuf_), writeBufLen_);
        f.close();
    }
    writeBufLen_ = 0;
}

void AppLogger::appendToBuffer(const char *data, size_t len) {
    if (!mounted_ || !data || len == 0) {
        return;
    }
    if (len >= kWriteBufSize) {
        flushBuffer();
        const size_t chunk = kWriteBufSize - 1;
        for (size_t off = 0; off < len; off += chunk) {
            const size_t n = (len - off > chunk) ? chunk : (len - off);
            memcpy(writeBuf_, data + off, n);
            writeBufLen_ = n;
            flushBuffer();
        }
        dirty_ = false;
        return;
    }
    if (writeBufLen_ + len > kWriteBufSize) {
        flushBuffer();
    }
    memcpy(writeBuf_ + writeBufLen_, data, len);
    writeBufLen_ += len;
    dirty_ = true;
}

void AppLogger::logLine(const char *line) {
    if (!line) {
        return;
    }
    Serial.println(line);
    if (!mounted_) {
        return;
    }
    appendToBuffer(line, strlen(line));
    appendToBuffer("\n", 1);
    ++lineCount_;
}

void AppLogger::logf(const char *fmt, ...) {
    if (!fmt) {
        return;
    }
    char buf[384];
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n <= 0) {
        return;
    }
    const size_t len = static_cast<size_t>(n < static_cast<int>(sizeof(buf))
                                               ? n
                                               : sizeof(buf) - 1);
    Serial.write(reinterpret_cast<const uint8_t *>(buf), len);
    if (!mounted_) {
        return;
    }
    appendToBuffer(buf, len);
    if (buf[len - 1] != '\n') {
        appendToBuffer("\n", 1);
    }
    ++lineCount_;
}

void AppLogger::tick(uint32_t nowMs) {
    if (!mounted_ || !dirty_) {
        return;
    }
    if (nowMs - lastFlushMs_ < OBD_LOG_FLUSH_MS) {
        return;
    }
    lastFlushMs_ = nowMs;
    flushBuffer();
    dirty_ = writeBufLen_ > 0;
}

void AppLogger::printTailFromFile(const char *path, size_t maxLines) {
    if (!mounted_ || !path || maxLines == 0) {
        return;
    }
    File f = LittleFS.open(path, "r");
    if (!f) {
        return;
    }
    const size_t sz = f.size();
    if (sz == 0) {
        f.close();
        return;
    }
    size_t start = (sz > 4096) ? (sz - 4096) : 0;
    f.seek(start);
    String tail = f.readString();
    f.close();

    size_t lineStart = 0;
    size_t shown = 0;
    for (size_t i = 0; i <= static_cast<size_t>(tail.length()); ++i) {
        if (i == static_cast<size_t>(tail.length()) || tail[i] == '\n') {
            if (i > lineStart) {
                ++shown;
            }
            lineStart = i + 1;
        }
    }
    lineStart = 0;
    size_t skip = (shown > maxLines) ? (shown - maxLines) : 0;
    size_t idx = 0;
    for (size_t i = 0; i <= static_cast<size_t>(tail.length()); ++i) {
        if (i == static_cast<size_t>(tail.length()) || tail[i] == '\n') {
            if (i > lineStart) {
                if (idx >= skip) {
                    String line = tail.substring(static_cast<int>(lineStart),
                                                 static_cast<int>(i));
                    line.trim();
                    if (line.length()) {
                        Serial.printf("  | %s\n", line.c_str());
                    }
                }
                ++idx;
            }
            lineStart = i + 1;
        }
    }
}

void AppLogger::dumpToSerial(bool includeOld) {
    if (!mounted_) {
        Serial.println("[LOG] LittleFS yok");
        return;
    }
    Serial.printf("[LOG] dump boots=%lu lines=%lu bytes=%lu\n", bootCount_,
                  lineCount_, fileBytes());
    if (includeOld && LittleFS.exists(OBD_LOG_OLD_PATH)) {
        Serial.println("----- obd.log.old -----");
        File f = LittleFS.open(OBD_LOG_OLD_PATH, "r");
        if (f) {
            while (f.available()) {
                Serial.write(f.read());
            }
            f.close();
        }
        Serial.println("\n----- obd.log -----");
    }
    if (!LittleFS.exists(OBD_LOG_PATH)) {
        Serial.println("[LOG] Dosya bos");
        return;
    }
    File f = LittleFS.open(OBD_LOG_PATH, "r");
    if (!f) {
        Serial.println("[LOG] Acilamadi");
        return;
    }
    while (f.available()) {
        Serial.write(f.read());
    }
    f.close();
    Serial.println("\n[LOG] dump bitti");
}

void AppLogger::printStats() {
    Serial.printf("[LOG] mounted=%d boots=%lu session_lines=%lu bytes=%lu\n",
                  mounted_ ? 1 : 0, bootCount_, lineCount_, fileBytes());
    Serial.printf("[LOG] path=%s max=%u KB\n", OBD_LOG_PATH,
                  static_cast<unsigned>(OBD_LOG_MAX_BYTES / 1024));
    Serial.println("[LOG] Komutlar: log dump | log clear | log stat");
}

void AppLogger::clear() {
    if (!mounted_) {
        return;
    }
    flushBuffer();
    if (LittleFS.exists(OBD_LOG_PATH)) {
        LittleFS.remove(OBD_LOG_PATH);
    }
    if (LittleFS.exists(OBD_LOG_OLD_PATH)) {
        LittleFS.remove(OBD_LOG_OLD_PATH);
    }
    lineCount_ = 0;
    logLine("[LOG] Flash log temizlendi");
}
