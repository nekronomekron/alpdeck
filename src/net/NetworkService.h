#pragma once

#include <Arduino.h>

#include <functional>

namespace NetworkService {

constexpr const char* kLogTag = "Network";

// Never blocks. Starts connecting when credentials are stored, otherwise
// raises the setup portal, and returns either way.
void init();

// Drives the connect state machine and the portal. Call from loop().
void loop();

bool isConnected();
bool isPortalActive();

// The radio is off entirely when this is false -- a stored setting, not a
// connection state. Distinguishing "switched off" from "not connected" is what
// lets the launcher show the difference.
bool isEnabled();

// Applies the wifi_enabled setting. Takes effect at once: switching off tears
// the connection and the portal down and powers the radio off; switching on
// restarts the connect state machine from stored credentials.
void setEnabled(bool enabled);

// Stores credentials and connects with them. Write-only by design: nothing
// reads a password back out of here.
void configure(const String& ssid, const String& password);

// Raises the setup portal on demand, rather than only when credentials happen
// to be missing.
void startSetupPortal();

// Fired once per transition, including when credentials first arrive at
// runtime via the portal. Register before init().
void onConnected(std::function<void()> callback);
void onDisconnected(std::function<void()> callback);

// Forgets the stored network and raises the portal.
void forget();

}  // namespace NetworkService
