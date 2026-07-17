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
};