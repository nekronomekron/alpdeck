#pragma once

#include <Arduino.h>

// Adafruit ANO Rotary Navigation Encoder (seesaw product 5740): a rotary dial
// plus a 5-way navigation switch.
//
// Threading: poll() owns the I2C bus and only ever runs on the main loop. Lua
// apps run on their own task and consume events through a FreeRTOS queue, so
// the bus is never touched from two tasks and needs no lock of its own.
class Input {
public:
    static constexpr const char* kLogTag = "Input";

    enum class Event : uint8_t {
        None = 0,
        RotateCw,
        RotateCcw,
        Up,
        Down,
        Left,
        Right,
        Select,
        SelectLong,
    };

    // Brings up I2C and the encoder. Returns false when the device is absent or
    // reports the wrong firmware; the rest of the system carries on without it.
    static bool init();

    static bool isAvailable();

    // Reads the encoder and publishes events. Main loop only.
    static void poll();

    // Pops one event, or Event::None when the queue is empty. Safe from any
    // task. timeoutMs > 0 blocks the calling task until an event arrives.
    static Event read(uint32_t timeoutMs = 0);

    // Drops anything queued, so a starting app does not inherit stale presses.
    static void flush();

    static const char* eventName(Event event);

private:
    static void publish(Event event);
    static void pollSwitches(uint32_t nowMs);
    static void pollRotation();

    static bool _available;
};
