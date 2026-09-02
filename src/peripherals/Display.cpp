#include "peripherals/Display.h"

#include "config/AppConfig.h"
#include "utils/Logger.h"

namespace Display {
namespace {

GxEPD2_DISPLAY_CLASS<GxEPD2_DRIVER_CLASS, MAX_HEIGHT(GxEPD2_DRIVER_CLASS)>
    panel(GxEPD2_DRIVER_CLASS(Config::DISPLAY_PIN_CS, Config::DISPLAY_PIN_DC,
                              Config::DISPLAY_PIN_RST,
                              Config::DISPLAY_PIN_BUSY));

bool frameIsOpen = false;
uint32_t lastRefreshDurationMs = 0;
uint32_t lastPowerDownDurationMs = 0;

// Guards the panel itself. Only three things touch it: endFrame(), the deferred
// power-down on the main loop, and the boot and sleep paths. Drawing does NOT --
// Adafruit_GFX writes into the page buffer in RAM and firstPage() only whitens
// it, so the long stretch between beginFrame() and endFrame() needs no lock and
// cannot block the main loop.
SemaphoreHandle_t panelMux = nullptr;

// millis() at the last frame, or 0 when the panel is hibernated. The panel's
// high-voltage rail must not be left up indefinitely, so this is a deadline and
// not merely an optimisation.
uint32_t poweredAtMs = 0;

// Long enough that a user working a screen never pays to wake the panel, short
// enough that the rail is down soon after they stop. Not tuned any longer than
// that on purpose: waking costs a measured 41ms, so stretching this to catch a
// pause would buy very little, and the 102ms power-down itself runs on the main
// loop -- better spent while nobody is pressing anything.
constexpr uint32_t kPowerDownAfterMs = 2000;

void lockPanel() {
    if (panelMux != nullptr) {
        xSemaphoreTake(panelMux, portMAX_DELAY);
    }
}

void unlockPanel() {
    if (panelMux != nullptr) {
        xSemaphoreGive(panelMux);
    }
}

// Hibernates and records what it cost. The caller holds the lock.
void hibernatePanel() {
    const uint32_t startedMs = millis();
    panel.hibernate();
    lastPowerDownDurationMs = millis() - startedMs;
    poweredAtMs = 0;
}

void openFrame() {
    panel.firstPage();
    frameIsOpen = true;
}

}  // namespace

void init() {
    LOGI(kLogTag, "Initializing display");

    panelMux = xSemaphoreCreateMutex();
    if (panelMux == nullptr) {
        // Not fatal, but the deferred power-down is switched off without it:
        // it is the one thing that touches the panel from a second task.
        LOGE(kLogTag, "Could not allocate the panel lock");
    }

    SPI.begin(Config::DISPLAY_PIN_SCK, Config::DISPLAY_PIN_MISO,
              Config::DISPLAY_PIN_MOSI, Config::DISPLAY_PIN_CS);

    // GxEPD2 presets each control pin's level with a digitalWrite before the
    // matching pinMode, to keep the line glitch-free. Core 3.x discards writes
    // to pins it hasn't yet registered as GPIO and logs an error for each, so
    // claim them here first. Cosmetic: GxEPD2 re-asserts every level itself.
    pinMode(Config::DISPLAY_PIN_CS, OUTPUT);
    pinMode(Config::DISPLAY_PIN_DC, OUTPUT);
    pinMode(Config::DISPLAY_PIN_RST, OUTPUT);

    panel.init(115200, true, 2, false);
    panel.setRotation(0);
}

void shutdown() {
    LOGI(kLogTag, "Blanking and shutting down display");

    drawFullWindow([](Adafruit_GFX& gfx) { gfx.fillScreen(kWhite); });
    powerDown();
}

void powerDown() {
    lockPanel();
    // No powerOff() first. hibernate() does one itself, and doing it out here
    // moved the only expensive part of the hibernate outside the span
    // hibernatePanel() times -- which made lastPowerDownMs() report 0 for a
    // power-down that really cost 102ms.
    hibernatePanel();
    unlockPanel();
}

void drawFullWindow(std::function<void(Adafruit_GFX&)> drawFunction) {
    lockPanel();
    panel.setFullWindow();
    panel.firstPage();
    do {
        drawFunction(panel);
    } while (panel.nextPage());

    // Hibernated here rather than left powered: these are the boot, standby and
    // shutdown screens, and each is the last thing drawn before the device stops
    // doing anything at all.
    hibernatePanel();
    unlockPanel();
}

void drawPartialWindow(int16_t x, int16_t y, int16_t w, int16_t h,
                       std::function<void(Adafruit_GFX&)> drawFunction) {
    lockPanel();
    panel.setPartialWindow(x, y, w, h);
    panel.firstPage();
    do {
        drawFunction(panel);
    } while (panel.nextPage());

    hibernatePanel();
    unlockPanel();
}

void beginFrame(RefreshMode mode) {
    if (frameIsOpen) {
        return;  // already drawing; keep the caller's existing frame
    }

    if (mode == RefreshMode::Partial) {
        panel.setPartialWindow(0, 0, panel.width(), panel.height());
    } else {
        panel.setFullWindow();
    }
    openFrame();
}

void beginFrame(int16_t x, int16_t y, int16_t w, int16_t h) {
    if (frameIsOpen) {
        return;
    }

    panel.setPartialWindow(x, y, w, h);
    openFrame();
}

void endFrame() {
    if (!frameIsOpen) {
        return;
    }
    frameIsOpen = false;

    // One page covers the panel, so this single call renders the whole frame.
    //
    // Deliberately no hibernate. Measured, it cost 102ms here, and the reset
    // and re-init it forced on the next frame cost 41ms more -- 143ms of a
    // 750ms frame spent switching the panel off and on again between two frames
    // a third of a second apart. The panel is left powered and loop() puts it
    // down once the drawing has actually stopped.
    lockPanel();

    const uint32_t startedMs = millis();
    panel.nextPage();
    lastRefreshDurationMs = millis() - startedMs;
    poweredAtMs = millis();

    unlockPanel();
}

void loop() {
    // Read without the lock: both are single words, and being one main-loop
    // pass late to power down is not worth a lock.
    if (poweredAtMs == 0 || frameIsOpen || panelMux == nullptr) {
        return;
    }
    if (millis() - poweredAtMs < kPowerDownAfterMs) {
        return;
    }

    // Never waits. A locked panel is one mid-refresh, which is both a panel
    // that must not be powered down and an app still drawing -- and blocking
    // here would stall input polling for the length of a refresh, which is the
    // one thing the main loop has to keep doing while the panel is busy.
    if (xSemaphoreTake(panelMux, 0) != pdTRUE) {
        return;
    }

    // Re-checked under the lock: a frame may have opened since the test above.
    if (!frameIsOpen && poweredAtMs != 0) {
        hibernatePanel();
    }
    xSemaphoreGive(panelMux);
}

bool frameOpen() { return frameIsOpen; }

uint32_t lastRefreshMs() { return lastRefreshDurationMs; }

uint32_t lastPowerDownMs() { return lastPowerDownDurationMs; }

Adafruit_GFX& canvas() { return panel; }

int16_t width() { return panel.width(); }

int16_t height() { return panel.height(); }

}  // namespace Display
