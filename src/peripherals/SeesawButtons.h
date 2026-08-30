#pragma once

#include <Adafruit_seesaw.h>
#include <Arduino.h>

#include "peripherals/Input.h"

// Debounced button bank on one seesaw device, shared by both controller
// drivers so the edge/long-press logic exists exactly once.
//
// Semantics: buttons fire on PRESS for low latency (games). A button with a
// long-press event instead fires its press event on RELEASE, because only the
// release reveals whether the hold was short or long.
class SeesawButtons {
public:
    using PublishFn = void (*)(Input::Event);

    struct Button {
        uint8_t pin;                  // seesaw-side pin, active-low pull-up
        Input::Event pressEvent;
        Input::Event longPressEvent;  // Event::None when unused
    };

    // Configures the pins (bulk pull-up + GPIO interrupts) on the device. The
    // button table must outlive this object (pass a static array).
    void begin(Adafruit_seesaw& device, const Button* buttons, size_t count);

    // Samples all pins in one bulk read and publishes edges. Call from the
    // main loop with millis().
    void poll(uint32_t nowMs, PublishFn publish);

    // Debounced level of one button, by its index in the table handed to
    // begin(). Returns what the last poll() left behind and touches no I2C, so
    // it is safe to read from a task that does not own the bus.
    bool pressed(size_t index) const {
        return index < _count && _states[index].pressed;
    }

private:
    static constexpr size_t kMaxButtons = 8;

    struct State {
        bool pressed = false;
        uint32_t changedAtMs = 0;
        bool longFired = false;
    };

    Adafruit_seesaw* _device = nullptr;
    const Button* _buttons = nullptr;
    size_t _count = 0;
    uint32_t _mask = 0;
    State _states[kMaxButtons];
};
