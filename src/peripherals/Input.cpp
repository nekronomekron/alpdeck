#include "peripherals/Input.h"

#include <Wire.h>

#include "config/AppConfig.h"
#include "peripherals/GamepadController.h"
#include "peripherals/InputDigest.h"
#include "peripherals/RotaryController.h"
#include "utils/Logger.h"

namespace Input {

namespace {

RotaryController rotary;
GamepadController gamepad;
QueueHandle_t events = nullptr;

constexpr uint8_t kQueueLength = 16;

// Written by poll() on the main loop, read by whatever task an app runs on.
// The spinlock keeps a reader from seeing half of an update; the struct is a
// few dozen bytes, so the critical section stays far shorter than one I2C
// transaction.
Snapshot latest;
portMUX_TYPE latestMux = portMUX_INITIALIZER_UNLOCKED;

uint32_t lastActivityMs = 0;

// Encoder position already folded into the digest. The wheel is read as an
// absolute value rather than as a stream of detent events, so coalescing it
// costs nothing no matter how long a refresh kept the app from asking.
//
// Written by poll() on the main loop and by flush() from whatever task calls
// it. A single aligned word, and the worst an interleaving can do is credit one
// detent to the screen being left instead of dropping it -- a lock for that
// would cost more than it saves.
int32_t wheelBaseline = 0;

// Queues one event for whichever task is reading. Handed to the drivers as a
// callback, so it has to exist before poll() uses it.
void publish(Event event) {
    if (events == nullptr) {
        return;
    }
    lastActivityMs = millis();
    InputDigest::classify(event);

    // Drop rather than block: input is worthless once it is stale, and the main
    // loop must never wait on a Lua app that has stopped reading. The digest
    // above has already kept whatever mattered about this event.
    //
    // The digest does its own waking, and deliberately not for a wheel event:
    // read() blocks on the queue itself, so nothing is waiting on a semaphore
    // for one.
    if (xQueueSend(events, &event, 0) != pdPASS) {
        LOGD(kLogTag, "Event queue full, dropped %s", eventName(event));
    }
}

}  // namespace

bool init() {
    events = xQueueCreate(kQueueLength, sizeof(Event));
    if (events == nullptr || !InputDigest::init()) {
        LOGE(kLogTag, "Could not allocate the input primitives");
        return false;
    }

    lastActivityMs = millis();
    Wire.begin(Config::I2C_PIN_SDA, Config::I2C_PIN_SCL, Config::I2C_FREQUENCY);

    // Both controllers are optional and share the daisy-chained bus; probe
    // each independently. The device is only unusable with neither present.
    rotary.begin();
    gamepad.begin();

    if (!isAvailable()) {
        LOGE(kLogTag, "No input controller found (rotary 0x%02X, gamepad 0x%02X)",
             Config::ROTARY_I2C_ADDRESS, Config::GAMEPAD_I2C_ADDRESS);
        return false;
    }
    return true;
}

bool isAvailable() { return hasRotary() || hasGamepad(); }

bool hasRotary() { return rotary.available(); }

bool hasGamepad() { return gamepad.available(); }

void poll() {
    const uint32_t nowMs = millis();
    rotary.poll(nowMs, publish);
    gamepad.poll(nowMs, publish);

    // Rebuild the level-triggered mirror from what the drivers just sampled.
    const RotaryController::State rotaryState = rotary.state();
    const GamepadController::State gamepadState = gamepad.state();

    Snapshot fresh;
    fresh.hasRotary = rotary.available();
    fresh.hasGamepad = gamepad.available();

    fresh.rotarySelect = rotaryState.select;
    fresh.rotaryUp = rotaryState.up;
    fresh.rotaryLeft = rotaryState.left;
    fresh.rotaryDown = rotaryState.down;
    fresh.rotaryRight = rotaryState.right;
    fresh.rotaryEncoder = rotaryState.encoder;

    fresh.gamepadA = gamepadState.a;
    fresh.gamepadB = gamepadState.b;
    fresh.gamepadX = gamepadState.x;
    fresh.gamepadY = gamepadState.y;
    fresh.gamepadStart = gamepadState.start;
    fresh.gamepadSelect = gamepadState.select;
    fresh.gamepadAxisX = gamepadState.axisX;
    fresh.gamepadAxisY = gamepadState.axisY;
    fresh.gamepadDeflectionX = gamepadState.deflectionX;
    fresh.gamepadDeflectionY = gamepadState.deflectionY;
    fresh.gamepadStickX = gamepadState.stickX;
    fresh.gamepadStickY = gamepadState.stickY;

    portENTER_CRITICAL(&latestMux);
    latest = fresh;
    portEXIT_CRITICAL(&latestMux);

    // Fold the wheel in as absolute travel. However long a refresh kept the app
    // from calling take(), the whole turn arrives as one number and nothing had
    // to be buffered to survive it -- which is the entire reason the encoder is
    // read this way rather than as a stream of detents.
    const int32_t travel = fresh.rotaryEncoder - wheelBaseline;
    wheelBaseline = fresh.rotaryEncoder;
    InputDigest::addWheel(travel);
}

Snapshot snapshot() {
    portENTER_CRITICAL(&latestMux);
    const Snapshot copy = latest;
    portEXIT_CRITICAL(&latestMux);
    return copy;
}

Event read(uint32_t timeoutMs) {
    Event event = Event::None;
    if (events == nullptr) {
        return event;
    }
    xQueueReceive(events, &event, pdMS_TO_TICKS(timeoutMs));
    return event;
}

Digest take(uint32_t timeoutMs) {
    // Drain first, wait second, and keep waiting out the remainder if the wake
    // turned out to be spurious. Callers act on an empty digest -- a screen
    // reads it as "nobody has touched this for two minutes" and closes -- so an
    // empty return has to mean the timeout genuinely elapsed and nothing else.
    const uint32_t deadlineMs = millis() + timeoutMs;

    while (true) {
        const Digest digest = InputDigest::consume();

        // The read() queue is the same input seen the other way round. Letting
        // it fill up behind a screen that only takes digests would log a
        // dropped event per press for the rest of the session.
        if (events != nullptr) {
            xQueueReset(events);
        }

        if (digest.any()) {
            return digest;
        }

        // Signed, so a millis() wrap during the wait reads as elapsed rather
        // than as another 49 days.
        const int32_t remainingMs = static_cast<int32_t>(deadlineMs - millis());
        if (remainingMs <= 0) {
            return digest;
        }

        if (!InputDigest::wait(static_cast<uint32_t>(remainingMs))) {
            return digest;  // nothing to block on; do not spin
        }
    }
}

void flush() {
    if (events != nullptr) {
        xQueueReset(events);
    }

    // Rebaselined on where the dial stands now, so the travel turned at the
    // screen being left is not reported to the one being entered. Read through
    // snapshot(), which takes its own lock.
    wheelBaseline = snapshot().rotaryEncoder;

    InputDigest::reset();
}

uint32_t lastEventMs() { return lastActivityMs; }

const char* eventName(Event event) {
    switch (event) {
    case Event::RotaryCw:
        return "rotary_cw";
    case Event::RotaryCcw:
        return "rotary_ccw";
    case Event::RotaryUp:
        return "rotary_up";
    case Event::RotaryDown:
        return "rotary_down";
    case Event::RotaryLeft:
        return "rotary_left";
    case Event::RotaryRight:
        return "rotary_right";
    case Event::RotarySelect:
        return "rotary_select";
    case Event::RotarySelectLong:
        return "rotary_select_long";
    case Event::GamepadUp:
        return "gamepad_up";
    case Event::GamepadDown:
        return "gamepad_down";
    case Event::GamepadLeft:
        return "gamepad_left";
    case Event::GamepadRight:
        return "gamepad_right";
    case Event::GamepadA:
        return "gamepad_a";
    case Event::GamepadB:
        return "gamepad_b";
    case Event::GamepadX:
        return "gamepad_x";
    case Event::GamepadY:
        return "gamepad_y";
    case Event::GamepadStart:
        return "gamepad_start";
    case Event::GamepadSelect:
        return "gamepad_select";
    default:
        return "none";
    }
}

}  // namespace Input
