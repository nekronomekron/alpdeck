#include <Preferences.h>
#include <WiFi.h>

#include "config/AppConfig.h"
#include "core/Settings.h"
#include "net/CaptivePortal.h"
#include "net/NetworkService.h"
#include "utils/JsonUtil.h"
#include "utils/Logger.h"

namespace NetworkService {
namespace {

Preferences prefs;

constexpr const char* kPrefsNamespace = "alpdeck-wifi";
constexpr const char* kKeySsid = "ssid";
constexpr const char* kKeyPass = "pass";

enum class State { Idle, Connecting, Connected, Portal };

State connectionState = State::Idle;
bool radioEnabled = true;
bool lastAttemptFailed = false;
unsigned long connectStartedMs = 0;
std::function<void()> connectedCallback;
std::function<void()> disconnectedCallback;

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

void startConnect(const String& ssid, const String& password) {
    LOGI(kLogTag, "Connecting to '%s'", ssid.c_str());

    // Returns immediately; loop() polls for the result. This is the whole
    // reason the boot no longer stalls waiting on a network.
    WiFi.begin(ssid.c_str(), password.c_str());

    connectStartedMs = millis();
    connectionState = State::Connecting;
}

void startPortal() {
#ifndef WOKWI_SIMULATOR
    CaptivePortal::begin(Config::WIFI_AP_SSID);
    connectionState = State::Portal;
#endif
}

void applyCredentials(const String& ssid, const String& password) {
    storeCredentials(ssid, password);
    lastAttemptFailed = false;

    // The portal deliberately stays up until the connection succeeds, so the
    // user keeps a page to read the result on if the password was wrong.
    startConnect(ssid, password);
}

// Announces a transition. Edge-triggered: calling it twice with the same value
// fires nothing, which is what lets loop() call it on every pass.
//
// Never call this directly -- go through enterState(). A callback is entitled
// to ask isConnected() and be told the truth, and it can only be told the truth
// if the state was already updated when the callback ran.
void setConnected(bool connected) {
    static bool wasConnected = false;
    if (connected == wasConnected) {
        return;
    }
    wasConnected = connected;

    if (connected) {
        LOGI(kLogTag, "Connected to %s, IP %s", WiFi.SSID().c_str(),
             WiFi.localIP().toString().c_str());
        if (connectedCallback) {
            connectedCallback();
        }
    } else {
        LOGW(kLogTag, "WiFi connection lost");
        if (disconnectedCallback) {
            disconnectedCallback();
        }
    }
}

// Moves to a new state and only then announces it.
//
// The order is the whole point. isConnected() reads connectionState, and the
// connect callback is applyFtpSetting(), which asks isConnected() to decide
// whether to start the server. Announcing first meant it was told "not
// connected" by the very transition that had just connected, so it stopped a
// server that had never started -- and silently, because stopping nothing logs
// nothing. FTP was unreachable after every boot until something else changed a
// setting and ran applyFtpSetting() again, which is why toggling it in the menu
// "fixed" it.
void enterState(State next, bool connected) {
    connectionState = next;
    setConnected(connected);
}

String statusJson() {
    String label;
    if (connectionState == State::Connected) {
        label = "connected";
    } else if (lastAttemptFailed) {
        label = "failed";
    } else {
        label = "connecting";
    }

    String out = "{\"state\":\"" + label + "\"";
    if (connectionState == State::Connected) {
        out += ",\"ssid\":\"" + jsonEscape(WiFi.SSID()) + "\"";
        out += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
    }
    out += '}';
    return out;
}

// Brings the radio up from whatever credentials are stored, or raises the
// portal when there are none. Shared by init() and by switching WiFi back on.
void connectOrOfferPortal() {
    const Credentials creds = loadCredentials();
    if (creds.valid()) {
        LOGI(kLogTag, "Stored credentials found for '%s'", creds.ssid.c_str());
        startConnect(creds.ssid, creds.password);
    } else {
        LOGI(kLogTag, "No stored credentials; starting setup portal");
        startPortal();
    }
}

// Everything the radio holds open, released in the order that leaves nothing
// half-torn-down: portal, then listeners, then the radio itself.
void powerDown() {
    if (CaptivePortal::isActive()) {
        CaptivePortal::stop();
    }
    // Drives the disconnect callback, which stops FTP. Idle first, for the
    // same reason the connect side sets Connected first.
    enterState(State::Idle, false);
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    lastAttemptFailed = false;
}

}  // namespace

void init() {
    radioEnabled = Settings::getBool(Settings::kWifiEnabled);
    if (!radioEnabled) {
        // Nothing is brought up at all -- not the radio, not the portal. This
        // is the setting's whole purpose, so it is checked before any of it.
        LOGI(kLogTag, "WiFi is switched off in settings");
        WiFi.mode(WIFI_OFF);
        return;
    }

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

    connectOrOfferPortal();
#endif
}

bool isEnabled() { return radioEnabled; }

void setEnabled(bool enabled) {
    if (enabled == radioEnabled) {
        return;
    }
    radioEnabled = enabled;

    if (!enabled) {
        LOGI(kLogTag, "Switching WiFi off");
        powerDown();
        return;
    }

    LOGI(kLogTag, "Switching WiFi on");
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    connectOrOfferPortal();
}

void configure(const String& ssid, const String& password) {
    if (!radioEnabled) {
        // Storing credentials while the radio is off would leave them
        // unapplied and the caller none the wiser, so switch it on.
        setEnabled(true);
        Settings::setBool(Settings::kWifiEnabled, true);
    }
    LOGI(kLogTag, "Credentials set for '%s'", ssid.c_str());
    applyCredentials(ssid, password);
}

void startSetupPortal() {
    if (!radioEnabled) {
        setEnabled(true);
        Settings::setBool(Settings::kWifiEnabled, true);
    }
    startPortal();
}

void loop() {
    CaptivePortal::loop();

    const bool connected = WiFi.status() == WL_CONNECTED;

    switch (connectionState) {
    case State::Connecting:
        if (connected) {
            lastAttemptFailed = false;
            enterState(State::Connected, true);
            // Tear the AP down only once there is a real connection to fall
            // back on, so a failed attempt never strands the user.
            if (CaptivePortal::isActive()) {
                CaptivePortal::stop();
            }
        } else if (millis() - connectStartedMs >
                   Config::WIFI_CONNECT_TIMEOUT_S * 1000UL) {
            lastAttemptFailed = true;
            LOGW(kLogTag, "Connect timed out");
            // Stored credentials that don't work are still correctable: fall
            // back to the portal rather than retrying forever in silence.
            startPortal();
            if (CaptivePortal::isActive()) {
                connectionState = State::Portal;
            } else {
                // No portal on this build (simulator): restart the timeout so
                // the driver keeps retrying without logging every loop pass.
                connectStartedMs = millis();
            }
        }
        break;

    case State::Connected:
        if (!connected) {
            // The driver auto-reconnects; just wait for it to come back.
            connectStartedMs = millis();
            enterState(State::Connecting, false);
        }
        break;

    case State::Portal:
        // A late connect can still land here if the AP reappeared.
        if (connected) {
            enterState(State::Connected, true);
            CaptivePortal::stop();
        }
        break;

    case State::Idle:
        break;
    }
}

void forget() {
    eraseCredentials();
    WiFi.disconnect(false, true);
    enterState(State::Idle, false);
    LOGW(kLogTag, "Stored network forgotten");
    startPortal();
}

bool isConnected() { return connectionState == State::Connected; }

bool isPortalActive() { return CaptivePortal::isActive(); }

void onConnected(std::function<void()> callback) {
    connectedCallback = std::move(callback);
}

void onDisconnected(std::function<void()> callback) {
    disconnectedCallback = std::move(callback);
}

}  // namespace NetworkService
