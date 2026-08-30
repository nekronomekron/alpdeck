#include "peripherals/Input.h"

#include <Wire.h>

#include "config/AppConfig.h"
#include "peripherals/GamepadController.h"
#include "utils/Logger.h"
#include "peripherals/RotaryController.h"

namespace {
RotaryController rotary;
GamepadController gamepad;
QueueHandle_t events = nullptr;

constexpr uint8_t kQueueLength = 16;

// Written by poll() on the main loop, read by whatever task an app runs on.
// The spinlock keeps a reader from seeing half of an update; the struct is a
// few dozen bytes, so the critical section stays far shorter than one I2C
// transaction.
Input::Snapshot latest;
portMUX_TYPE latestMux = portMUX_INITIALIZER_UNLOCKED;
}  // namespace

bool Input::init() {
    events = xQueueCreate(kQueueLength, sizeof(Event));
    if (events == nullptr) {
        LOGE(kLogTag, "Could not allocate the event queue");
        return false;
    }

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

bool Input::isAvailable() { return hasRotary() || hasGamepad(); }

bool Input::hasRotary() { return rotary.available(); }

bool Input::hasGamepad() { return gamepad.available(); }

void Input::publish(Event event) {
    if (events == nullptr) {
        return;
    }
    // Drop rather than block: input is worthless once it is stale, and the main
    // loop must never wait on a Lua app that has stopped reading.
    if (xQueueSend(events, &event, 0) != pdPASS) {
        LOGD(kLogTag, "Event queue full, dropped %s", eventName(event));
    }
}

void Input::poll() {
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
}

Input::Snapshot Input::snapshot() {
    portENTER_CRITICAL(&latestMux);
    const Snapshot copy = latest;
    portEXIT_CRITICAL(&latestMux);
    return copy;
}

Input::Event Input::read(uint32_t timeoutMs) {
    Event event = Event::None;
    if (events == nullptr) {
        return event;
    }
    xQueueReceive(events, &event, pdMS_TO_TICKS(timeoutMs));
    return event;
}

void Input::flush() {
    if (events != nullptr) {
        xQueueReset(events);
    }
}

const char* Input::eventName(Event event) {
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
