#pragma once

#include <Arduino.h>
#include <stdarg.h>

// Serial log with levels and tags. Thread-safe: lines are composed into a
// single buffer and written under a mutex, because both the main loop and the
// Lua task log concurrently.
class Logger {
public:
    enum Level { Error = 0, Warn = 1, Info = 2, Debug = 3 };

    static void begin(Print& serial = Serial, Level level = Info);

    static void setLevel(Level level);
    static Level level();

    static void setSerialOutputEnabled(bool enabled);
    static bool serialOutputEnabled();

    static void log(Level level, const char* tag, const char* fmt, ...);

private:
    static const char* levelName(Level level);
    static void vlog(Level level, const char* tag, const char* fmt,
                     va_list args);

    static Print* _serial;
    static Level _level;
    static bool _serialOutputEnabled;
};

#define LOGE(tag, fmt, ...) Logger::log(Logger::Error, tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) Logger::log(Logger::Warn, tag, fmt, ##__VA_ARGS__)
#define LOGI(tag, fmt, ...) Logger::log(Logger::Info, tag, fmt, ##__VA_ARGS__)
#define LOGD(tag, fmt, ...) Logger::log(Logger::Debug, tag, fmt, ##__VA_ARGS__)
