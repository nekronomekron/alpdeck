#pragma once

#include <Arduino.h>
#include <stdio.h>

// Escapes a string for embedding in a JSON literal. SSIDs are user-controlled
// bytes, so quotes, backslashes and control characters must not reach the
// document raw.
inline String jsonEscape(const String& raw) {
    String out;
    out.reserve(raw.length() + 8);
    for (size_t i = 0; i < raw.length(); i++) {
        const char c = raw[i];
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c >= 0 && c < 0x20) {
            char esc[7];
            snprintf(esc, sizeof(esc), "\\u%04x", c);
            out += esc;
        } else {
            out += c;
        }
    }
    return out;
}
