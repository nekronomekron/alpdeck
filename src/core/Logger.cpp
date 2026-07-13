#include "core/Logger.h"

#include <HWCDC.h>
#include <stdio.h>
#include <string.h>

HWCDC* Logger::_serial = &Serial;
Logger::Level Logger::_level = Logger::Info;
bool Logger::_serial_output_enabled = true;

constexpr size_t _kLogBufferSize = 256;

void Logger::begin(HWCDC& serial, Level level) {
    _serial = &serial;
    _level = level;
}

void Logger::setLevel(Level level) { _level = level; }

Logger::Level Logger::level() { return _level; }

void Logger::setSerialOutputEnabled(bool enabled) { _serial_output_enabled = enabled; }

bool Logger::serialOutputEnabled() { return _serial_output_enabled; }

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

void Logger::vlog(Level level, const char* tag, const char* fmt, va_list args) {
    if (level > _level) {
        return;
    }

    char buffer[_kLogBufferSize];
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    bool print_to_serial = (_serial && _serial_output_enabled);

    if (print_to_serial) {
        _serial->print('[');
        _serial->print(levelName(level));
        _serial->print(']');
        if (tag && tag[0] != '\0') {
            _serial->print('[');
            _serial->print(tag);
            _serial->print(']');
        }
        _serial->print(' ');
        _serial->println(buffer);
    }
}