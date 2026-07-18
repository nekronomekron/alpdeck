#include "core/RotaryController.h"

#include "config/AppConfig.h"
#include "core/Logger.h"

namespace {
// Only SELECT distinguishes a long press; the directional keys fire on press.
const SeesawButtons::Button kButtons[] = {
    {Config::ROTARY_PIN_SELECT, Input::Event::RotarySelect,
     Input::Event::RotarySelectLong},
    {Config::ROTARY_PIN_UP, Input::Event::RotaryUp, Input::Event::None},
    {Config::ROTARY_PIN_LEFT, Input::Event::RotaryLeft, Input::Event::None},
    {Config::ROTARY_PIN_DOWN, Input::Event::RotaryDown, Input::Event::None},
    {Config::ROTARY_PIN_RIGHT, Input::Event::RotaryRight, Input::Event::None},
};
}  // namespace

bool RotaryController::begin() {
    if (!_device.begin(Config::ROTARY_I2C_ADDRESS)) {
        LOGI(kLogTag, "No rotary controller at 0x%02X",
             Config::ROTARY_I2C_ADDRESS);
        return false;
    }

    // Guards against a seesaw board of a different product answering on the
    // same address: the pin map would be wrong for it.
    const uint16_t product = (_device.getVersion() >> 16) & 0xFFFF;
    if (product != Config::ROTARY_PRODUCT_ID) {
        LOGE(kLogTag, "Found seesaw product %u at 0x%02X, expected %u", product,
             Config::ROTARY_I2C_ADDRESS, Config::ROTARY_PRODUCT_ID);
        return false;
    }

    _buttons.begin(_device, kButtons, sizeof(kButtons) / sizeof(kButtons[0]));
    _device.enableEncoderInterrupt();

    _available = true;
    LOGI(kLogTag, "Ready at 0x%02X (product %u)", Config::ROTARY_I2C_ADDRESS,
         product);
    return true;
}

void RotaryController::poll(uint32_t nowMs, SeesawButtons::PublishFn publish) {
    if (!_available) {
        return;
    }

    // Deltas accumulate on the seesaw, so a slow poll loses no detents.
    const int32_t delta = _device.getEncoderDelta();
    if (delta != 0) {
        const Input::Event event =
            delta > 0 ? Input::Event::RotaryCw : Input::Event::RotaryCcw;
        for (int32_t i = 0; i < abs(delta); i++) {
            publish(event);
        }
    }

    _buttons.poll(nowMs, publish);
}
