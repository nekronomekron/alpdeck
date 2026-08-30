#pragma once

#include <Arduino.h>

#include <functional>

class Network {
public:
    static constexpr const char* kLogTag = "Network";

    // Never blocks. Starts connecting when credentials are stored, otherwise
    // raises the setup portal, and returns either way.
    static void init();

    // Drives the connect state machine and the portal. Call from loop().
    static void loop();

    static bool isConnected();
    static bool isPortalActive();

    // Fired once per transition, including when credentials first arrive at
    // runtime via the portal. Register before init().
    static void onConnected(std::function<void()> callback);
    static void onDisconnected(std::function<void()> callback);

    // Forgets the stored network and raises the portal.
    static void forget();

private:
    enum class State { Idle, Connecting, Connected, Portal };

    static void startConnect(const String& ssid, const String& password);
    static void startPortal();
    static void applyCredentials(const String& ssid, const String& password);
    static String statusJson();
    static void setConnected(bool connected);

    static State _state;
    static bool _lastAttemptFailed;
    static unsigned long _connectStartedMs;
    static std::function<void()> _onConnected;
    static std::function<void()> _onDisconnected;
};
