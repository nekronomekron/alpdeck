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
constexpr const char* APP_SUBTITLE = "apps | games | tools";

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

// SD card. Shares the display's SPI bus, so only CS is board-specific: the
// LOLIN S3 PRO wires its onboard TF slot to IO46.
constexpr int16_t SD_PIN_CS = 46;

// FTP exposes LittleFS and the SD card as sibling mounts, /flash and /sd.
constexpr const char* FTP_MOUNT_FLASH = "flash";
constexpr const char* FTP_MOUNT_SD = "sd";
constexpr const char* FTP_USER = APP_NAME;
constexpr const char* FTP_PASSWORD = APP_NAME;

// WiFi setup portal, raised in AP mode when no credentials are stored. The AP
// is open; anyone in range can reach the portal and set the network.
constexpr const char* WIFI_AP_SSID = APP_NAME;
constexpr const char* WIFI_WOKWI_SSID = "Wokwi-GUEST";

// How long a connect attempt runs before falling back to the portal. Nothing
// blocks for this long — it only bounds the state machine.
constexpr uint16_t WIFI_CONNECT_TIMEOUT_S = 15;

}  // namespace Config
