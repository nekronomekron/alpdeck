#pragma once

#include <Adafruit_seesaw.h>
#include <Arduino.h>

#include "peripherals/SeesawButtons.h"

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

    // Level-triggered view of the controller, for apps that need to know what
    // is held right now rather than what changed. Filled by poll(), so reading
    // it costs no I2C and is safe off the main loop.
    //
    // Everything here is in the BOARD's frame, exactly as the gamepad_* events
    // are: a rotated mounting is the app's business, not the driver's.
    struct State {
        bool a = false;
        bool b = false;
        bool x = false;
        bool y = false;
        bool start = false;
        bool select = false;
        int8_t axisX = 0;  // digitised stick, -1 = left, +1 = right
        int8_t axisY = 0;  // -1 = up, +1 = down
        int16_t deflectionX = 0;  // signed travel from centre, invert applied
        int16_t deflectionY = 0;  // so the sign matches axisX/axisY
        uint16_t stickX = 0;  // raw ADC, 0..1023, ~512 at rest
        uint16_t stickY = 0;
    };

    State state() const;

    void poll(uint32_t nowMs, SeesawButtons::PublishFn publish);

private:
    // One stick axis digitised to a direction: -1, 0 or +1.
    struct Axis {
        uint8_t pin;
        bool invert;
        Input::Event negativeEvent;  // stick pushed towards lower values
        Input::Event positiveEvent;
        int8_t engaged;
        uint16_t raw;        // last ADC sample, kept for state()
        int16_t deflection;  // that sample as signed travel, invert applied
    };

    void pollAxis(Axis& axis, SeesawButtons::PublishFn publish);

    Adafruit_seesaw _device{&Wire};
    SeesawButtons _buttons;
    Axis _axisX;
    Axis _axisY;
    bool _available = false;
};
