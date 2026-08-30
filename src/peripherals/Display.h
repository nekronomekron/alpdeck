#pragma once

#include <Adafruit_GFX.h>
#include <Arduino.h>

#include <functional>

class Display {
public:
    static constexpr const char* kLogTag = "Display";

    static void init();

    static void shutdown();

    static void drawFullWindow(std::function<void(Adafruit_GFX&)> drawFunction);
    static void drawPartialWindow(
        int16_t x, int16_t y, int16_t w, int16_t h,
        std::function<void(Adafruit_GFX&)> drawFunction);

    // Immediate-mode frame, for callers that cannot draw from inside a
    // callback -- notably the Lua bindings, where invoking a script from within
    // the paged loop could longjmp straight through it on error.
    //
    // Safe here only because the panel fits one page: MAX_HEIGHT resolves to
    // the full 300 rows, so endFrame()'s single nextPage() flushes everything.
    // A smaller MAX_DISPLAY_BUFFER_SIZE would silently render only the top
    // slice and this would need to become a real paged loop again.
    static void beginFrame(bool partial = false);
    static void endFrame();
    static bool frameOpen();

    static Adafruit_GFX& canvas();

    static int16_t width();
    static int16_t height();

    static constexpr uint16_t kBlack = 0x0000;
    static constexpr uint16_t kWhite = 0xFFFF;

private:
    static bool _frameOpen;
};