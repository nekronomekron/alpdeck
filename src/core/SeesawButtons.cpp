#include "core/SeesawButtons.h"

#include "config/AppConfig.h"
#include "core/Logger.h"

void SeesawButtons::begin(Adafruit_seesaw& device, const Button* buttons,
                          size_t count) {
    _device = &device;
    _buttons = buttons;
    _count = count > kMaxButtons ? kMaxButtons : count;
    if (count > kMaxButtons) {
        LOGE(Input::kLogTag, "Button table truncated to %u entries",
             kMaxButtons);
    }

    _mask = 0;
    for (size_t i = 0; i < _count; i++) {
        _mask |= (1UL << _buttons[i].pin);
        _states[i] = State{};
    }

    // One transaction for all pins rather than one per pin.
    _device->pinModeBulk(_mask, INPUT_PULLUP);
    _device->setGPIOInterrupts(_mask, true);
}

void SeesawButtons::poll(uint32_t nowMs, PublishFn publish) {
    if (_device == nullptr || _count == 0) {
        return;
    }

    const uint32_t bits = _device->digitalReadBulk(_mask);

    // A failed I2C read comes back as zeros, which active-low decodes as every
    // button pressed at once -- physically implausible on either controller.
    // Skip the sample rather than publish a burst of phantom presses.
    if ((bits & _mask) == 0) {
        LOGD(Input::kLogTag, "Implausible button sample (bus glitch?), skipped");
        return;
    }

    for (size_t i = 0; i < _count; i++) {
        const Button& button = _buttons[i];
        State& state = _states[i];
        const bool pressed = (bits & (1UL << button.pin)) == 0;  // active low
        const bool hasLongPress = button.longPressEvent != Input::Event::None;

        if (pressed != state.pressed) {
            if (nowMs - state.changedAtMs < Config::INPUT_DEBOUNCE_MS) {
                continue;  // contact bounce, not a real edge
            }
            state.pressed = pressed;
            state.changedAtMs = nowMs;

            if (pressed) {
                state.longFired = false;
                if (!hasLongPress) {
                    publish(button.pressEvent);
                }
            } else if (hasLongPress && !state.longFired) {
                // Emitted on release so it cannot race the long press.
                publish(button.pressEvent);
            }
            continue;
        }

        if (pressed && hasLongPress && !state.longFired &&
            nowMs - state.changedAtMs >= Config::INPUT_LONG_PRESS_MS) {
            state.longFired = true;
            publish(button.longPressEvent);
        }
    }
}
