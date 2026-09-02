#pragma once

#include <Arduino.h>

// FTP access to the device's storage: LittleFS as /flash and, when a card is
// mounted, SD as /sd. Started from the network's connect callback and torn
// down again on disconnect, so it only ever exists while it is reachable.
//
// Threading: the server object is created and destroyed by loop() alone, on
// the main loop. Everything else here only records what the server SHOULD be
// -- because the callers are the settings hook, the connect callback and two
// Lua bindings, and those run on the Lua task while the main loop is inside
// server->handle(). Deleting it out from under that is a dangling pointer, and
// it was reachable from the options menu.
namespace FtpService {

constexpr const char* kLogTag = "FTP";

// Records whether the server should be running. Takes effect on the next
// loop(), which is where the object is actually built or torn down.
//
// Whether the card is mounted is not a parameter: start-up asks Vfs, so there
// is one answer to "is there an SD card", in the module that owns the mount.
void setEnabled(bool enabled);

// Asks for the running server to be rebuilt, so its filesystem list or its
// login matches what has just changed. Nothing happens while it is stopped: a
// server that starts later reads both fresh anyway. Cheap -- a new listening
// socket, not a reconnect.
void requestRebuild();

// Replaces the stored login and rebuilds the server. Write-only: nothing reads
// the password back out, for the same reason the WiFi key is not readable.
void configure(const String& user, const String& password);

// True while a login other than the compiled-in default is stored. The
// settings screen shows this so "alpdeck/alpdeck" is visibly the default
// rather than silently the password.
bool hasCustomLogin();

// Reconciles the server with what was asked for, then services client
// connections. Call from loop().
void loop();

}  // namespace FtpService
