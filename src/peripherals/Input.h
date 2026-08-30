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
namespace Input {

constexpr const char* kLogTag = "Input";

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

// Level-triggered mirror of both controllers, refreshed by every poll().
// The event queue reports edges only -- it can say a button was pressed,
// never that it is still held, and it says nothing at all about a release.
// A test or a game that needs "what is held right now" reads this instead.
//
// Gamepad fields are in the BOARD's frame, like the gamepad_* events: a
// rotated mounting is the app's business, not the driver's.
struct Snapshot {
    bool hasRotary = false;
    bool hasGamepad = false;

    bool rotarySelect = false;
    bool rotaryUp = false;
    bool rotaryLeft = false;
    bool rotaryDown = false;
    bool rotaryRight = false;
    int32_t rotaryEncoder = 0;  // detents since boot, cw positive

    bool gamepadA = false;
    bool gamepadB = false;
    bool gamepadX = false;
    bool gamepadY = false;
    bool gamepadStart = false;
    bool gamepadSelect = false;
    int8_t gamepadAxisX = 0;  // -1 left, +1 right
    int8_t gamepadAxisY = 0;  // -1 up, +1 down
    int16_t gamepadDeflectionX = 0;  // signed travel, sign matches axisX
    int16_t gamepadDeflectionY = 0;
    uint16_t gamepadStickX = 0;  // raw ADC, 0..1023
    uint16_t gamepadStickY = 0;
};

// Brings up I2C and probes both controllers. Returns true when at least one
// was found; false means the device has no way to be operated and the boot
// must not continue into the launcher.
bool init();

// At least one controller answered.
bool isAvailable();
bool hasRotary();
bool hasGamepad();

// Reads the controllers and publishes events. Main loop only.
void poll();

// Pops one event, or Event::None when the queue is empty. Safe from any task.
// timeoutMs > 0 blocks the calling task until an event arrives.
Event read(uint32_t timeoutMs = 0);

// Copy of what the last poll() saw. Safe from any task: it reads a cached
// snapshot and never touches the I2C bus.
Snapshot snapshot();

// Drops anything queued, so a starting app does not inherit stale presses.
void flush();

const char* eventName(Event event);

}  // namespace Input
