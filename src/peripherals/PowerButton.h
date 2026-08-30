#pragma once

#include <Arduino.h>

#include <functional>

// The physical power button, and with it the device's deep-sleep lifecycle.
//
// One button serves both directions: held down while running it puts the
// device to sleep, and pressed while asleep it wakes it again. Waking is an
// EXT0 wakeup, which means a reset -- there is no "resume", so nothing here
// tries to preserve state across it.
namespace PowerButton {

constexpr const char* kLogTag = "Power";

// Claims the pin (releasing it from RTC hold, if we just woke through it) and
// decides whether this boot is legitimate.
//
// Returns false when the device woke from a press too brief to count, having
// already put it back to sleep -- in practice it never returns in that case,
// because deep sleep does not come back.
bool begin();

// Watches for a hold long enough to mean "sleep". Call from loop().
void poll();

// Runs just before the device powers down, for whatever needs to leave the
// hardware in a presentable state -- blanking the panel, above all. Optional:
// a sleep triggered before the display exists must not try to draw.
void onBeforeSleep(std::function<void()> callback);

// Blanks, arms the wake source and stops the CPU. Never returns.
[[noreturn]] void enterDeepSleep();

}  // namespace PowerButton
