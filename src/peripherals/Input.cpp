#include "peripherals/Input.h"

#include <Wire.h>

#include "config/AppConfig.h"
#include "peripherals/GamepadController.h"
#include "peripherals/RotaryController.h"
#include "utils/Logger.h"

namespace Input {

namespace {

RotaryController rotary;
GamepadController gamepad;
QueueHandle_t events = nullptr;

constexpr uint8_t kQueueLength = 16;

// Navigation saturates rather than wraps. Nothing on a 400x300 panel means
// anything past a few dozen steps, and a counter that wrapped would send the
// cursor the opposite way -- the one failure a user could never explain.
constexpr int16_t kNavLimit = 4096;

// Written by poll() on the main loop, read by whatever task an app runs on.
// The spinlock keeps a reader from seeing half of an update; the struct is a
// few dozen bytes, so the critical section stays far shorter than one I2C
// transaction.
Snapshot latest;
portMUX_TYPE latestMux = portMUX_INITIALIZER_UNLOCKED;

uint32_t lastActivityMs = 0;

// The digest, and the ordering rule that keeps an action honest.
//
// Navigation lands in `pending` until an action is captured, and in `deferred`
// afterwards; take() returns `pending` with the action and promotes `deferred`
// to be the next digest. An action is therefore a divider in the input stream,
// which is what makes "turn, then press" and "press, then turn" two different
// things rather than one ambiguous heap of counters.
struct Steps {
    int16_t navX = 0;
    int16_t navY = 0;
    int16_t wheel = 0;
};

Steps pending;
Steps deferred;
Event pendingAction = Event::None;
portMUX_TYPE digestMux = portMUX_INITIALIZER_UNLOCKED;

// Encoder position already folded into a digest. The wheel is read as an
// absolute value rather than as a stream of detent events, so coalescing it
// costs nothing no matter how long a refresh kept the app from asking.
int32_t wheelBaseline = 0;

// Given whenever something lands in the digest, so take() can block without
// polling. Binary rather than counting: it answers "is there anything", and the
// digest itself carries how much.
//
// It is given by accumulate() and captureAction() rather than by publish(),
// and that is not a detail. publish() sees rotary_cw before poll() has folded
// the wheel in from the encoder position, so waking there hands take() an empty
// digest -- which reads as "no input at all" and closed the options menu on
// every turn of the dial.
SemaphoreHandle_t wake = nullptr;

// Given after the digest has been written, never before. Anything else is a
// reader woken to look at data that has not arrived yet.
void signalWake() {
    if (wake != nullptr) {
        xSemaphoreGive(wake);
    }
}

void addStep(int16_t& target, int16_t delta) {
    const int32_t sum = static_cast<int32_t>(target) + delta;
    target = static_cast<int16_t>(sum > kNavLimit    ? kNavLimit
                                  : sum < -kNavLimit ? -kNavLimit
                                                     : sum);
}

// Folds navigation into whichever half of the digest is currently open.
void accumulate(int16_t dx, int16_t dy, int16_t wheel) {
    portENTER_CRITICAL(&digestMux);
    Steps& target = pendingAction == Event::None ? pending : deferred;
    addStep(target.navX, dx);
    addStep(target.navY, dy);
    addStep(target.wheel, wheel);
    portEXIT_CRITICAL(&digestMux);

    signalWake();
}

// Queue of one. A second press while the first is still unread is the user
// hammering a button through a slow refresh; honouring it would open a menu
// and immediately act inside it.
void captureAction(Event event) {
    portENTER_CRITICAL(&digestMux);
    const bool accepted = pendingAction == Event::None;
    if (accepted) {
        pendingAction = event;
    }
    portEXIT_CRITICAL(&digestMux);

    if (accepted) {
        signalWake();
    } else {
        LOGD(kLogTag, "Dropped %s: an action is already pending",
             eventName(event));
    }
}

// Sorts one event into the digest. The wheel is deliberately absent: its
// detents are folded in from the absolute position poll() reads, so the
// cw/ccw events exist for read() only.
void classify(Event event) {
    switch (event) {
    case Event::RotaryUp:
    case Event::GamepadUp:
        accumulate(0, -1, 0);
        break;
    case Event::RotaryDown:
    case Event::GamepadDown:
        accumulate(0, 1, 0);
        break;
    case Event::RotaryLeft:
    case Event::GamepadLeft:
        accumulate(-1, 0, 0);
        break;
    case Event::RotaryRight:
    case Event::GamepadRight:
        accumulate(1, 0, 0);
        break;
    case Event::RotaryCw:
    case Event::RotaryCcw:
        break;  // absolute, folded in by poll()
    default:
        captureAction(event);
        break;
    }
}

// Queues one event for whichever task is reading. Handed to the drivers as a
// callback, so it has to exist before poll() uses it.
void publish(Event event) {
    if (events == nullptr) {
        return;
    }
    lastActivityMs = millis();
    classify(event);

    // Drop rather than block: input is worthless once it is stale, and the main
    // loop must never wait on a Lua app that has stopped reading. The digest
    // above has already kept whatever mattered about this event.
    //
    // No wake here: classify() gave one if this event reached the digest, and a
    // wheel event deliberately did not -- read() blocks on the queue itself, so
    // nothing is waiting on the semaphore for it.
    if (xQueueSend(events, &event, 0) != pdPASS) {
        LOGD(kLogTag, "Event queue full, dropped %s", eventName(event));
    }
}

}  // namespace

bool init() {
    events = xQueueCreate(kQueueLength, sizeof(Event));
    wake = xSemaphoreCreateBinary();
    if (events == nullptr || wake == nullptr) {
        LOGE(kLogTag, "Could not allocate the input primitives");
        return false;
    }

    lastActivityMs = millis();
    Wire.begin(Config::I2C_PIN_SDA, Config::I2C_PIN_SCL, Config::I2C_FREQUENCY);

    // Both controllers are optional and share the daisy-chained bus; probe
    // each independently. The device is only unusable with neither present.
    rotary.begin();
    gamepad.begin();

    if (!isAvailable()) {
        LOGE(kLogTag, "No input controller found (rotary 0x%02X, gamepad 0x%02X)",
             Config::ROTARY_I2C_ADDRESS, Config::GAMEPAD_I2C_ADDRESS);
        return false;
    }
    return true;
}

bool isAvailable() { return hasRotary() || hasGamepad(); }

bool hasRotary() { return rotary.available(); }

bool hasGamepad() { return gamepad.available(); }

void poll() {
    const uint32_t nowMs = millis();
    rotary.poll(nowMs, publish);
    gamepad.poll(nowMs, publish);

    // Rebuild the level-triggered mirror from what the drivers just sampled.
    const RotaryController::State rotaryState = rotary.state();
    const GamepadController::State gamepadState = gamepad.state();

    Snapshot fresh;
    fresh.hasRotary = rotary.available();
    fresh.hasGamepad = gamepad.available();

    fresh.rotarySelect = rotaryState.select;
    fresh.rotaryUp = rotaryState.up;
    fresh.rotaryLeft = rotaryState.left;
    fresh.rotaryDown = rotaryState.down;
    fresh.rotaryRight = rotaryState.right;
    fresh.rotaryEncoder = rotaryState.encoder;

    fresh.gamepadA = gamepadState.a;
    fresh.gamepadB = gamepadState.b;
    fresh.gamepadX = gamepadState.x;
    fresh.gamepadY = gamepadState.y;
    fresh.gamepadStart = gamepadState.start;
    fresh.gamepadSelect = gamepadState.select;
    fresh.gamepadAxisX = gamepadState.axisX;
    fresh.gamepadAxisY = gamepadState.axisY;
    fresh.gamepadDeflectionX = gamepadState.deflectionX;
    fresh.gamepadDeflectionY = gamepadState.deflectionY;
    fresh.gamepadStickX = gamepadState.stickX;
    fresh.gamepadStickY = gamepadState.stickY;

    portENTER_CRITICAL(&latestMux);
    latest = fresh;
    portEXIT_CRITICAL(&latestMux);

    // Fold the wheel in as absolute travel. However long a refresh kept the app
    // from calling take(), the whole turn arrives as one number and nothing had
    // to be buffered to survive it -- which is the entire reason the encoder is
    // read this way rather than as a stream of detents.
    const int32_t travel = fresh.rotaryEncoder - wheelBaseline;
    if (travel != 0) {
        wheelBaseline = fresh.rotaryEncoder;
        const int32_t clamped = travel > kNavLimit    ? kNavLimit
                                : travel < -kNavLimit ? -kNavLimit
                                                      : travel;
        accumulate(0, 0, static_cast<int16_t>(clamped));
    }
}

Snapshot snapshot() {
    portENTER_CRITICAL(&latestMux);
    const Snapshot copy = latest;
    portEXIT_CRITICAL(&latestMux);
    return copy;
}

Event read(uint32_t timeoutMs) {
    Event event = Event::None;
    if (events == nullptr) {
        return event;
    }
    xQueueReceive(events, &event, pdMS_TO_TICKS(timeoutMs));
    return event;
}

Digest take(uint32_t timeoutMs) {
    // Drain first, wait second, and keep waiting out the remainder if the wake
    // turned out to be spurious. Callers act on an empty digest -- a screen
    // reads it as "nobody has touched this for two minutes" and closes -- so an
    // empty return has to mean the timeout genuinely elapsed and nothing else.
    const uint32_t deadlineMs = millis() + timeoutMs;

    while (true) {
        Digest digest;

        portENTER_CRITICAL(&digestMux);
        digest.navX = pending.navX;
        digest.navY = pending.navY;
        digest.wheel = pending.wheel;
        digest.action = pendingAction;

        // Whatever arrived behind the action becomes the next digest. When this
        // one is empty so was the deferred half, because nothing is held back
        // unless an action is pending.
        pending = deferred;
        deferred = Steps{};
        pendingAction = Event::None;
        portEXIT_CRITICAL(&digestMux);

        // The read() queue is the same input seen the other way round. Letting
        // it fill up behind a screen that only takes digests would log a
        // dropped event per press for the rest of the session.
        if (events != nullptr) {
            xQueueReset(events);
        }

        if (digest.any() || wake == nullptr) {
            return digest;
        }

        // Signed, so a millis() wrap during the wait reads as elapsed rather
        // than as another 49 days.
        const int32_t remainingMs = static_cast<int32_t>(deadlineMs - millis());
        if (remainingMs <= 0) {
            return digest;
        }

        xSemaphoreTake(wake, pdMS_TO_TICKS(remainingMs));
    }
}

void flush() {
    if (events != nullptr) {
        xQueueReset(events);
    }

    // Read outside the critical section: snapshot() takes a lock of its own,
    // and nesting the two spinlocks buys nothing here.
    const int32_t position = snapshot().rotaryEncoder;

    portENTER_CRITICAL(&digestMux);
    pending = Steps{};
    deferred = Steps{};
    pendingAction = Event::None;
    wheelBaseline = position;
    portEXIT_CRITICAL(&digestMux);

    if (wake != nullptr) {
        xSemaphoreTake(wake, 0);
    }
}

uint32_t lastEventMs() { return lastActivityMs; }

const char* eventName(Event event) {
    switch (event) {
    case Event::RotaryCw:
        return "rotary_cw";
    case Event::RotaryCcw:
        return "rotary_ccw";
    case Event::RotaryUp:
        return "rotary_up";
    case Event::RotaryDown:
        return "rotary_down";
    case Event::RotaryLeft:
        return "rotary_left";
    case Event::RotaryRight:
        return "rotary_right";
    case Event::RotarySelect:
        return "rotary_select";
    case Event::RotarySelectLong:
        return "rotary_select_long";
    case Event::GamepadUp:
        return "gamepad_up";
    case Event::GamepadDown:
        return "gamepad_down";
    case Event::GamepadLeft:
        return "gamepad_left";
    case Event::GamepadRight:
        return "gamepad_right";
    case Event::GamepadA:
        return "gamepad_a";
    case Event::GamepadB:
        return "gamepad_b";
    case Event::GamepadX:
        return "gamepad_x";
    case Event::GamepadY:
        return "gamepad_y";
    case Event::GamepadStart:
        return "gamepad_start";
    case Event::GamepadSelect:
        return "gamepad_select";
    default:
        return "none";
    }
}

}  // namespace Input
