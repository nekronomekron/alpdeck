#include "utils/Bootscreen.h"

#include <stdio.h>

#include "config/AppConfig.h"

Bootscreen::Bootscreen(int16_t w, int16_t h, uint16_t black, uint16_t white)
    : _w(w), _h(h), _black(black), _white(white) {}

void Bootscreen::init(Adafruit_GFX& gfx) {
    gfx.fillRect(0, 0, _w, _h, _white);
    drawLogo(gfx);

    textCentered(gfx, Config::APP_NAME, cx(), base() + 22, 4);
    textCentered(gfx, Config::APP_SUBTITLE, cx(), base() + 62, 1);

    gfx.setTextSize(1);
    gfx.setTextColor(_black);
    gfx.setCursor(6, _h - 12);

    char buffer[50];
    snprintf(buffer, sizeof(buffer), "%s v%d.%d", Config::APP_NAME,
             Config::APP_VERSION_MAJOR, Config::APP_VERSION_MINOR);

    gfx.print(buffer);
}

void Bootscreen::drawError(Adafruit_GFX& gfx, const char* message) {
    if (message == nullptr || message[0] == '\0') {
        return;
    }

    // Split on at most one '\n' into two lines.
    char buffer[96];
    strncpy(buffer, message, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char* line1 = buffer;
    char* line2 = strchr(buffer, '\n');
    if (line2 != nullptr) {
        *line2 = '\0';
        line2++;
    }

    constexpr int16_t kSignW = 30;
    constexpr int16_t kSignH = 26;
    constexpr int16_t kGap = 12;
    constexpr int16_t kPadX = 10;  // border padding around sign + text
    constexpr int16_t kPadY = 8;

    // Centre sign + text as one block in the area the logo leaves free.
    size_t longest = strlen(line1);
    if (line2 != nullptr && strlen(line2) > longest) {
        longest = strlen(line2);
    }
    const int16_t textW = (int16_t)(6 * longest);
    const int16_t blockW = kSignW + kGap + textW;
    const int16_t blockH = kSignH + 2 * kPadY;

    int16_t left = cx() - blockW / 2;
    if (left < kPadX + 2) {
        left = kPadX + 2;
    }

    // Centre the block vertically in the free band between the subtitle
    // (base()+62, size-1 text, ~8px tall) and the version line at _h-12.
    const int16_t bandTop = base() + 70;
    const int16_t bandBottom = _h - 12;
    const int16_t borderTop = bandTop + (bandBottom - bandTop - blockH) / 2;
    const int16_t top = borderTop + kPadY;

    gfx.drawRect(left - kPadX, borderTop, blockW + 2 * kPadX, blockH, _black);

    drawWarningSign(gfx, left, top);

    gfx.setTextSize(1);
    gfx.setTextColor(_black);
    const int16_t textX = left + kSignW + kGap;
    if (line2 != nullptr) {
        gfx.setCursor(textX, top + 4);
        gfx.print(line1);
        gfx.setCursor(textX, top + 16);
        gfx.print(line2);
    } else {
        gfx.setCursor(textX, top + 10);  // vertically centred on the sign
        gfx.print(line1);
    }
}

void Bootscreen::drawWarningSign(Adafruit_GFX& gfx, int16_t x, int16_t y) {
    constexpr int16_t kW = 30;
    constexpr int16_t kH = 26;
    gfx.fillTriangle(x + kW / 2, y, x, y + kH - 1, x + kW - 1, y + kH - 1,
                     _black);

    // The '!' ink is a narrow centred column, so it stays inside the triangle
    // even near the apex. White on the filled sign.
    gfx.setTextSize(2);
    gfx.setTextColor(_white);
    gfx.setCursor(x + kW / 2 - 5, y + 9);
    gfx.print('!');
}

void Bootscreen::drawLogo(Adafruit_GFX& gfx) {
    const int16_t b = base();

    // Right peak: two straight flanks.
    const int16_t bxc = cx() + 70, bh = 82, bapex = b - bh;
    gfx.drawLine(bxc - bh, b, bxc, bapex, _black);
    gfx.drawLine(bxc, bapex, bxc + bh, b, _black);

    const int16_t y0 = bapex + 27;
    zigzag(gfx, bxc, y0);

    // Left peak: filled triangle with carved-out snow line.
    const int16_t fx = cx() - 62, fh = 74, a = b - fh;
    for (int16_t t = 0; t <= fh; t++) {
        gfx.drawFastHLine(fx - t, a + t, 2 * t + 1, _black);
    }

    for (int16_t t = 3; t < 17; t++) {
        gfx.drawFastHLine(fx - t + 3, a + t, 2 * t - 5, _white);
    }
    for (int16_t t = 17; t < 25; t++) {
        const int16_t wz = 49 - 2 * t;
        gfx.drawFastHLine(fx + t - 32, a + t, wz, _white);
        gfx.drawFastHLine(fx + t - 16, a + t, wz, _white);
    }

    gfx.fillRect(cx() - 142, b, 296, 2, _black);
}

void Bootscreen::textCentered(Adafruit_GFX& gfx, const char* s, int16_t xc,
                              int16_t y, uint8_t size) {
    // 6px advance per glyph in the GFX built-in font.
    const int16_t tw = 6 * (int16_t)strlen(s) * size;
    gfx.setTextSize(size);
    gfx.setTextColor(_black);
    gfx.setCursor(xc - tw / 2, y);
    gfx.print(s);
}

void Bootscreen::zigzag(Adafruit_GFX& gfx, int16_t xc, int16_t y0) {
    const int16_t px[7] = {-27, -18, -9, 0, 9, 18, 27};
    const int16_t py[7] = {0, 9, 0, 9, 0, 9, 0};
    for (int i = 0; i < 6; i++) {
        gfx.drawLine(xc + px[i], y0 + py[i], xc + px[i + 1], y0 + py[i + 1],
                     _black);
    }
}
