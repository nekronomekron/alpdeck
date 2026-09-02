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
// apps run on their own task and consume input through FreeRTOS primitives, so
// the bus is never touched from two tasks and needs no lock of its own.
//
// There are two ways to consume the same input, and which one an app picks
// decides how it behaves when it cannot keep up with the user:
//
//   read() -- every edge, in order, oldest first. What a game wants.
//   take() -- one coalesced digest of everything since the last call. What a
//             screen with a 609ms refresh wants.
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

// Everything that happened since the last take(), coalesced.
//
// The queue behind read() is the right shape for a game and the wrong shape
// for a screen. A whole-panel refresh measures 609ms here, and a user turning
// the dial puts eight detents into the queue while one frame is being pushed;
// draining them one at a time then costs five more seconds to arrive where the
// dial already was. The digest collapses that into a single move.
//
// The two classes of event are treated differently because they fail
// differently. Navigation is relative and safe to add up: eight detents is one
// move of eight, and nothing is lost by never having drawn the seven cells in
// between. An action is a commitment and must be neither invented nor doubled,
// so exactly one is held -- a second press arriving while one is still pending
// is dropped rather than queued behind it.
struct Digest {
    int16_t navX = 0;   // net steps right; negative is left
    int16_t navY = 0;   // net steps down; negative is up
    int16_t wheel = 0;  // net detents cw; negative is ccw

    // The first discrete press since the last take(), Event::None when there
    // was none.
    Event action = Event::None;

    bool any() const {
        return navX != 0 || navY != 0 || wheel != 0 || action != Event::None;
    }
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

// Consumes the digest. timeoutMs > 0 blocks the calling task until there is
// something in it. Safe from any task.
//
// Navigation arriving AFTER an action is held back for the next digest, so an
// action always applies to the position the user was looking at when they
// pressed it, never to one the same digest moved them to afterwards. That
// ordering is the reason this is not just a pair of counters.
//
// take() consumes the read() queue as well: the two are views of the same
// input, and a screen mixing them would see a press once or twice depending on
// timing. Pick one per app and stay with it.
Digest take(uint32_t timeoutMs = 0);

// Copy of what the last poll() saw. Safe from any task: it reads a cached
// snapshot and never touches the I2C bus.
Snapshot snapshot();

// Drops everything buffered -- the queue, the digest, and the wheel travel
// that has not been reported yet -- and rebaselines the encoder on where it
// stands now.
//
// Call this on every context switch: starting an app, opening a screen,
// returning from one. Without it the detents a user turned while looking at
// the previous screen arrive on the next one and move a cursor they were not
// even watching, which is the single most confusing thing input can do.
void flush();

const char* eventName(Event event);

// millis() at the last published event, or at init() if there has been none.
// The idle timer is built on this rather than on a counter of its own, so
// "activity" means exactly what the queue saw.
uint32_t lastEventMs();

}  // namespace Input
