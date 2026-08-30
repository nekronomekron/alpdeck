#include "peripherals/PowerButton.h"

#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "config/AppConfig.h"
#include "utils/Logger.h"

namespace PowerButton {
namespace {

constexpr gpio_num_t kPin =
    static_cast<gpio_num_t>(Config::POWER_BUTTON_PIN);

// How long the line must stay quiet before a release counts, so contact bounce
// on the way up cannot immediately re-trigger the press logic.
constexpr uint32_t kReleaseSettleMs = 50;

std::function<void()> beforeSleep;

// Active low: the pin idles high through its pull-up.
bool isPressed() { return digitalRead(kPin) == LOW; }

void waitForRelease() {
    uint32_t quietSinceMs = millis();
    while (millis() - quietSinceMs < kReleaseSettleMs) {
        if (isPressed()) {
            quietSinceMs = millis();
        }
        delay(5);
    }
}

bool heldFor(uint32_t durationMs) {
    const uint32_t startedMs = millis();
    while (millis() - startedMs < durationMs) {
        if (!isPressed()) {
            return false;
        }
        delay(10);
    }
    return true;
}

const char* wakeCauseName(esp_sleep_wakeup_cause_t cause) {
    switch (cause) {
    case ESP_SLEEP_WAKEUP_EXT0:
        return "power button";
    case ESP_SLEEP_WAKEUP_TIMER:
        return "timer";
    default:
        return "power-on or reset";
    }
}

}  // namespace

bool begin() {
    // Deep sleep leaves the pin held by the RTC; hand it back to the GPIO
    // matrix before configuring it, or the pull-up below is ignored.
    rtc_gpio_deinit(kPin);
    pinMode(Config::POWER_BUTTON_PIN, INPUT_PULLUP);

    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    LOGI(kLogTag, "Boot cause: %s", wakeCauseName(cause));

    if (cause != ESP_SLEEP_WAKEUP_EXT0) {
        return true;
    }

    // EXT0 fires on the very first contact, so a brush against the button
    // would otherwise boot the device. The hold time can only be checked
    // after the fact, once we are running again.
    if (!heldFor(Config::POWER_BUTTON_HOLD_MS)) {
        LOGI(kLogTag, "Press too brief; going back to sleep");
        enterDeepSleep();
    }

    LOGI(kLogTag, "Wake confirmed");
    // Without this the same press that woke the device is still down when
    // poll() first runs, and would be read as a request to sleep again.
    waitForRelease();
    return true;
}

void poll() {
    static uint32_t pressStartedMs = 0;

    if (!isPressed()) {
        pressStartedMs = 0;
    } else if (pressStartedMs == 0) {
        pressStartedMs = millis();
    } else if (millis() - pressStartedMs >= Config::POWER_BUTTON_HOLD_MS) {
        enterDeepSleep();
    }
}

void onBeforeSleep(std::function<void()> callback) {
    beforeSleep = std::move(callback);
}

void enterDeepSleep() {
    LOGI(kLogTag, "Entering deep sleep");

    // Sleeping with the button still down would arm the wake source against a
    // level that is already asserted, and the device would wake immediately.
    waitForRelease();

    // Unset when sleep is requested before the display exists -- a too-brief
    // wake press lands here during begin(), long before Display::init().
    if (beforeSleep) {
        beforeSleep();
    }

    rtc_gpio_pullup_en(kPin);
    rtc_gpio_pulldown_dis(kPin);

    esp_sleep_enable_ext0_wakeup(kPin, 0);  // wake on the pin going low
    esp_deep_sleep_start();                 // never returns: waking is a reset

    // esp_deep_sleep_start() is marked noreturn, but the compiler cannot see
    // that through the [[noreturn]] contract of this function on every path.
    while (true) {
    }
}

}  // namespace PowerButton
