#include "obd_parse.h"
#include <cstring>

namespace ObdParse {

uint8_t hexNibble(char c) {
    if (c >= '0' && c <= '9') {
        return static_cast<uint8_t>(c - '0');
    }
    if (c >= 'A' && c <= 'F') {
        return static_cast<uint8_t>(c - 'A' + 10);
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<uint8_t>(c - 'a' + 10);
    }
    return 0xFF;
}

bool extractHexTokens(const String &s, String *tokens, size_t maxTokens,
                      size_t &count) {
    count = 0;
    String work = s;
    work.toUpperCase();
    int i = 0;
    while (i < work.length() && count < maxTokens) {
        while (i < work.length() &&
               (work[i] == ' ' || work[i] == ':' || work[i] == '\n')) {
            ++i;
        }
        if (i >= work.length()) {
            break;
        }
        int start = i;
        while (i < work.length() && work[i] != ' ' && work[i] != '\n') {
            ++i;
        }
        String tok = work.substring(start, i);
        tok.trim();
        if (tok.length() >= 2) {
            tokens[count++] = tok;
        }
    }
    return count > 0;
}

static bool parsePackedMode41(const String &upper, uint8_t expectedPid,
                              uint8_t &a, uint8_t &b) {
    String hex;
    for (unsigned i = 0; i < upper.length(); ++i) {
        const char c = upper[i];
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')) {
            hex += c;
        }
    }
    if (hex.length() < 6) {
        return false;
    }
    for (size_t i = 0; i + 5 < hex.length(); i += 2) {
        const uint8_t b0 =
            static_cast<uint8_t>(strtol(hex.substring(i, i + 2).c_str(), nullptr, 16));
        const uint8_t b1 = static_cast<uint8_t>(
            strtol(hex.substring(i + 2, i + 4).c_str(), nullptr, 16));
        if (b0 != 0x41 || b1 != expectedPid) {
            continue;
        }
        a = static_cast<uint8_t>(
            strtol(hex.substring(i + 4, i + 6).c_str(), nullptr, 16));
        b = (i + 7 < hex.length())
                ? static_cast<uint8_t>(
                      strtol(hex.substring(i + 6, i + 8).c_str(), nullptr, 16))
                : 0;
        return true;
    }
    return false;
}

bool parseMode41(const String &response, uint8_t expectedPid, uint8_t &a,
                 uint8_t &b) {
    String upper = response;
    upper.toUpperCase();
    if (upper.indexOf("NO DATA") >= 0 || upper.indexOf("ERROR") >= 0 ||
        upper.indexOf("UNABLE") >= 0 || upper.indexOf("CAN ERROR") >= 0 ||
        upper.indexOf("STOPPED") >= 0) {
        return false;
    }

    if (parsePackedMode41(upper, expectedPid, a, b)) {
        return true;
    }

    String tokens[32];
    size_t n = 0;
    if (!extractHexTokens(upper, tokens, 32, n)) {
        return false;
    }

    for (size_t i = 0; i + 2 < n; ++i) {
        uint8_t b0 = 0, b1 = 0, b2 = 0, b3 = 0;
        if (tokens[i].length() < 2 || tokens[i + 1].length() < 2) {
            continue;
        }
        b0 = static_cast<uint8_t>(strtol(tokens[i].c_str(), nullptr, 16));
        b1 = static_cast<uint8_t>(strtol(tokens[i + 1].c_str(), nullptr, 16));
        if (b0 != 0x41 || b1 != expectedPid) {
            continue;
        }
        b2 = static_cast<uint8_t>(strtol(tokens[i + 2].c_str(), nullptr, 16));
        b3 = (i + 3 < n && tokens[i + 3].length() >= 2)
                 ? static_cast<uint8_t>(
                       strtol(tokens[i + 3].c_str(), nullptr, 16))
                 : 0;
        a = b2;
        b = b3;
        return true;
    }
    return false;
}

static char vinHexToChar(uint8_t asciiCode) {
    static const char table[] =
        " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
        "abcdefghijklmnopqrstuvwxyz{|}`";
    if (asciiCode > 0x20 && asciiCode < 0x80) {
        return table[asciiCode - 32];
    }
    return '\0';
}

bool parseVinFromMode49(const String &response, char *out, size_t outLen) {
    if (!out || outLen < 4) {
        return false;
    }
    out[0] = '\0';

    String upper = response;
    upper.toUpperCase();
    if (upper.indexOf("NO DATA") >= 0 || upper.indexOf("ERROR") >= 0) {
        return false;
    }

    String vin;
    uint8_t expectedLen = 0;
    String tokens[48];
    size_t n = 0;
    if (!extractHexTokens(upper, tokens, 48, n)) {
        return false;
    }

    for (size_t i = 0; i < n; ++i) {
        if (tokens[i].length() >= 3 &&
            tokens[i].startsWith("49") && expectedLen == 0) {
            expectedLen =
                static_cast<uint8_t>(strtol(tokens[i].c_str(), nullptr, 16));
            continue;
        }
        if (tokens[i].length() != 2) {
            continue;
        }
        const uint8_t byte =
            static_cast<uint8_t>(strtol(tokens[i].c_str(), nullptr, 16));
        const char ch = vinHexToChar(byte);
        if (ch) {
            vin += ch;
        }
    }

    if (expectedLen > 0 && vin.length() >= expectedLen) {
        vin = vin.substring(3);
    } else if (vin.length() > 17) {
        vin = vin.substring(vin.length() - 17);
    }

    vin.trim();
    if (vin.length() < 11) {
        return false;
    }

    strncpy(out, vin.c_str(), outLen - 1);
    out[outLen - 1] = '\0';
    return true;
}

const char *dtcPrefixForNibble(uint8_t nibble) {
    static const char *const kMap[16] = {
        "P0", "P1", "P2", "P3", "C0", "C1", "C2", "C3",
        "B0", "B1", "B2", "B3", "U0", "U1", "U2", "U3",
    };
    if (nibble >= 16) {
        return "P0";
    }
    return kMap[nibble];
}

size_t parseDtcCodes(const String &response, char codes[][6], size_t maxCodes) {
    String tokens[64];
    size_t n = 0;
    if (!extractHexTokens(response, tokens, 64, n) || maxCodes == 0) {
        return 0;
    }

    size_t out = 0;
    for (size_t i = 0; i + 1 < n && out < maxCodes; ++i) {
        if (tokens[i] != "43") {
            continue;
        }
        if (tokens[i + 1].length() < 4) {
            continue;
        }
        const uint8_t first =
            static_cast<uint8_t>(strtol(tokens[i + 1].substring(0, 1).c_str(),
                                        nullptr, 16));
        const char *prefix = dtcPrefixForNibble(first);
        char code[6];
        snprintf(code, sizeof(code), "%s%s", prefix,
                 tokens[i + 1].substring(1).c_str());
        strncpy(codes[out], code, 5);
        codes[out][5] = '\0';
        ++out;
        ++i;
    }
    return out;
}

} // namespace ObdParse
