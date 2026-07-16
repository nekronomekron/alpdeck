#include <Arduino.h>
#include <ESP-FTP-Server-Lib.h>
#include <LittleFS.h>
#include <LuaWrapper.h>
#include <SD.h>
#include <WiFi.h>

#include "config/AppConfig.h"
#include "core/Display.h"
#include "core/Logger.h"
#include "utils/Bootscreen.h"

#ifndef WOKWI_SIMULATOR
const char* ssid = "IoT";
const char* password = "05021904";
#else
const char* ssid = "Wokwi-GUEST";
const char* password = "";
#endif

LuaWrapper lua;
FTPServer ftpSrv;
bool sdMounted = false;

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

    LOGI("Wifi", "%s, %s", ssid, password);

    WiFi.begin(ssid, password);
    const unsigned long wifiTimeoutMs = 15000;
    const unsigned long wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - wifiStart < wifiTimeoutMs) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi connect timed out; continuing without network.");
        return;
    }

    Serial.print("Connected. IP: ");
    Serial.println(WiFi.localIP());

    ftpSrv.addUser(Config::FTP_USER, Config::FTP_PASSWORD);
    ftpSrv.addFilesystem(Config::FTP_MOUNT_FLASH, &LittleFS);
    if (sdMounted) {
        ftpSrv.addFilesystem(Config::FTP_MOUNT_SD, &SD);
    }
    ftpSrv.begin();
    LOGI("FTP", "Server started on %s", WiFi.localIP().toString().c_str());

    Display::shutdown();
}

void loop() {
    ftpSrv.handle();

    delay(10);
}