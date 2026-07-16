#include "Bootscreen.h"

#include "config/AppConfig.h"

Bootscreen::Bootscreen(int16_t w, int16_t h, uint16_t black, uint16_t white)
    : _w(w), _h(h), _black(black), _white(white) {}

void Bootscreen::init(Adafruit_GFX& gfx) {
    gfx.fillRect(0, 0, _w, _h, _white);
    drawLogo(gfx);

    textCentered(gfx, Config::APP_NAME, cx(), base() + 22, 4);
    textCentered(gfx, Config::APP_SUBTITLE, cx(), base() + 62, 1);

    drawProgress(gfx, 0.0f);
    // textCentered(gfx, "initializing...", cx(), base() + 108, 1);

    gfx.setTextSize(1);
    gfx.setTextColor(_black);
    gfx.setCursor(6, _h - 12);

    char buffer[50];
    snprintf(buffer, sizeof(buffer), "%s v%d.%d", Config::APP_NAME,
             Config::APP_VERSION_MAJOR, Config::APP_VERSION_MINOR);

    gfx.print(buffer);
}

void Bootscreen::drawProgress(Adafruit_GFX& gfx, float progress) {
    int16_t px, py, pw, ph;
    progressWindow(px, py, pw, ph);
    if (progress < 0.0f)
        progress = 0.0f;
    if (progress > 1.0f)
        progress = 1.0f;

    gfx.fillRect(px, py, pw, ph, _white);
    gfx.drawRect(px, py, pw, ph, _black);
    const int16_t fill = (int16_t)((pw - 4) * progress);
    if (fill > 0) {
        gfx.fillRect(px + 2, py + 2, fill, ph - 4, _black);
        const int16_t dw = (8 < pw - 4 - fill) ? 8 : pw - 4 - fill;
        for (int16_t yy = py + 2; yy < py + ph - 2; yy++)
            for (int16_t xx = px + 2 + fill; xx < px + 2 + fill + dw; xx++)
                if (((xx + yy) & 1) == 0)
                    gfx.drawPixel(xx, yy, _black);
    }
}

void Bootscreen::drawLogo(Adafruit_GFX& gfx) {
    const int16_t b = base();

    const int16_t bxc = cx() + 70, bh = 82, bapex = b - bh;
    gfx.drawLine(bxc - bh, b, bxc, bapex, _black);
    gfx.drawLine(bxc, bapex, bxc + bh, b, _black);

    const int16_t y0 = bapex + 27;
    zigzag(gfx, bxc, y0);

    const int16_t fx = cx() - 62, fh = 74, a = b - fh;
    for (int16_t t = 0; t <= fh; t++)
        gfx.drawFastHLine(fx - t, a + t, 2 * t + 1, _black);

    for (int16_t t = 3; t < 17; t++)
        gfx.drawFastHLine(fx - t + 3, a + t, 2 * t - 5, _white);
    for (int16_t t = 17; t < 25; t++) {
        const int16_t wz = 49 - 2 * t;
        gfx.drawFastHLine(fx + t - 32, a + t, wz, _white);
        gfx.drawFastHLine(fx + t - 16, a + t, wz, _white);
    }

    gfx.fillRect(cx() - 142, b, 296, 2, _black);
}

void Bootscreen::textCentered(Adafruit_GFX& gfx, const char* s, int16_t xc,
                              int16_t y, uint8_t size) {
    const int16_t tw = 6 * (int16_t)strlen(s) * size;
    gfx.setTextSize(size);
    gfx.setTextColor(_black);
    gfx.setCursor(xc - tw / 2, y);
    gfx.print(s);
}

void Bootscreen::zigzag(Adafruit_GFX& gfx, int16_t xc, int16_t y0) {
    const int16_t px[7] = {-27, -18, -9, 0, 9, 18, 27};
    const int16_t py[7] = {0, 9, 0, 9, 0, 9, 0};
    for (int i = 0; i < 6; i++)
        gfx.drawLine(xc + px[i], y0 + py[i], xc + px[i + 1], y0 + py[i + 1],
                     _black);
}