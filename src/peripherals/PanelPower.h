#pragma once

#include <Arduino.h>

#include <functional>

// The e-paper panel's lock and its power deadline.
//
// Split from Display because it answers a different question. Display knows how
// to draw; this knows that the panel is a shared device with a high-voltage rail
// that must not be left up, and that the main loop wants to switch it off while
// a Lua app may be halfway through a refresh. It never touches the panel itself
// -- it is handed a function that hibernates and called when a frame lands.
//
// Why deferred at all: hibernating after every frame cost 102ms for the
// power-down and 41ms more on the NEXT frame for the reset it forced. That is a
// fifth of a 750ms frame spent switching a panel off and on again between two
// frames a third of a second apart.
namespace PanelPower {

constexpr const char* kLogTag = "Panel";

// Long enough that a user working a screen never pays to wake the panel, short
// enough that the rail is down soon after they stop. Not tuned any longer on
// purpose: waking costs a measured 41ms, so stretching this to catch a pause
// would buy very little.
constexpr uint32_t kPowerDownAfterMs = 2000;

// Allocates the lock and takes the function that puts the panel down. Call once,
// from Display::init(). Without a lock the deferred power-down switches itself
// off rather than running unguarded -- it is the one thing that reaches the
// panel from a second task.
void begin(std::function<void()> hibernate);

// Whether to wait for the panel or give up when it is busy.
enum class Wait : uint8_t {
    Forever,  // drawing paths: the frame has to happen
    Never,    // the main loop: a busy panel is one mid-refresh, come back later
};

// Scoped lock over the panel. Every path that reaches the hardware takes one,
// and none of them can forget to release it -- the manual pairs this replaced
// spanned five functions with early returns between them.
class Lock {
public:
    explicit Lock(Wait wait = Wait::Forever);
    ~Lock();

    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;

    // False only for Wait::Never on a busy panel. Nothing may touch the panel
    // when this is false.
    bool held() const { return held_; }

private:
    bool held_ = false;
};

// A frame has just been pushed, so the rail is up and the deadline starts now.
void markPowered();

// True while the rail is up.
bool isPowered();

// Hibernates now and records what it cost. The caller must already hold a Lock;
// the drawing paths do, and take one for the whole frame.
void powerDownLocked();

// Hibernates now, taking the lock itself. For the boot, standby and sleep paths,
// and for an app that wants to measure what waking costs.
void powerDownNow();

// Puts the panel down once it has been left alone for kPowerDownAfterMs. Call
// from the main loop.
//
// `isDrawing` is asked whether a frame is open -- a question only Display can
// answer -- and it is a predicate rather than a value because it is asked twice:
// once before reaching for the lock, and again while holding it, since a frame
// can open in between. Never blocks: a busy panel is one mid-refresh, and
// waiting here would stall input polling for the length of a refresh.
void loop(bool (*isDrawing)());

// How long the last hibernate took. Measured at 102ms and constant: it is the
// power rail coming down, not the panel drawing.
uint32_t lastPowerDownMs();

}  // namespace PanelPower
