#pragma once

// Brings the device up in the one order that works, and owns the policy for
// what happens when a script ends.
//
// The order is not arbitrary. The display must be initialised before the SD
// card, because it is what calls SPI.begin() for the bus they share. Input is
// probed before the network, because a device with no controller is unusable
// and should stop on a readable screen rather than continue into a launcher
// nobody can drive. Everything runnable above this layer is a Lua script:
// boot.lua first, then the launcher, then whatever the launcher picks.
namespace BootSequence {

constexpr const char* kLogTag = "Boot";

// Runs the whole start-up. Returns once the first script has been handed to
// the Lua host -- or never, if a fatal step stopped the device.
void run();

// Pumps every subsystem that needs servicing. Call from loop().
void loop();

}  // namespace BootSequence
