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

// SD card. Shares the display's SPI bus, so only CS is board-specific. Matches
// TF_CS in the LOLIN S3 PRO variant's pins_arduino.h.
constexpr int16_t SD_PIN_CS = 46;

// Adafruit ANO Rotary Navigation Encoder on the I2C STEMMA QT adapter (seesaw,
// product 5740) using the board's default I2C pins: both are broken out on the
// header and clear of the display and SD.
constexpr int16_t I2C_PIN_SDA = 9;
constexpr int16_t I2C_PIN_SCL = 10;
constexpr uint32_t I2C_FREQUENCY = 400000;

// The ANO adapter answers on 0x49 — not the 0x36 the plain QT encoder uses.
constexpr uint8_t ENCODER_I2C_ADDRESS = 0x49;
constexpr uint16_t ENCODER_PRODUCT_ID = 5740;

// Switch pins are seesaw-side, not ESP32 GPIOs. All are active-low pull-ups.
constexpr uint8_t ENCODER_PIN_SELECT = 1;
constexpr uint8_t ENCODER_PIN_UP = 2;
constexpr uint8_t ENCODER_PIN_LEFT = 3;
constexpr uint8_t ENCODER_PIN_DOWN = 4;
constexpr uint8_t ENCODER_PIN_RIGHT = 5;

constexpr uint16_t ENCODER_LONG_PRESS_MS = 700;
constexpr uint8_t ENCODER_DEBOUNCE_MS = 25;

// Lua script run at the end of setup(), read from LittleFS. It reaches the
// device over FTP as /flash/boot.lua, and ships in the data/ directory.
constexpr const char* BOOT_SCRIPT_PATH = "/boot.lua";

// The launcher lives on LittleFS, not the SD card, so the device still boots to
// something usable with no card inserted. Apps live on the SD card.
constexpr const char* LAUNCHER_PATH = "/launcher.lua";
constexpr const char* APPS_DIR = "/sd/apps";
constexpr const char* APP_ENTRY_FILE = "main.lua";
constexpr const char* APP_MANIFEST_FILE = "app.lua";

// Lua apps run on their own FreeRTOS task so a busy script cannot stall the
// network or FTP. Pinned to core 1 alongside the Arduino loop; core 0 carries
// the WiFi stack.
constexpr uint32_t LUA_TASK_STACK_BYTES = 16384;
constexpr uint8_t LUA_TASK_PRIORITY = 1;
constexpr uint8_t LUA_TASK_CORE = 1;

// The VM hook fires every N bytecode instructions to yield and to check for a
// stop request. Lower is more responsive but costs throughput.
constexpr int LUA_HOOK_INSTRUCTION_COUNT = 1000;

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
