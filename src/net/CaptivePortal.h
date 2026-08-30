#pragma once

#include <Arduino.h>

#include <functional>

// AP-mode setup portal: soft AP, wildcard DNS and a small web UI for picking a
// network. Owns no WiFi credentials itself — submissions are handed to the
// onSubmit callback, and it renders whatever onStatus reports back.
class CaptivePortal {
public:
    static constexpr const char* kLogTag = "Portal";

    // Raises the AP and starts serving. Non-blocking; drive it with loop().
    // Pass an empty password for an open AP.
    static void begin(const char* apSsid, const char* apPassword = "");
    static void stop();

    // Services DNS and HTTP. Call from loop() while active.
    static void loop();

    static bool isActive();

    // Invoked with the credentials the user submitted.
    static void onSubmit(std::function<void(const String&, const String&)> cb);

    // Must return a JSON object describing the current connection state; it is
    // polled by the portal page after a submit.
    static void onStatus(std::function<String()> cb);

private:
    static void handleRoot();
    static void handleScan();
    static void handleSave();
    static void handleStatus();
    static void handleRedirect();

    static bool _active;
    static std::function<void(const String&, const String&)> _onSubmit;
    static std::function<String()> _onStatus;
};
