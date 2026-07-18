#pragma once

#include <Arduino.h>

// Input facade over the I2C controller daisy chain. Two controllers are
// supported, each optional, but at least one must be present:
//   - Adafruit ANO Rotary Navigation Encoder (seesaw 5740, events rotary_*)
//   - Adafruit Mini I2C Gamepad with seesaw   (seesaw 5743, events gamepad_*)
// Event names carry the source so apps can tell the controllers apart, e.g.
// two players each holding one controller.
//
// Threading: poll() owns the I2C bus and only ever runs on the main loop. Lua
// apps run on their own task and consume events through a FreeRTOS queue, so
// the bus is never touched from two tasks and needs no lock of its own.
class Input {
public:
    static constexpr const char* kLogTag = "Input";

    enum class Event : uint8_t {
        None = 0,
        RotaryCw,
        RotaryCcw,
        RotaryUp,
        RotaryDown,
        RotaryLeft,
        RotaryRight,
        RotarySelect,
        RotarySelectLong,
        GamepadUp,
        GamepadDown,
        GamepadLeft,
        GamepadRight,
        GamepadA,
        GamepadB,
        GamepadX,
        GamepadY,
        GamepadStart,
        GamepadSelect,
    };

    // Brings up I2C and probes both controllers. Returns true when at least
    // one was found; false means the device has no way to be operated and the
    // boot must not continue into the launcher.
    static bool init();

    // At least one controller answered.
    static bool isAvailable();
    static bool hasRotary();
    static bool hasGamepad();

    // Reads the controllers and publishes events. Main loop only.
    static void poll();

    // Pops one event, or Event::None when the queue is empty. Safe from any
    // task. timeoutMs > 0 blocks the calling task until an event arrives.
    static Event read(uint32_t timeoutMs = 0);

    // Drops anything queued, so a starting app does not inherit stale presses.
    static void flush();

    static const char* eventName(Event event);

private:
    static void publish(Event event);
};
