#pragma once

#include <Arduino.h>
#include <FS.h>

// One path vocabulary for the whole firmware, mirroring the FTP mounts:
// /sd/... is the card, everything else is LittleFS. Used by the Lua host to
// load scripts and by the fs bindings, so both always agree on where a path
// leads.
namespace Vfs {

// Maps a virtual path onto the backing filesystem and the path local to it.
fs::FS& resolve(const String& path, String& localPath);

// True when the path resolves to the SD card (for diagnostics).
bool isOnSd(const String& path);

}  // namespace Vfs
