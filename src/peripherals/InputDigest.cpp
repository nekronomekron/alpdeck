#include "peripherals/InputDigest.h"

#include "utils/Logger.h"

namespace InputDigest {
namespace {

using Input::Event;

// Navigation saturates rather than wraps. See addWheel() in the header.
constexpr int16_t kNavLimit = 4096;

// The digest, and the ordering rule that keeps an action honest.
//
// Navigation lands in `pending` until an action is captured, and in `deferred`
// afterwards; consume() returns `pending` with the action and promotes
// `deferred` to be the next digest. An action is therefore a divider in the
// input stream, which is what makes "turn, then press" and "press, then turn"
// two different things rather than one ambiguous heap of counters.
struct Steps {
    int16_t navX = 0;
    int16_t navY = 0;
    int16_t wheel = 0;
};

Steps pending;
Steps deferred;
Event pendingAction = Event::None;
portMUX_TYPE digestMux = portMUX_INITIALIZER_UNLOCKED;

// Given whenever something lands in the digest, so a consumer can block without
// polling. Binary rather than counting: it answers "is there anything", and the
// digest itself carries how much.
//
// It is given by accumulate() and captureAction(), never by the event queue's
// side of things. Input::poll() sees rotary_cw before it has folded the wheel
// in from the encoder position, so waking on the event would hand consume() an
// empty digest -- which reads as "no input at all" and closed the options menu
// on every turn of the dial.
SemaphoreHandle_t wake = nullptr;

// Given after the digest has been written, never before. Anything else is a
// reader woken to look at data that has not arrived yet.
void signalWake() {
    if (wake != nullptr) {
        xSemaphoreGive(wake);
    }
}

int16_t clampStep(int32_t value) {
    if (value > kNavLimit) {
        return kNavLimit;
    }
    if (value < -kNavLimit) {
        return -kNavLimit;
    }
    return static_cast<int16_t>(value);
}

void addStep(int16_t& target, int16_t delta) {
    target = clampStep(static_cast<int32_t>(target) + delta);
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
        LOGD(Input::kLogTag, "Dropped %s: an action is already pending",
             Input::eventName(event));
    }
}

}  // namespace

bool init() {
    wake = xSemaphoreCreateBinary();
    return wake != nullptr;
}

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
        break;  // absolute, folded in by addWheel()
    default:
        captureAction(event);
        break;
    }
}

void addWheel(int32_t detents) {
    if (detents == 0) {
        return;
    }
    accumulate(0, 0, clampStep(detents));
}

Input::Digest consume() {
    Input::Digest digest;

    portENTER_CRITICAL(&digestMux);
    digest.navX = pending.navX;
    digest.navY = pending.navY;
    digest.wheel = pending.wheel;
    digest.action = pendingAction;

    // Whatever arrived behind the action becomes the next digest. When this one
    // is empty so was the deferred half, because nothing is held back unless an
    // action is pending.
    pending = deferred;
    deferred = Steps{};
    pendingAction = Event::None;
    portEXIT_CRITICAL(&digestMux);

    return digest;
}

bool wait(uint32_t timeoutMs) {
    if (wake == nullptr) {
        return false;
    }
    xSemaphoreTake(wake, pdMS_TO_TICKS(timeoutMs));
    return true;
}

void reset() {
    portENTER_CRITICAL(&digestMux);
    pending = Steps{};
    deferred = Steps{};
    pendingAction = Event::None;
    portEXIT_CRITICAL(&digestMux);

    if (wake != nullptr) {
        xSemaphoreTake(wake, 0);
    }
}

}  // namespace InputDigest
