#pragma once

#include <Arduino.h>

// FTP access to the device's storage: LittleFS as /flash and, when a card is
// mounted, SD as /sd. Started from the network's connect callback and torn
// down again on disconnect, so it only ever exists while it is reachable.
namespace FtpService {

constexpr const char* kLogTag = "FTP";

// Asks Vfs for the card rather than being told about it: one answer to "is
// there an SD card", in the module that owns the mount.
void start();
void stop();

// Rebuilds the server, but only if it is running, so its filesystem list
// matches a card that has just been mounted or lost. Cheap: a new listening
// socket, not a reconnect.
void remount();

// Replaces the stored login and restarts the server if it is running.
// Write-only: nothing reads the password back out, for the same reason the
// WiFi key is not readable.
void configure(const String& user, const String& password);

// True while a login other than the compiled-in default is stored. The
// settings screen shows this so "alpdeck/alpdeck" is visibly the default
// rather than silently the password.
bool hasCustomLogin();

// Services client connections. Call from loop(); no-op while stopped.
void loop();

}  // namespace FtpService
