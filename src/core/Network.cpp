#include "core/Network.h"

#include <WiFi.h>

#include "config/AppConfig.h"
#include "core/Logger.h"

#ifndef WOKWI_SIMULATOR
#include <WiFiManager.h>

namespace {
WiFiManager wm;
}
#endif

bool Network::_connected = false;
std::function<void()> Network::_onConnected;
std::function<void()> Network::_onDisconnected;

void Network::init() {
#ifdef WOKWI_SIMULATOR
    // The simulator only ever joins the open "Wokwi-GUEST" SSID and offers no
    // way to drive a captive portal, so the portal is compiled out entirely.
    LOGI(kLogTag, "Wokwi build: joining %s", Config::WIFI_WOKWI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(Config::WIFI_WOKWI_SSID, "");
#else
    // Stored credentials live in NVS and are only readable once the WiFi driver
    // is up, so the mode has to be set before getWiFiIsSaved() is meaningful.
    WiFi.mode(WIFI_STA);

    wm.setDebugOutput(Config::LOG_LEVEL >= Logger::Debug);
    wm.setConfigPortalBlocking(false);
    wm.setConfigPortalTimeout(0);  // portal stays up until it is configured
    wm.setConnectTimeout(Config::WIFI_CONNECT_TIMEOUT_S);
    wm.setHostname(Config::APP_NAME);

    if (wm.getWiFiIsSaved()) {
        LOGI(kLogTag, "Stored credentials found, connecting to %s",
             WiFi.SSID().c_str());
        // Bounded by setConnectTimeout. On failure this raises the portal
        // itself, so stale or mistyped credentials stay correctable.
        wm.autoConnect(Config::WIFI_AP_SSID, "");
    } else {
        LOGI(kLogTag, "No stored credentials; starting setup portal");
        startPortal();
    }
#endif

    updateConnectionState();
}

void Network::startPortal() {
#ifndef WOKWI_SIMULATOR
    LOGD(kLogTag, "Stack free before portal: %u bytes",
         uxTaskGetStackHighWaterMark(nullptr));

    // Returns immediately: the portal is non-blocking, and process() drives it.
    wm.startConfigPortal(Config::WIFI_AP_SSID, "");

    LOGD(kLogTag, "Stack free after portal: %u bytes",
         uxTaskGetStackHighWaterMark(nullptr));
    LOGI(kLogTag, "Setup portal on SSID '%s' at %s", Config::WIFI_AP_SSID,
         WiFi.softAPIP().toString().c_str());
#endif
}

void Network::loop() {
#ifndef WOKWI_SIMULATOR
    wm.process();
#endif
    updateConnectionState();
}

void Network::updateConnectionState() {
    const bool connected = WiFi.status() == WL_CONNECTED;
    if (connected == _connected) {
        return;
    }
    _connected = connected;

    if (connected) {
        LOGI(kLogTag, "Connected to %s, IP %s", WiFi.SSID().c_str(),
             WiFi.localIP().toString().c_str());
        if (_onConnected) {
            _onConnected();
        }
    } else {
        LOGW(kLogTag, "WiFi connection lost");
        if (_onDisconnected) {
            _onDisconnected();
        }
    }
}

bool Network::isConnected() { return _connected; }

bool Network::isPortalActive() {
#ifndef WOKWI_SIMULATOR
    return wm.getConfigPortalActive();
#else
    return false;
#endif
}

void Network::onConnected(std::function<void()> callback) {
    _onConnected = std::move(callback);
}

void Network::onDisconnected(std::function<void()> callback) {
    _onDisconnected = std::move(callback);
}
