#pragma once

#include <Arduino.h>
#include <GxEPD2_BW.h>

// Optional local secrets (ignored by git). See include/secrets.h.example.
#if defined(__has_include)
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#endif

// GxEPD2 Driver
#define GxEPD2_DISPLAY_CLASS GxEPD2_BW
#define GxEPD2_DRIVER_CLASS GxEPD2_420_GDEY042T81

#define MAX_DISPLAY_BUFFER_SIZE 65536ul
#define MAX_HEIGHT(EPD)                                        \
    (EPD::HEIGHT <= MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8) \
         ? EPD::HEIGHT                                         \
         : MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8))

namespace Config {
constexpr const char* APP_NAME = "alpdeck";
constexpr const char* APP_SUBTITLE = "games | tools | hackz";

constexpr uint8_t APP_VERSION_MAJOR = 0;
constexpr uint8_t APP_VERSION_MINOR = 1;

constexpr uint8_t LOG_LEVEL = 3;  // 0=error, 1=warn, 2=info, 3=debug
constexpr bool LOG_SERIAL_OUTPUT = true;
constexpr bool LOG_SERIAL_SENSORS_OUTPUT = false;

// Display Pins
constexpr int16_t DISPLAY_PIN_CS = 7;
constexpr int16_t DISPLAY_PIN_DC = 8;
constexpr int16_t DISPLAY_PIN_RST = 2;
constexpr int16_t DISPLAY_PIN_BUSY = 1;

constexpr int16_t DISPLAY_PIN_SCK = 12;
constexpr int16_t DISPLAY_PIN_MISO = 13;
constexpr int16_t DISPLAY_PIN_MOSI = 11;

}  // namespace Config
