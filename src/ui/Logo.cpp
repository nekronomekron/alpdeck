#include "ui/Logo.h"

namespace Logo {
namespace {

// ---------------------------------------------------------------- geometry
//
// Normalised into a unit box: x from 0 (left edge of the mark) to 1, y from 0
// (its top) down to 1. Everything else is derived, so changing the shape means
// changing these numbers and nothing else.
//
// LOGO_GEOMETRY_BEGIN -- tools/verify/render_logo.py parses the block between
// these markers so the preview renderer cannot drift from the firmware. Keep
// the "name = value;" shape if you edit it.
constexpr float kAspect = 0.46f;  // height / width

// The taller peak, on the right. It sets the top and the bottom of the box.
constexpr float kRightApexX = 0.650f;
constexpr float kRightApexY = 0.000f;
constexpr float kRightLeftFootX = 0.300f;
constexpr float kRightRightFootX = 1.000f;
constexpr float kRightFootY = 1.000f;

// The lower, wider peak on the left. Its right flank crosses the right peak's
// left flank -- that crossing is the whole composition.
constexpr float kLeftApexX = 0.290f;
constexpr float kLeftApexY = 0.160f;
constexpr float kLeftLeftFootX = 0.000f;
constexpr float kLeftRightFootX = 0.580f;
constexpr float kLeftFootY = 0.940f;

// Snow cap, as fractions of a peak's own height measured down from its apex:
// where the cap meets the flanks, how deep the notches cut, and how high the
// rise between them comes back up.
constexpr float kCapFlank = 0.340f;
constexpr float kCapNotch = 0.460f;
constexpr float kCapRise = 0.400f;
// LOGO_GEOMETRY_END

struct Peak {
    float apexX;
    float apexY;
    float leftFootX;
    float rightFootX;
    float footY;
};

constexpr Peak kRightPeak = {kRightApexX, kRightApexY, kRightLeftFootX,
                             kRightRightFootX, kRightFootY};
constexpr Peak kLeftPeak = {kLeftApexX, kLeftApexY, kLeftLeftFootX,
                            kLeftRightFootX, kLeftFootY};

// ----------------------------------------------------------------- drawing

struct Transform {
    int16_t originX;
    int16_t originY;
    int16_t width;
    int16_t height;

    int16_t px(float value) const {
        return originX + static_cast<int16_t>(value * width + 0.5f);
    }
    int16_t py(float value) const {
        return originY + static_cast<int16_t>(value * height + 0.5f);
    }
};

// A line of the given weight. Adafruit_GFX has no stroke width, and drawing
// the same line twice with a diagonal offset thickens corners unevenly, so the
// offset goes perpendicular to whichever axis the line mostly runs along.
void stroke(Adafruit_GFX& gfx, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
            uint16_t color, int16_t weight) {
    const bool mostlyHorizontal = abs(x1 - x0) >= abs(y1 - y0);

    for (int16_t step = 0; step < weight; step++) {
        const int16_t dx = mostlyHorizontal ? 0 : step;
        const int16_t dy = mostlyHorizontal ? step : 0;
        gfx.drawLine(x0 + dx, y0 + dy, x1 + dx, y1 + dy, color);
    }
}

// Where a peak's flank sits at a given depth below its apex, as a fraction of
// the peak's height. Used to hang the snow cap off the flanks rather than
// guessing coordinates that then fail to meet them.
float flankX(const Peak& peak, float footX, float depth) {
    return peak.apexX + (footX - peak.apexX) * depth;
}

float depthY(const Peak& peak, float depth) {
    return peak.apexY + (peak.footY - peak.apexY) * depth;
}

void drawPeak(Adafruit_GFX& gfx, const Transform& t, const Peak& peak,
              uint16_t color, int16_t weight, bool simplifiedCap) {
    // Outline: left flank, right flank, base. No fill -- the peaks overlap and
    // a filled one would swallow the other.
    stroke(gfx, t.px(peak.leftFootX), t.py(peak.footY), t.px(peak.apexX),
           t.py(peak.apexY), color, weight);
    stroke(gfx, t.px(peak.apexX), t.py(peak.apexY), t.px(peak.rightFootX),
           t.py(peak.footY), color, weight);
    stroke(gfx, t.px(peak.leftFootX), t.py(peak.footY), t.px(peak.rightFootX),
           t.py(peak.footY), color, weight);

    // Snow cap, hung between the two flanks at kCapFlank depth.
    const float leftX = flankX(peak, peak.leftFootX, kCapFlank);
    const float rightX = flankX(peak, peak.rightFootX, kCapFlank);
    const float flankY = depthY(peak, kCapFlank);
    const float notchY = depthY(peak, kCapNotch);
    const float riseY = depthY(peak, kCapRise);
    const float span = rightX - leftX;

    if (simplifiedCap) {
        // One chevron. At icon sizes the notches are sub-pixel anyway, and a
        // clean V still reads as a snow cap.
        stroke(gfx, t.px(leftX), t.py(flankY), t.px(leftX + span * 0.5f),
               t.py(notchY), color, weight);
        stroke(gfx, t.px(leftX + span * 0.5f), t.py(notchY), t.px(rightX),
               t.py(flankY), color, weight);
        return;
    }

    const float xs[5] = {leftX, leftX + span * 0.25f, leftX + span * 0.5f,
                         leftX + span * 0.75f, rightX};
    const float ys[5] = {flankY, notchY, riseY, notchY, flankY};

    for (int i = 0; i < 4; i++) {
        stroke(gfx, t.px(xs[i]), t.py(ys[i]), t.px(xs[i + 1]), t.py(ys[i + 1]),
               color, weight);
    }
}

}  // namespace

int16_t height(int16_t width) {
    return static_cast<int16_t>(width * kAspect + 0.5f);
}

void draw(Adafruit_GFX& gfx, int16_t x, int16_t y, int16_t width,
          uint16_t color) {
    if (width <= 0) {
        return;
    }

    const Transform t{x, y, width, height(width)};
    const bool simplified = width < kSimplifyBelowWidth;
    // A single-pixel outline looks fragile on e-paper at poster size, and a
    // two-pixel one is unreadable at icon size.
    const int16_t weight = width >= 150 ? 2 : 1;

    // Right peak first, then left. Neither occludes the other -- both outlines
    // stay whole and the flanks cross, which is the reference.
    drawPeak(gfx, t, kRightPeak, color, weight, simplified);
    drawPeak(gfx, t, kLeftPeak, color, weight, simplified);
}

}  // namespace Logo
