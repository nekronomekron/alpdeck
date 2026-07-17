#include <Arduino.h>
#include <LittleFS.h>
#include <LuaWrapper.h>
#include <SD.h>

#include "config/AppConfig.h"
#include "core/Display.h"
#include "core/DynamicFTPServer.h"
#include "core/Logger.h"
#include "core/Network.h"
#include "utils/Bootscreen.h"

LuaWrapper lua;

bool sdMounted = false;
bool ftpStarted = false;

void setup() {
    // Give the USB-CDC host (serial monitor) a moment to attach, but never
    // block forever: with no host attached — or in the Wokwi simulator, where
    // the CDC connected-state isn't asserted — this must fall through.
    const unsigned long serialStart = millis();
    while (!Serial && millis() - serialStart < 1000) {
        yield();
    }

    delay(1000);
    Serial.begin(115200);

    Logger::begin(Serial, static_cast<Logger::Level>(Config::LOG_LEVEL));
    Logger::setSerialOutputEnabled(Config::LOG_SERIAL_OUTPUT);

    Display::init();

    Bootscreen bootscreen;
    Display::drawFullWindow([&](Adafruit_GFX& gfx) { bootscreen.init(gfx); });

    String script = String("print('Hello world from Lua!')");
    LOGI("Lua", "Executing Lua script: %s", lua.Lua_dostring(&script));

    // NOTE: In the Wokwi simulator, WiFi only joins the open SSID "Wokwi-GUEST"
    // (empty password). Any other SSID never reaches WL_CONNECTED.

    // delay(2000);

    // int16_t x, y, w, h;
    // bootscreen.progressWindow(x, y, w, h);

    // Display::drawPartialWindow(x, y, w, h, [&](Adafruit_GFX& gfx) {
    //     bootscreen.drawProgress(gfx, 0.2f);
    // });
    // delay(2000);
    // Display::drawPartialWindow(x, y, w, h, [&](Adafruit_GFX& gfx) {
    //     bootscreen.drawProgress(gfx, 0.4f);
    // });
    // delay(2000);
    // Display::drawPartialWindow(x, y, w, h, [&](Adafruit_GFX& gfx) {
    //     bootscreen.drawProgress(gfx, 0.6f);
    // });
    // delay(2000);
    // Display::drawPartialWindow(x, y, w, h, [&](Adafruit_GFX& gfx) {
    //     bootscreen.drawProgress(gfx, 0.8f);
    // });
    // delay(2000);
    // Display::drawPartialWindow(x, y, w, h, [&](Adafruit_GFX& gfx) {
    //     bootscreen.drawProgress(gfx, 1.0f);
    // });
    // delay(2000);

    if (!LittleFS.begin(true)) {
        LOGE("FS", "LittleFS mount failed");
    } else {
        LOGI("FS", "LittleFS mounted (%u/%u bytes used)", LittleFS.usedBytes(),
             LittleFS.totalBytes());
    }

    // The SD card shares the display's SPI bus; Display::init() already called
    // SPI.begin() for it, and the display is hibernated by now with CS released.
    sdMounted = SD.begin(Config::SD_PIN_CS, SPI);
    if (!sdMounted) {
        LOGW("FS", "SD mount failed; /%s will not be served",
             Config::FTP_MOUNT_SD);
    } else {
        LOGI("FS", "SD mounted (%llu bytes)", SD.cardSize());
    }

    // FTP only exists once there's a network to serve it on, so it is started
    // from the connect callback rather than here — that covers both a boot-time
    // auto-connect and credentials arriving later via the setup portal.
    Network::onConnected([&]() { DynamicFTPServer::init(sdMounted); });
    Network::onDisconnected(DynamicFTPServer::shutdown);
    Network::init();

    Display::shutdown();
}

void loop() {
    Network::loop();
    DynamicFTPServer::loop();

    delay(10);
}