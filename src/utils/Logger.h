#pragma once

#include <Arduino.h>
#include <stdarg.h>

// Serial log with levels and tags. Thread-safe: lines are composed into a
// single buffer and written under a mutex, because both the main loop and the
// Lua task log concurrently.
namespace Logger {

enum Level { Error = 0, Warn = 1, Info = 2, Debug = 3 };

void begin(Print& serial = Serial, Level level = Info);

void setLevel(Level level);
Level level();

void setSerialOutputEnabled(bool enabled);
bool serialOutputEnabled();

void log(Level level, const char* tag, const char* fmt, ...);

}  // namespace Logger

#define LOGE(tag, fmt, ...) Logger::log(Logger::Error, tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) Logger::log(Logger::Warn, tag, fmt, ##__VA_ARGS__)
#define LOGI(tag, fmt, ...) Logger::log(Logger::Info, tag, fmt, ##__VA_ARGS__)
#define LOGD(tag, fmt, ...) Logger::log(Logger::Debug, tag, fmt, ##__VA_ARGS__)
