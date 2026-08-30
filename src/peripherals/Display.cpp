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

}  // namespace

void init() {
    LOGI(kLogTag, "Initializing display");

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
    LOGI(kLogTag, "Shutting down display");

    drawFullWindow([](Adafruit_GFX& gfx) { gfx.fillScreen(kWhite); });

    panel.powerOff();
    panel.hibernate();
}

void drawFullWindow(std::function<void(Adafruit_GFX&)> drawFunction) {
    panel.setFullWindow();
    panel.firstPage();
    do {
        drawFunction(panel);
    } while (panel.nextPage());

    panel.hibernate();
}

void drawPartialWindow(int16_t x, int16_t y, int16_t w, int16_t h,
                       std::function<void(Adafruit_GFX&)> drawFunction) {
    panel.setPartialWindow(x, y, w, h);
    panel.firstPage();
    do {
        drawFunction(panel);
    } while (panel.nextPage());

    panel.hibernate();
}

void beginFrame(bool partial) {
    if (frameIsOpen) {
        return;  // already drawing; keep the caller's existing frame
    }

    if (partial) {
        panel.setPartialWindow(0, 0, panel.width(), panel.height());
    } else {
        panel.setFullWindow();
    }

    panel.firstPage();
    frameIsOpen = true;
}

void endFrame() {
    if (!frameIsOpen) {
        return;
    }
    frameIsOpen = false;

    // One page covers the panel, so this single call renders the whole frame.
    panel.nextPage();
    panel.hibernate();
}

bool frameOpen() { return frameIsOpen; }

Adafruit_GFX& canvas() { return panel; }

int16_t width() { return panel.width(); }

int16_t height() { return panel.height(); }

}  // namespace Display
