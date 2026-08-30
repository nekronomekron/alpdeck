#include "utils/Logger.h"

#include <stdio.h>

namespace Logger {
namespace {

constexpr size_t kLineBufferSize = 256;

// Serialises whole lines across tasks. Created lazily so logging before
// begin() still works (just without the guarantee).
SemaphoreHandle_t lineMutex = nullptr;

Print* output = &Serial;
Level activeLevel = Info;
bool serialEnabled = true;

const char* levelName(Level level) {
    switch (level) {
    case Error:
        return "ERROR";
    case Warn:
        return "WARN ";
    case Info:
        return "INFO ";
    case Debug:
        return "DEBUG";
    default:
        return "?";
    }
}

void writeLine(Level level, const char* tag, const char* fmt, va_list args) {
    if (level > activeLevel || output == nullptr || !serialEnabled) {
        return;
    }

    // Compose the full line first so it leaves as one write: interleaved
    // fragments from two tasks would otherwise shred the output.
    char line[kLineBufferSize];
    const bool hasTag = tag != nullptr && tag[0] != '\0';
    int offset = snprintf(line, sizeof(line), "[%s]%s%s%s ", levelName(level),
                          hasTag ? "[" : "", hasTag ? tag : "",
                          hasTag ? "]" : "");
    if (offset < 0) {
        return;
    }
    // snprintf reports the untruncated length; clamp before indexing.
    if (offset >= static_cast<int>(sizeof(line))) {
        offset = sizeof(line) - 1;
    }
    vsnprintf(line + offset, sizeof(line) - offset, fmt, args);

    const bool locked = lineMutex != nullptr &&
                        xSemaphoreTake(lineMutex, pdMS_TO_TICKS(50)) == pdTRUE;

    output->println(line);

    if (locked) {
        xSemaphoreGive(lineMutex);
    }
}

}  // namespace

void begin(Print& serial, Level level) {
    output = &serial;
    activeLevel = level;
    if (lineMutex == nullptr) {
        lineMutex = xSemaphoreCreateMutex();
    }
}

void setLevel(Level level) { activeLevel = level; }

Level level() { return activeLevel; }

void setSerialOutputEnabled(bool enabled) { serialEnabled = enabled; }

bool serialOutputEnabled() { return serialEnabled; }

void log(Level level, const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    writeLine(level, tag, fmt, args);
    va_end(args);
}

}  // namespace Logger
