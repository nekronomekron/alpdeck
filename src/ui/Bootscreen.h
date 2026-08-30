#pragma once

#include <Adafruit_GFX.h>
#include <string.h>

class Bootscreen {
public:
    Bootscreen(int16_t w = 400, int16_t h = 300, uint16_t black = 0x0000,
               uint16_t white = 0xFFFF);

    void init(Adafruit_GFX& gfx);

    // Renders a fatal boot error below the logo: a warning triangle with an
    // exclamation mark, the message to its right. The message may contain one
    // '\n' for a second line.
    void drawError(Adafruit_GFX& gfx, const char* message);

private:
    int16_t _w, _h;
    uint16_t _black, _white;

    int16_t cx() const { return _w / 2; }
    int16_t base() const { return (int16_t)((int32_t)_h * 38 / 100); }

    void drawLogo(Adafruit_GFX& gfx);
    void drawWarningSign(Adafruit_GFX& gfx, int16_t x, int16_t y);

    void textCentered(Adafruit_GFX& gfx, const char* s, int16_t xc, int16_t y,
                      uint8_t size);

    void zigzag(Adafruit_GFX& gfx, int16_t xc, int16_t y0);
};
