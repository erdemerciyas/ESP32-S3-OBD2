#pragma once

#include <Arduino.h>
#include <cstddef>
#include <cstdint>

namespace ObdParse {

uint8_t hexNibble(char c);
bool extractHexTokens(const String &s, String *tokens, size_t maxTokens,
                      size_t &count);
bool parseMode41(const String &response, uint8_t expectedPid, uint8_t &a,
                 uint8_t &b);
bool parseVinFromMode49(const String &response, char *out, size_t outLen);
size_t parseDtcCodes(const String &response, char codes[][6], size_t maxCodes);
const char *dtcPrefixForNibble(uint8_t nibble);

} // namespace ObdParse
