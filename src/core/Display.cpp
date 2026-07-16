#include "Display.h"

#include "config/AppConfig.h"
#include "core/Logger.h"

GxEPD2_DISPLAY_CLASS<GxEPD2_DRIVER_CLASS, MAX_HEIGHT(GxEPD2_DRIVER_CLASS)>
    display(GxEPD2_DRIVER_CLASS(Config::DISPLAY_PIN_CS, Config::DISPLAY_PIN_DC,
                                Config::DISPLAY_PIN_RST,
                                Config::DISPLAY_PIN_BUSY));

void Display::init() {
    LOGI(Display::kLogTag, "Initializing display");

    SPI.begin(Config::DISPLAY_PIN_SCK, Config::DISPLAY_PIN_MISO,
              Config::DISPLAY_PIN_MOSI, Config::DISPLAY_PIN_CS);

    display.init(115200, true, 2, false);
    display.setRotation(0);
}

void Display::shutdown() {
    LOGI(Display::kLogTag, "Shutting down display");

    drawFullWindow([](Adafruit_GFX& gfx) {
        gfx.fillScreen(0xFFFF);  // Fill with white
    });

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