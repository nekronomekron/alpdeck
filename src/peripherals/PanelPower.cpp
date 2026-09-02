#include "peripherals/PanelPower.h"

#include "utils/Logger.h"

namespace PanelPower {
namespace {

// Guards the panel itself. Only the paths that talk to the hardware take it --
// drawing does NOT, because Adafruit_GFX writes into the page buffer in RAM and
// firstPage() only whitens it. The long stretch between beginFrame() and
// endFrame() therefore needs no lock and cannot block the main loop.
SemaphoreHandle_t panelMux = nullptr;

std::function<void()> hibernatePanel;

// millis() at the last frame, or 0 when the panel is hibernated. The rail must
// not be left up indefinitely, so this is a deadline and not merely an
// optimisation.
uint32_t poweredAtMs = 0;

uint32_t lastPowerDownDurationMs = 0;

}  // namespace

void begin(std::function<void()> hibernate) {
    hibernatePanel = std::move(hibernate);

    panelMux = xSemaphoreCreateMutex();
    if (panelMux == nullptr) {
        // Not fatal. Without it the deferred power-down switches itself off, and
        // the panel is hibernated only by the paths that were going to do it
        // anyway; a device that draws is better than one that refuses to boot.
        LOGE(kLogTag, "Could not allocate the panel lock");
    }
}

Lock::Lock(Wait wait) {
    if (panelMux == nullptr) {
        // No lock to take, and nothing to contend with either: the deferred
        // power-down is the only second user and it stands down without one.
        held_ = true;
        return;
    }
    const TickType_t timeout = wait == Wait::Forever ? portMAX_DELAY : 0;
    held_ = xSemaphoreTake(panelMux, timeout) == pdTRUE;
}

Lock::~Lock() {
    if (held_ && panelMux != nullptr) {
        xSemaphoreGive(panelMux);
    }
}

void markPowered() { poweredAtMs = millis(); }

bool isPowered() { return poweredAtMs != 0; }

void powerDownLocked() {
    if (!hibernatePanel) {
        return;
    }

    // No powerOff() first. hibernate() does one itself, and doing it outside
    // this span moved the only expensive part of the hibernate out of what is
    // timed here -- which made lastPowerDownMs() report 0 for a power-down that
    // really cost 102ms.
    const uint32_t startedMs = millis();
    hibernatePanel();
    lastPowerDownDurationMs = millis() - startedMs;
    poweredAtMs = 0;
}

void powerDownNow() {
    Lock lock;
    powerDownLocked();
}

void loop(bool (*isDrawing)()) {
    // Read without the lock: single words, and being one main-loop pass late to
    // power down is not worth taking one.
    if (poweredAtMs == 0 || panelMux == nullptr) {
        return;
    }
    if (isDrawing != nullptr && isDrawing()) {
        return;
    }
    if (millis() - poweredAtMs < kPowerDownAfterMs) {
        return;
    }

    Lock lock(Wait::Never);
    if (!lock.held()) {
        return;  // mid-refresh: a panel that must not be powered down anyway
    }

    // Re-checked under the lock: a frame may have opened, or landed, since the
    // tests above.
    if (poweredAtMs != 0 && !(isDrawing != nullptr && isDrawing())) {
        powerDownLocked();
    }
}

uint32_t lastPowerDownMs() { return lastPowerDownDurationMs; }

}  // namespace PanelPower
