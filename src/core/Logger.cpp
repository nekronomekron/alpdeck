#include "core/Logger.h"

#include <stdio.h>

namespace {
constexpr size_t kLogBufferSize = 256;

// Serialises whole lines across tasks. Created lazily so logging before
// begin() still works (just without the guarantee).
SemaphoreHandle_t logMutex = nullptr;
}  // namespace

Print* Logger::_serial = &Serial;
Logger::Level Logger::_level = Logger::Info;
bool Logger::_serialOutputEnabled = true;

void Logger::begin(Print& serial, Level level) {
    _serial = &serial;
    _level = level;
    if (logMutex == nullptr) {
        logMutex = xSemaphoreCreateMutex();
    }
}

void Logger::setLevel(Level level) { _level = level; }

Logger::Level Logger::level() { return _level; }

void Logger::setSerialOutputEnabled(bool enabled) {
    _serialOutputEnabled = enabled;
}

bool Logger::serialOutputEnabled() { return _serialOutputEnabled; }

const char* Logger::levelName(Level level) {
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

void Logger::log(Level level, const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlog(level, tag, fmt, args);
    va_end(args);
}

void Logger::vlog(Level level, const char* tag, const char* fmt,
                  va_list args) {
    if (level > _level || _serial == nullptr || !_serialOutputEnabled) {
        return;
    }

    // Compose the full line first so it leaves as one write: interleaved
    // fragments from two tasks would otherwise shred the output.
    char buffer[kLogBufferSize];
    int offset = snprintf(buffer, sizeof(buffer), "[%s]%s%s%s ",
                          levelName(level),
                          (tag != nullptr && tag[0] != '\0') ? "[" : "",
                          (tag != nullptr) ? tag : "",
                          (tag != nullptr && tag[0] != '\0') ? "]" : "");
    if (offset < 0) {
        return;
    }
    // snprintf reports the untruncated length; clamp before indexing.
    if (offset >= static_cast<int>(sizeof(buffer))) {
        offset = sizeof(buffer) - 1;
    }
    vsnprintf(buffer + offset, sizeof(buffer) - offset, fmt, args);

    const bool locked =
        logMutex != nullptr &&
        xSemaphoreTake(logMutex, pdMS_TO_TICKS(50)) == pdTRUE;

    _serial->println(buffer);

    if (locked) {
        xSemaphoreGive(logMutex);
    }
}
