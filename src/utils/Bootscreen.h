#pragma once

#include <Adafruit_GFX.h>
#include <string.h>

class Bootscreen {
public:
    Bootscreen(int16_t w = 400, int16_t h = 300, uint16_t black = 0x0000,
               uint16_t white = 0xFFFF);

    void init(Adafruit_GFX& gfx);

    void drawProgress(Adafruit_GFX& gfx, float progress);

    void progressWindow(int16_t& x, int16_t& y, int16_t& w, int16_t& h) const {
        w = 140;
        h = 10;
        x = cx() - w / 2;
        y = base() + 84;
    }

private:
    int16_t _w, _h;
    uint16_t _black, _white;

    int16_t cx() const { return _w / 2; }
    int16_t base() const { return (int16_t)((int32_t)_h * 38 / 100); }

    void drawLogo(Adafruit_GFX& gfx);

    void textCentered(Adafruit_GFX& gfx, const char* s, int16_t xc, int16_t y,
                      uint8_t size);

    void zigzag(Adafruit_GFX& gfx, int16_t xc, int16_t y0);
};