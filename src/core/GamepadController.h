#pragma once

#include <Adafruit_seesaw.h>
#include <Arduino.h>

#include "core/SeesawButtons.h"

// Adafruit Mini I2C Gamepad with seesaw (product 5743): six buttons plus an
// analog thumb stick. Publishes the gamepad_* events; the stick is digitised
// into direction events with hysteresis, one event per deflection.
class GamepadController {
public:
    static constexpr const char* kLogTag = "Gamepad";

    // Probes the device. Returns false when it is absent or reports the wrong
    // product; the rest of the system carries on without it.
    bool begin();

    bool available() const { return _available; }

    void poll(uint32_t nowMs, SeesawButtons::PublishFn publish);

private:
    // One stick axis digitised to a direction: -1, 0 or +1.
    struct Axis {
        uint8_t pin;
        bool invert;
        Input::Event negativeEvent;  // stick pushed towards lower values
        Input::Event positiveEvent;
        int8_t engaged;
    };

    void pollAxis(Axis& axis, SeesawButtons::PublishFn publish);

    Adafruit_seesaw _device{&Wire};
    SeesawButtons _buttons;
    Axis _axisX;
    Axis _axisY;
    bool _available = false;
};
