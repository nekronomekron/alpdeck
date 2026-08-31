#include "core/BootSequence.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <SD.h>
#include <string.h>

#include "config/AppConfig.h"
#include "core/Settings.h"
#include "core/lua/LuaContext.h"
#include "core/lua/LuaHost.h"
#include "net/FtpService.h"
#include "net/Network.h"
#include "peripherals/Display.h"
#include "peripherals/Input.h"
#include "peripherals/PowerButton.h"
#include "ui/Bootscreen.h"
#include "utils/Logger.h"

namespace BootSequence {
namespace {

bool sdMounted = false;

// How long to wait for a serial monitor to attach before giving up on it.
constexpr uint32_t kSerialAttachTimeoutMs = 1000;

// Fatal boot error: shown on the bootscreen below the logo (warning sign plus
// message), then the device halts. Nothing else runs -- a device that cannot
// reach the launcher is better stopped on a readable screen than half-alive.
[[noreturn]] void fail(const char* message) {
    LOGE(kLogTag, "Fatal: %s", message);

    Display::drawFullWindow([&](Adafruit_GFX& gfx) {
        Bootscreen::draw(gfx);
        Bootscreen::drawError(gfx, message);
    });

    while (true) {
        yield();
    }
}

void startLauncher() {
    // The launcher only browses; an empty root denies it every fs write.
    LuaContext::setSandboxRoot("");

    if (!LuaHost::run(Config::LAUNCHER_PATH)) {
        // Without a launcher there is nothing to operate. A fresh flash where
        // the filesystem image was never uploaded lands here.
        fail("launcher not found\nflash the filesystem (uploadfs)");
    }
}

void startApp(const String& path) {
    // Confine writes to the app's own directory: strip the trailing entry file.
    String root = path;
    const int slash = root.lastIndexOf('/');
    if (slash > 0) {
        root = root.substring(0, slash);
    }
    LuaContext::setSandboxRoot(root);

    if (!LuaHost::run(path)) {
        LOGE(kLogTag, "%s could not start; returning to the launcher",
             path.c_str());
        startLauncher();
    }
}

// FTP only makes sense with a network under it, so its own setting and the
// connection state are checked together. Called on a settings change and from
// the connect callback, which is what keeps one rule in one place.
void applyFtpSetting() {
    const bool wanted = Settings::getBool(Settings::kFtpEnabled) &&
                        Network::isConnected();
    if (wanted) {
        FtpService::start(sdMounted);
    } else {
        FtpService::stop();
    }
}

// The kernel's half of the declarative settings contract: Lua writes a value,
// this decides what it means for the hardware.
void onSettingChanged(const char* key) {
    if (strcmp(key, Settings::kWifiEnabled) == 0) {
        Network::setEnabled(Settings::getBool(Settings::kWifiEnabled));
        applyFtpSetting();
    } else if (strcmp(key, Settings::kFtpEnabled) == 0) {
        applyFtpSetting();
    }
    // The rest -- standby screen, sleep timeout, refresh interval -- are read
    // where they are used, so they need no apply step.
}

// Fires on the main loop once a script's VM is fully torn down, so starting
// the next one here can never leave two states alive.
void onScriptFinished(const LuaHost::Finished& finished) {
    const String request = LuaContext::takeLaunchRequest();

    if (!request.isEmpty()) {
        startApp(request);
        return;
    }

    // Anything else -- clean return, error, or cancellation -- lands back at
    // the launcher, which is what makes a crashing app survivable.
    if (finished.path != Config::LAUNCHER_PATH) {
        startLauncher();
        return;
    }

    // The launcher itself returned without a launch request. Restarting it
    // immediately would spin -- but leaving a dead panel with only a log line
    // is close to a brick, so a launcher that failed outright says so on
    // screen. Its own error text is the useful part: with the launcher split
    // across /lib modules, a missing or broken one lands exactly here.
    if (finished.exit == LuaHost::Exit::Failed ||
        finished.exit == LuaHost::Exit::NotFound) {
        LOGE(kLogTag, "Launcher stopped unexpectedly: %s",
             finished.message.c_str());

        String message = "launcher failed\n";
        message += finished.message.isEmpty() ? "see the serial log"
                                              : finished.message;
        fail(message.c_str());
    }
}

void beginSerial() {
    Serial.begin(115200);

    // Give the USB-CDC host (serial monitor) a moment to attach, but never
    // block forever: with no host attached -- or in the Wokwi simulator, where
    // the CDC connected state is not asserted -- this must fall through.
    const uint32_t startedMs = millis();
    while (!Serial && millis() - startedMs < kSerialAttachTimeoutMs) {
        yield();
    }
    delay(1000);

    Logger::begin(Serial, static_cast<Logger::Level>(Config::LOG_LEVEL));
    Logger::setSerialOutputEnabled(Config::LOG_SERIAL_OUTPUT);
}

void mountFilesystems() {
    // The launcher lives on LittleFS; without the mount the device can never
    // reach anything runnable.
    if (!LittleFS.begin(true)) {
        fail("flash filesystem failed");
    }
    LOGI("FS", "LittleFS mounted (%u/%u bytes used)", LittleFS.usedBytes(),
         LittleFS.totalBytes());

    // The SD card shares the display's SPI bus; Display::init() already called
    // SPI.begin() for it, and the display is hibernated by now with CS
    // released.
    sdMounted = SD.begin(Config::SD_PIN_CS, SPI);
    if (!sdMounted) {
        LOGW("FS", "SD mount failed; /%s will not be served",
             Config::FTP_MOUNT_SD);
        return;
    }

    LOGI("FS", "SD mounted (%llu bytes)", SD.cardSize());

    // Log the card's top level. Apps live at /apps here (the /sd prefix is a
    // virtual mount the bindings strip), so this is a quick check that the
    // layout is right.
    File root = SD.open("/");
    if (root && root.isDirectory()) {
        for (File entry = root.openNextFile(); entry;
             entry = root.openNextFile()) {
            LOGD("FS", "  sd:/%s%s", entry.name(),
                 entry.isDirectory() ? "/" : "");
            entry.close();
        }
        root.close();
    }
}

}  // namespace

void run() {
    beginSerial();
    Settings::begin();
    Settings::onChanged(onSettingChanged);

    // Before anything else: a wake from a press too brief to count goes
    // straight back to sleep, and must do so without having touched any
    // peripheral.
    PowerButton::begin();

    Display::init();
    Display::drawFullWindow(Bootscreen::draw);

    // Only now is there a panel worth blanking on the way down.
    PowerButton::onBeforeSleep(Display::shutdown);

    mountFilesystems();

    // Input comes before the network: with no controller at all the device is
    // unusable and must stop on the error screen before anything else starts.
    // The simulator has no seesaw hardware, so it boots on without input.
    if (!Input::init()) {
#ifdef WOKWI_SIMULATOR
        LOGW(kLogTag, "No input controller (simulator build); continuing");
#else
        fail("no input controller found\nconnect a rotary or gamepad");
#endif
    }

    // FTP only exists once there is a network to serve it on, so it is started
    // from the connect callback rather than here -- that covers both a
    // boot-time auto-connect and credentials arriving later via the portal.
    Network::onConnected(applyFtpSetting);
    Network::onDisconnected(FtpService::stop);
    Network::init();

    if (!LuaHost::init()) {
        fail("system error: lua host failed");
    }
    LuaHost::onFinished(onScriptFinished);

    // Deliberately no Display::shutdown() here. It blanked the panel and
    // hibernated it immediately before the first script ran, so anything
    // drawing afterwards started from a cleared screen. The bootscreen now
    // stays up until the launcher's first frame replaces it.

    // boot.lua is the user hook, so it runs with the same privileges as an app
    // but rooted at LittleFS. If it requests a launch, onScriptFinished
    // honours it and the launcher is skipped.
    LuaContext::setSandboxRoot("");
    if (!LuaHost::run(Config::BOOT_SCRIPT_PATH)) {
        LOGW(kLogTag, "%s missing; starting the launcher directly",
             Config::BOOT_SCRIPT_PATH);
        startLauncher();
    }
}

void loop() {
    Network::loop();
    FtpService::loop();
    Input::poll();
    LuaHost::loop();
    PowerButton::poll();
}

}  // namespace BootSequence
