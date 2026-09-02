#include "peripherals/Display.h"

#include "config/AppConfig.h"
#include "peripherals/PanelPower.h"
#include "utils/Logger.h"

namespace Display {
namespace {

GxEPD2_DISPLAY_CLASS<GxEPD2_DRIVER_CLASS, MAX_HEIGHT(GxEPD2_DRIVER_CLASS)>
    panel(GxEPD2_DRIVER_CLASS(Config::DISPLAY_PIN_CS, Config::DISPLAY_PIN_DC,
                              Config::DISPLAY_PIN_RST,
                              Config::DISPLAY_PIN_BUSY));

bool frameIsOpen = false;
uint32_t lastRefreshDurationMs = 0;

void openFrame() {
    panel.firstPage();
    frameIsOpen = true;
}

}  // namespace

void init() {
    LOGI(kLogTag, "Initializing display");

    // The panel's lock and its power deadline live in PanelPower; this is the
    // only place that knows how to put this particular panel down.
    PanelPower::begin([] { panel.hibernate(); });

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

void powerDown() { PanelPower::powerDownNow(); }

void drawFullWindow(std::function<void(Adafruit_GFX&)> drawFunction) {
    const PanelPower::Lock lock;
    panel.setFullWindow();
    panel.firstPage();
    do {
        drawFunction(panel);
    } while (panel.nextPage());

    // Hibernated here rather than left powered: these are the boot, standby and
    // shutdown screens, and each is the last thing drawn before the device stops
    // doing anything at all.
    PanelPower::powerDownLocked();
}

void drawPartialWindow(int16_t x, int16_t y, int16_t w, int16_t h,
                       std::function<void(Adafruit_GFX&)> drawFunction) {
    const PanelPower::Lock lock;
    panel.setPartialWindow(x, y, w, h);
    panel.firstPage();
    do {
        drawFunction(panel);
    } while (panel.nextPage());

    PanelPower::powerDownLocked();
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
    const PanelPower::Lock lock;

    const uint32_t startedMs = millis();
    panel.nextPage();
    lastRefreshDurationMs = millis() - startedMs;
    PanelPower::markPowered();
}

void loop() { PanelPower::loop(frameOpen); }

bool frameOpen() { return frameIsOpen; }

uint32_t lastRefreshMs() { return lastRefreshDurationMs; }

uint32_t lastPowerDownMs() { return PanelPower::lastPowerDownMs(); }

Adafruit_GFX& canvas() { return panel; }

int16_t width() { return panel.width(); }

int16_t height() { return panel.height(); }

}  // namespace Display
