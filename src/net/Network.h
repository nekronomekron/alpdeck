#pragma once

#include <Arduino.h>

#include <functional>

namespace Network {

constexpr const char* kLogTag = "Network";

// Never blocks. Starts connecting when credentials are stored, otherwise
// raises the setup portal, and returns either way.
void init();

// Drives the connect state machine and the portal. Call from loop().
void loop();

bool isConnected();
bool isPortalActive();

// Fired once per transition, including when credentials first arrive at
// runtime via the portal. Register before init().
void onConnected(std::function<void()> callback);
void onDisconnected(std::function<void()> callback);

// Forgets the stored network and raises the portal.
void forget();

}  // namespace Network
