#pragma once

#include <Arduino.h>
#include <FS.h>

// One path vocabulary for the whole firmware, mirroring the FTP mounts:
// /sd/... is the card, everything else is LittleFS. Used by the Lua host to
// load scripts and by the fs bindings, so both always agree on where a path
// leads.
namespace Vfs {

constexpr const char* kLogTag = "Vfs";

// Mounts the SD card, and is also how a card that was swapped or seated late
// gets picked up: an already-mounted card is released first, so calling this
// again is a re-read rather than a no-op.
//
// The mount lives here rather than in the boot sequence because this is what
// owns the /sd half of the path vocabulary; a mount state anywhere else could
// disagree with where a path actually leads.
//
// Not safe to call while anything is reading the card. In practice that means
// from the Lua task, which is the only thing that touches it once the boot is
// over.
bool mountSd();

bool sdMounted();

// Maps a virtual path onto the backing filesystem and the path local to it.
fs::FS& resolve(const String& path, String& localPath);

// True when the path resolves to the SD card (for diagnostics).
bool isOnSd(const String& path);

}  // namespace Vfs
