#include "core/Network.h"

#include <Preferences.h>
#include <WiFi.h>

#include "config/AppConfig.h"
#include "core/CaptivePortal.h"
#include "core/Logger.h"
#include "utils/JsonUtil.h"

namespace {
Preferences prefs;

constexpr const char* kPrefsNamespace = "alpdeck-wifi";
constexpr const char* kKeySsid = "ssid";
constexpr const char* kKeyPass = "pass";

struct Credentials {
    String ssid;
    String password;
    bool valid() const { return !ssid.isEmpty(); }
};

Credentials loadCredentials() {
    Credentials creds;
    prefs.begin(kPrefsNamespace, true);  // read-only
    creds.ssid = prefs.getString(kKeySsid, "");
    creds.password = prefs.getString(kKeyPass, "");
    prefs.end();
    return creds;
}

void storeCredentials(const String& ssid, const String& password) {
    prefs.begin(kPrefsNamespace, false);
    prefs.putString(kKeySsid, ssid);
    prefs.putString(kKeyPass, password);
    prefs.end();
}

void eraseCredentials() {
    prefs.begin(kPrefsNamespace, false);
    prefs.clear();
    prefs.end();
}
}  // namespace

Network::State Network::_state = Network::State::Idle;
bool Network::_lastAttemptFailed = false;
unsigned long Network::_connectStartedMs = 0;
std::function<void()> Network::_onConnected;
std::function<void()> Network::_onDisconnected;

void Network::init() {
#ifdef WOKWI_SIMULATOR
    // The simulator only joins the open "Wokwi-GUEST" SSID and cannot drive a
    // captive portal, so the portal is skipped entirely there.
    LOGI(kLogTag, "Wokwi build: joining %s", Config::WIFI_WOKWI_SSID);
    WiFi.mode(WIFI_STA);
    startConnect(Config::WIFI_WOKWI_SSID, "");
#else
    WiFi.mode(WIFI_STA);
    // Keep the driver from rewriting its own copy of the credentials on every
    // begin(); the Preferences namespace is the single source of truth.
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);

    CaptivePortal::onSubmit(applyCredentials);
    CaptivePortal::onStatus(statusJson);

    const Credentials creds = loadCredentials();
    if (creds.valid()) {
        LOGI(kLogTag, "Stored credentials found for '%s'", creds.ssid.c_str());
        startConnect(creds.ssid, creds.password);
    } else {
        LOGI(kLogTag, "No stored credentials; starting setup portal");
        startPortal();
    }
#endif
}

void Network::startConnect(const String& ssid, const String& password) {
    LOGI(kLogTag, "Connecting to '%s'", ssid.c_str());

    // Returns immediately; loop() polls for the result. This is the whole
    // reason the boot no longer stalls waiting on a network.
    WiFi.begin(ssid.c_str(), password.c_str());

    _connectStartedMs = millis();
    _state = State::Connecting;
}

void Network::startPortal() {
#ifndef WOKWI_SIMULATOR
    CaptivePortal::begin(Config::WIFI_AP_SSID);
    _state = State::Portal;
#endif
}

void Network::applyCredentials(const String& ssid, const String& password) {
    storeCredentials(ssid, password);
    _lastAttemptFailed = false;

    // The portal deliberately stays up until the connection succeeds, so the
    // user keeps a page to read the result on if the password was wrong.
    startConnect(ssid, password);
}

void Network::forget() {
    eraseCredentials();
    WiFi.disconnect(false, true);
    setConnected(false);
    LOGW(kLogTag, "Stored network forgotten");
    startPortal();
}

void Network::loop() {
    CaptivePortal::loop();

    const bool connected = WiFi.status() == WL_CONNECTED;

    switch (_state) {
    case State::Connecting:
        if (connected) {
            _lastAttemptFailed = false;
            setConnected(true);
            _state = State::Connected;
            // Tear the AP down only once there is a real connection to fall
            // back on, so a failed attempt never strands the user.
            if (CaptivePortal::isActive()) {
                CaptivePortal::stop();
            }
        } else if (millis() - _connectStartedMs >
                   Config::WIFI_CONNECT_TIMEOUT_S * 1000UL) {
            _lastAttemptFailed = true;
            LOGW(kLogTag, "Connect timed out");
            // Stored credentials that don't work are still correctable: fall
            // back to the portal rather than retrying forever in silence.
            startPortal();
            if (CaptivePortal::isActive()) {
                _state = State::Portal;
            } else {
                // No portal on this build (simulator): restart the timeout so
                // the driver keeps retrying without logging every loop pass.
                _connectStartedMs = millis();
            }
        }
        break;

    case State::Connected:
        if (!connected) {
            setConnected(false);
            // The driver auto-reconnects; just wait for it to come back.
            _connectStartedMs = millis();
            _state = State::Connecting;
        }
        break;

    case State::Portal:
        // A late connect can still land here if the AP reappeared.
        if (connected) {
            setConnected(true);
            _state = State::Connected;
            CaptivePortal::stop();
        }
        break;

    case State::Idle:
        break;
    }
}

void Network::setConnected(bool connected) {
    static bool wasConnected = false;
    if (connected == wasConnected) {
        return;
    }
    wasConnected = connected;

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

String Network::statusJson() {
    String state;
    if (_state == State::Connected) {
        state = "connected";
    } else if (_lastAttemptFailed) {
        state = "failed";
    } else {
        state = "connecting";
    }

    String out = "{\"state\":\"" + state + "\"";
    if (_state == State::Connected) {
        out += ",\"ssid\":\"" + jsonEscape(WiFi.SSID()) + "\"";
        out += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
    }
    out += '}';
    return out;
}

bool Network::isConnected() { return _state == State::Connected; }

bool Network::isPortalActive() { return CaptivePortal::isActive(); }

void Network::onConnected(std::function<void()> callback) {
    _onConnected = std::move(callback);
}

void Network::onDisconnected(std::function<void()> callback) {
    _onDisconnected = std::move(callback);
}
