#pragma once

#include <Adafruit_seesaw.h>
#include <Arduino.h>

#include "core/SeesawButtons.h"

// Adafruit ANO Rotary Navigation Encoder (seesaw product 5740): a rotary dial
// plus a 5-way navigation switch. Publishes the rotary_* events.
class RotaryController {
public:
    static constexpr const char* kLogTag = "Rotary";

    // Probes the device. Returns false when it is absent or reports the wrong
    // product; the rest of the system carries on without it.
    bool begin();

    bool available() const { return _available; }

    void poll(uint32_t nowMs, SeesawButtons::PublishFn publish);

private:
    Adafruit_seesaw _device{&Wire};
    SeesawButtons _buttons;
    bool _available = false;
};
