#include "core/Input.h"

#include <Wire.h>

#include "config/AppConfig.h"
#include "core/GamepadController.h"
#include "core/Logger.h"
#include "core/RotaryController.h"

namespace {
RotaryController rotary;
GamepadController gamepad;
QueueHandle_t events = nullptr;

constexpr uint8_t kQueueLength = 16;
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
