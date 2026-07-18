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

// Input controllers sit on the I2C STEMMA QT daisy chain using the board's
// default I2C pins: both are broken out on the header and clear of the display
// and SD. Either controller is optional, but at least one must be present.
constexpr int16_t I2C_PIN_SDA = 9;
constexpr int16_t I2C_PIN_SCL = 10;
constexpr uint32_t I2C_FREQUENCY = 400000;

// Shared button behaviour across controllers.
constexpr uint16_t INPUT_LONG_PRESS_MS = 700;
constexpr uint8_t INPUT_DEBOUNCE_MS = 25;

// Adafruit ANO Rotary Navigation Encoder (seesaw product 5740). The ANO
// adapter answers on 0x49 — not the 0x36 the plain QT encoder uses.
constexpr uint8_t ROTARY_I2C_ADDRESS = 0x49;
constexpr uint16_t ROTARY_PRODUCT_ID = 5740;

// Switch pins are seesaw-side, not ESP32 GPIOs. All are active-low pull-ups.
constexpr uint8_t ROTARY_PIN_SELECT = 1;
constexpr uint8_t ROTARY_PIN_UP = 2;
constexpr uint8_t ROTARY_PIN_LEFT = 3;
constexpr uint8_t ROTARY_PIN_DOWN = 4;
constexpr uint8_t ROTARY_PIN_RIGHT = 5;

// Adafruit Mini I2C Gamepad with seesaw (product 5743). Button and joystick
// pins are seesaw-side, from Adafruit's gamepad_qt example.
constexpr uint8_t GAMEPAD_I2C_ADDRESS = 0x50;
constexpr uint16_t GAMEPAD_PRODUCT_ID = 5743;

constexpr uint8_t GAMEPAD_PIN_SELECT = 0;
constexpr uint8_t GAMEPAD_PIN_B = 1;
constexpr uint8_t GAMEPAD_PIN_Y = 2;
constexpr uint8_t GAMEPAD_PIN_A = 5;
constexpr uint8_t GAMEPAD_PIN_X = 6;
constexpr uint8_t GAMEPAD_PIN_START = 16;

constexpr uint8_t GAMEPAD_PIN_STICK_X = 14;
constexpr uint8_t GAMEPAD_PIN_STICK_Y = 15;

// The stick reads 0..1023 with ~512 at rest. A direction engages beyond
// STICK_PRESS from centre and releases below STICK_RELEASE (hysteresis, so a
// held stick cannot chatter). Orientation is per Adafruit's example (both axes
// inverted); verify on hardware and flip the flags if a direction is mirrored.
constexpr int16_t GAMEPAD_STICK_CENTER = 512;
constexpr int16_t GAMEPAD_STICK_PRESS = 300;
constexpr int16_t GAMEPAD_STICK_RELEASE = 150;
constexpr bool GAMEPAD_STICK_INVERT_X = true;
constexpr bool GAMEPAD_STICK_INVERT_Y = true;

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
