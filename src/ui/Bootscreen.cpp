#include "ui/Bootscreen.h"

#include <stdio.h>
#include <string.h>

#include "config/AppConfig.h"
#include "ui/Logo.h"

namespace Bootscreen {
namespace {

constexpr uint16_t kBlack = 0x0000;
constexpr uint16_t kWhite = 0xFFFF;

// Layout, top to bottom. Every offset below the logo is derived from its
// height rather than from a magic number, so resizing the mark moves the text
// with it instead of overlapping it.
constexpr int16_t kLogoTop = 22;
constexpr int16_t kLogoWidth = 200;
constexpr int16_t kTitleGap = 12;   // logo bottom to title top
constexpr int16_t kTitleSize = 4;
constexpr int16_t kSubtitleGap = 10;
constexpr int16_t kVersionBottomInset = 12;

// Error block.
constexpr int16_t kSignWidth = 30;
constexpr int16_t kSignHeight = 26;
constexpr int16_t kSignTextGap = 12;
constexpr int16_t kFramePadX = 10;
constexpr int16_t kFramePadY = 8;
constexpr size_t kMessageLimit = 96;

int16_t logoBottom() { return kLogoTop + Logo::height(kLogoWidth); }

int16_t titleTop() { return logoBottom() + kTitleGap; }

// Measured rather than assumed, so the layout survives a font change.
void textSize(Adafruit_GFX& gfx, const char* text, uint8_t size,
              int16_t& width, int16_t& height, int16_t& topOffset) {
    gfx.setTextSize(size);

    int16_t boundsX = 0;
    int16_t boundsY = 0;
    uint16_t boundsW = 0;
    uint16_t boundsH = 0;
    gfx.getTextBounds(text, 0, 0, &boundsX, &boundsY, &boundsW, &boundsH);

    width = static_cast<int16_t>(boundsW);
    height = static_cast<int16_t>(boundsH);
    topOffset = boundsY;
}

// Draws text centred horizontally, with y as its TOP edge whichever font is
// active. Returns the height, so the caller can stack the next line.
int16_t drawCentered(Adafruit_GFX& gfx, const char* text, int16_t y,
                     uint8_t size) {
    int16_t width = 0;
    int16_t height = 0;
    int16_t topOffset = 0;
    textSize(gfx, text, size, width, height, topOffset);

    gfx.setTextColor(kBlack);
    gfx.setCursor(gfx.width() / 2 - width / 2, y - topOffset);
    gfx.print(text);
    return height;
}

void drawWarningSign(Adafruit_GFX& gfx, int16_t x, int16_t y) {
    gfx.fillTriangle(x + kSignWidth / 2, y, x, y + kSignHeight - 1,
                     x + kSignWidth - 1, y + kSignHeight - 1, kBlack);

    // The '!' ink is a narrow centred column, so it stays inside the triangle
    // even near the apex. White on the filled sign.
    gfx.setFont(nullptr);
    gfx.setTextSize(2);
    gfx.setTextColor(kWhite);
    gfx.setCursor(x + kSignWidth / 2 - 5, y + 9);
    gfx.print('!');
}

}  // namespace

void draw(Adafruit_GFX& gfx) {
    gfx.fillScreen(kWhite);

    Logo::draw(gfx, gfx.width() / 2 - kLogoWidth / 2, kLogoTop, kLogoWidth,
               kBlack);

    // The built-in 6x8 face, deliberately: its blocky character cells suit the
    // mark better than a proportional one. The other faces stay available to
    // apps through display.font().
    gfx.setFont(nullptr);
    const int16_t titleHeight =
        drawCentered(gfx, Config::APP_NAME, titleTop(), kTitleSize);

    drawCentered(gfx, Config::APP_SUBTITLE,
                 titleTop() + titleHeight + kSubtitleGap, 1);

    char version[48];
    snprintf(version, sizeof(version), "%s v%d.%d", Config::APP_NAME,
             Config::APP_VERSION_MAJOR, Config::APP_VERSION_MINOR);

    gfx.setTextSize(1);
    gfx.setTextColor(kBlack);
    gfx.setCursor(6, gfx.height() - kVersionBottomInset);
    gfx.print(version);
}

void drawStandby(Adafruit_GFX& gfx) {
    gfx.fillScreen(kWhite);

    Logo::draw(gfx, gfx.width() / 2 - kLogoWidth / 2, kLogoTop, kLogoWidth,
               kBlack);

    gfx.setFont(nullptr);
    const int16_t titleHeight =
        drawCentered(gfx, Config::APP_NAME, titleTop(), kTitleSize);
    drawCentered(gfx, "standby", titleTop() + titleHeight + kSubtitleGap, 2);

    gfx.setTextSize(1);
    gfx.setTextColor(kBlack);
    gfx.setCursor(6, gfx.height() - kVersionBottomInset);
    gfx.print("hold the power button to wake");
}

void drawError(Adafruit_GFX& gfx, const char* message) {
    if (message == nullptr || message[0] == '\0') {
        return;
    }

    // Copy so the split can be done in place. Too long is marked, not dropped:
    // a truncated fatal message that looks complete is worse than an obvious
    // one, because it sends you looking for the wrong fault.
    char buffer[kMessageLimit];
    const bool truncated = strlen(message) >= sizeof(buffer);
    strncpy(buffer, message, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    if (truncated && sizeof(buffer) > 4) {
        strcpy(buffer + sizeof(buffer) - 4, "...");
    }

    char* firstLine = buffer;
    char* secondLine = strchr(buffer, '\n');
    if (secondLine != nullptr) {
        *secondLine = '\0';
        secondLine++;
    }

    gfx.setFont(nullptr);
    gfx.setTextSize(1);

    // Centre the sign and the text as one block in the band the layout leaves
    // free: below the subtitle, above the version line.
    size_t longest = strlen(firstLine);
    if (secondLine != nullptr && strlen(secondLine) > longest) {
        longest = strlen(secondLine);
    }
    const int16_t textWidth = static_cast<int16_t>(6 * longest);
    const int16_t blockWidth = kSignWidth + kSignTextGap + textWidth;
    const int16_t blockHeight = kSignHeight + 2 * kFramePadY;

    int16_t left = gfx.width() / 2 - blockWidth / 2;
    if (left < kFramePadX + 2) {
        left = kFramePadX + 2;
    }

    // The band: everything between the subtitle's baseline and the version.
    const int16_t bandTop = titleTop() + 8 * kTitleSize + kSubtitleGap + 24;
    const int16_t bandBottom = gfx.height() - kVersionBottomInset - 6;
    const int16_t frameTop =
        bandTop + (bandBottom - bandTop - blockHeight) / 2;
    const int16_t contentTop = frameTop + kFramePadY;

    gfx.drawRect(left - kFramePadX, frameTop, blockWidth + 2 * kFramePadX,
                 blockHeight, kBlack);

    drawWarningSign(gfx, left, contentTop);

    gfx.setFont(nullptr);
    gfx.setTextSize(1);
    gfx.setTextColor(kBlack);
    const int16_t textX = left + kSignWidth + kSignTextGap;
    if (secondLine != nullptr) {
        gfx.setCursor(textX, contentTop + 4);
        gfx.print(firstLine);
        gfx.setCursor(textX, contentTop + 16);
        gfx.print(secondLine);
    } else {
        gfx.setCursor(textX, contentTop + 10);  // centred on the sign
        gfx.print(firstLine);
    }
}

}  // namespace Bootscreen
