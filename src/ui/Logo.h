#pragma once

#include <Adafruit_GFX.h>

// The alpdeck mark: two overlapping peaks in outline, each with a notched snow
// cap. Line art, no fills -- where the peaks overlap the flanks simply cross,
// which is what reads as two mountains rather than one silhouette.
//
// Free functions rather than a class: there is no state, and the bootscreen is
// not the only thing that will want to draw it.
namespace Logo {

// Draws the mark with its bounding box's top-left corner at (x, y).
//
// The width is the one knob; the height follows from the aspect ratio, so ask
// logoHeight() rather than assuming. Below kSimplifyBelowWidth the snow caps
// collapse from a notched crown to a single chevron -- at icon sizes the
// notches turn to mush, and a legible simplification beats a faithful smudge.
void draw(Adafruit_GFX& gfx, int16_t x, int16_t y, int16_t width,
          uint16_t color);

// Height of the mark at a given width. Rounded the same way draw() rounds, so
// the two always agree.
int16_t height(int16_t width);

// Below this width the snow caps simplify.
constexpr int16_t kSimplifyBelowWidth = 90;

}  // namespace Logo
