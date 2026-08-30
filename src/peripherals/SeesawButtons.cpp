#include "peripherals/SeesawButtons.h"

#include "config/AppConfig.h"
#include "utils/Logger.h"

void SeesawButtons::begin(Adafruit_seesaw& device, const Button* buttons,
                          size_t count) {
    device_ = &device;
    buttons_ = buttons;
    count_ = count > kMaxButtons ? kMaxButtons : count;
    if (count > kMaxButtons) {
        LOGE(Input::kLogTag, "Button table truncated to %u entries",
             kMaxButtons);
    }

    mask_ = 0;
    for (size_t i = 0; i < count_; i++) {
        mask_ |= (1UL << buttons_[i].pin);
        states_[i] = State{};
    }

    // One transaction for all pins rather than one per pin.
    device_->pinModeBulk(mask_, INPUT_PULLUP);
    device_->setGPIOInterrupts(mask_, true);
}

void SeesawButtons::poll(uint32_t nowMs, PublishFn publish) {
    if (device_ == nullptr || count_ == 0) {
        return;
    }

    const uint32_t bits = device_->digitalReadBulk(mask_);

    // A failed I2C read comes back as zeros, which active-low decodes as every
    // button pressed at once -- physically implausible on either controller.
    // Skip the sample rather than publish a burst of phantom presses.
    if ((bits & mask_) == 0) {
        LOGD(Input::kLogTag, "Implausible button sample (bus glitch?), skipped");
        return;
    }

    for (size_t i = 0; i < count_; i++) {
        const Button& button = buttons_[i];
        State& state = states_[i];
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
