#include "ui/Logo.h"

namespace Logo {
namespace {

// ---------------------------------------------------------------- geometry
//
// Normalised into a unit box: x from 0 (left edge of the mark) to 1, y from 0
// (its top) down to 1. Everything else is derived, so changing the shape means
// changing these numbers and nothing else.
//
// LOGO_GEOMETRY_BEGIN -- tools/verify/render_ui.py parses the block between
// these markers so the preview renderer cannot drift from the firmware. Keep
// the "name = value;" shape if you edit it.
constexpr float kAspect = 0.46f;  // height / width

// The taller peak, at the back. It sets the top of the box.
constexpr float kRightApexX = 0.650f;
constexpr float kRightApexY = 0.000f;
constexpr float kRightLeftFootX = 0.300f;
constexpr float kRightRightFootX = 1.000f;

// The lower, wider peak, in front. It hides the part of the taller one that
// falls behind it.
constexpr float kLeftApexX = 0.290f;
constexpr float kLeftApexY = 0.220f;
constexpr float kLeftLeftFootX = 0.000f;
constexpr float kLeftRightFootX = 0.580f;

// Both peaks stand on the same ground line.
constexpr float kFootY = 1.000f;

// Snow cap, as fractions of a peak's own height measured down from its apex:
// where the cap meets the flanks, how deep the notches cut, and how high the
// rise between them comes back up. The cap is filled, not outlined, so it
// reads against the peak rather than dissolving into it.
constexpr float kCapFlank = 0.300f;
constexpr float kCapNotch = 0.540f;
constexpr float kCapRise = 0.340f;
// LOGO_GEOMETRY_END

struct Peak {
    float apexX;
    float apexY;
    float leftFootX;
    float rightFootX;
};

constexpr Peak kBackPeak = {kRightApexX, kRightApexY, kRightLeftFootX,
                            kRightRightFootX};
constexpr Peak kFrontPeak = {kLeftApexX, kLeftApexY, kLeftLeftFootX,
                             kLeftRightFootX};

constexpr int kMaxCapPoints = 6;

// ------------------------------------------------------------- occlusion

struct Point {
    int16_t x;
    int16_t y;
};

// The front peak, in pixels. Anything of the back peak that lands inside it is
// not drawn: the peaks overlap in depth, not as crossing wireframes.
struct Occluder {
    bool active = false;
    Point a{};
    Point b{};
    Point c{};
};

int32_t cross(const Point& from, const Point& to, int16_t x, int16_t y) {
    return static_cast<int32_t>(to.x - from.x) * (y - from.y) -
           static_cast<int32_t>(to.y - from.y) * (x - from.x);
}

// Boundary counts as inside: the front peak draws its own outline afterwards,
// so hiding the shared edge avoids a doubled line.
bool hidden(const Occluder& occluder, int16_t x, int16_t y) {
    if (!occluder.active) {
        return false;
    }

    const int32_t d1 = cross(occluder.a, occluder.b, x, y);
    const int32_t d2 = cross(occluder.b, occluder.c, x, y);
    const int32_t d3 = cross(occluder.c, occluder.a, x, y);

    const bool anyNegative = d1 < 0 || d2 < 0 || d3 < 0;
    const bool anyPositive = d1 > 0 || d2 > 0 || d3 > 0;
    return !(anyNegative && anyPositive);
}

// --------------------------------------------------------------- rasteriser
//
// Adafruit_GFX draws whole primitives and cannot skip pixels, so the lines and
// the cap fill are rasterised here where the occluder can be consulted per
// pixel. It is a logo: a few thousand pixels, drawn twice per boot.

void plot(Adafruit_GFX& gfx, int16_t x, int16_t y, uint16_t color,
          const Occluder& occluder) {
    if (!hidden(occluder, x, y)) {
        gfx.drawPixel(x, y, color);
    }
}

void line(Adafruit_GFX& gfx, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
          uint16_t color, int16_t weight, const Occluder& occluder) {
    // Weight is added perpendicular to the line's dominant axis; a diagonal
    // offset would thicken the corners unevenly.
    const bool mostlyHorizontal = abs(x1 - x0) >= abs(y1 - y0);

    const bool steep = abs(y1 - y0) > abs(x1 - x0);
    if (steep) {
        int16_t swap = x0;
        x0 = y0;
        y0 = swap;
        swap = x1;
        x1 = y1;
        y1 = swap;
    }
    if (x0 > x1) {
        int16_t swap = x0;
        x0 = x1;
        x1 = swap;
        swap = y0;
        y0 = y1;
        y1 = swap;
    }

    const int16_t dx = x1 - x0;
    const int16_t dy = abs(y1 - y0);
    int16_t error = dx / 2;
    const int16_t step = (y0 < y1) ? 1 : -1;

    for (int16_t x = x0, y = y0; x <= x1; x++) {
        for (int16_t pass = 0; pass < weight; pass++) {
            const int16_t offsetX = mostlyHorizontal ? 0 : pass;
            const int16_t offsetY = mostlyHorizontal ? pass : 0;
            if (steep) {
                plot(gfx, y + offsetX, x + offsetY, color, occluder);
            } else {
                plot(gfx, x + offsetX, y + offsetY, color, occluder);
            }
        }
        error -= dy;
        if (error < 0) {
            y += step;
            error += dx;
        }
    }
}

// Even-odd scanline fill of a closed polygon. The snow cap is the only filled
// shape, and its lower edge is a zigzag, so a scanline can cross it more than
// twice -- which is exactly what a general fill handles and a triangle fan
// would get wrong.
void fillPolygon(Adafruit_GFX& gfx, const Point* points, int count,
                 uint16_t color, const Occluder& occluder) {
    int16_t top = points[0].y;
    int16_t bottom = points[0].y;
    for (int i = 1; i < count; i++) {
        if (points[i].y < top) {
            top = points[i].y;
        }
        if (points[i].y > bottom) {
            bottom = points[i].y;
        }
    }

    for (int16_t y = top; y <= bottom; y++) {
        int16_t crossings[kMaxCapPoints];
        int found = 0;

        for (int i = 0; i < count && found < kMaxCapPoints; i++) {
            const Point& from = points[i];
            const Point& to = points[(i + 1) % count];
            if (from.y == to.y) {
                continue;  // horizontal edges contribute no crossing
            }
            const int16_t lower = from.y < to.y ? from.y : to.y;
            const int16_t upper = from.y < to.y ? to.y : from.y;
            if (y < lower || y >= upper) {
                continue;  // half-open, so a shared vertex counts once
            }
            crossings[found++] = static_cast<int16_t>(
                from.x + static_cast<int32_t>(to.x - from.x) * (y - from.y) /
                             (to.y - from.y));
        }

        for (int i = 1; i < found; i++) {
            const int16_t value = crossings[i];
            int j = i - 1;
            while (j >= 0 && crossings[j] > value) {
                crossings[j + 1] = crossings[j];
                j--;
            }
            crossings[j + 1] = value;
        }

        for (int i = 0; i + 1 < found; i += 2) {
            for (int16_t x = crossings[i]; x <= crossings[i + 1]; x++) {
                plot(gfx, x, y, color, occluder);
            }
        }
    }
}

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

// Where a peak's flank sits at a given depth below its apex, as a fraction of
// the peak's height. The cap hangs off the flanks rather than off coordinates
// guessed to be near them.
float flankX(const Peak& peak, float footX, float depth) {
    return peak.apexX + (footX - peak.apexX) * depth;
}

float depthY(const Peak& peak, float depth) {
    return peak.apexY + (kFootY - peak.apexY) * depth;
}

void drawPeak(Adafruit_GFX& gfx, const Transform& t, const Peak& peak,
              uint16_t color, int16_t weight, bool simplifiedCap,
              const Occluder& occluder) {
    const int16_t apexX = t.px(peak.apexX);
    const int16_t apexY = t.py(peak.apexY);
    const int16_t leftX = t.px(peak.leftFootX);
    const int16_t rightX = t.px(peak.rightFootX);
    const int16_t footY = t.py(kFootY);

    line(gfx, leftX, footY, apexX, apexY, color, weight, occluder);
    line(gfx, apexX, apexY, rightX, footY, color, weight, occluder);
    line(gfx, leftX, footY, rightX, footY, color, weight, occluder);

    // Snow cap: the region between the apex and a notched line hung across
    // both flanks, filled solid so it reads against the outline.
    const float capLeftX = flankX(peak, peak.leftFootX, kCapFlank);
    const float capRightX = flankX(peak, peak.rightFootX, kCapFlank);
    const float flankY = depthY(peak, kCapFlank);
    const float notchY = depthY(peak, kCapNotch);
    const float riseY = depthY(peak, kCapRise);
    const float span = capRightX - capLeftX;

    Point cap[kMaxCapPoints];
    int count = 0;
    cap[count++] = {apexX, apexY};
    cap[count++] = {t.px(capLeftX), t.py(flankY)};

    if (simplifiedCap) {
        // One notch. At icon sizes the full crown is sub-pixel and turns to
        // mush; a single V still reads as a snow cap.
        cap[count++] = {t.px(capLeftX + span * 0.5f), t.py(notchY)};
    } else {
        cap[count++] = {t.px(capLeftX + span * 0.25f), t.py(notchY)};
        cap[count++] = {t.px(capLeftX + span * 0.5f), t.py(riseY)};
        cap[count++] = {t.px(capLeftX + span * 0.75f), t.py(notchY)};
    }

    cap[count++] = {t.px(capRightX), t.py(flankY)};
    fillPolygon(gfx, cap, count, color, occluder);
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

    Occluder front;
    front.active = true;
    front.a = {t.px(kFrontPeak.apexX), t.py(kFrontPeak.apexY)};
    front.b = {t.px(kFrontPeak.leftFootX), t.py(kFootY)};
    front.c = {t.px(kFrontPeak.rightFootX), t.py(kFootY)};

    // Back peak first, with everything behind the front one omitted rather
    // than overpainted -- the mark stays transparent, so it can sit on
    // something other than white.
    drawPeak(gfx, t, kBackPeak, color, weight, simplified, front);

    const Occluder none;
    drawPeak(gfx, t, kFrontPeak, color, weight, simplified, none);
}

}  // namespace Logo
