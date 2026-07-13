#pragma once

#include <Arduino.h>
#include <stdarg.h>
#include <stddef.h>

#include "HWCDC.h"

class Logger {
public:
    enum Level { Error = 0, Warn = 1, Info = 2, Debug = 3 };

    static void begin(HWCDC& serial = Serial, Level level = Info);

    static void setLevel(Level level);
    static Level level();

    static void setSerialOutputEnabled(bool enabled);
    static bool serialOutputEnabled();

    static void log(Level level, const char* tag, const char* fmt, ...);

private:
    static const char* levelName(Level level);
    static void vlog(Level level, const char* tag, const char* fmt, va_list args);

    static HWCDC* _serial;
    static Level _level;
    static bool _serial_output_enabled;
};

#define LOGE(tag, fmt, ...) Logger::log(Logger::Error, tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) Logger::log(Logger::Warn, tag, fmt, ##__VA_ARGS__)
#define LOGI(tag, fmt, ...) Logger::log(Logger::Info, tag, fmt, ##__VA_ARGS__)
#define LOGD(tag, fmt, ...) Logger::log(Logger::Debug, tag, fmt, ##__VA_ARGS__)
