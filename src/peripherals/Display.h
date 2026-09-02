#pragma once

#include <Adafruit_GFX.h>
#include <Arduino.h>

#include <functional>

namespace Display {

constexpr const char* kLogTag = "Display";

constexpr uint16_t kBlack = 0x0000;
constexpr uint16_t kWhite = 0xFFFF;

void init();

// Blanks the panel and powers it down.
void shutdown();

// Powers down leaving whatever is on the panel in place. E-paper holds its
// image with no power, which is what makes a standby screen free.
void powerDown();

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
// Measured on the GDEY042T81 at room temperature, whole panel, from the Timing
// app, on a panel still powered from the previous frame. The datasheet
// constants GxEPD2 carries (400/1200) are the fallbacks it uses when there is
// no BUSY pin, and are not what this panel does.
enum class RefreshMode : uint8_t {
    Partial,  // 609ms, leaves faint ghosting
    Full,     // 1989ms, clears it
};

void beginFrame(RefreshMode mode = RefreshMode::Partial);

// Frame bound to a rectangle: drawing is clipped to it and endFrame() pushes
// only that area. Always a partial refresh -- a full refresh drives the whole
// panel by nature, so a region would be a lie.
void beginFrame(int16_t x, int16_t y, int16_t w, int16_t h);

void endFrame();
bool frameOpen();

// Hibernates the panel once it has been left alone for a moment. Call from the
// main loop; does nothing while a frame is open or the panel is already down.
//
// The deadline, the lock and the measurements behind it are PanelPower's --
// see that header for why the power-down is deferred at all.
void loop();

// How long the panel took to draw the last frame, in milliseconds. The refresh
// is synchronous, so this is the real cost an app has to budget for; there is
// no asynchronous state to poll.
uint32_t lastRefreshMs();

// How long the last hibernate took, in milliseconds. Since the power-down became
// deferred this is no longer part of the frame a caller is waiting on -- loop()
// pays it once the drawing has stopped. Reported anyway, because it is what
// makes the deferral visible as having worked: it reads 0 across a run of
// frames, and 102ms once the panel actually goes down.
uint32_t lastPowerDownMs();

Adafruit_GFX& canvas();

int16_t width();
int16_t height();

}  // namespace Display
