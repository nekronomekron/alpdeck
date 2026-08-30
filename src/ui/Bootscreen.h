#pragma once

#include <Adafruit_GFX.h>

// The screen the device shows while it starts, and the one it stops on when a
// fatal step fails.
//
// A namespace, not a class: it holds nothing between calls, and every
// dimension comes from the Adafruit_GFX it is handed rather than from
// constructor arguments that could disagree with the actual panel.
namespace Bootscreen {

// Logo, name, subtitle and the version line. Fills the whole surface.
void draw(Adafruit_GFX& gfx);

// A fatal boot error, in the band the layout leaves free below the subtitle: a
// warning triangle with the message beside it, inside a thin frame. The
// message may contain one '\n' for a second line; anything longer than fits is
// cut with an ellipsis rather than silently dropped.
void drawError(Adafruit_GFX& gfx, const char* message);

}  // namespace Bootscreen
