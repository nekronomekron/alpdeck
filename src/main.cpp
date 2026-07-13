#include <Arduino.h>
#include <LuaWrapper.h>
#include <SPIFFS.h>
#include <SimpleFTPServer.h>
#include <WiFi.h>

#include "config/AppConfig.h"
#include "core/Logger.h"

#ifndef WOKWI_SIMULATOR
const char* ssid = "IoT";
const char* password = "05021904";
#else
const char* ssid = "Wokwi-GUEST";
const char* password = "";
#endif

LuaWrapper lua;
FtpServer ftpSrv;

void setup() {
    // Give the USB-CDC host (serial monitor) a moment to attach, but never
    // block forever: with no host attached — or in the Wokwi simulator, where
    // the CDC connected-state isn't asserted — this must fall through.
    const unsigned long serialStart = millis();
    while (!Serial && millis() - serialStart < 3000) {
        yield();
    }

    Serial.begin(115200);

    Logger::begin(Serial, static_cast<Logger::Level>(Config::LOG_LEVEL));
    Logger::setSerialOutputEnabled(Config::LOG_SERIAL_OUTPUT);

    String script = String("print('Hello world from Lua!')");
    LOGI("Lua", "Executing Lua script: %s", lua.Lua_dostring(&script));

    // NOTE: In the Wokwi simulator, WiFi only joins the open SSID "Wokwi-GUEST"
    // (empty password). Any other SSID never reaches WL_CONNECTED.

    LOGI("Wifi", "%s, %s", ssid, password);

    WiFi.begin(ssid, password);
    const unsigned long wifiTimeoutMs = 15000;
    const unsigned long wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < wifiTimeoutMs) {
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

    // Mount filesystem before starting FTP
    if (SPIFFS.begin(true)) {
        Serial.println("SPIFFS opened!");
        // username, password for ftp.
        ftpSrv.begin("esp32", "esp32");
    }
}

void loop() { ftpSrv.handleFTP(); }