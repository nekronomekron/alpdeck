#include "core/Display.h"

#include "config/AppConfig.h"
#include "core/Logger.h"

namespace {
GxEPD2_DISPLAY_CLASS<GxEPD2_DRIVER_CLASS, MAX_HEIGHT(GxEPD2_DRIVER_CLASS)>
    display(GxEPD2_DRIVER_CLASS(Config::DISPLAY_PIN_CS, Config::DISPLAY_PIN_DC,
                                Config::DISPLAY_PIN_RST,
                                Config::DISPLAY_PIN_BUSY));
}  // namespace

void Display::init() {
    LOGI(Display::kLogTag, "Initializing display");

    SPI.begin(Config::DISPLAY_PIN_SCK, Config::DISPLAY_PIN_MISO,
              Config::DISPLAY_PIN_MOSI, Config::DISPLAY_PIN_CS);

    // GxEPD2 presets each control pin's level with a digitalWrite before the
    // matching pinMode, to keep the line glitch-free. Core 3.x discards writes
    // to pins it hasn't yet registered as GPIO and logs an error for each, so
    // claim them here first. Cosmetic: GxEPD2 re-asserts every level itself.
    pinMode(Config::DISPLAY_PIN_CS, OUTPUT);
    pinMode(Config::DISPLAY_PIN_DC, OUTPUT);
    pinMode(Config::DISPLAY_PIN_RST, OUTPUT);

    display.init(115200, true, 2, false);
    display.setRotation(0);
}

void Display::shutdown() {
    LOGI(Display::kLogTag, "Shutting down display");

    drawFullWindow([](Adafruit_GFX& gfx) { gfx.fillScreen(kWhite); });

    display.powerOff();
    display.hibernate();
}

void Display::drawFullWindow(std::function<void(Adafruit_GFX&)> drawFunction) {
    display.setFullWindow();
    display.firstPage();
    do {
        drawFunction(display);
    } while (display.nextPage());

    display.hibernate();
}

void Display::drawPartialWindow(
    int16_t x, int16_t y, int16_t w, int16_t h,
    std::function<void(Adafruit_GFX&)> drawFunction) {
    display.setPartialWindow(x, y, w, h);
    display.firstPage();
    do {
        drawFunction(display);
    } while (display.nextPage());

    display.hibernate();
}

bool Display::_frameOpen = false;

void Display::beginFrame(bool partial) {
    if (_frameOpen) {
        return;  // already drawing; keep the caller's existing frame
    }

    if (partial) {
        display.setPartialWindow(0, 0, display.width(), display.height());
    } else {
        display.setFullWindow();
    }

    display.firstPage();
    _frameOpen = true;
}

void Display::endFrame() {
    if (!_frameOpen) {
        return;
    }
    _frameOpen = false;

    // One page covers the panel, so this single call renders the whole frame.
    display.nextPage();
    display.hibernate();
}

bool Display::frameOpen() { return _frameOpen; }

Adafruit_GFX& Display::canvas() { return display; }

int16_t Display::width() { return display.width(); }

int16_t Display::height() { return display.height(); }