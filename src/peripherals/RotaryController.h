#pragma once

#include <Adafruit_seesaw.h>
#include <Arduino.h>

#include "peripherals/Controller.h"
#include "peripherals/SeesawButtons.h"

// Adafruit ANO Rotary Navigation Encoder (seesaw product 5740): a rotary dial
// plus a 5-way navigation switch. Publishes the rotary_* events.
class RotaryController : public Controller {
public:
    static constexpr const char* kLogTag = "Rotary";

    // Probes the device. Returns false when it is absent or reports the wrong
    // product; the rest of the system carries on without it.
    bool begin() override;

    bool available() const override { return available_; }

    // Level-triggered view of the controller, for apps that need to know what
    // is held right now rather than what changed. Filled by poll(), so reading
    // it costs no I2C and is safe off the main loop.
    struct State {
        bool select = false;
        bool up = false;
        bool left = false;
        bool down = false;
        bool right = false;
        int32_t encoder = 0;  // detents accumulated since boot, cw positive
    };

    State state() const;

    void poll(uint32_t nowMs, SeesawButtons::PublishFn publish) override;
    void fill(Input::Snapshot& snapshot) const override;

private:
    int32_t encoder_ = 0;
    Adafruit_seesaw device_{&Wire};
    SeesawButtons buttons_;
    bool available_ = false;
};
