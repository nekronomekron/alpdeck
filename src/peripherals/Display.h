#pragma once

#include <Adafruit_GFX.h>
#include <Arduino.h>

#include <functional>

namespace Display {

constexpr const char* kLogTag = "Display";

constexpr uint16_t kBlack = 0x0000;
constexpr uint16_t kWhite = 0xFFFF;

void init();
void shutdown();

void drawFullWindow(std::function<void(Adafruit_GFX&)> drawFunction);
void drawPartialWindow(int16_t x, int16_t y, int16_t w, int16_t h,
                       std::function<void(Adafruit_GFX&)> drawFunction);

// Immediate-mode frame, for callers that cannot draw from inside a callback --
// notably the Lua bindings, where invoking a script from within the paged loop
// could longjmp straight through it on error.
//
// Safe here only because the panel fits one page: MAX_HEIGHT resolves to the
// full 300 rows, so endFrame()'s single nextPage() flushes everything. A
// smaller MAX_DISPLAY_BUFFER_SIZE would silently render only the top slice and
// this would need to become a real paged loop again.
enum class RefreshMode : uint8_t {
    Partial,  // ~400ms, leaves faint ghosting
    Full,     // ~1200ms, clears it
};

void beginFrame(RefreshMode mode = RefreshMode::Partial);

// Frame bound to a rectangle: drawing is clipped to it and endFrame() pushes
// only that area. Always a partial refresh -- a full refresh drives the whole
// panel by nature, so a region would be a lie.
void beginFrame(int16_t x, int16_t y, int16_t w, int16_t h);

void endFrame();
bool frameOpen();

// How long the last endFrame() took, in milliseconds. The refresh is
// synchronous, so this is the real cost an app has to budget for; there is no
// asynchronous state to poll.
uint32_t lastRefreshMs();

Adafruit_GFX& canvas();

int16_t width();
int16_t height();

}  // namespace Display
