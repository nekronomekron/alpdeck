#include "peripherals/GamepadController.h"

#include "config/AppConfig.h"
#include "utils/Logger.h"

namespace {
// Index into kButtons below. state() reads the button bank by position, so the
// two must stay in step.
enum ButtonIndex : size_t {
    kSelect = 0,
    kB,
    kY,
    kA,
    kX,
    kStart,
};

// All buttons fire on press; the gamepad has no long-press mapping.
const SeesawButtons::Button kButtons[] = {
    {Config::GAMEPAD_PIN_SELECT, Input::Event::GamepadSelect,
     Input::Event::None},
    {Config::GAMEPAD_PIN_B, Input::Event::GamepadB, Input::Event::None},
    {Config::GAMEPAD_PIN_Y, Input::Event::GamepadY, Input::Event::None},
    {Config::GAMEPAD_PIN_A, Input::Event::GamepadA, Input::Event::None},
    {Config::GAMEPAD_PIN_X, Input::Event::GamepadX, Input::Event::None},
    {Config::GAMEPAD_PIN_START, Input::Event::GamepadStart,
     Input::Event::None},
};
}  // namespace

bool GamepadController::begin() {
    if (!device_.begin(Config::GAMEPAD_I2C_ADDRESS)) {
        LOGI(kLogTag, "No gamepad at 0x%02X", Config::GAMEPAD_I2C_ADDRESS);
        return false;
    }

    // Guards against a seesaw board of a different product answering on the
    // same address: the pin map would be wrong for it.
    const uint16_t product = (device_.getVersion() >> 16) & 0xFFFF;
    if (product != Config::GAMEPAD_PRODUCT_ID) {
        LOGE(kLogTag, "Found seesaw product %u at 0x%02X, expected %u", product,
             Config::GAMEPAD_I2C_ADDRESS, Config::GAMEPAD_PRODUCT_ID);
        return false;
    }

    buttons_.begin(device_, kButtons, sizeof(kButtons) / sizeof(kButtons[0]));

    axisX_ = {Config::GAMEPAD_PIN_STICK_X, Config::GAMEPAD_STICK_INVERT_X,
              Input::Event::GamepadLeft, Input::Event::GamepadRight, 0,
              Config::GAMEPAD_STICK_CENTER, 0};
    axisY_ = {Config::GAMEPAD_PIN_STICK_Y, Config::GAMEPAD_STICK_INVERT_Y,
              Input::Event::GamepadUp, Input::Event::GamepadDown, 0,
              Config::GAMEPAD_STICK_CENTER, 0};

    available_ = true;
    LOGI(kLogTag, "Ready at 0x%02X (product %u)", Config::GAMEPAD_I2C_ADDRESS,
         product);
    return true;
}

GamepadController::State GamepadController::state() const {
    State state;
    if (!available_) {
        return state;  // absent controller reads as "nothing held"
    }

    state.select = buttons_.pressed(kSelect);
    state.b = buttons_.pressed(kB);
    state.y = buttons_.pressed(kY);
    state.a = buttons_.pressed(kA);
    state.x = buttons_.pressed(kX);
    state.start = buttons_.pressed(kStart);

    state.axisX = axisX_.engaged;
    state.axisY = axisY_.engaged;
    state.deflectionX = axisX_.deflection;
    state.deflectionY = axisY_.deflection;
    state.stickX = axisX_.raw;
    state.stickY = axisY_.raw;
    return state;
}

void GamepadController::pollAxis(Axis& axis, SeesawButtons::PublishFn publish) {
    axis.raw = device_.analogRead(axis.pin);

    int16_t deflection =
        static_cast<int16_t>(axis.raw) - Config::GAMEPAD_STICK_CENTER;
    if (axis.invert) {
        deflection = -deflection;
    }
    axis.deflection = deflection;

    // Hysteresis: engage beyond STICK_PRESS, release below STICK_RELEASE. In
    // between, the previous direction holds, so a stick held near the
    // threshold cannot chatter.
    int8_t direction = axis.engaged;
    if (deflection >= Config::GAMEPAD_STICK_PRESS) {
        direction = 1;
    } else if (deflection <= -Config::GAMEPAD_STICK_PRESS) {
        direction = -1;
    } else if (abs(deflection) <= Config::GAMEPAD_STICK_RELEASE) {
        direction = 0;
    }

    if (direction != axis.engaged) {
        axis.engaged = direction;
        if (direction > 0) {
            publish(axis.positiveEvent);
        } else if (direction < 0) {
            publish(axis.negativeEvent);
        }
        // Returning to centre publishes nothing: one event per deflection.
    }
}

void GamepadController::poll(uint32_t nowMs, SeesawButtons::PublishFn publish) {
    if (!available_) {
        return;
    }

    buttons_.poll(nowMs, publish);
    pollAxis(axisX_, publish);
    pollAxis(axisY_, publish);
}
