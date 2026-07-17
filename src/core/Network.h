#pragma once

#include <Arduino.h>

#include <functional>

class Network {
public:
    static constexpr const char* kLogTag = "Network";

    // Never blocks waiting for a network. Connects when credentials are stored,
    // otherwise raises the AP-mode setup portal and returns immediately.
    static void init();

    // Drives the portal and detects connection changes. Call from loop().
    static void loop();

    static bool isConnected();
    static bool isPortalActive();

    // Fired once per transition, including when credentials first arrive at
    // runtime via the portal. Register before init().
    static void onConnected(std::function<void()> callback);
    static void onDisconnected(std::function<void()> callback);

private:
    static void startPortal();
    static void updateConnectionState();

    static bool _connected;
    static std::function<void()> _onConnected;
    static std::function<void()> _onDisconnected;
};
