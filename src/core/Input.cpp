#include "core/Input.h"

#include <Adafruit_seesaw.h>
#include <Wire.h>

#include "config/AppConfig.h"
#include "core/Logger.h"

namespace {
Adafruit_seesaw encoder(&Wire);
QueueHandle_t events = nullptr;

constexpr uint8_t kQueueLength = 16;

struct Switch {
    uint8_t pin;
    Input::Event event;
    bool pressed;
    unsigned long changedAtMs;
    bool longFired;
};

// Only SELECT distinguishes a long press; the directional keys repeat instead.
Switch switches[] = {
    {Config::ENCODER_PIN_SELECT, Input::Event::Select, false, 0, false},
    {Config::ENCODER_PIN_UP, Input::Event::Up, false, 0, false},
    {Config::ENCODER_PIN_LEFT, Input::Event::Left, false, 0, false},
    {Config::ENCODER_PIN_DOWN, Input::Event::Down, false, 0, false},
    {Config::ENCODER_PIN_RIGHT, Input::Event::Right, false, 0, false},
};

uint32_t switchMask() {
    uint32_t mask = 0;
    for (const Switch& sw : switches) {
        mask |= (1UL << sw.pin);
    }
    return mask;
}
}  // namespace

bool Input::_available = false;

bool Input::init() {
    events = xQueueCreate(kQueueLength, sizeof(Event));
    if (events == nullptr) {
        LOGE(kLogTag, "Could not allocate the event queue");
        return false;
    }

    Wire.begin(Config::I2C_PIN_SDA, Config::I2C_PIN_SCL, Config::I2C_FREQUENCY);

    if (!encoder.begin(Config::ENCODER_I2C_ADDRESS)) {
        LOGW(kLogTag, "No encoder at 0x%02X; continuing without input",
             Config::ENCODER_I2C_ADDRESS);
        return false;
    }

    // Guards against a seesaw board of a different product answering on the
    // same address: the pin map below would be wrong for it.
    const uint16_t product = (encoder.getVersion() >> 16) & 0xFFFF;
    if (product != Config::ENCODER_PRODUCT_ID) {
        LOGE(kLogTag, "Found seesaw product %u, expected %u", product,
             Config::ENCODER_PRODUCT_ID);
        return false;
    }

    // One transaction for all five switches rather than five.
    encoder.pinModeBulk(switchMask(), INPUT_PULLUP);
    encoder.setGPIOInterrupts(switchMask(), true);
    encoder.enableEncoderInterrupt();

    _available = true;
    LOGI(kLogTag, "ANO encoder ready at 0x%02X (product %u)",
         Config::ENCODER_I2C_ADDRESS, product);
    return true;
}

bool Input::isAvailable() { return _available; }

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

void Input::pollRotation() {
    // Deltas accumulate on the seesaw, so a slow poll loses no detents.
    const int32_t delta = encoder.getEncoderDelta();
    if (delta == 0) {
        return;
    }

    const Event event = delta > 0 ? Event::RotateCw : Event::RotateCcw;
    for (int32_t i = 0; i < abs(delta); i++) {
        publish(event);
    }
}

void Input::pollSwitches(uint32_t nowMs) {
    const uint32_t bits = encoder.digitalReadBulk(switchMask());

    for (Switch& sw : switches) {
        const bool pressed = (bits & (1UL << sw.pin)) == 0;  // active low

        if (pressed != sw.pressed) {
            if (nowMs - sw.changedAtMs < Config::ENCODER_DEBOUNCE_MS) {
                continue;  // contact bounce, not a real edge
            }
            sw.pressed = pressed;
            sw.changedAtMs = nowMs;

            if (pressed) {
                sw.longFired = false;
            } else if (sw.event == Event::Select && !sw.longFired) {
                // Emitted on release so it cannot race the long press.
                publish(Event::Select);
            } else if (sw.event != Event::Select) {
                publish(sw.event);
            }
            continue;
        }

        if (pressed && sw.event == Event::Select && !sw.longFired &&
            nowMs - sw.changedAtMs >= Config::ENCODER_LONG_PRESS_MS) {
            sw.longFired = true;
            publish(Event::SelectLong);
        }
    }
}

void Input::poll() {
    if (!_available) {
        return;
    }

    const uint32_t nowMs = millis();
    pollRotation();
    pollSwitches(nowMs);
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
    case Event::RotateCw:
        return "cw";
    case Event::RotateCcw:
        return "ccw";
    case Event::Up:
        return "up";
    case Event::Down:
        return "down";
    case Event::Left:
        return "left";
    case Event::Right:
        return "right";
    case Event::Select:
        return "select";
    case Event::SelectLong:
        return "select_long";
    default:
        return "none";
    }
}
